#include "zar-drag-content.h"

#include <errno.h>

struct _ZarDragContent {
    GdkContentProvider parent_instance;
    ZarArchive *archive;
    GArray *roots;
    GSList *files; /* GFile, once extracted */
    GError *error;
};

G_DEFINE_FINAL_TYPE (ZarDragContent, zar_drag_content, GDK_TYPE_CONTENT_PROVIDER)

static char *
drag_root (void)
{
    return g_build_filename (g_get_user_cache_dir (), "zargui-drag", NULL);
}

static gboolean
extract (ZarDragContent *self)
{
    if (self->files || self->error)
        return self->files != NULL;

    g_autofree char *root = drag_root ();
    g_mkdir_with_parents (root, 0700);
    g_autofree char *template = g_build_filename (root, "XXXXXX", NULL);
    if (!g_mkdtemp (template)) {
        g_set_error (&self->error, G_IO_ERROR, g_io_error_from_errno (errno), "Can’t create a temporary folder.");
        return FALSE;
    }
    for (guint i = 0; i < self->roots->len; i++) {
        guint index = g_array_index (self->roots, guint, i);
        g_autofree char *target = g_build_filename (template, zar_archive_get_entry (self->archive, index)->name, NULL);
        char message[1024] = "";
        if (zarpack_extract_entry (zar_archive_get_handle (self->archive), index, target, 0, NULL, NULL,
                                   message, sizeof message) != ZARPACK_OK) {
            g_set_error_literal (&self->error, G_IO_ERROR, G_IO_ERROR_FAILED, message);
            g_slist_free_full (self->files, g_object_unref);
            self->files = NULL;
            return FALSE;
        }
        self->files = g_slist_append (self->files, g_file_new_for_path (target));
    }
    return TRUE;
}

static GdkContentFormats *
zar_drag_content_ref_formats (GdkContentProvider *provider)
{
    (void) provider;
    g_autoptr (GdkContentFormats) formats = gdk_content_formats_new_for_gtype (GDK_TYPE_FILE_LIST);
    return gdk_content_formats_union_serialize_mime_types (g_steal_pointer (&formats));
}

static gboolean
zar_drag_content_get_value (GdkContentProvider *provider, GValue *value, GError **error)
{
    ZarDragContent *self = ZAR_DRAG_CONTENT (provider);
    if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
        return GDK_CONTENT_PROVIDER_CLASS (zar_drag_content_parent_class)->get_value (provider, value, error);
    if (!extract (self)) {
        g_propagate_error (error, g_error_copy (self->error));
        return FALSE;
    }
    g_value_take_boxed (value, gdk_file_list_new_from_list (self->files));
    return TRUE;
}

static void
on_serialized (GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (GTask) task = user_data;
    GError *error = NULL;
    if (gdk_content_serialize_finish (result, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);
    (void) source;
}

static void
zar_drag_content_write_mime_type_async (GdkContentProvider *provider, const char *mime_type,
                                        GOutputStream *stream, int io_priority, GCancellable *cancellable,
                                        GAsyncReadyCallback callback, gpointer user_data)
{
    GTask *task = g_task_new (provider, cancellable, callback, user_data);
    g_auto (GValue) value = G_VALUE_INIT;
    g_value_init (&value, GDK_TYPE_FILE_LIST);
    GError *error = NULL;
    if (!zar_drag_content_get_value (provider, &value, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    gdk_content_serialize_async (stream, mime_type, &value, io_priority, cancellable, on_serialized, task);
}

static gboolean
zar_drag_content_write_mime_type_finish (GdkContentProvider *provider, GAsyncResult *result, GError **error)
{
    (void) provider;
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
zar_drag_content_finalize (GObject *object)
{
    ZarDragContent *self = ZAR_DRAG_CONTENT (object);
    zar_archive_unref (self->archive);
    g_array_unref (self->roots);
    g_slist_free_full (self->files, g_object_unref);
    g_clear_error (&self->error);
    G_OBJECT_CLASS (zar_drag_content_parent_class)->finalize (object);
}

static void
zar_drag_content_class_init (ZarDragContentClass *klass)
{
    GdkContentProviderClass *provider_class = GDK_CONTENT_PROVIDER_CLASS (klass);
    provider_class->ref_formats = zar_drag_content_ref_formats;
    provider_class->get_value = zar_drag_content_get_value;
    provider_class->write_mime_type_async = zar_drag_content_write_mime_type_async;
    provider_class->write_mime_type_finish = zar_drag_content_write_mime_type_finish;
    G_OBJECT_CLASS (klass)->finalize = zar_drag_content_finalize;
}

static void
zar_drag_content_init (ZarDragContent *self)
{
    (void) self;
}

GdkContentProvider *
zar_drag_content_new (ZarArchive *archive, GArray *roots)
{
    ZarDragContent *self = g_object_new (ZAR_TYPE_DRAG_CONTENT, NULL);
    self->archive = zar_archive_ref (archive);
    self->roots = roots;
    return GDK_CONTENT_PROVIDER (self);
}

static void
delete_recursively (GFile *file)
{
    g_autoptr (GFileEnumerator) children =
        g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    if (children) {
        GFile *child;
        while (g_file_enumerator_iterate (children, NULL, &child, NULL, NULL) && child)
            delete_recursively (child);
    }
    g_file_delete (file, NULL, NULL);
}

void
zar_drag_content_clean_up (void)
{
    g_autofree char *root = drag_root ();
    g_autoptr (GFile) folder = g_file_new_for_path (root);
    delete_recursively (folder);
}
