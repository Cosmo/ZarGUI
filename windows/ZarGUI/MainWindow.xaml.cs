using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.System;

namespace ZarGUI;

public sealed partial class MainWindow : Window
{
    private enum View { Idle, Packing, Done, Failed }

    private readonly Queue<string> _queue = new();
    private ProgressJob? _job;
    private bool _running;
    private bool _overwriteNext;
    private bool _dialogOpen;
    private string? _lastArchive;

    public MainWindow(IEnumerable<string> initialItems)
    {
        InitializeComponent();
        Title = "ZarGUI";
        SystemBackdrop = new MicaBackdrop();
        Shell.ResizeClient(this, 360, 220);
        Root.Loaded += (_, _) =>
        {
            UpdateOutputRow();
            Open(initialItems);
        };
        // Ctrl+, has no named VirtualKey, so it is registered here rather than in XAML.
        var settingsKey = new KeyboardAccelerator { Key = (VirtualKey)188, Modifiers = VirtualKeyModifiers.Control };
        settingsKey.Invoked += OnSettingsAccelerator;
        Root.KeyboardAccelerators.Add(settingsKey);
        AppSettings.Changed += UpdateOutputRow;
        Closed += (_, _) => AppSettings.Changed -= UpdateOutputRow;
    }

    /// <summary>Archives open in their own window; folders are packed.</summary>
    private void Open(IEnumerable<string> paths)
    {
        var folders = new List<string>();
        foreach (string path in paths)
        {
            if (Shell.IsArchive(path)) Shell.OpenArchiveWindow(path);
            else if (Directory.Exists(path)) folders.Add(path);
            else if (!_running) ShowFailed("Drop a folder to archive it, or a .zar file to open it.");
        }
        foreach (string folder in folders) _queue.Enqueue(folder);
        UpdateQueueText();
        StartNextIfIdle();
    }

    private void Show(View view)
    {
        IdlePanel.Visibility = view == View.Idle ? Visibility.Visible : Visibility.Collapsed;
        PackingPanel.Visibility = view == View.Packing ? Visibility.Visible : Visibility.Collapsed;
        DonePanel.Visibility = view == View.Done ? Visibility.Visible : Visibility.Collapsed;
        FailedPanel.Visibility = view == View.Failed ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ShowFailed(string message)
    {
        FailedText.Text = message;
        Show(View.Failed);
    }

    private void SetTargeted(bool targeted)
    {
        var v = targeted ? Visibility.Visible : Visibility.Collapsed;
        DropFill.Visibility = DropStrokeActive.Visibility = v;
    }

    private void UpdateOutputRow()
    {
        string? folder = AppSettings.Current.OutputFolder;
        OutputText.Text = folder ?? "Next to the original folder";
        ResetButton.Visibility = folder is null ? Visibility.Collapsed : Visibility.Visible;
        ChooseOutputButton.IsEnabled = ResetButton.IsEnabled = !_running;
    }

    private void UpdateQueueText()
    {
        QueueText.Visibility = _queue.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        QueueText.Text = $"{_queue.Count} more in queue";
    }

    private void OnDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        e.AcceptedOperation = DataPackageOperation.Copy;
        e.DragUIOverride.Caption = "Archive or open";
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
            Open(items.Select(item => item.Path));
        }
        finally
        {
            deferral.Complete();
        }
    }

    private void StartNextIfIdle()
    {
        if (_running || _dialogOpen || _queue.Count == 0) return;
        string input = _queue.Dequeue();
        var settings = AppSettings.Current;
        bool confirmedReplace = _overwriteNext;
        _overwriteNext = false;
        string? output = confirmedReplace ? settings.OutputFolder : OutputFor(input);
        bool overwrite = confirmedReplace || settings.ExistingArchive == ExistingArchive.Replace;
        var options = settings.PackOptions;
        _running = true;

        string name = Path.GetFileName(input.TrimEnd('\\', '/'));
        PackingTitle.Text = $"Archiving “{(name.Length > 0 ? name : input)}”";
        PackingFile.Text = "";
        Bar.IsIndeterminate = true;
        UpdateQueueText();
        UpdateOutputRow();
        Show(View.Packing);

        var job = new ProgressJob(p => DispatcherQueue.TryEnqueue(() => ShowProgress(p)));
        _job = job;
        Task.Run(() => Packer.Run(input, output, overwrite, options, job))
            .ContinueWith(t => DispatcherQueue.TryEnqueue(() => Finished(t.Result, input)), TaskScheduler.Default);
    }

    /// <summary>The save folder, or with "Keep both" a free "Name (2).zar"-style path.</summary>
    private static string? OutputFor(string input)
    {
        string? folder = AppSettings.Current.OutputFolder;
        if (AppSettings.Current.ExistingArchive != ExistingArchive.KeepBoth) return folder;
        string archive = Packer.ResolveOutput(input, folder);
        string stem = Path.GetFileNameWithoutExtension(archive);
        string parent = Path.GetDirectoryName(archive) ?? "";
        string candidate = archive;
        for (int n = 2; File.Exists(candidate); n++)
            candidate = Path.Combine(parent, $"{stem} ({n}).zar");
        return candidate;
    }

    private void ShowProgress(ProgressJob.Progress p)
    {
        if (!_running) return;
        if (p.Fraction is double fraction)
        {
            Bar.IsIndeterminate = false;
            Bar.Value = fraction;
        }
        PackingFile.Text = p.CurrentFile;
    }

    private async void Finished(Outcome outcome, string input)
    {
        _running = false;
        _job = null;
        UpdateOutputRow();

        switch (outcome.Kind)
        {
            case OutcomeKind.Success:
                _lastArchive = outcome.Text;
                DoneTitle.Text = $"Created “{Path.GetFileName(outcome.Text)}”";
                Show(View.Done);
                break;
            case OutcomeKind.Cancelled:
                Show(View.Idle);
                break;
            case OutcomeKind.Exists:
                Show(View.Idle);
                await AskToReplaceAsync(input, outcome.Text);
                return;
            case OutcomeKind.Failure:
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
        bool replace = await dialog.ShowAsync() == ContentDialogResult.Primary;
        _dialogOpen = false;
        if (replace)
        {
            _overwriteNext = true;
            var rest = _queue.ToList();
            _queue.Clear();
            _queue.Enqueue(input);
            foreach (string r in rest) _queue.Enqueue(r);
        }
        StartNextIfIdle();
    }

    private void OnCancel(object sender, RoutedEventArgs e)
    {
        _queue.Clear();
        UpdateQueueText();
        _job?.Cancel();
    }

    private void OnShowInExplorer(object sender, RoutedEventArgs e)
    {
        if (_lastArchive is not null) Shell.Reveal(_lastArchive);
    }

    private async void OnChooseFolder(object sender, RoutedEventArgs e)
    {
        string? folder = await Shell.PickFolderAsync(this);
        if (folder is not null) Open(new[] { folder });
    }

    private async void OnOpenArchive(object sender, RoutedEventArgs e) => await OpenArchivesAsync();

    private void OnOpenArchiveAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        _ = OpenArchivesAsync();
    }

    private async Task OpenArchivesAsync() => Open(await Shell.PickArchivesAsync(this));

    private async void OnChooseOutput(object sender, RoutedEventArgs e)
    {
        string? folder = await Shell.PickFolderAsync(this);
        if (folder is null) return;
        AppSettings.Current.OutputFolder = folder;
        AppSettings.Current.Save();
    }

    private void OnResetOutput(object sender, RoutedEventArgs e)
    {
        AppSettings.Current.OutputFolder = null;
        AppSettings.Current.Save();
    }

    private void OnOpenSettings(object sender, RoutedEventArgs e) => SettingsWindow.ShowSingle();

    private void OnSettingsAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = true;
        SettingsWindow.ShowSingle();
    }
}
