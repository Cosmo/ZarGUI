#include "zar-window.h"

#include "zar-archive-window.h"
#include "zar-job.h"
#include "zar-util.h"

struct _ZarWindow {
    AdwApplicationWindow parent_instance;
    GSettings *settings;

    AdwToastOverlay *toasts;
    GtkWidget *drop_area;
    AdwStatusPage *status;
    GtkWidget *idle_buttons;
    GtkWidget *busy_box;
    GtkProgressBar *progress;
    GtkLabel *queue_label;

    GQueue queue; /* char * folder paths */
    ZarJob *job;
    char *current;
    gboolean replace_next;
    gboolean dialog_open;
};

G_DEFINE_FINAL_TYPE (ZarWindow, zar_window, ADW_TYPE_APPLICATION_WINDOW)

static void start_next_if_idle (ZarWindow *self);

/* The page shows either the idle hint, a drop hint, or progress. */
static void
show_idle (ZarWindow *self)
{
    adw_status_page_set_title (self->status, "Drop a Folder or Archive");
    adw_status_page_set_description (self->status,
                                     "Folders are packed into a .zar archive. Archives open so you can browse and extract them.");
    adw_status_page_set_child (self->status, self->idle_buttons);
}

static void
show_busy (ZarWindow *self)
{
    g_autofree char *name = g_path_get_basename (self->current);
    g_autofree char *title = g_strdup_printf ("Creating “%s.zar”", name);
    adw_status_page_set_title (self->status, title);
    adw_status_page_set_description (self->status, NULL);
    guint waiting = g_queue_get_length (&self->queue);
    g_autofree char *queue = waiting ? g_strdup_printf ("%u more waiting", waiting) : g_strdup ("");
    gtk_label_set_text (self->queue_label, queue);
    adw_status_page_set_child (self->status, self->busy_box);
}

static void
show_current_state (ZarWindow *self)
{
    if (self->job)
        show_busy (self);
    else
        show_idle (self);
}

/* Opening and queueing */

void
zar_window_open (ZarWindow *self, GFile **files, int n_files)
{
    GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
    for (int i = 0; i < n_files; i++) {
        if (zar_is_archive (files[i])) {
            zar_archive_window_open (app, files[i]);
        } else if (zar_is_folder (files[i])) {
            char *path = g_file_get_path (files[i]);
            if (path)
                g_queue_push_tail (&self->queue, path);
        }
    }
    start_next_if_idle (self);
}

static void
open_file_list (ZarWindow *self, GSList *list)
{
    g_autoptr (GPtrArray) files = g_ptr_array_new ();
    for (GSList *l = list; l; l = l->next)
        g_ptr_array_add (files, l->data);
    zar_window_open (self, (GFile **) files->pdata, (int) files->len);
}

/* Dropping */

typedef enum { DROP_NOTHING, DROP_FOLDERS, DROP_ARCHIVES, DROP_BOTH } DropKind;

static DropKind
drop_kind (const GValue *value)
{
    if (!value || !G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
        return DROP_NOTHING;
    gboolean folders = FALSE, archives = FALSE;
    for (GSList *l = gdk_file_list_get_files (g_value_get_boxed (value)); l; l = l->next) {
        folders |= zar_is_folder (l->data);
        archives |= zar_is_archive (l->data);
    }
    return folders && archives ? DROP_BOTH : folders ? DROP_FOLDERS : archives ? DROP_ARCHIVES : DROP_NOTHING;
}

static void
on_drop_value (GtkDropTarget *target, GParamSpec *pspec, gpointer user_data)
{
    ZarWindow *self = user_data;
    (void) pspec;
    DropKind kind = drop_kind (gtk_drop_target_get_value (target));
    if (kind == DROP_NOTHING) {
        if (gtk_drop_target_get_value (target))
            gtk_drop_target_reject (target);
        return;
    }
    adw_status_page_set_title (self->status, kind == DROP_FOLDERS    ? "Drop to Create Archive"
                                             : kind == DROP_ARCHIVES ? "Drop to Open"
                                                                     : "Drop to Archive Folders and Open Archives");
    adw_status_page_set_description (self->status, NULL);
    adw_status_page_set_child (self->status, NULL);
}

static void
on_drop_leave (GtkDropTarget *target, gpointer user_data)
{
    (void) target;
    show_current_state (user_data);
}

static gboolean
on_drop (GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data)
{
    ZarWindow *self = user_data;
    (void) target, (void) x, (void) y;
    show_current_state (self);
    if (drop_kind (value) == DROP_NOTHING)
        return FALSE;
    open_file_list (self, gdk_file_list_get_files (g_value_get_boxed (value)));
    return TRUE;
}

/* Choosing with dialogs */

static void
on_folder_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (ZarWindow) self = user_data;
    g_autoptr (GFile) folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, NULL);
    if (folder)
        zar_window_open (self, &folder, 1);
}

static void
choose_folder (ZarWindow *self)
{
    g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Choose a Folder to Archive");
    gtk_file_dialog_set_accept_label (dialog, "Create Archive");
    gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, on_folder_chosen, g_object_ref (self));
}

static void
on_archives_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (ZarWindow) self = user_data;
    g_autoptr (GListModel) files = gtk_file_dialog_open_multiple_finish (GTK_FILE_DIALOG (source), result, NULL);
    if (!files)
        return;
    g_autoptr (GPtrArray) list = g_ptr_array_new_with_free_func (g_object_unref);
    for (guint i = 0; i < g_list_model_get_n_items (files); i++)
        g_ptr_array_add (list, g_list_model_get_item (files, i));
    zar_window_open (self, (GFile **) list->pdata, (int) list->len);
}

static void
choose_archives (ZarWindow *self)
{
    g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Open Archives");
    g_autoptr (GtkFileFilter) filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, "ZArchive (.zar)");
    gtk_file_filter_add_pattern (filter, "*.zar");
    gtk_file_filter_add_mime_type (filter, "application/x-zarchive");
    g_autoptr (GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
    g_list_store_append (filters, filter);
    gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
    gtk_file_dialog_open_multiple (dialog, GTK_WINDOW (self), NULL, on_archives_chosen, g_object_ref (self));
}

static void
on_choose_folder_clicked (GtkButton *button, gpointer user_data)
{
    (void) button;
    choose_folder (user_data);
}

static void
on_open_archive_clicked (GtkButton *button, gpointer user_data)
{
    (void) button;
    choose_archives (user_data);
}

static void
action_choose_folder (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    choose_folder (user_data);
}

static void
action_open (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    choose_archives (user_data);
}

/* Packing */

typedef struct {
    char *input;
    char *output; /* folder or archive path; NULL: next to the input */
    gboolean overwrite;
    gboolean keep_system_files;
    int compression_level;
} PackRequest;

static void
pack_request_free (gpointer data)
{
    PackRequest *request = data;
    g_free (request->input);
    g_free (request->output);
    g_free (request);
}

static zarpack_status
run_pack (ZarJob *job, gpointer data, char *message, gsize message_size)
{
    PackRequest *request = data;
    zarpack_options options = { 0 };
    options.input_dir = request->input;
    options.output = request->output;
    options.overwrite = request->overwrite;
    options.progress = zar_job_progress_callback ();
    options.user = job;
    options.keep_system_files = request->keep_system_files;
    options.compression_level = request->compression_level;
    char error[1024] = "";
    zarpack_status status = zarpack_pack (&options, message, message_size, error, sizeof error);
    if (status != ZARPACK_OK)
        g_strlcpy (message, error, message_size); /* on success, `message` holds the archive path */
    return status;
}

static char *
resolved_output (const char *input, const char *output)
{
    char path[4096] = "";
    zarpack_resolve_output (input, output, path, sizeof path);
    return g_strdup (path);
}

/* The save folder, or with "Keep Both" a free "Name (2).zar"-style path. */
static char *
output_for (ZarWindow *self, const char *input)
{
    g_autofree char *folder = g_settings_get_string (self->settings, "output-folder");
    const char *output = *folder ? folder : NULL;
    if (g_settings_get_enum (self->settings, "existing-archive") != 2)
        return g_strdup (output);
    g_autofree char *archive = resolved_output (input, output);
    g_autofree char *parent = g_path_get_dirname (archive);
    g_autofree char *stem = zar_stem (archive);
    return zar_unique_path (parent, stem, ".zar");
}

static void
on_pack_progress (guint64 done, guint64 total, const char *file, gpointer user_data)
{
    ZarWindow *self = user_data;
    if (total > 0)
        gtk_progress_bar_set_fraction (self->progress, (double) done / (double) total);
    else
        gtk_progress_bar_pulse (self->progress);
    adw_status_page_set_description (self->status, file);
}

static void
on_toast_show (AdwToast *toast, gpointer user_data)
{
    zar_show_in_folder (GTK_WIDGET (user_data), g_object_get_data (G_OBJECT (toast), "path"));
}

static void
show_error (GtkWidget *parent, const char *heading, const char *message)
{
    AdwDialog *dialog = adw_alert_dialog_new (heading, message);
    adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", "_OK");
    adw_dialog_present (dialog, parent);
}

static void
on_replace_answered (GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (ZarWindow) self = user_data;
    const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);
    if (g_str_equal (response, "replace")) {
        g_queue_push_head (&self->queue, g_strdup (self->current));
        self->replace_next = TRUE;
    }
    self->dialog_open = FALSE;
    start_next_if_idle (self);
}

static void
ask_to_replace (ZarWindow *self)
{
    g_autofree char *name = g_path_get_basename (self->current);
    g_autofree char *body = g_strdup_printf ("“%s.zar” already exists in this location. Replacing it can’t be undone.", name);
    AdwDialog *dialog = adw_alert_dialog_new ("Replace Existing Archive?", body);
    adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", "_Cancel", "replace", "_Replace", NULL);
    adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "replace", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
    adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
    self->dialog_open = TRUE;
    adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL, on_replace_answered, g_object_ref (self));
}

static void
on_pack_done (zarpack_status status, const char *message, gpointer user_data)
{
    ZarWindow *self = user_data;
    self->job = NULL;
    show_idle (self);

    switch (status) {
    case ZARPACK_OK: {
        g_autofree char *name = g_path_get_basename (message);
        g_autofree char *title = g_strdup_printf ("Created “%s”", name);
        AdwToast *toast = adw_toast_new (title);
        adw_toast_set_button_label (toast, "Show");
        g_object_set_data_full (G_OBJECT (toast), "path", g_strdup (message), g_free);
        g_signal_connect (toast, "button-clicked", G_CALLBACK (on_toast_show), self);
        adw_toast_overlay_add_toast (self->toasts, toast);
        break;
    }
    case ZARPACK_CANCELLED:
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS:
        ask_to_replace (self);
        return;
    default:
        show_error (GTK_WIDGET (self), "Couldn’t Create Archive", message);
        break;
    }
    start_next_if_idle (self);
}

static void
start_next_if_idle (ZarWindow *self)
{
    if (self->job || self->dialog_open || g_queue_is_empty (&self->queue))
        return;
    g_free (self->current);
    self->current = g_queue_pop_head (&self->queue);

    static const int levels[] = { 1, 0, 12 }; /* faster, standard (the core's default), smaller */
    PackRequest *request = g_new0 (PackRequest, 1);
    request->input = g_strdup (self->current);
    if (self->replace_next) {
        g_autofree char *folder = g_settings_get_string (self->settings, "output-folder");
        request->output = *folder ? g_strdup (folder) : NULL;
    } else {
        request->output = output_for (self, self->current);
    }
    request->overwrite = self->replace_next || g_settings_get_enum (self->settings, "existing-archive") == 1;
    request->keep_system_files = !g_settings_get_boolean (self->settings, "skip-system-files");
    request->compression_level = levels[CLAMP (g_settings_get_enum (self->settings, "compression"), 0, 2)];
    self->replace_next = FALSE;

    gtk_progress_bar_set_fraction (self->progress, 0);
    self->job = zar_job_start (run_pack, request, pack_request_free, on_pack_progress, on_pack_done, G_OBJECT (self));
    show_busy (self);
}

static void
on_cancel_clicked (GtkButton *button, gpointer user_data)
{
    ZarWindow *self = user_data;
    (void) button;
    g_queue_clear_full (&self->queue, g_free);
    if (self->job)
        zar_job_cancel (self->job);
}

/* Construction */

static GtkWidget *
pill_button (const char *label, gboolean suggested, GCallback callback, gpointer user_data)
{
    GtkWidget *button = gtk_button_new_with_mnemonic (label);
    gtk_widget_add_css_class (button, "pill");
    if (suggested)
        gtk_widget_add_css_class (button, "suggested-action");
    g_signal_connect (button, "clicked", callback, user_data);
    return button;
}

static GtkWidget *
build_menu_button (void)
{
    g_autoptr (GMenu) menu = g_menu_new ();
    g_autoptr (GMenu) section = g_menu_new ();
    g_menu_append (section, "_Open Archive…", "win.open");
    g_menu_append (section, "_Create Archive From Folder…", "win.choose-folder");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
    g_autoptr (GMenu) app_section = g_menu_new ();
    g_menu_append (app_section, "_Preferences", "app.preferences");
    g_menu_append (app_section, "_About ZarGUI", "app.about");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (app_section));

    GtkWidget *button = gtk_menu_button_new ();
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), G_MENU_MODEL (menu));
    gtk_menu_button_set_primary (GTK_MENU_BUTTON (button), TRUE);
    gtk_widget_set_tooltip_text (button, "Main Menu");
    return button;
}

static void
zar_window_dispose (GObject *object)
{
    ZarWindow *self = ZAR_WINDOW (object);
    if (self->job)
        zar_job_cancel (self->job);
    g_queue_clear_full (&self->queue, g_free);
    g_clear_pointer (&self->current, g_free);
    g_clear_object (&self->settings);
    g_clear_object (&self->idle_buttons);
    g_clear_object (&self->busy_box);
    G_OBJECT_CLASS (zar_window_parent_class)->dispose (object);
}

static void
zar_window_class_init (ZarWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = zar_window_dispose;
}

static void
zar_window_init (ZarWindow *self)
{
    static const GActionEntry actions[] = {
        { .name = "choose-folder", .activate = action_choose_folder },
        { .name = "open", .activate = action_open },
    };
    g_action_map_add_action_entries (G_ACTION_MAP (self), actions, G_N_ELEMENTS (actions), self);

    self->settings = g_settings_new (ZAR_APP_ID);
    g_queue_init (&self->queue);
    gtk_window_set_title (GTK_WINDOW (self), "ZarGUI");
    gtk_window_set_default_size (GTK_WINDOW (self), 460, 440);

    /* Idle: two ways in besides dropping */
    self->idle_buttons = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_halign (self->idle_buttons, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (self->idle_buttons),
                    pill_button ("_Choose Folder…", TRUE, G_CALLBACK (on_choose_folder_clicked), self));
    gtk_box_append (GTK_BOX (self->idle_buttons),
                    pill_button ("_Open Archive…", FALSE, G_CALLBACK (on_open_archive_clicked), self));

    /* Busy: progress, queue and Cancel */
    self->busy_box = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_size_request (self->busy_box, 280, -1);
    gtk_widget_set_halign (self->busy_box, GTK_ALIGN_CENTER);
    self->progress = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
    self->queue_label = GTK_LABEL (gtk_label_new (NULL));
    gtk_widget_add_css_class (GTK_WIDGET (self->queue_label), "dim-label");
    gtk_box_append (GTK_BOX (self->busy_box), GTK_WIDGET (self->progress));
    gtk_box_append (GTK_BOX (self->busy_box), GTK_WIDGET (self->queue_label));
    GtkWidget *cancel = pill_button ("_Cancel", FALSE, G_CALLBACK (on_cancel_clicked), self);
    gtk_widget_set_halign (cancel, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (self->busy_box), cancel);

    self->status = ADW_STATUS_PAGE (adw_status_page_new ());
    adw_status_page_set_icon_name (self->status, ZAR_APP_ID);
    show_idle (self);

    /* The whole page is the drop area; it highlights while something is dragged over it. */
    self->drop_area = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (self->drop_area, "drop-area");
    gtk_widget_set_vexpand (GTK_WIDGET (self->status), TRUE);
    gtk_box_append (GTK_BOX (self->drop_area), GTK_WIDGET (self->status));
    GtkDropTarget *target = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    gtk_drop_target_set_preload (target, TRUE);
    g_signal_connect (target, "notify::value", G_CALLBACK (on_drop_value), self);
    g_signal_connect (target, "leave", G_CALLBACK (on_drop_leave), self);
    g_signal_connect (target, "drop", G_CALLBACK (on_drop), self);
    gtk_widget_add_controller (self->drop_area, GTK_EVENT_CONTROLLER (target));

    self->toasts = ADW_TOAST_OVERLAY (adw_toast_overlay_new ());
    adw_toast_overlay_set_child (self->toasts, self->drop_area);

    GtkWidget *header = adw_header_bar_new ();
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), build_menu_button ());
    GtkWidget *view = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), GTK_WIDGET (self->toasts));
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), view);
}

ZarWindow *
zar_window_new (AdwApplication *app)
{
    return g_object_new (ZAR_TYPE_WINDOW, "application", app, NULL);
}
