import SwiftUI

struct SettingsView: View {
    @AppStorage(Preferences.Key.outputFolder) private var outputFolder = ""
    @AppStorage(Preferences.Key.existingArchive) private var existingArchive = ExistingArchive.ask
    @AppStorage(Preferences.Key.compression) private var compression = Compression.standard
    @AppStorage(Preferences.Key.skipSystemFiles) private var skipSystemFiles = true
    @AppStorage(Preferences.Key.extractDestination) private var extractDestination = ExtractDestination.ask
    @AppStorage(Preferences.Key.revealAfterExtract) private var revealAfterExtract = false

    var body: some View {
        Form {
            Section("Archiving") {
                Picker("Save archives in:", selection: saveLocation) {
                    Text("Next to the original folder").tag(SaveLocation.nextToFolder)
                    if !outputFolder.isEmpty {
                        Text((outputFolder as NSString).lastPathComponent).tag(SaveLocation.folder)
                    }
                    Divider()
                    Text("Other…").tag(SaveLocation.other)
                }
                Picker("If an archive already exists:", selection: $existingArchive) {
                    Text("Ask").tag(ExistingArchive.ask)
                    Text("Replace it").tag(ExistingArchive.replace)
                    Text("Keep both").tag(ExistingArchive.keepBoth)
                }
                Picker(selection: $compression) {
                    Text("Faster").tag(Compression.faster)
                    Text("Standard").tag(Compression.standard)
                    Text("Smaller").tag(Compression.smaller)
                } label: {
                    Text("Compression:")
                    Text("Smaller archives take longer to create. Every setting can be read by any .zar reader.")
                }
                Toggle(isOn: $skipSystemFiles) {
                    Text("Skip system files")
                    Text(".DS_Store, ._ files, Thumbs.db and similar")
                }
            }
            Section("Extracting") {
                Picker("Extract to:", selection: $extractDestination) {
                    Text("Ask each time").tag(ExtractDestination.ask)
                    Text("The folder containing the archive").tag(ExtractDestination.nextToArchive)
                }
                Toggle("Show extracted items in Finder", isOn: $revealAfterExtract)
            }
        }
        .formStyle(.grouped)
        .frame(width: 480)
        .fixedSize(horizontal: false, vertical: true)
    }

    private enum SaveLocation: Hashable { case nextToFolder, folder, other }

    /// "Other…" opens a folder panel, like the download location in Safari.
    private var saveLocation: Binding<SaveLocation> {
        Binding {
            outputFolder.isEmpty ? .nextToFolder : .folder
        } set: { choice in
            switch choice {
            case .nextToFolder: outputFolder = ""
            case .folder: break
            case .other: Preferences.chooseOutputFolder()
            }
        }
    }
}
