using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace ZarGUI;

public sealed partial class SettingsWindow : Window
{
    private static SettingsWindow? _open;
    private readonly AppSettings _settings = AppSettings.Current;
    private bool _loading = true;

    private SettingsWindow()
    {
        InitializeComponent();
        Title = "ZarGUI Settings";
        SystemBackdrop = new MicaBackdrop();
        Shell.ResizeClient(this, 560, 600);

        ExistingCombo.SelectedIndex = (int)_settings.ExistingArchive;
        CompressionCombo.SelectedIndex = (int)_settings.Compression;
        SkipSystemToggle.IsOn = _settings.SkipSystemFiles;
        DestinationCombo.SelectedIndex = (int)_settings.ExtractDestination;
        RevealToggle.IsOn = _settings.RevealAfterExtract;
        UpdateOutput();
        _loading = false;
        Closed += (_, _) => _open = null;
    }

    /// <summary>Brings the settings window to the front, creating it if needed.</summary>
    public static void ShowSingle()
    {
        _open ??= new SettingsWindow();
        _open.Activate();
    }

    private void OnChanged(object sender, RoutedEventArgs e)
    {
        if (_loading) return;
        _settings.ExistingArchive = (ExistingArchive)Math.Max(0, ExistingCombo.SelectedIndex);
        _settings.Compression = (Compression)Math.Max(0, CompressionCombo.SelectedIndex);
        _settings.SkipSystemFiles = SkipSystemToggle.IsOn;
        _settings.ExtractDestination = (ExtractDestination)Math.Max(0, DestinationCombo.SelectedIndex);
        _settings.RevealAfterExtract = RevealToggle.IsOn;
        _settings.Save();
    }

    private async void OnChooseOutput(object sender, RoutedEventArgs e)
    {
        string? folder = await Shell.PickFolderAsync(this);
        if (folder is null) return;
        _settings.OutputFolder = folder;
        _settings.Save();
        UpdateOutput();
    }

    private void OnResetOutput(object sender, RoutedEventArgs e)
    {
        _settings.OutputFolder = null;
        _settings.Save();
        UpdateOutput();
    }

    private void UpdateOutput()
    {
        OutputText.Text = _settings.OutputFolder ?? "Next to the original folder";
        ResetOutputButton.Visibility = _settings.OutputFolder is null ? Visibility.Collapsed : Visibility.Visible;
    }
}
