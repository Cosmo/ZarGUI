import AppKit
import UniformTypeIdentifiers

extension URL {
    var isZarArchive: Bool {
        pathExtension.lowercased() == "zar"
            && (try? resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
    }
}

extension Notification.Name {
    static let openItems = Notification.Name("ZarGUI.openItems")
}

/// File > Open: folders to archive or .zar files to view.
enum OpenPanel {
    @MainActor static func run() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.folder, UTType(filenameExtension: "zar") ?? .data]
        panel.message = String(localized: "Choose folders to archive or .zar files to open.")
        if panel.runModal() == .OK {
            NotificationCenter.default.post(name: .openItems, object: panel.urls)
        }
    }
}
