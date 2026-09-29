using System.Runtime.InteropServices;
using System.Text;

namespace ZarGUI;

/// <summary>P/Invoke surface of core/include/zarpack.h. Strings are UTF-8.</summary>
internal static class Native
{
    private const string Lib = "zarpack";

    public const int Ok = 0, Cancelled = 1, ErrInput = 2, ErrOutputExists = 3;

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
        public int KeepSystemFiles; // 0: skip .DS_Store, ._*, Thumbs.db, ...
        public int CompressionLevel; // 0: default
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Entry
    {
        public IntPtr Path;
        public IntPtr Name;
        public ulong Size;
        public long Parent;
        public int IsDir;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int ProgressCallback(IntPtr progress, IntPtr user);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_pack(ref Options options, byte[] outPath, nuint outPathSize,
                                          byte[] errMsg, nuint errMsgSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern nuint zarpack_resolve_output([MarshalAs(UnmanagedType.LPUTF8Str)] string inputDir,
                                                      [MarshalAs(UnmanagedType.LPUTF8Str)] string? output,
                                                      byte[] buf, nuint bufSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_open([MarshalAs(UnmanagedType.LPUTF8Str)] string archivePath,
                                          out IntPtr archive, byte[] errMsg, nuint errMsgSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void zarpack_close(IntPtr archive);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern nuint zarpack_entry_count(IntPtr archive);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_entry_get(IntPtr archive, nuint index, out Entry entry);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_extract(IntPtr archive, nuint[] indices, nuint count,
                                             [MarshalAs(UnmanagedType.LPUTF8Str)] string destDir, int overwrite,
                                             IntPtr progress, IntPtr user, byte[] errMsg, nuint errMsgSize);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int zarpack_extract_entry(IntPtr archive, nuint index,
                                                   [MarshalAs(UnmanagedType.LPUTF8Str)] string targetPath, int overwrite,
                                                   IntPtr progress, IntPtr user, byte[] errMsg, nuint errMsgSize);

    /// <summary>Decodes a NUL-terminated UTF-8 buffer filled by the core.</summary>
    public static string Utf8(byte[] buffer)
    {
        int length = Array.IndexOf(buffer, (byte)0);
        return Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
    }
}
