#include "zar-preferences.h"

#include "zar-util.h"

#include <adwaita.h>

typedef struct {
    GSettings *settings;
    AdwActionRow *location_row;
    GtkWidget *reset_button;
} Preferences;

static void
preferences_free (gpointer data)
{
    Preferences *prefs = data;
    g_object_unref (prefs->settings);
    g_free (prefs);
}

static void
show_location (Preferences *prefs)
{
    g_autofree char *folder = g_settings_get_string (prefs->settings, "output-folder");
    adw_action_row_set_subtitle (prefs->location_row, *folder ? folder : "Next to the original folder");
    gtk_widget_set_visible (prefs->reset_button, *folder != '\0');
}

static void
on_location_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
    Preferences *prefs = user_data;
    g_autoptr (GFile) folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, NULL);
    if (!folder)
        return;
    g_autofree char *path = g_file_get_path (folder);
    if (path)
        g_settings_set_string (prefs->settings, "output-folder", path);
    show_location (prefs);
}

static void
on_choose_location (GtkButton *button, gpointer user_data)
{
    g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Save New Archives In");
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (button));
    gtk_file_dialog_select_folder (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, NULL,
                                   on_location_chosen, user_data);
}

static void
on_reset_location (GtkButton *button, gpointer user_data)
{
    Preferences *prefs = user_data;
    g_settings_set_string (prefs->settings, "output-folder", "");
    show_location (prefs);
    (void) button;
}

/* Binds a combo row's selection to an enum key (the options are in the enum's order). */
static gboolean
enum_to_selected (GValue *value, GVariant *variant, gpointer user_data)
{
    g_value_set_uint (value, 0);
    const char *nick = g_variant_get_string (variant, NULL);
    const char *const *nicks = user_data;
    for (guint i = 0; nicks[i]; i++)
        if (g_str_equal (nick, nicks[i]))
            g_value_set_uint (value, i);
    return TRUE;
}

static GVariant *
selected_to_enum (const GValue *value, const GVariantType *type, gpointer user_data)
{
    (void) type;
    const char *const *nicks = user_data;
    guint selected = g_value_get_uint (value);
    return g_variant_new_string (nicks[selected < g_strv_length ((char **) nicks) ? selected : 0]);
}

static GtkWidget *
combo_row (GSettings *settings, const char *key, const char *title, const char *subtitle,
           const char *const *labels, const char *const *nicks)
{
    AdwComboRow *row = ADW_COMBO_ROW (adw_combo_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
    if (subtitle)
        adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
    adw_combo_row_set_model (row, G_LIST_MODEL (gtk_string_list_new (labels)));
    g_settings_bind_with_mapping (settings, key, row, "selected", G_SETTINGS_BIND_DEFAULT, enum_to_selected,
                                  selected_to_enum, (gpointer) nicks, NULL);
    return GTK_WIDGET (row);
}

static GtkWidget *
switch_row (GSettings *settings, const char *key, const char *title, const char *subtitle)
{
    GtkWidget *row = adw_switch_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
    if (subtitle)
        adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
    g_settings_bind (settings, key, row, "active", G_SETTINGS_BIND_DEFAULT);
    return row;
}

void
zar_preferences_present (GtkWidget *parent)
{
    static const char *const existing_labels[] = { "Ask", "Replace It", "Keep Both", NULL };
    static const char *const existing_nicks[] = { "ask", "replace", "keep-both", NULL };
    static const char *const compression_labels[] = { "Faster", "Standard", "Smaller", NULL };
    static const char *const compression_nicks[] = { "faster", "standard", "smaller", NULL };
    static const char *const destination_labels[] = { "Ask Every Time", "Next to the Archive", NULL };
    static const char *const destination_nicks[] = { "ask", "next-to-archive", NULL };

    Preferences *prefs = g_new0 (Preferences, 1);
    prefs->settings = g_settings_new (ZAR_APP_ID);

    AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG (adw_preferences_dialog_new ());
    g_object_set_data_full (G_OBJECT (dialog), "zar-preferences", prefs, preferences_free);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());

    AdwPreferencesGroup *creating = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (creating, "Creating Archives");

    prefs->location_row = ADW_ACTION_ROW (adw_action_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (prefs->location_row), "Save Location");
    prefs->reset_button = gtk_button_new_from_icon_name ("edit-undo-symbolic");
    gtk_widget_set_tooltip_text (prefs->reset_button, "Save Next to the Original Folder");
    gtk_widget_set_valign (prefs->reset_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (prefs->reset_button, "flat");
    g_signal_connect (prefs->reset_button, "clicked", G_CALLBACK (on_reset_location), prefs);
    GtkWidget *choose = gtk_button_new_with_label ("Choose…");
    gtk_widget_set_valign (choose, GTK_ALIGN_CENTER);
    g_signal_connect (choose, "clicked", G_CALLBACK (on_choose_location), prefs);
    adw_action_row_add_suffix (prefs->location_row, prefs->reset_button);
    adw_action_row_add_suffix (prefs->location_row, choose);
    adw_preferences_group_add (creating, GTK_WIDGET (prefs->location_row));
    show_location (prefs);

    adw_preferences_group_add (creating, combo_row (prefs->settings, "existing-archive", "If the Archive Exists", NULL,
                                                    existing_labels, existing_nicks));
    adw_preferences_group_add (creating,
                               combo_row (prefs->settings, "compression", "Compression",
                                          "Smaller archives take longer to create. Every setting can be read by any .zar reader.",
                                          compression_labels, compression_nicks));
    adw_preferences_group_add (creating, switch_row (prefs->settings, "skip-system-files", "Skip System Files",
                                                     ".DS_Store, ._ files, Thumbs.db and similar"));

    AdwPreferencesGroup *extracting = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
    adw_preferences_group_set_title (extracting, "Extracting");
    adw_preferences_group_add (extracting, combo_row (prefs->settings, "extract-destination", "Extract To", NULL,
                                                      destination_labels, destination_nicks));
    adw_preferences_group_add (extracting, switch_row (prefs->settings, "open-folder-after-extract",
                                                       "Open Folder When Done", NULL));

    adw_preferences_page_add (page, creating);
    adw_preferences_page_add (page, extracting);
    adw_preferences_dialog_add (dialog, page);
    adw_dialog_present (ADW_DIALOG (dialog), parent);
}

void
zar_about_present (GtkWidget *parent)
{
    AdwAboutDialog *about = ADW_ABOUT_DIALOG (adw_about_dialog_new ());
    adw_about_dialog_set_application_name (about, "ZarGUI");
    adw_about_dialog_set_application_icon (about, ZAR_APP_ID);
    adw_about_dialog_set_version (about, ZAR_VERSION);
    adw_about_dialog_set_developer_name (about, "ZarGUI contributors");
    adw_about_dialog_set_comments (about, "Create, browse and extract .zar archives.");
    adw_about_dialog_set_license_type (about, GTK_LICENSE_MIT_X11);
    adw_about_dialog_set_website (about, "https://github.com/Cosmo/ZarGUI");
    adw_about_dialog_set_issue_url (about, "https://github.com/Cosmo/ZarGUI/issues");
    adw_about_dialog_add_legal_section (about, "ZArchive", "Copyright 2022 Exzap", GTK_LICENSE_CUSTOM,
                                        "MIT No Attribution. https://github.com/Exzap/ZArchive");
    adw_about_dialog_add_legal_section (about, "Zstandard", "Copyright (c) Meta Platforms, Inc. and affiliates.",
                                        GTK_LICENSE_BSD_3, NULL);
    adw_dialog_present (ADW_DIALOG (about), parent);
}
