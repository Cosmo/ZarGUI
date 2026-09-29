using Microsoft.UI.Xaml;

namespace ZarGUI;

public partial class App : Application
{
    private Window? _window;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Folders and .zar files passed on the command line (drop on ZarGUI.exe, "Open with", shortcuts).
        var items = Environment.GetCommandLineArgs().Skip(1).ToList();
        Shell.CleanDragFolders();
        _window = new MainWindow(items);
        _window.Activate();
    }
}
