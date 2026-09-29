import SwiftUI

@main
struct ZarGUIApp: App {
    @StateObject private var model = PackModel()

    init() { Preferences.registerDefaults() }

    var body: some Scene {
        Window("ZarGUI", id: "main") {
            ContentView()
                .environmentObject(model)
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 360, height: 240)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open…") { OpenPanel.run() }
                    .keyboardShortcut("o")
            }
        }

        WindowGroup("Archive", for: URL.self) { $url in
            if let url { ArchiveView(url: url) }
        }
        .defaultSize(width: 560, height: 420)

        Settings {
            SettingsView()
        }
    }
}
