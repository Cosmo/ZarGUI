import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// Finder-style list of an archive's contents, backed by NSOutlineView for
/// native selection, type-select and dragging items out as file promises.
struct ArchiveOutline: NSViewRepresentable {
    let archive: Archive
    let promiseWriter: FilePromiseWriter
    @Binding var selection: Set<Int>
    let onExtract: (Set<Int>) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSScrollView {
        let outline = NSOutlineView()
        outline.addTableColumn(Self.column(.name, title: String(localized: "Name"), width: 320, minWidth: 150))
        outline.addTableColumn(Self.column(.size, title: String(localized: "Size"), width: 90, minWidth: 70))
        outline.outlineTableColumn = outline.tableColumns[0]
        outline.usesAlternatingRowBackgroundColors = true
        outline.allowsMultipleSelection = true
        outline.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle
        outline.setDraggingSourceOperationMask(.copy, forLocal: false)
        outline.dataSource = context.coordinator
        outline.delegate = context.coordinator
        outline.target = context.coordinator
        outline.doubleAction = #selector(Coordinator.toggleFolder(_:))
        let menu = NSMenu()
        menu.delegate = context.coordinator
        outline.menu = menu
        context.coordinator.outlineView = outline

        let scroll = NSScrollView()
        scroll.documentView = outline
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.parent = self
    }

    private static func column(_ id: NSUserInterfaceItemIdentifier, title: String, width: CGFloat,
                               minWidth: CGFloat) -> NSTableColumn {
        let column = NSTableColumn(identifier: id)
        column.title = title
        column.width = width
        column.minWidth = minWidth
        if id == .size { column.headerCell.alignment = .right }
        return column
    }

    /// NSOutlineView tracks items by identity, so nodes are wrapped in objects.
    final class Item: NSObject {
        let node: Archive.Node
        lazy var children: [Item] = (node.children ?? []).map(Item.init)
        init(_ node: Archive.Node) { self.node = node }
    }

    final class Coordinator: NSObject, NSOutlineViewDataSource, NSOutlineViewDelegate, NSMenuDelegate {
        var parent: ArchiveOutline
        weak var outlineView: NSOutlineView?
        private let roots: [Item]
        private var icons: [String: NSImage] = [:]
        private let sizeFormatter: ByteCountFormatter = {
            let formatter = ByteCountFormatter()
            formatter.countStyle = .file
            return formatter
        }()

        init(_ parent: ArchiveOutline) {
            self.parent = parent
            roots = parent.archive.roots.map(Item.init)
        }

        private func item(_ any: Any?) -> Item? { any as? Item }

        // MARK: Data source

        func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
            self.item(item)?.children.count ?? roots.count
        }

        func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
            self.item(item)?.children[index] ?? roots[index]
        }

        func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
            self.item(item)?.node.isDirectory == true
        }

        func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
            self.item(item).map { parent.promiseWriter.provider(for: $0.node) }
        }

        // MARK: Delegate

        func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
            guard let node = self.item(item)?.node, let column = tableColumn?.identifier else { return nil }
            let cell = outlineView.makeView(withIdentifier: column, owner: nil) as? NSTableCellView
                ?? Self.makeCell(column)
            if column == .name {
                cell.textField?.stringValue = node.name
                cell.imageView?.image = icon(for: node)
            } else {
                cell.textField?.stringValue = sizeFormatter.string(fromByteCount: Int64(clamping: node.size))
                cell.textField?.textColor = node.isDirectory ? .secondaryLabelColor : .labelColor
            }
            return cell
        }

        func outlineView(_ outlineView: NSOutlineView, typeSelectStringFor tableColumn: NSTableColumn?,
                         item: Any) -> String? {
            tableColumn?.identifier == .name ? self.item(item)?.node.name : nil
        }

        func outlineViewSelectionDidChange(_ notification: Notification) {
            guard let outline = notification.object as? NSOutlineView else { return }
            parent.selection = ids(at: outline.selectedRowIndexes, in: outline)
        }

        @objc func toggleFolder(_ outline: NSOutlineView) {
            guard let item = item(outline.item(atRow: outline.clickedRow)), item.node.isDirectory else { return }
            if outline.isItemExpanded(item) { outline.collapseItem(item) } else { outline.expandItem(item) }
        }

        // MARK: Context menu

        func menuNeedsUpdate(_ menu: NSMenu) {
            menu.removeAllItems()
            guard let outline = outlineView else { return }
            let targets = clickedIDs(in: outline)
            guard !targets.isEmpty else { return }
            let extract = NSMenuItem(title: String(localized: "Extract…"), action: #selector(extractFromMenu(_:)),
                                     keyEquivalent: "")
            extract.target = self
            extract.representedObject = targets
            menu.addItem(extract)
        }

        @objc private func extractFromMenu(_ sender: NSMenuItem) {
            if let ids = sender.representedObject as? Set<Int> { parent.onExtract(ids) }
        }

        /// The clicked row, or the whole selection when the clicked row is part of it (Finder behavior).
        private func clickedIDs(in outline: NSOutlineView) -> Set<Int> {
            let row = outline.clickedRow
            guard row >= 0 else { return ids(at: outline.selectedRowIndexes, in: outline) }
            if outline.selectedRowIndexes.contains(row) { return ids(at: outline.selectedRowIndexes, in: outline) }
            return ids(at: IndexSet(integer: row), in: outline)
        }

        private func ids(at rows: IndexSet, in outline: NSOutlineView) -> Set<Int> {
            Set(rows.compactMap { item(outline.item(atRow: $0))?.node.id })
        }

        // MARK: Cells and icons

        private func icon(for node: Archive.Node) -> NSImage {
            let ext = node.isDirectory ? "/" : (node.name as NSString).pathExtension.lowercased()
            if let cached = icons[ext] { return cached }
            let type: UTType = node.isDirectory ? .folder : (UTType(filenameExtension: ext) ?? .data)
            let image = NSWorkspace.shared.icon(for: type)
            icons[ext] = image
            return image
        }

        private static func makeCell(_ column: NSUserInterfaceItemIdentifier) -> NSTableCellView {
            let cell = NSTableCellView()
            cell.identifier = column
            let text = NSTextField(labelWithString: "")
            text.lineBreakMode = .byTruncatingMiddle
            text.translatesAutoresizingMaskIntoConstraints = false
            cell.addSubview(text)
            cell.textField = text
            if column == .name {
                let image = NSImageView()
                image.translatesAutoresizingMaskIntoConstraints = false
                cell.addSubview(image)
                cell.imageView = image
                NSLayoutConstraint.activate([
                    image.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
                    image.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
                    image.widthAnchor.constraint(equalToConstant: 16),
                    image.heightAnchor.constraint(equalToConstant: 16),
                    text.leadingAnchor.constraint(equalTo: image.trailingAnchor, constant: 6),
                ])
            } else {
                text.alignment = .right
                text.font = .monospacedDigitSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
                text.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2).isActive = true
            }
            NSLayoutConstraint.activate([
                text.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -2),
                text.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
            return cell
        }
    }
}

private extension NSUserInterfaceItemIdentifier {
    static let name = NSUserInterfaceItemIdentifier("name")
    static let size = NSUserInterfaceItemIdentifier("size")
}
