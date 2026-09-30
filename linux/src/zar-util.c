#include "zar-util.h"

gboolean
zar_is_folder (GFile *file)
{
    return g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY;
}

gboolean
zar_is_archive (GFile *file)
{
    g_autofree char *name = g_file_get_basename (file);
    g_autofree char *lower = name ? g_ascii_strdown (name, -1) : NULL;
    return lower && g_str_has_suffix (lower, ".zar") &&
           g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_REGULAR;
}

char *
zar_unique_path (const char *folder, const char *stem, const char *extension)
{
    g_autofree char *name = g_strconcat (stem, extension, NULL);
    char *candidate = g_build_filename (folder, name, NULL);
    for (int n = 2; g_file_test (candidate, G_FILE_TEST_EXISTS); n++) {
        g_free (candidate);
        g_autofree char *numbered = g_strdup_printf ("%s (%d)%s", stem, n, extension);
        candidate = g_build_filename (folder, numbered, NULL);
    }
    return candidate;
}

char *
zar_stem (const char *path)
{
    g_autofree char *name = g_path_get_basename (path);
    const char *dot = strrchr (name, '.');
    return dot && dot != name ? g_strndup (name, (gsize) (dot - name)) : g_strdup (name);
}

void
zar_show_in_folder (GtkWidget *parent, const char *path)
{
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GtkFileLauncher) launcher = gtk_file_launcher_new (file);
    gtk_file_launcher_open_containing_folder (launcher, GTK_WINDOW (gtk_widget_get_root (parent)), NULL, NULL, NULL);
}
