import Foundation

@MainActor
final class PackModel: ObservableObject {
    enum Phase {
        case idle
        case packing(name: String, fraction: Double?, file: String)
        case done(URL)
        case failed(String)
    }

    @Published var phase: Phase = .idle
    @Published var queuedCount = 0
    /// Set when an archive already exists; the view asks whether to replace it.
    @Published var overwriteRequest: URL?

    private var queue: [URL] = []
    private var current: ProgressJob?
    private var pendingOverwrite: URL?
    private var isRunning = false

    var isBusy: Bool { isRunning }

    func add(_ urls: [URL]) {
        let folders = urls.filter {
            (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
        }
        guard !folders.isEmpty else {
            if !isRunning { phase = .failed(String(localized: "Drop a folder to archive it, or a .zar file to open it.")) }
            return
        }
        queue.append(contentsOf: folders)
        queuedCount = queue.count
        startNextIfIdle()
    }

    func cancel() {
        queue.removeAll()
        queuedCount = 0
        current?.cancel()
    }

    func confirmOverwrite(_ replace: Bool) {
        overwriteRequest = nil
        guard let folder = pendingOverwrite else { return }
        pendingOverwrite = nil
        if replace {
            queue.insert(folder, at: 0)
            overwriteNext = true
            queuedCount = queue.count
        }
        startNextIfIdle()
    }

    private var overwriteNext = false

    private func startNextIfIdle() {
        guard !isRunning, overwriteRequest == nil, !queue.isEmpty else { return }
        let input = queue.removeFirst()
        queuedCount = queue.count
        isRunning = true
        let confirmedReplace = overwriteNext
        overwriteNext = false
        let output = confirmedReplace ? Preferences.outputFolder : Self.output(for: input)
        let overwrite = confirmedReplace || Preferences.existingArchive == .replace
        let options = Packer.Options(skipSystemFiles: Preferences.skipSystemFiles,
                                     compressionLevel: Preferences.compression.level)
        let name = input.lastPathComponent
        phase = .packing(name: name, fraction: nil, file: "")

        let job = ProgressJob { [weak self] p in
            Task { @MainActor in
                guard let self, case .packing = self.phase else { return }
                self.phase = .packing(name: name, fraction: p.fraction, file: p.currentFile)
            }
        }
        current = job

        Task.detached(priority: .userInitiated) { [weak self] in
            let outcome = Packer.run(input: input, output: output, overwrite: overwrite, options: options, job: job)
            await self?.finished(outcome, input: input)
        }
    }

    private func finished(_ outcome: Packer.Outcome, input: URL) {
        isRunning = false
        current = nil
        switch outcome {
        case .success(let url):
            phase = .done(url)
        case .cancelled:
            phase = .idle
        case .exists(let url):
            pendingOverwrite = input
            overwriteRequest = url
            phase = .idle
            return
        case .failure(let message):
            phase = .failed(message)
        }
        startNextIfIdle()
    }

    /// The chosen save folder, or, with "Keep both", a free "Name 2.zar"-style path.
    private static func output(for input: URL) -> URL? {
        let folder = Preferences.outputFolder
        guard Preferences.existingArchive == .keepBoth else { return folder }
        let archive = Packer.resolvedOutput(input: input, output: folder)
        let stem = archive.deletingPathExtension().lastPathComponent
        var candidate = archive
        var n = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            candidate = archive.deletingLastPathComponent().appendingPathComponent("\(stem) \(n).zar")
            n += 1
        }
        return candidate
    }
}
