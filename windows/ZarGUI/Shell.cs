using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Windows.Graphics;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace ZarGUI;

/// <summary>Window and File Explorer helpers shared by the app's windows.</summary>
internal static class Shell
{
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    public static void ResizeClient(Window window, int width, int height)
    {
        double scale = GetDpiForWindow(WindowNative.GetWindowHandle(window)) / 96.0;
        window.AppWindow.ResizeClient(new SizeInt32((int)(width * scale), (int)(height * scale)));
    }

    public static async Task<string?> PickFolderAsync(Window owner)
    {
        var picker = new FolderPicker { SuggestedStartLocation = PickerLocationId.Desktop };
        picker.FileTypeFilter.Add("*");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(owner));
        return (await picker.PickSingleFolderAsync())?.Path;
    }

    public static async Task<IReadOnlyList<string>> PickArchivesAsync(Window owner)
    {
        var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.Desktop };
        picker.FileTypeFilter.Add(".zar");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(owner));
        var files = await picker.PickMultipleFilesAsync();
        return files.Select(f => f.Path).ToList();
    }

    public static bool IsArchive(string path) =>
        File.Exists(path) && string.Equals(Path.GetExtension(path), ".zar", StringComparison.OrdinalIgnoreCase);

    /// <summary>Opens File Explorer with the item selected.</summary>
    public static void Reveal(string path) =>
        Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{path}\"") { UseShellExecute = true });

    public static void OpenArchiveWindow(string path) => new ArchiveWindow(path).Activate();

    private static readonly string DragRoot = Path.Combine(Path.GetTempPath(), "ZarGUI");

    /// <summary>A new, empty folder for items dragged out of an archive. File Explorer copies from
    /// here after the drop, so these are only removed on the next start (see <see cref="CleanDragFolders"/>).</summary>
    public static string NewDragFolder()
    {
        string folder = Path.Combine(DragRoot, "drag-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        return folder;
    }

    public static void CleanDragFolders()
    {
        try
        {
            if (Directory.Exists(DragRoot)) Directory.Delete(DragRoot, recursive: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Still in use by another instance; try again next time.
        }
    }
}
