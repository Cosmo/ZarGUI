import CZarpack
import Foundation

enum Packer {
    enum Outcome: Sendable {
        case success(URL)
        case cancelled
        case exists(URL)
        case failure(String)
    }

    struct Options: Sendable {
        var skipSystemFiles = true
        var compressionLevel: Int32 = 0
    }

    /// Blocking; call off the main thread. `output` is a folder or an archive path; nil means next to `input`.
    static func run(input: URL, output: URL?, overwrite: Bool, options: Options, job: ProgressJob) -> Outcome {
        var outPath = [CChar](repeating: 0, count: 4096)
        var errMsg = [CChar](repeating: 0, count: 1024)
        let context = Unmanaged.passUnretained(job).toOpaque()

        let status: zarpack_status = withOptionalCString(output?.path) { outputPath in
            input.path.withCString { inputPath in
                var raw = zarpack_options()
                raw.input_dir = inputPath
                raw.output = outputPath
                raw.overwrite = overwrite ? 1 : 0
                raw.progress = ProgressJob.callback
                raw.user = context
                raw.keep_system_files = options.skipSystemFiles ? 0 : 1
                raw.compression_level = options.compressionLevel
                return zarpack_pack(&raw, &outPath, outPath.count, &errMsg, errMsg.count)
            }
        }

        switch status {
        case ZARPACK_OK:
            return .success(URL(fileURLWithPath: String(cString: outPath)))
        case ZARPACK_CANCELLED:
            return .cancelled
        case ZARPACK_ERR_OUTPUT_EXISTS:
            return .exists(resolvedOutput(input: input, output: output))
        default:
            return .failure(String(cString: errMsg))
        }
    }

    /// Where the core will write for this input and output setting.
    static func resolvedOutput(input: URL, output: URL?) -> URL {
        var buf = [CChar](repeating: 0, count: 4096)
        _ = withOptionalCString(output?.path) { output in
            input.path.withCString { zarpack_resolve_output($0, output, &buf, buf.count) }
        }
        return URL(fileURLWithPath: String(cString: buf))
    }
}

private func withOptionalCString<R>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
    if let string { return string.withCString { body($0) } }
    return body(nil)
}
