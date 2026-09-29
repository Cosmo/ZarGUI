using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;

namespace ZarGUI;

public sealed partial class ArchiveWindow : Window
{
    private readonly string _path;
    private Archive? _archive;
    private ProgressJob? _job;
    private Task? _extraction;

    public ArchiveWindow(string path)
    {
        InitializeComponent();
        _path = path;
        Title = Path.GetFileName(path);
        SystemBackdrop = new MicaBackdrop();
        Shell.ResizeClient(this, 640, 480);
        Root.Loaded += async (_, _) => await LoadAsync();
        Closed += OnClosed;
    }

    private async Task LoadAsync()
    {
        try
        {
            _archive = await Task.Run(() => Archive.Open(_path));
            Tree.ItemsSource = _archive.Roots;
            ExtractAllButton.IsEnabled = true;
            UpdateStatus();
        }
        catch (ArchiveOpenException ex)
        {
            ErrorText.Text = ex.Message;
            ErrorPanel.Visibility = Visibility.Visible;
        }
        finally
        {
            LoadingRing.IsActive = false;
            LoadingRing.Visibility = Visibility.Collapsed;
        }
    }

    private List<int> SelectedIndices() =>
        Tree.SelectedItems.OfType<ArchiveNode>().Select(n => n.Index).ToList();

    private void OnSelectionChanged(TreeView sender, TreeViewSelectionChangedEventArgs args) => UpdateStatus();

    private void UpdateStatus()
    {
        if (_archive is null) return;
        int selected = Tree.SelectedItems.Count;
        StatusText.Text = selected > 0
            ? $"{selected} of {_archive.Nodes.Count} selected"
            : $"{_archive.FileCount} {(_archive.FileCount == 1 ? "file" : "files")}, {Format.Bytes(_archive.TotalSize)}";
        ExtractSelectedButton.IsEnabled = selected > 0 && _extraction is null;
        ExtractAllButton.IsEnabled = _extraction is null;
    }

    private async void OnExtractSelected(object sender, RoutedEventArgs e)
    {
        if (_archive is null) return;
        var roots = _archive.TopLevel(SelectedIndices());
        if (roots.Count == 0) return;
        string? folder = await ChooseDestinationAsync();
        if (folder is null) return;
        string reveal = roots.Count == 1 ? Path.Combine(folder, _archive.Nodes[roots[0]].Name) : folder;
        await ExtractAsync(roots, folder, reveal);
    }

    private async void OnExtractAll(object sender, RoutedEventArgs e)
    {
        string? folder = await ChooseDestinationAsync();
        if (folder is null) return;
        string target = UniqueFolder(folder, Path.GetFileNameWithoutExtension(_path));
        await ExtractAsync(new List<int>(), target, target);
    }

    /// <summary>The folder containing the archive, or one the user picks, depending on the setting.</summary>
    private Task<string?> ChooseDestinationAsync() =>
        AppSettings.Current.ExtractDestination == ExtractDestination.NextToArchive
            ? Task.FromResult(Path.GetDirectoryName(_path))
            : Shell.PickFolderAsync(this);

    private void OnCancel(object sender, RoutedEventArgs e) => _job?.Cancel();

    /// <summary>Dragging items out: they are extracted only when dropped (delayed rendering).</summary>
    private void OnDragItemsStarting(TreeView sender, TreeViewDragItemsStartingEventArgs args)
    {
        if (_archive is null)
        {
            args.Cancel = true;
            return;
        }
        var indices = _archive.TopLevel(args.Items.OfType<ArchiveNode>().Select(n => n.Index));
        args.Data.RequestedOperation = DataPackageOperation.Copy;
        args.Data.SetDataProvider(StandardDataFormats.StorageItems, request => ProvideDraggedItems(request, indices));
    }

    private async void ProvideDraggedItems(DataProviderRequest request, List<int> indices)
    {
        var deferral = request.GetDeferral();
        try
        {
            var items = await ExtractForDragAsync(indices);
            if (items is not null) request.SetData(items);
        }
        finally
        {
            deferral.Complete();
        }
    }

    private async Task<List<IStorageItem>?> ExtractForDragAsync(List<int> indices)
    {
        if (_archive is not { } archive) return null;
        string folder = Shell.NewDragFolder();
        var items = new List<IStorageItem>();
        foreach (int index in indices)
        {
            var node = archive.Nodes[index];
            string target = Path.Combine(folder, node.Name);
            StatusText.Text = $"Preparing “{node.Name}”…";
            var outcome = await Task.Run(() => archive.ExtractEntry(index, target, new ProgressJob(_ => { })));
            if (outcome.Kind != OutcomeKind.Success)
            {
                UpdateStatus();
                if (outcome.Kind == OutcomeKind.Failure) await ConfirmAsync("Couldn’t extract", outcome.Text, primary: null);
                return null;
            }
            items.Add(node.IsDirectory ? await StorageFolder.GetFolderFromPathAsync(target)
                                       : await StorageFile.GetFileFromPathAsync(target));
        }
        UpdateStatus();
        return items;
    }

    private async Task ExtractAsync(List<int> indices, string destination, string reveal, bool overwrite = false)
    {
        if (_archive is null || _extraction is not null) return;
        var archive = _archive;
        var job = new ProgressJob(p => DispatcherQueue.TryEnqueue(() => ShowProgress(p)));
        _job = job;
        ShowExtracting(true);
        var task = Task.Run(() => archive.Extract(indices, destination, overwrite, job));
        _extraction = task;
        Outcome outcome = await task;
        _extraction = null;
        _job = null;
        ShowExtracting(false);

        switch (outcome.Kind)
        {
            case OutcomeKind.Success when AppSettings.Current.RevealAfterExtract:
                Shell.Reveal(reveal);
                break;
            case OutcomeKind.Success:
                await ShowDoneAsync(reveal);
                break;
            case OutcomeKind.Exists:
                if (await ConfirmAsync("Replace existing items?",
                                       "Some items already exist in the destination. Replacing them can’t be undone.",
                                       "Replace"))
                    await ExtractAsync(indices, destination, reveal, overwrite: true);
                break;
            case OutcomeKind.Failure:
                await ConfirmAsync("Couldn’t extract", outcome.Text, primary: null);
                break;
        }
    }

    private void ShowExtracting(bool extracting)
    {
        var visible = extracting ? Visibility.Visible : Visibility.Collapsed;
        ProgressPanel.Visibility = CancelButton.Visibility = visible;
        StatusText.Visibility = extracting ? Visibility.Collapsed : Visibility.Visible;
        Bar.IsIndeterminate = true;
        ProgressFile.Text = "";
        UpdateStatus();
    }

    private void ShowProgress(ProgressJob.Progress p)
    {
        if (_job is null) return;
        if (p.Fraction is double fraction)
        {
            Bar.IsIndeterminate = false;
            Bar.Value = fraction;
        }
        ProgressFile.Text = p.CurrentFile;
    }

    private async Task ShowDoneAsync(string reveal)
    {
        if (await ConfirmAsync("Extraction complete", $"Extracted to “{Path.GetFileName(reveal)}”.",
                               "Show in File Explorer", close: "Close"))
            Shell.Reveal(reveal);
    }

    /// <summary>Shows a dialog; true if the primary button was chosen.</summary>
    private async Task<bool> ConfirmAsync(string title, string message, string? primary, string close = "Cancel")
    {
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot,
            Title = title,
            Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true },
            CloseButtonText = primary is null ? "OK" : close,
            DefaultButton = ContentDialogButton.Close,
        };
        if (primary is not null) dialog.PrimaryButtonText = primary;
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }

    /// <summary>"Name", or "Name (2)", "Name (3)", … if taken (like File Explorer).</summary>
    private static string UniqueFolder(string parent, string name)
    {
        string candidate = Path.Combine(parent, name);
        for (int n = 2; Directory.Exists(candidate) || File.Exists(candidate); n++)
            candidate = Path.Combine(parent, $"{name} ({n})");
        return candidate;
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _job?.Cancel();
        var archive = _archive;
        if (_extraction is { } running) running.ContinueWith(_ => archive?.Dispose(), TaskScheduler.Default);
        else archive?.Dispose();
    }
}
