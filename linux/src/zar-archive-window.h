#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define ZAR_TYPE_ARCHIVE_WINDOW (zar_archive_window_get_type ())
G_DECLARE_FINAL_TYPE (ZarArchiveWindow, zar_archive_window, ZAR, ARCHIVE_WINDOW, AdwApplicationWindow)

/* Opens `file` in a new window; if it isn't a valid archive, the window says why. */
void zar_archive_window_open (GtkApplication *app, GFile *file);

G_END_DECLS
