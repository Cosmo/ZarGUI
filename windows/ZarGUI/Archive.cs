using System.Runtime.InteropServices;

namespace ZarGUI;

internal sealed class ArchiveNode
{
    public required int Index { get; init; }
    public required int Parent { get; init; }
    public required string Name { get; init; }
    public required ulong Size { get; init; }
    public required bool IsDirectory { get; init; }
    public List<ArchiveNode> Children { get; } = new();

    public string Glyph => IsDirectory ? "" : ""; // Folder, Document
    public string SizeText => Format.Bytes(Size);
}

internal sealed class ArchiveOpenException(string message) : Exception(message);

/// <summary>An opened .zar archive. Reads are thread-safe; run one extraction at a time.</summary>
internal sealed class Archive : IDisposable
{
    private IntPtr _handle;

    private Archive(IntPtr handle, string path)
    {
        _handle = handle;
        FilePath = path;
        Nodes = LoadNodes(handle);
        Roots = Nodes.Where(n => n.Parent < 0).ToList();
        FileCount = Nodes.Count(n => !n.IsDirectory);
        TotalSize = Roots.Aggregate(0UL, (sum, n) => sum + n.Size);
    }

    public string FilePath { get; }
    public IReadOnlyList<ArchiveNode> Nodes { get; }
    public IReadOnlyList<ArchiveNode> Roots { get; }
    public int FileCount { get; }
    public ulong TotalSize { get; }

    /// <summary>Blocking; call off the UI thread.</summary>
    public static Archive Open(string path)
    {
        var error = new byte[1024];
        int status;
        IntPtr handle;
        try
        {
            status = Native.zarpack_open(path, out handle, error, (nuint)error.Length);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            throw new ArchiveOpenException("zarpack.dll is missing or damaged. Reinstall ZarGUI.");
        }
        if (status != Native.Ok) throw new ArchiveOpenException(Native.Utf8(error));
        return new Archive(handle, path);
    }

    /// <summary>The given entries without those inside another selected folder.</summary>
    public List<int> TopLevel(IEnumerable<int> indices)
    {
        var selected = indices.ToHashSet();
        return selected.Where(i => !HasSelectedAncestor(i, selected)).Order().ToList();
    }

    /// <summary>Blocking; call off the UI thread. An empty list extracts everything.</summary>
    public Outcome Extract(IReadOnlyList<int> indices, string destination, bool overwrite, ProgressJob job)
    {
        var error = new byte[1024];
        var selection = indices.Select(i => (nuint)i).ToArray();
        int status = Native.zarpack_extract(_handle, selection, (nuint)selection.Length, destination,
                                            overwrite ? 1 : 0, job.Callback, IntPtr.Zero,
                                            error, (nuint)error.Length);
        GC.KeepAlive(job);
        return status switch
        {
            Native.Ok => new Outcome(OutcomeKind.Success),
            Native.Cancelled => new Outcome(OutcomeKind.Cancelled),
            Native.ErrOutputExists => new Outcome(OutcomeKind.Exists, Native.Utf8(error)),
            _ => new Outcome(OutcomeKind.Failure, Native.Utf8(error)),
        };
    }

    /// <summary>Extracts one entry to exactly <paramref name="target"/>. Blocking.</summary>
    public Outcome ExtractEntry(int index, string target, ProgressJob job)
    {
        var error = new byte[1024];
        int status = Native.zarpack_extract_entry(_handle, (nuint)index, target, 0, job.Callback, IntPtr.Zero,
                                                  error, (nuint)error.Length);
        GC.KeepAlive(job);
        return status switch
        {
            Native.Ok => new Outcome(OutcomeKind.Success),
            Native.Cancelled => new Outcome(OutcomeKind.Cancelled),
            Native.ErrOutputExists => new Outcome(OutcomeKind.Exists, Native.Utf8(error)),
            _ => new Outcome(OutcomeKind.Failure, Native.Utf8(error)),
        };
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        Native.zarpack_close(_handle);
        _handle = IntPtr.Zero;
    }

    private bool HasSelectedAncestor(int index, HashSet<int> selected)
    {
        for (int p = Nodes[index].Parent; p >= 0; p = Nodes[p].Parent)
            if (selected.Contains(p)) return true;
        return false;
    }

    private static List<ArchiveNode> LoadNodes(IntPtr handle)
    {
        int count = (int)Native.zarpack_entry_count(handle);
        var nodes = new List<ArchiveNode>(count);
        for (int i = 0; i < count; i++)
        {
            Native.zarpack_entry_get(handle, (nuint)i, out var e);
            var node = new ArchiveNode
            {
                Index = i,
                Parent = (int)e.Parent,
                Name = Marshal.PtrToStringUTF8(e.Name) ?? "",
                Size = e.Size,
                IsDirectory = e.IsDir != 0,
            };
            nodes.Add(node);
            if (node.Parent >= 0) nodes[node.Parent].Children.Add(node); // parents come first
        }
        return nodes;
    }
}
