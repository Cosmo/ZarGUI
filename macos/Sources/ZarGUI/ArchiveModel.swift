import AppKit
import Foundation

@MainActor
final class ArchiveModel: ObservableObject {
    enum Load {
        case loading
        case loaded(Archive)
        case failed(String)
    }

    enum Extraction {
        case idle
        case running(fraction: Double?, file: String)
        case done([URL])
        case failed(String)
    }

    struct Request {
        let indices: [Int]
        let destination: URL
        let reveal: [URL]
    }

    let url: URL
    @Published private(set) var load: Load = .loading
    @Published var selection = Set<Int>()
    @Published private(set) var extraction: Extraction = .idle
    /// Set when extracting would replace existing files; the view asks first.
    @Published var replaceRequest: Request?
    /// Items being written by a drag out of the window.
    @Published private(set) var dragExport: (name: String, fraction: Double?)?
    private(set) var promiseWriter: FilePromiseWriter?

    private var job: ProgressJob?

    init(url: URL) {
        self.url = url
        Task.detached(priority: .userInitiated) { [url] in
            let result: Load
            do { result = .loaded(try Archive(url: url)) }
            catch let error as Archive.OpenError { result = .failed(error.message) }
            catch { result = .failed(error.localizedDescription) }
            await MainActor.run { [weak self] in self?.loaded(result) }
        }
    }

    private func loaded(_ result: Load) {
        if case .loaded(let archive) = result { promiseWriter = makePromiseWriter(for: archive) }
        load = result
    }

    private func makePromiseWriter(for archive: Archive) -> FilePromiseWriter {
        let events = FilePromiseWriter.Events(
            started: { [weak self] name in
                Task { @MainActor in self?.dragExport = (name, nil) }
            },
            progress: { [weak self] fraction in
                Task { @MainActor in
                    guard let self, let name = self.dragExport?.name else { return }
                    self.dragExport = (name, fraction)
                }
            },
            finished: { [weak self] error in
                Task { @MainActor in
                    self?.dragExport = nil
                    if let error { self?.extraction = .failed(error) }
                }
            })
        return FilePromiseWriter(archive: archive, events: events)
    }

    var archive: Archive? {
        if case .loaded(let a) = load { return a }
        return nil
    }

    var isExtracting: Bool {
        if case .running = extraction { return true }
        return false
    }

    // MARK: Actions

    func extractAll() {
        guard archive != nil else { return }
        chooseDestination(message: String(localized: "Choose where to extract “\(url.lastPathComponent)”.")) { folder in
            let target = Self.uniqueFolder(in: folder, named: self.url.deletingPathExtension().lastPathComponent)
            self.start(Request(indices: [], destination: target, reveal: [target]), overwrite: false)
        }
    }

    func extract(_ ids: Set<Int>) {
        guard let archive, !ids.isEmpty else { return }
        // Items inside another selected folder come along with it.
        let roots = ids.filter { id in
            var p = archive.parents[id]
            while p >= 0 { if ids.contains(p) { return false }; p = archive.parents[p] }
            return true
        }.sorted()
        let message = roots.count == 1
            ? String(localized: "Choose where to extract “\(archive.names[roots[0]])”.")
            : String(localized: "Choose where to extract \(roots.count) items.")
        chooseDestination(message: message) { folder in
            let reveal = roots.map { folder.appendingPathComponent(archive.names[$0]) }
            self.start(Request(indices: roots, destination: folder, reveal: reveal), overwrite: false)
        }
    }

    func confirmReplace(_ replace: Bool) {
        guard let request = replaceRequest else { return }
        replaceRequest = nil
        if replace { start(request, overwrite: true) }
    }

    func cancelExtraction() { job?.cancel() }

    func dismissExtraction() {
        if !isExtracting { extraction = .idle }
    }

    // MARK: Private

    private func start(_ request: Request, overwrite: Bool) {
        guard let archive, !isExtracting else { return }
        extraction = .running(fraction: nil, file: "")
        let job = ProgressJob { [weak self] p in
            Task { @MainActor in
                guard let self, self.isExtracting else { return }
                self.extraction = .running(fraction: p.fraction, file: p.currentFile)
            }
        }
        self.job = job
        Task.detached(priority: .userInitiated) {
            let outcome = archive.extract(request.indices, to: request.destination, overwrite: overwrite, job: job)
            await MainActor.run { [weak self] in self?.finished(outcome, request: request) }
        }
    }

    private func finished(_ outcome: Archive.Outcome, request: Request) {
        job = nil
        switch outcome {
        case .success where Preferences.revealAfterExtract:
            extraction = .idle
            NSWorkspace.shared.activateFileViewerSelecting(request.reveal)
        case .success: extraction = .done(request.reveal)
        case .cancelled: extraction = .idle
        case .exists:
            extraction = .idle
            replaceRequest = request
        case .failure(let message): extraction = .failed(message)
        }
    }

    /// The folder containing the archive, or one the user picks, depending on the setting.
    private func chooseDestination(message: String, then action: @escaping (URL) -> Void) {
        if Preferences.extractDestination == .nextToArchive {
            return action(url.deletingLastPathComponent())
        }
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = url.deletingLastPathComponent()
        panel.prompt = String(localized: "Extract")
        panel.message = message
        let handler: (NSApplication.ModalResponse) -> Void = { response in
            if response == .OK, let folder = panel.url { action(folder) }
        }
        if let window = NSApp.keyWindow { panel.beginSheetModal(for: window, completionHandler: handler) }
        else { handler(panel.runModal()) }
    }

    /// "Name", or "Name 2", "Name 3", … if taken (like Finder).
    private static func uniqueFolder(in folder: URL, named name: String) -> URL {
        var candidate = folder.appendingPathComponent(name, isDirectory: true)
        var n = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            candidate = folder.appendingPathComponent("\(name) \(n)", isDirectory: true)
            n += 1
        }
        return candidate
    }
}
