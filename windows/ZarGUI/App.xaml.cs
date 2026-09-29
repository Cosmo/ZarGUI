using Microsoft.UI.Xaml;

namespace ZarGUI;

public partial class App : Application
{
    private Window? _window;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Folders passed on the command line (drop on ZarGUI.exe, "Open with", shortcuts).
        var folders = Environment.GetCommandLineArgs().Skip(1).Where(Directory.Exists).ToList();
        _window = new MainWindow(folders);
        _window.Activate();
    }
}
