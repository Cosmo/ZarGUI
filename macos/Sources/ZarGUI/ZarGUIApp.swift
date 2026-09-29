import SwiftUI

@main
struct ZarGUIApp: App {
    @StateObject private var model = PackModel()

    var body: some Scene {
        Window("ZarGUI", id: "main") {
            ContentView()
                .environmentObject(model)
                // Folders dropped on the Dock icon or opened via Finder.
                .onOpenURL { model.add([$0]) }
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 360, height: 240)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Choose Folder…") { model.chooseFoldersToPack() }
                    .keyboardShortcut("o")
            }
        }
    }
}
