import AppKit
import SwiftUI

struct ArchiveView: View {
    @StateObject private var model: ArchiveModel

    init(url: URL) { _model = StateObject(wrappedValue: ArchiveModel(url: url)) }

    var body: some View {
        Group {
            switch model.load {
            case .loading:
                ProgressView("Opening…").frame(maxWidth: .infinity, maxHeight: .infinity)
            case .failed(let message):
                ContentUnavailableView {
                    Label("Can’t Open Archive", systemImage: "exclamationmark.triangle")
                } description: {
                    Text(message)
                }
            case .loaded(let archive):
                contents(archive)
            }
        }
        .frame(minWidth: 420, minHeight: 280)
        .navigationTitle(model.url.lastPathComponent)
        .navigationDocument(model.url)
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                Button {
                    model.extract(model.selection)
                } label: {
                    Label("Extract Selected", systemImage: "square.and.arrow.down")
                }
                .help("Extract the selected items")
                .disabled(model.archive == nil || model.selection.isEmpty || model.isExtracting)

                Button {
                    model.extractAll()
                } label: {
                    Label("Extract All", systemImage: "square.and.arrow.down.on.square")
                }
                .help("Extract everything into a new folder")
                .disabled(model.archive == nil || model.isExtracting)
            }
        }
        .sheet(isPresented: Binding(get: { !isIdle }, set: { if !$0 { model.dismissExtraction() } })) {
            ExtractionSheet(model: model)
        }
        .alert("Replace existing items?",
               isPresented: Binding(get: { model.replaceRequest != nil },
                                    set: { if !$0 { model.confirmReplace(false) } })) {
            Button("Replace", role: .destructive) { model.confirmReplace(true) }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Some items already exist in the destination. Replacing them can’t be undone.")
        }
    }

    private var isIdle: Bool {
        if case .idle = model.extraction { return true }
        return false
    }

    @ViewBuilder private func contents(_ archive: Archive) -> some View {
        if let writer = model.promiseWriter {
            ArchiveOutline(archive: archive, promiseWriter: writer, selection: $model.selection,
                           onExtract: { model.extract($0) })
                .safeAreaInset(edge: .bottom, spacing: 0) { statusBar(archive) }
        }
    }

    private func statusBar(_ archive: Archive) -> some View {
        VStack(spacing: 0) {
            Divider()
            Group {
                if let export = model.dragExport {
                    HStack(spacing: 8) {
                        Text("Copying “\(export.name)”…").lineLimit(1).truncationMode(.middle)
                        if let fraction = export.fraction {
                            ProgressView(value: fraction).frame(width: 100)
                        }
                    }
                } else {
                    Text(status(archive))
                }
            }
            .font(.callout)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 5)
        }
        .background(.bar)
    }

    private func status(_ archive: Archive) -> String {
        let size = archive.totalSize.formatted(.byteCount(style: .file))
        let files = archive.fileCount == 1 ? String(localized: "1 file") : String(localized: "\(archive.fileCount) files")
        if model.selection.isEmpty { return "\(files), \(size)" }
        return String(localized: "\(model.selection.count) of \(archive.names.count) selected")
    }
}

private struct ExtractionSheet: View {
    @ObservedObject var model: ArchiveModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            switch model.extraction {
            case .idle:
                EmptyView()
            case .running(let fraction, let file):
                Text("Extracting…").font(.headline)
                if let fraction {
                    ProgressView(value: fraction)
                        .accessibilityLabel("Extraction progress")
                        .accessibilityValue("\(Int(fraction * 100)) percent")
                } else {
                    ProgressView().progressViewStyle(.linear)
                }
                Text(file.isEmpty ? " " : file)
                    .font(.caption).foregroundStyle(.secondary)
                    .lineLimit(1).truncationMode(.middle)
                HStack {
                    Spacer()
                    Button("Cancel") { model.cancelExtraction() }
                        .keyboardShortcut(.cancelAction)
                }
            case .done(let urls):
                Label("Extraction complete", systemImage: "checkmark.circle.fill")
                    .font(.headline)
                    .symbolRenderingMode(.multicolor)
                HStack {
                    Spacer()
                    Button("Show in Finder") {
                        NSWorkspace.shared.activateFileViewerSelecting(urls)
                        model.dismissExtraction()
                    }
                    Button("Done") { model.dismissExtraction() }
                        .keyboardShortcut(.defaultAction)
                }
            case .failed(let message):
                Label("Couldn’t extract", systemImage: "exclamationmark.triangle.fill")
                    .font(.headline)
                    .symbolRenderingMode(.multicolor)
                Text(message)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Spacer()
                    Button("OK") { model.dismissExtraction() }
                        .keyboardShortcut(.defaultAction)
                }
            }
        }
        .padding(20)
        .frame(width: 360)
        .interactiveDismissDisabled(model.isExtracting)
    }
}
