import AppKit
import Foundation

enum ExistingArchive: String, CaseIterable {
    case ask, replace, keepBoth
}

enum ExtractDestination: String, CaseIterable {
    case ask, nextToArchive
}

enum Compression: String, CaseIterable {
    case faster, standard, smaller

    /// zstd level passed to the core; 0 is the core's default.
    var level: Int32 {
        switch self {
        case .faster: 1
        case .standard: 0
        case .smaller: 12
        }
    }
}

/// UserDefaults keys and typed access. Views bind with @AppStorage using the same keys.
enum Preferences {
    enum Key {
        static let outputFolder = "outputFolder"
        static let existingArchive = "existingArchive"
        static let compression = "compression"
        static let skipSystemFiles = "skipSystemFiles"
        static let extractDestination = "extractDestination"
        static let revealAfterExtract = "revealAfterExtract"
    }

    static func registerDefaults() {
        UserDefaults.standard.register(defaults: [
            Key.outputFolder: "",
            Key.existingArchive: ExistingArchive.ask.rawValue,
            Key.compression: Compression.standard.rawValue,
            Key.skipSystemFiles: true,
            Key.extractDestination: ExtractDestination.ask.rawValue,
            Key.revealAfterExtract: false,
        ])
    }

    private static var defaults: UserDefaults { .standard }

    /// nil means next to the original folder.
    static var outputFolder: URL? {
        get {
            let path = defaults.string(forKey: Key.outputFolder) ?? ""
            return path.isEmpty ? nil : URL(fileURLWithPath: path, isDirectory: true)
        }
        set { defaults.set(newValue?.path ?? "", forKey: Key.outputFolder) }
    }

    static var existingArchive: ExistingArchive {
        ExistingArchive(rawValue: defaults.string(forKey: Key.existingArchive) ?? "") ?? .ask
    }

    static var compression: Compression {
        Compression(rawValue: defaults.string(forKey: Key.compression) ?? "") ?? .standard
    }

    static var skipSystemFiles: Bool { defaults.bool(forKey: Key.skipSystemFiles) }

    static var extractDestination: ExtractDestination {
        ExtractDestination(rawValue: defaults.string(forKey: Key.extractDestination) ?? "") ?? .ask
    }

    static var revealAfterExtract: Bool { defaults.bool(forKey: Key.revealAfterExtract) }

    /// Asks for the folder new archives are saved in. Returns false if cancelled.
    @MainActor @discardableResult
    static func chooseOutputFolder() -> Bool {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.prompt = String(localized: "Choose")
        panel.message = String(localized: "Choose where to save new archives.")
        guard panel.runModal() == .OK, let url = panel.url else { return false }
        outputFolder = url
        return true
    }
}
