#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ZAR_APP_ID "io.github.Cosmo.ZarGUI"

gboolean zar_is_archive (GFile *file);
gboolean zar_is_folder (GFile *file);

/* "Name", or "Name (2)", "Name (3)", … if taken. */
char *zar_unique_path (const char *folder, const char *stem, const char *extension);
/* The file name without its last extension. */
char *zar_stem (const char *path);

/* Opens the file manager with `path` selected. */
void zar_show_in_folder (GtkWidget *parent, const char *path);

G_END_DECLS
