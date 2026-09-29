using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace ZarGUI;

public sealed partial class MainWindow : Window
{
    private readonly Queue<string> _queue = new();
    private PackJob? _job;
    private bool _running;
    private bool _overwriteNext;
    private bool _dialogOpen;
    private string? _outputFolder;
    private string? _lastArchive;

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    public MainWindow(IEnumerable<string> initialFolders)
    {
        InitializeComponent();
        Title = "ZarGUI";
        SystemBackdrop = new MicaBackdrop();

        // Small utility window: 360 x 220 DIPs of client area.
        double scale = GetDpiForWindow(WindowNative.GetWindowHandle(this)) / 96.0;
        AppWindow.ResizeClient(new SizeInt32((int)(360 * scale), (int)(220 * scale)));

        foreach (var f in initialFolders) _queue.Enqueue(f);
        Root.Loaded += (_, _) => StartNextIfIdle();
    }

    // ---- State display -------------------------------------------------

    private enum View { Idle, Packing, Done, Failed }

    private void Show(View view)
    {
        IdlePanel.Visibility = view == View.Idle ? Visibility.Visible : Visibility.Collapsed;
        PackingPanel.Visibility = view == View.Packing ? Visibility.Visible : Visibility.Collapsed;
        DonePanel.Visibility = view == View.Done ? Visibility.Visible : Visibility.Collapsed;
        FailedPanel.Visibility = view == View.Failed ? Visibility.Visible : Visibility.Collapsed;
    }

    private void SetTargeted(bool targeted)
    {
        var v = targeted ? Visibility.Visible : Visibility.Collapsed;
        DropFill.Visibility = DropStrokeActive.Visibility = v;
    }

    private void UpdateOutputRow()
    {
        OutputText.Text = _outputFolder ?? "Next to the original folder";
        ResetButton.Visibility = _outputFolder is null ? Visibility.Collapsed : Visibility.Visible;
        ChooseOutputButton.IsEnabled = ResetButton.IsEnabled = !_running;
    }

    private void UpdateQueueText()
    {
        QueueText.Visibility = _queue.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        QueueText.Text = $"{_queue.Count} more in queue";
    }

    // ---- Drag and drop -------------------------------------------------

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        e.AcceptedOperation = DataPackageOperation.Copy;
        e.DragUIOverride.Caption = "Create archive";
        e.DragUIOverride.IsGlyphVisible = false;
        SetTargeted(true);
    }

    private void OnDragLeave(object sender, DragEventArgs e) => SetTargeted(false);

    private async void OnDrop(object sender, DragEventArgs e)
    {
        SetTargeted(false);
        var deferral = e.GetDeferral();
        try
        {
            var items = await e.DataView.GetStorageItemsAsync();
            var folders = items.OfType<StorageFolder>().Select(f => f.Path).ToList();
            if (folders.Count == 0)
            {
                if (!_running) ShowFailed("Only folders can be archived. Drop a folder instead.");
                return;
            }
            Add(folders);
        }
        finally { deferral.Complete(); }
    }

    // ---- Queue ---------------------------------------------------------

    private void Add(IEnumerable<string> folders)
    {
        foreach (var f in folders) _queue.Enqueue(f);
        UpdateQueueText();
        StartNextIfIdle();
    }

    private void StartNextIfIdle()
    {
        if (_running || _dialogOpen || _queue.Count == 0) return;
        string input = _queue.Dequeue();
        bool overwrite = _overwriteNext;
        _overwriteNext = false;
        _running = true;

        string name = Path.GetFileName(input.TrimEnd('\\', '/'));
        if (name.Length == 0) name = input;
        PackingTitle.Text = $"Archiving “{name}”";
        PackingFile.Text = "";
        Bar.IsIndeterminate = true;
        UpdateQueueText();
        UpdateOutputRow();
        Show(View.Packing);

        PackJob? job = null;
        job = new PackJob((done, total, file) => DispatcherQueue.TryEnqueue(() =>
        {
            if (_job != job) return;
            if (total > 0)
            {
                Bar.IsIndeterminate = false;
                Bar.Value = (double)done / total;
            }
            PackingFile.Text = file;
        }));
        _job = job;

        string? output = _outputFolder;
        Task.Run(() => job.Run(input, output, overwrite))
            .ContinueWith(t => DispatcherQueue.TryEnqueue(() => Finished(t.Result, input)),
                          TaskScheduler.Default);
    }

    private async void Finished(PackJob.Outcome outcome, string input)
    {
        _running = false;
        _job = null;
        UpdateOutputRow();

        switch (outcome.Kind)
        {
            case PackJob.Kind.Success:
                _lastArchive = outcome.Text;
                DoneTitle.Text = $"Created “{Path.GetFileName(outcome.Text)}”";
                Show(View.Done);
                break;
            case PackJob.Kind.Cancelled:
                Show(View.Idle);
                break;
            case PackJob.Kind.Exists:
                Show(View.Idle);
                await AskToReplaceAsync(input, outcome.Text);
                return;
            case PackJob.Kind.Failure:
                ShowFailed(outcome.Text);
                break;
        }
        StartNextIfIdle();
    }

    private async Task AskToReplaceAsync(string input, string archivePath)
    {
        _dialogOpen = true;
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            Title = "Replace existing archive?",
            Content = $"“{Path.GetFileName(archivePath)}” already exists in this location. " +
                      "Replacing it can’t be undone.",
            PrimaryButtonText = "Replace",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Close,
        };
        var result = await dialog.ShowAsync();
        _dialogOpen = false;
        if (result == ContentDialogResult.Primary)
        {
            _overwriteNext = true;
            var rest = _queue.ToList();
            _queue.Clear();
            _queue.Enqueue(input);
            foreach (var r in rest) _queue.Enqueue(r);
        }
        StartNextIfIdle();
    }

    private void ShowFailed(string message)
    {
        FailedText.Text = message;
        Show(View.Failed);
    }

    // ---- Buttons -------------------------------------------------------

    private void OnCancel(object sender, RoutedEventArgs e)
    {
        _queue.Clear();
        UpdateQueueText();
        _job?.Cancel();
    }

    private void OnShowInExplorer(object sender, RoutedEventArgs e)
    {
        if (_lastArchive is null) return;
        Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{_lastArchive}\"")
        { UseShellExecute = true });
    }

    private FolderPicker NewPicker()
    {
        var picker = new FolderPicker { SuggestedStartLocation = PickerLocationId.Desktop };
        picker.FileTypeFilter.Add("*");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        return picker;
    }

    private async void OnChooseFolders(object sender, RoutedEventArgs e) => await ChooseFolderToPackAsync();

    private async Task ChooseFolderToPackAsync()
    {
        var folder = await NewPicker().PickSingleFolderAsync();
        if (folder is not null) Add(new[] { folder.Path });
    }

    private void OnChooseFoldersAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        _ = ChooseFolderToPackAsync();
    }

    private async void OnChooseOutput(object sender, RoutedEventArgs e)
    {
        var folder = await NewPicker().PickSingleFolderAsync();
        if (folder is null) return;
        _outputFolder = folder.Path;
        UpdateOutputRow();
    }

    private void OnResetOutput(object sender, RoutedEventArgs e)
    {
        _outputFolder = null;
        UpdateOutputRow();
    }
}
