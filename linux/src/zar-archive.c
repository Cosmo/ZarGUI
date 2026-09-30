#include "zar-archive.h"

struct _ZarArchive {
    gatomicrefcount refs;
    char *path;
    zarpack_archive *handle;
    GArray *entries; /* ZarEntry */
    GArray *roots;   /* guint */
    guint file_count;
    guint64 total_size;
};

static void
clear_entry (gpointer data)
{
    ZarEntry *entry = data;
    g_free (entry->name);
    g_array_unref (entry->children);
}

ZarArchive *
zar_archive_open (const char *path, GError **error)
{
    zarpack_archive *handle = NULL;
    char message[1024] = "";
    if (zarpack_open (path, &handle, message, sizeof message) != ZARPACK_OK) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
        return NULL;
    }

    ZarArchive *archive = g_new0 (ZarArchive, 1);
    g_atomic_ref_count_init (&archive->refs);
    archive->path = g_strdup (path);
    archive->handle = handle;
    archive->roots = g_array_new (FALSE, FALSE, sizeof (guint));

    guint count = (guint) zarpack_entry_count (handle);
    archive->entries = g_array_sized_new (FALSE, TRUE, sizeof (ZarEntry), count);
    g_array_set_clear_func (archive->entries, clear_entry);
    for (guint i = 0; i < count; i++) {
        zarpack_entry e;
        zarpack_entry_get (handle, i, &e);
        ZarEntry entry = { g_strdup (e.name), e.parent, e.size, e.is_dir != 0,
                           g_array_new (FALSE, FALSE, sizeof (guint)) };
        g_array_append_val (archive->entries, entry);
        /* Parents always come before their children. */
        if (e.parent >= 0)
            g_array_append_val (g_array_index (archive->entries, ZarEntry, e.parent).children, i);
        else
            g_array_append_val (archive->roots, i);
        if (!e.is_dir)
            archive->file_count++;
    }
    for (guint r = 0; r < archive->roots->len; r++)
        archive->total_size += zar_archive_get_entry (archive, g_array_index (archive->roots, guint, r))->size;
    return archive;
}

ZarArchive *
zar_archive_ref (ZarArchive *archive)
{
    g_atomic_ref_count_inc (&archive->refs);
    return archive;
}

void
zar_archive_unref (ZarArchive *archive)
{
    if (!g_atomic_ref_count_dec (&archive->refs))
        return;
    zarpack_close (archive->handle);
    g_array_unref (archive->entries);
    g_array_unref (archive->roots);
    g_free (archive->path);
    g_free (archive);
}

const char *
zar_archive_get_path (ZarArchive *archive)
{
    return archive->path;
}

zarpack_archive *
zar_archive_get_handle (ZarArchive *archive)
{
    return archive->handle;
}

const ZarEntry *
zar_archive_get_entry (ZarArchive *archive, guint index)
{
    return &g_array_index (archive->entries, ZarEntry, index);
}

GArray *
zar_archive_get_children (ZarArchive *archive, gint64 folder)
{
    return folder < 0 ? archive->roots : zar_archive_get_entry (archive, (guint) folder)->children;
}

guint
zar_archive_get_file_count (ZarArchive *archive)
{
    return archive->file_count;
}

guint64
zar_archive_get_total_size (ZarArchive *archive)
{
    return archive->total_size;
}

static int
compare_indices (gconstpointer a, gconstpointer b)
{
    guint x = *(const guint *) a, y = *(const guint *) b;
    return x < y ? -1 : x > y;
}

GArray *
zar_archive_top_level (ZarArchive *archive, GArray *indices)
{
    g_autoptr (GHashTable) selected = g_hash_table_new (g_direct_hash, g_direct_equal);
    for (guint i = 0; i < indices->len; i++)
        g_hash_table_add (selected, GUINT_TO_POINTER (g_array_index (indices, guint, i) + 1));

    GArray *result = g_array_new (FALSE, FALSE, sizeof (guint));
    for (guint i = 0; i < indices->len; i++) {
        guint index = g_array_index (indices, guint, i);
        gboolean nested = FALSE;
        for (gint64 p = zar_archive_get_entry (archive, index)->parent; p >= 0 && !nested;
             p = zar_archive_get_entry (archive, (guint) p)->parent)
            nested = g_hash_table_contains (selected, GUINT_TO_POINTER ((guint) p + 1));
        if (!nested)
            g_array_append_val (result, index);
    }
    g_array_sort (result, compare_indices);
    return result;
}
