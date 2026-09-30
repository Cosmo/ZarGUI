#include "zar-drag-content.h"
#include "zar-preferences.h"
#include "zar-util.h"
#include "zar-window.h"

#include <adwaita.h>

/* The drop area highlights in the accent color while something is dragged over it. */
static const char style[] =
    ".drop-area { margin: 12px; border-radius: 12px; transition: background-color 150ms, box-shadow 150ms; }\n"
    ".drop-area:drop(active) { background-color: alpha(@accent_bg_color, 0.12);"
    " box-shadow: inset 0 0 0 2px @accent_bg_color; }\n";

static ZarWindow *
main_window (GtkApplication *app)
{
    for (GList *l = gtk_application_get_windows (app); l; l = l->next)
        if (ZAR_IS_WINDOW (l->data))
            return l->data;
    return zar_window_new (ADW_APPLICATION (app));
}

static void
action_preferences (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    zar_preferences_present (GTK_WIDGET (gtk_application_get_active_window (user_data)));
}

static void
action_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    zar_about_present (GTK_WIDGET (gtk_application_get_active_window (user_data)));
}

static void
action_quit (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    for (GList *l = gtk_application_get_windows (user_data); l;) {
        GList *next = l->next;
        gtk_window_close (l->data); /* each window cancels its work */
        l = next;
    }
}

static void
set_accels (GtkApplication *app, const char *action, const char *accel, const char *second)
{
    const char *accels[] = { accel, second, NULL };
    gtk_application_set_accels_for_action (app, action, accels);
}

static void
on_startup (GApplication *application, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION (application);
    (void) user_data;
    static const GActionEntry actions[] = {
        { .name = "preferences", .activate = action_preferences },
        { .name = "about", .activate = action_about },
        { .name = "quit", .activate = action_quit },
    };
    g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), app);
    set_accels (app, "app.preferences", "<Control>comma", NULL);
    set_accels (app, "app.quit", "<Control>q", NULL);
    set_accels (app, "window.close", "<Control>w", NULL);
    set_accels (app, "win.open", "<Control>o", NULL);
    set_accels (app, "win.choose-folder", "<Control>n", NULL);
    set_accels (app, "win.go-up", "<Alt>Up", "BackSpace");

    g_autoptr (GtkCssProvider) css = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (css, style);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (css),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    zar_drag_content_clean_up ();
}

static void
on_activate (GApplication *application, gpointer user_data)
{
    (void) user_data;
    gtk_window_present (GTK_WINDOW (main_window (GTK_APPLICATION (application))));
}

/* Folders and .zar files from the command line or the file manager ("Open With"). */
static void
on_open (GApplication *application, GFile **files, int n_files, const char *hint, gpointer user_data)
{
    (void) hint, (void) user_data;
    ZarWindow *window = main_window (GTK_APPLICATION (application));
    gtk_window_present (GTK_WINDOW (window));
    zar_window_open (window, files, n_files);
}

int
main (int argc, char **argv)
{
    g_autoptr (AdwApplication) app = adw_application_new (ZAR_APP_ID, G_APPLICATION_HANDLES_OPEN);
    g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
    g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);
    return g_application_run (G_APPLICATION (app), argc, argv);
}
