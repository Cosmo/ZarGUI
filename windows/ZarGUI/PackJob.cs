using System.Runtime.InteropServices;

namespace ZarGUI;

/// <summary>One blocking packing run; call <see cref="Run"/> off the UI thread.</summary>
internal sealed class PackJob
{
    public enum Kind { Success, Cancelled, Exists, Failure }
    public sealed record Outcome(Kind Kind, string Text);

    private const int BufferSize = 32768;
    private volatile bool _cancel;
    private readonly Action<ulong, ulong, string> _onProgress;

    public PackJob(Action<ulong, ulong, string> onProgress) => _onProgress = onProgress;

    public void Cancel() => _cancel = true;

    public Outcome Run(string input, string? outputFolder, bool overwrite)
    {
        IntPtr inPtr = Marshal.StringToCoTaskMemUTF8(input);
        IntPtr outFolderPtr = outputFolder is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(outputFolder);
        IntPtr outPath = Marshal.AllocHGlobal(BufferSize);
        IntPtr err = Marshal.AllocHGlobal(1024);
        Marshal.WriteByte(outPath, 0);
        Marshal.WriteByte(err, 0);
        Native.ProgressCallback callback = OnProgress; // keep alive for the whole call
        try
        {
            var options = new Native.Options
            {
                InputDir = inPtr,
                Output = outFolderPtr,
                Overwrite = overwrite ? 1 : 0,
                ProgressFn = Marshal.GetFunctionPointerForDelegate(callback),
            };
            int status = Native.zarpack_pack(ref options, outPath, BufferSize, err, 1024);
            return status switch
            {
                Native.Ok => new Outcome(Kind.Success, Marshal.PtrToStringUTF8(outPath) ?? ""),
                Native.Cancelled => new Outcome(Kind.Cancelled, ""),
                Native.ErrOutputExists => new Outcome(Kind.Exists, ResolveOutput(inPtr, outFolderPtr)),
                _ => new Outcome(Kind.Failure, Marshal.PtrToStringUTF8(err) ?? "Unknown error."),
            };
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            return new Outcome(Kind.Failure, "zarpack.dll is missing or damaged. Reinstall ZarGUI.");
        }
        finally
        {
            GC.KeepAlive(callback);
            Marshal.FreeCoTaskMem(inPtr);
            if (outFolderPtr != IntPtr.Zero) Marshal.FreeCoTaskMem(outFolderPtr);
            Marshal.FreeHGlobal(outPath);
            Marshal.FreeHGlobal(err);
        }
    }

    private int OnProgress(IntPtr progress, IntPtr user)
    {
        var p = Marshal.PtrToStructure<Native.Progress>(progress);
        string file = p.CurrentFile == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(p.CurrentFile) ?? "";
        _onProgress(p.BytesDone, p.BytesTotal, file);
        return _cancel ? 1 : 0;
    }

    private static string ResolveOutput(IntPtr input, IntPtr outFolder)
    {
        IntPtr buf = Marshal.AllocHGlobal(BufferSize);
        try
        {
            Marshal.WriteByte(buf, 0);
            Native.zarpack_resolve_output(input, outFolder, buf, BufferSize);
            return Marshal.PtrToStringUTF8(buf) ?? "";
        }
        finally { Marshal.FreeHGlobal(buf); }
    }
}
