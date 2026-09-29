import CZarpack
import Foundation
import os

/// One packing run. The C callback holds an unretained pointer to this, so it
/// must outlive the `zarpack_pack` call (it does: `run` blocks).
final class PackJob: @unchecked Sendable {
    struct Progress: Sendable {
        var bytesDone: UInt64
        var bytesTotal: UInt64
        var currentFile: String
    }

    enum Outcome: Sendable {
        case success(URL)
        case cancelled
        case exists(URL)
        case failure(String)
    }

    private let cancelFlag = OSAllocatedUnfairLock(initialState: false)
    private let onProgress: @Sendable (Progress) -> Void

    init(onProgress: @escaping @Sendable (Progress) -> Void) {
        self.onProgress = onProgress
    }

    func cancel() { cancelFlag.withLock { $0 = true } }

    func run(input: URL, outputFolder: URL?, overwrite: Bool) -> Outcome {
        var outPath = [CChar](repeating: 0, count: 4096)
        var errMsg = [CChar](repeating: 0, count: 1024)
        let context = Unmanaged.passUnretained(self).toOpaque()

        let status: zarpack_status = withOptionalCString(outputFolder?.path) { output in
            input.path.withCString { inputPath in
                var options = zarpack_options()
                options.input_dir = inputPath
                options.output = output
                options.overwrite = overwrite ? 1 : 0
                options.user = context
                options.progress = { progress, user in
                    guard let progress, let user else { return 0 }
                    let job = Unmanaged<PackJob>.fromOpaque(user).takeUnretainedValue()
                    let file = progress.pointee.current_file.map { String(cString: $0) } ?? ""
                    job.onProgress(Progress(bytesDone: progress.pointee.bytes_done,
                                            bytesTotal: progress.pointee.bytes_total,
                                            currentFile: file))
                    return job.cancelFlag.withLock { $0 } ? 1 : 0
                }
                return zarpack_pack(&options, &outPath, outPath.count, &errMsg, errMsg.count)
            }
        }

        switch status {
        case ZARPACK_OK:
            return .success(URL(fileURLWithPath: String(cString: outPath)))
        case ZARPACK_CANCELLED:
            return .cancelled
        case ZARPACK_ERR_OUTPUT_EXISTS:
            return .exists(resolvedOutput(input: input, outputFolder: outputFolder))
        default:
            return .failure(String(cString: errMsg))
        }
    }

    private func resolvedOutput(input: URL, outputFolder: URL?) -> URL {
        var buf = [CChar](repeating: 0, count: 4096)
        _ = withOptionalCString(outputFolder?.path) { output in
            input.path.withCString { zarpack_resolve_output($0, output, &buf, buf.count) }
        }
        return URL(fileURLWithPath: String(cString: buf))
    }
}

private func withOptionalCString<R>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
    if let string { return string.withCString { body($0) } }
    return body(nil)
}
