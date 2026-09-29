import CZarpack
import Foundation
import os

/// An opened .zar archive. The C handle is thread-safe for reads; the model
/// runs at most one extraction at a time.
final class Archive: @unchecked Sendable {
    struct Node: Identifiable, Hashable, Sendable {
        let id: Int            // entry index in the core
        let name: String
        let size: UInt64
        let isDirectory: Bool
        var children: [Node]?  // nil for files
    }

    struct OpenError: Error { let message: String }

    let url: URL
    let roots: [Node]
    let names: [String]      // by entry index
    let parents: [Int]       // by entry index, -1 for top level
    let fileCount: Int
    let totalSize: UInt64
    fileprivate let handle: OpaquePointer

    init(url: URL) throws {
        var raw: OpaquePointer?
        var err = [CChar](repeating: 0, count: 1024)
        let status = url.path.withCString { zarpack_open($0, &raw, &err, err.count) }
        guard status == ZARPACK_OK, let raw else { throw OpenError(message: String(cString: err)) }
        handle = raw
        self.url = url

        // Entries arrive parents-first; build the tree bottom-up.
        let count = zarpack_entry_count(raw)
        var nodes: [Node] = []
        var parents: [Int] = []
        nodes.reserveCapacity(count)
        var files = 0
        var total: UInt64 = 0
        for i in 0..<count {
            var e = zarpack_entry()
            zarpack_entry_get(raw, i, &e)
            let isDir = e.is_dir != 0
            nodes.append(Node(id: i, name: String(cString: e.name), size: e.size,
                              isDirectory: isDir, children: isDir ? [] : nil))
            parents.append(Int(e.parent))
            if !isDir { files += 1; total += e.size }
        }
        // Children have higher indices than their parent, so walking backwards
        // finishes each subtree before it is copied into its parent.
        var topLevel: [Node] = []
        for i in stride(from: count - 1, through: 0, by: -1) {
            nodes[i].children?.reverse()
            if parents[i] >= 0 { nodes[parents[i]].children!.append(nodes[i]) }
            else { topLevel.append(nodes[i]) }
        }
        roots = topLevel.reversed()
        names = nodes.map(\.name)
        self.parents = parents
        fileCount = files
        totalSize = total
    }

    deinit { zarpack_close(handle) }

    enum Outcome: Sendable {
        case success
        case cancelled
        case exists
        case failure(String)
    }

    /// Blocking; call off the main thread. Empty `indices` extracts everything.
    func extract(_ indices: [Int], to destination: URL, overwrite: Bool, job: ProgressJob) -> Outcome {
        var err = [CChar](repeating: 0, count: 1024)
        let selection = indices.map { Int($0) }
        let context = Unmanaged.passUnretained(job).toOpaque()
        let status = selection.withUnsafeBufferPointer { buf in
            destination.path.withCString { dest in
                zarpack_extract(handle, buf.baseAddress, buf.count, dest, overwrite ? 1 : 0,
                                ProgressJob.callback, context, &err, err.count)
            }
        }
        switch status {
        case ZARPACK_OK: return .success
        case ZARPACK_CANCELLED: return .cancelled
        case ZARPACK_ERR_OUTPUT_EXISTS: return .exists
        default: return .failure(String(cString: err))
        }
    }
}

extension Archive {
    /// Extracts one entry to exactly `target` (a drag and drop). Blocking.
    func extractEntry(_ index: Int, to target: URL, job: ProgressJob) -> Outcome {
        var err = [CChar](repeating: 0, count: 1024)
        let context = Unmanaged.passUnretained(job).toOpaque()
        let status = target.path.withCString {
            zarpack_extract_entry(handle, index, $0, 0, ProgressJob.callback, context, &err, err.count)
        }
        switch status {
        case ZARPACK_OK: return .success
        case ZARPACK_CANCELLED: return .cancelled
        case ZARPACK_ERR_OUTPUT_EXISTS: return .exists
        default: return .failure(String(cString: err))
        }
    }
}

/// Relays core progress callbacks and a cancel flag.
final class ProgressJob: @unchecked Sendable {
    struct Progress: Sendable {
        var bytesDone: UInt64
        var bytesTotal: UInt64
        var currentFile: String
        var fraction: Double? { bytesTotal > 0 ? Double(bytesDone) / Double(bytesTotal) : nil }
    }

    private let cancelFlag = OSAllocatedUnfairLock(initialState: false)
    private let onProgress: @Sendable (Progress) -> Void

    init(onProgress: @escaping @Sendable (Progress) -> Void) { self.onProgress = onProgress }

    func cancel() { cancelFlag.withLock { $0 = true } }

    static let callback: zarpack_progress_fn = { progress, user in
        guard let progress, let user else { return 0 }
        let job = Unmanaged<ProgressJob>.fromOpaque(user).takeUnretainedValue()
        let file = progress.pointee.current_file.map { String(cString: $0) } ?? ""
        job.onProgress(Progress(bytesDone: progress.pointee.bytes_done,
                                bytesTotal: progress.pointee.bytes_total, currentFile: file))
        return job.cancelFlag.withLock { $0 } ? 1 : 0
    }
}
