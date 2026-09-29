using System.Runtime.InteropServices;

namespace ZarGUI;

/// <summary>P/Invoke surface of core/include/zarpack.h. Strings are UTF-8.</summary>
internal static class Native
{
    private const string Lib = "zarpack";

    public const int Ok = 0, Cancelled = 1, ErrInput = 2, ErrOutputExists = 3, ErrIo = 4;

    [StructLayout(LayoutKind.Sequential)]
    public struct Progress
    {
        public ulong BytesDone, BytesTotal, FilesDone, FilesTotal;
        public IntPtr CurrentFile;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Options
    {
        public IntPtr InputDir;
        public IntPtr Output;
        public int Overwrite;
        public IntPtr ProgressFn;
        public IntPtr User;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int ProgressCallback(IntPtr progress, IntPtr user);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_pack(ref Options options, IntPtr outPath, nuint outPathSize,
                                          IntPtr errMsg, nuint errMsgSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern nuint zarpack_resolve_output(IntPtr inputDir, IntPtr output,
                                                      IntPtr buf, nuint bufSize);
}
