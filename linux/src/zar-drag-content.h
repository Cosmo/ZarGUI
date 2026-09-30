#pragma once

#include <gtk/gtk.h>

#include "zar-archive.h"

G_BEGIN_DECLS

#define ZAR_TYPE_DRAG_CONTENT (zar_drag_content_get_type ())
G_DECLARE_FINAL_TYPE (ZarDragContent, zar_drag_content, ZAR, DRAG_CONTENT, GdkContentProvider)

/* Content for dragging archive entries to a file manager. The entries are
 * extracted only when the drop target asks for them (on drop), into a folder
 * in the app's cache, and offered as a file list (through the file transfer
 * portal when sandboxed). Takes ownership of `roots` (guint entry indices). */
GdkContentProvider *zar_drag_content_new (ZarArchive *archive, GArray *roots);

/* Removes folders left over from earlier drags. */
void zar_drag_content_clean_up (void);

G_END_DECLS
