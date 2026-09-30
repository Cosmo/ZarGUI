#pragma once

#include <gio/gio.h>

#include "zar-archive.h"

G_BEGIN_DECLS

#define ZAR_TYPE_ARCHIVE_ITEM (zar_archive_item_get_type ())
G_DECLARE_FINAL_TYPE (ZarArchiveItem, zar_archive_item, ZAR, ARCHIVE_ITEM, GObject)

/* One file or folder shown in an archive window. */
ZarArchiveItem *zar_archive_item_new (ZarArchive *archive, guint index);

guint zar_archive_item_get_index (ZarArchiveItem *item);
const char *zar_archive_item_get_name (ZarArchiveItem *item);
gboolean zar_archive_item_is_folder (ZarArchiveItem *item);
/* Empty for folders, like the file manager. */
const char *zar_archive_item_get_size_text (ZarArchiveItem *item);
const char *zar_archive_item_get_type_text (ZarArchiveItem *item);
GIcon *zar_archive_item_get_icon (ZarArchiveItem *item);

G_END_DECLS
