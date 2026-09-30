#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define ZAR_TYPE_WINDOW (zar_window_get_type ())
G_DECLARE_FINAL_TYPE (ZarWindow, zar_window, ZAR, WINDOW, AdwApplicationWindow)

/* The drop window: folders are packed into archives, .zar files open in their own window. */
ZarWindow *zar_window_new (AdwApplication *app);
void zar_window_open (ZarWindow *self, GFile **files, int n_files);

G_END_DECLS
