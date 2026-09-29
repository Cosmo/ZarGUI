import AppKit
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: PackModel
    @State private var isTargeted = false

    var body: some View {
        VStack(spacing: 16) {
            dropZone
            outputRow
        }
        .padding(16)
        .frame(minWidth: 340, minHeight: 220)
        .dropDestination(for: URL.self) { urls, _ in
            model.add(urls)
            return true
        } isTargeted: { isTargeted = $0 }
        .alert("Replace existing archive?",
               isPresented: Binding(get: { model.overwriteRequest != nil },
                                    set: { if !$0 { model.confirmOverwrite(false) } }),
               presenting: model.overwriteRequest) { _ in
            Button("Replace", role: .destructive) { model.confirmOverwrite(true) }
            Button("Cancel", role: .cancel) {}
        } message: { url in
            Text("“\(url.lastPathComponent)” already exists in this location. Replacing it can’t be undone.")
        }
    }

    // MARK: Drop zone

    private var dropZone: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(isTargeted ? Color.accentColor.opacity(0.12) : Color.clear)
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(isTargeted ? Color.accentColor : Color.secondary.opacity(0.5),
                              style: StrokeStyle(lineWidth: 2, dash: [8, 6]))
            content.padding(16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .animation(.easeOut(duration: 0.15), value: isTargeted)
    }

    @ViewBuilder private var content: some View {
        switch model.phase {
        case .idle:
            VStack(spacing: 10) {
                Image(systemName: "archivebox")
                    .font(.system(size: 40, weight: .light))
                    .foregroundStyle(.secondary)
                    .accessibilityHidden(true)
                Text("Drop a folder to create a .zar archive")
                    .font(.headline)
                Button("Choose Folder…") { model.chooseFoldersToPack() }
            }
        case .packing(let name, let fraction, let file):
            VStack(spacing: 10) {
                Text("Archiving “\(name)”").font(.headline)
                if let fraction {
                    ProgressView(value: fraction)
                        .accessibilityLabel("Archiving progress")
                        .accessibilityValue("\(Int(fraction * 100)) percent")
                } else {
                    ProgressView().progressViewStyle(.linear)
                }
                Text(file.isEmpty ? " " : file)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if model.queuedCount > 0 {
                    Text("\(model.queuedCount) more in queue")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Button("Cancel") { model.cancel() }
                    .keyboardShortcut(.cancelAction)
            }
        case .done(let url):
            VStack(spacing: 10) {
                Image(systemName: "checkmark.circle.fill")
                    .font(.system(size: 40))
                    .foregroundStyle(.green)
                    .accessibilityHidden(true)
                Text("Created “\(url.lastPathComponent)”").font(.headline)
                Text("Drop another folder to continue.")
                    .font(.caption).foregroundStyle(.secondary)
                Button("Show in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([url])
                }
            }
        case .failed(let message):
            VStack(spacing: 10) {
                Image(systemName: "exclamationmark.triangle.fill")
                    .font(.system(size: 40))
                    .foregroundStyle(.yellow)
                    .accessibilityHidden(true)
                Text("Couldn’t create the archive").font(.headline)
                Text(message)
                    .font(.callout).foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .textSelection(.enabled)
            }
        }
    }

    // MARK: Output row

    private var outputRow: some View {
        HStack {
            Text("Save in:")
            Text(model.outputFolder?.abbreviatingWithTilde ?? String(localized: "Next to the original folder"))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer()
            if model.outputFolder != nil {
                Button("Reset") { model.outputFolder = nil }
                    .disabled(model.isBusy)
            }
            Button("Choose…") { model.chooseOutputFolder() }
                .disabled(model.isBusy)
        }
    }
}

private extension URL {
    var abbreviatingWithTilde: String { (path as NSString).abbreviatingWithTildeInPath }
}
