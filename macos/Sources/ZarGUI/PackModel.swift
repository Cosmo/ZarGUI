import AppKit
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
    @Published var outputFolder: URL?
    @Published var queuedCount = 0
    /// Set when an archive already exists; the view asks whether to replace it.
    @Published var overwriteRequest: URL?

    private var queue: [URL] = []
    private var current: PackJob?
    private var pendingOverwrite: URL?
    private var isRunning = false

    var isBusy: Bool { isRunning }

    func add(_ urls: [URL]) {
        let folders = urls.filter {
            (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
        }
        guard !folders.isEmpty else {
            if !isRunning { phase = .failed(String(localized: "Only folders can be archived. Drop a folder instead.")) }
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
        let overwrite = overwriteNext
        overwriteNext = false
        let output = outputFolder
        let name = input.lastPathComponent
        phase = .packing(name: name, fraction: nil, file: "")

        let job = PackJob { [weak self] p in
            Task { @MainActor in
                guard let self, case .packing = self.phase else { return }
                let fraction = p.bytesTotal > 0 ? Double(p.bytesDone) / Double(p.bytesTotal) : nil
                self.phase = .packing(name: name, fraction: fraction, file: p.currentFile)
            }
        }
        current = job

        Task.detached(priority: .userInitiated) { [weak self] in
            let outcome = job.run(input: input, outputFolder: output, overwrite: overwrite)
            await self?.finished(outcome, input: input)
        }
    }

    private func finished(_ outcome: PackJob.Outcome, input: URL) {
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

    func chooseOutputFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.prompt = String(localized: "Choose")
        panel.message = String(localized: "Choose where to save the archives.")
        if panel.runModal() == .OK { outputFolder = panel.url }
    }

    func chooseFoldersToPack() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.prompt = String(localized: "Archive")
        panel.message = String(localized: "Choose folders to archive as .zar.")
        if panel.runModal() == .OK { add(panel.urls) }
    }
}
