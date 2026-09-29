import AppKit
import UniformTypeIdentifiers

/// Writes dragged archive entries where Finder (or any other app) drops them,
/// extracting only what was dragged, directly to the destination.
final class FilePromiseWriter: NSObject, NSFilePromiseProviderDelegate, @unchecked Sendable {
    struct Events: Sendable {
        let started: @Sendable (String) -> Void
        let progress: @Sendable (Double?) -> Void
        let finished: @Sendable (String?) -> Void  // error message, if any
    }

    private let archive: Archive
    private let events: Events
    private let queue: OperationQueue = {
        let queue = OperationQueue()
        queue.qualityOfService = .userInitiated
        queue.maxConcurrentOperationCount = 1
        return queue
    }()

    init(archive: Archive, events: Events) {
        self.archive = archive
        self.events = events
    }

    func provider(for node: Archive.Node) -> NSFilePromiseProvider {
        let type: UTType = node.isDirectory ? .folder : (UTType(filenameExtension: (node.name as NSString).pathExtension) ?? .data)
        let provider = NSFilePromiseProvider(fileType: type.identifier, delegate: self)
        provider.userInfo = node
        return provider
    }

    func filePromiseProvider(_ provider: NSFilePromiseProvider, fileNameForType fileType: String) -> String {
        (provider.userInfo as? Archive.Node)?.name ?? "Untitled"
    }

    func operationQueue(for provider: NSFilePromiseProvider) -> OperationQueue { queue }

    func filePromiseProvider(_ provider: NSFilePromiseProvider, writePromiseTo url: URL,
                             completionHandler: @escaping (Error?) -> Void) {
        guard let node = provider.userInfo as? Archive.Node else { return completionHandler(nil) }
        events.started(node.name)
        let job = ProgressJob { [events] in events.progress($0.fraction) }
        switch archive.extractEntry(node.id, to: url, job: job) {
        case .success, .cancelled:
            events.finished(nil)
            completionHandler(nil)
        case .exists:
            let message = String(localized: "“\(url.lastPathComponent)” already exists.")
            events.finished(message)
            completionHandler(CocoaError(.fileWriteFileExists))
        case .failure(let message):
            events.finished(message)
            completionHandler(CocoaError(.fileWriteUnknown))
        }
    }
}
