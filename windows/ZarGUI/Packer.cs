using System.Runtime.InteropServices;

namespace ZarGUI;

internal static class Packer
{
    public sealed record Options(bool SkipSystemFiles, int CompressionLevel);

    /// <summary>Blocking; call off the UI thread. <paramref name="output"/> is a folder or an
    /// archive path; null means next to <paramref name="input"/>.</summary>
    public static Outcome Run(string input, string? output, bool overwrite, Options settings, ProgressJob job)
    {
        var outPath = new byte[32768];
        var error = new byte[1024];
        IntPtr inputPtr = Marshal.StringToCoTaskMemUTF8(input);
        IntPtr outputPtr = output is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(output);
        try
        {
            var options = new Native.Options
            {
                InputDir = inputPtr,
                Output = outputPtr,
                Overwrite = overwrite ? 1 : 0,
                ProgressFn = job.Callback,
                KeepSystemFiles = settings.SkipSystemFiles ? 0 : 1,
                CompressionLevel = settings.CompressionLevel,
            };
            int status = Native.zarpack_pack(ref options, outPath, (nuint)outPath.Length, error, (nuint)error.Length);
            GC.KeepAlive(job);
            return status switch
            {
                Native.Ok => new Outcome(OutcomeKind.Success, Native.Utf8(outPath)),
                Native.Cancelled => new Outcome(OutcomeKind.Cancelled),
                Native.ErrOutputExists => new Outcome(OutcomeKind.Exists, ResolveOutput(input, output)),
                _ => new Outcome(OutcomeKind.Failure, Native.Utf8(error)),
            };
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            return new Outcome(OutcomeKind.Failure, "zarpack.dll is missing or damaged. Reinstall ZarGUI.");
        }
        finally
        {
            Marshal.FreeCoTaskMem(inputPtr);
            if (outputPtr != IntPtr.Zero) Marshal.FreeCoTaskMem(outputPtr);
        }
    }

    /// <summary>Where the core will write for this input and output setting.</summary>
    public static string ResolveOutput(string input, string? output)
    {
        var buffer = new byte[32768];
        Native.zarpack_resolve_output(input, output, buffer, (nuint)buffer.Length);
        return Native.Utf8(buffer);
    }
}
