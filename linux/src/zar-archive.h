#pragma once

#include <glib.h>

#include "zarpack.h"

G_BEGIN_DECLS

typedef struct {
    char *name;
    gint64 parent;    /* -1 for top level */
    guint64 size;     /* file size, or total size of a folder's files */
    gboolean is_dir;
    GArray *children; /* guint entry indices */
} ZarEntry;

/* An opened .zar archive. Reference counted, because a drag out may still read
 * from it after its window has closed. */
typedef struct _ZarArchive ZarArchive;

ZarArchive *zar_archive_open (const char *path, GError **error);
ZarArchive *zar_archive_ref (ZarArchive *archive);
void zar_archive_unref (ZarArchive *archive);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ZarArchive, zar_archive_unref)

const char *zar_archive_get_path (ZarArchive *archive);
zarpack_archive *zar_archive_get_handle (ZarArchive *archive);
const ZarEntry *zar_archive_get_entry (ZarArchive *archive, guint index);
/* Entry indices inside `folder` (-1: top level). Owned by the archive. */
GArray *zar_archive_get_children (ZarArchive *archive, gint64 folder);
guint zar_archive_get_file_count (ZarArchive *archive);
guint64 zar_archive_get_total_size (ZarArchive *archive);

/* The given entries without those inside another given folder, sorted. */
GArray *zar_archive_top_level (ZarArchive *archive, GArray *indices);

G_END_DECLS
