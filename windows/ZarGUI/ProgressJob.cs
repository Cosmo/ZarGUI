using System.Runtime.InteropServices;

namespace ZarGUI;

internal enum OutcomeKind { Success, Cancelled, Exists, Failure }

internal sealed record Outcome(OutcomeKind Kind, string Text = "");

/// <summary>Relays core progress callbacks to the UI and carries the cancel flag.</summary>
internal sealed class ProgressJob
{
    public readonly record struct Progress(ulong BytesDone, ulong BytesTotal, string CurrentFile)
    {
        public double? Fraction => BytesTotal > 0 ? (double)BytesDone / BytesTotal : null;
    }

    private readonly Action<Progress> _onProgress;
    private readonly Native.ProgressCallback _callback; // must live as long as the native call
    private volatile bool _cancel;

    public ProgressJob(Action<Progress> onProgress)
    {
        _onProgress = onProgress;
        _callback = OnProgress;
    }

    public IntPtr Callback => Marshal.GetFunctionPointerForDelegate(_callback);

    public void Cancel() => _cancel = true;

    private int OnProgress(IntPtr progress, IntPtr user)
    {
        var p = Marshal.PtrToStructure<Native.Progress>(progress);
        string file = Marshal.PtrToStringUTF8(p.CurrentFile) ?? "";
        _onProgress(new Progress(p.BytesDone, p.BytesTotal, file));
        return _cancel ? 1 : 0;
    }
}
