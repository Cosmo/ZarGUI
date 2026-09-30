#include "zar-archive-item.h"

struct _ZarArchiveItem {
    GObject parent_instance;
    guint index;
    char *name;
    gboolean is_folder;
    char *size_text;
    char *type_text;
    GIcon *icon;
};

G_DEFINE_FINAL_TYPE (ZarArchiveItem, zar_archive_item, G_TYPE_OBJECT)

static void
zar_archive_item_finalize (GObject *object)
{
    ZarArchiveItem *self = ZAR_ARCHIVE_ITEM (object);
    g_free (self->name);
    g_free (self->size_text);
    g_free (self->type_text);
    g_clear_object (&self->icon);
    G_OBJECT_CLASS (zar_archive_item_parent_class)->finalize (object);
}

static void
zar_archive_item_class_init (ZarArchiveItemClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = zar_archive_item_finalize;
}

static void
zar_archive_item_init (ZarArchiveItem *self)
{
    (void) self;
}

ZarArchiveItem *
zar_archive_item_new (ZarArchive *archive, guint index)
{
    const ZarEntry *entry = zar_archive_get_entry (archive, index);
    ZarArchiveItem *self = g_object_new (ZAR_TYPE_ARCHIVE_ITEM, NULL);
    self->index = index;
    self->name = g_strdup (entry->name);
    self->is_folder = entry->is_dir;
    self->size_text = entry->is_dir ? g_strdup ("") : g_format_size (entry->size);

    /* Type and icon from the name, as the file manager would show them. */
    g_autofree char *content_type = NULL;
    if (entry->is_dir)
        content_type = g_strdup ("inode/directory");
    else
        content_type = g_content_type_guess (entry->name, NULL, 0, NULL);
    self->type_text = g_content_type_get_description (content_type);
    self->icon = g_content_type_get_icon (content_type);
    return self;
}

guint
zar_archive_item_get_index (ZarArchiveItem *item)
{
    return item->index;
}

const char *
zar_archive_item_get_name (ZarArchiveItem *item)
{
    return item->name;
}

gboolean
zar_archive_item_is_folder (ZarArchiveItem *item)
{
    return item->is_folder;
}

const char *
zar_archive_item_get_size_text (ZarArchiveItem *item)
{
    return item->size_text;
}

const char *
zar_archive_item_get_type_text (ZarArchiveItem *item)
{
    return item->type_text;
}

GIcon *
zar_archive_item_get_icon (ZarArchiveItem *item)
{
    return item->icon;
}
