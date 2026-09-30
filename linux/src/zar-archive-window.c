#include "zar-archive-window.h"

#include "zar-archive-item.h"
#include "zar-archive.h"
#include "zar-drag-content.h"
#include "zar-job.h"
#include "zar-util.h"

struct _ZarArchiveWindow {
    AdwApplicationWindow parent_instance;
    GSettings *settings;
    ZarArchive *archive;
    char *path;
    gint64 folder; /* -1: the archive's top level */

    AdwWindowTitle *title;
    AdwToastOverlay *toasts;
    GtkStack *stack;
    AdwStatusPage *message_page;
    GtkColumnView *list;
    GListStore *items;
    GtkMultiSelection *selection;
    GtkPopoverMenu *context_menu;

    GtkActionBar *action_bar;
    GtkLabel *selection_label;
    GtkWidget *extract_button;
    GtkWidget *progress_box;
    GtkProgressBar *progress;
    GtkLabel *progress_label;

    ZarJob *job;
    GArray *extract_indices; /* empty: everything */
    char *extract_destination;
    char *extract_reveal;
};

G_DEFINE_FINAL_TYPE (ZarArchiveWindow, zar_archive_window, ADW_TYPE_APPLICATION_WINDOW)

static void update_commands (ZarArchiveWindow *self);

/* Navigation */

static void
navigate (ZarArchiveWindow *self, gint64 folder)
{
    self->folder = folder;
    GArray *children = zar_archive_get_children (self->archive, folder);
    g_autoptr (GPtrArray) items = g_ptr_array_new_with_free_func (g_object_unref);
    for (guint i = 0; i < children->len; i++)
        g_ptr_array_add (items, zar_archive_item_new (self->archive, g_array_index (children, guint, i)));
    g_list_store_splice (self->items, 0, g_list_model_get_n_items (G_LIST_MODEL (self->items)), items->pdata, items->len);

    g_autoptr (GString) location = g_string_new (NULL);
    for (gint64 f = folder; f >= 0; f = zar_archive_get_entry (self->archive, (guint) f)->parent) {
        g_string_prepend (location, zar_archive_get_entry (self->archive, (guint) f)->name);
        g_string_prepend_c (location, '/');
    }
    adw_window_title_set_subtitle (self->title, location->len ? location->str : NULL);

    if (children->len == 0) {
        adw_status_page_set_icon_name (self->message_page, "folder-symbolic");
        adw_status_page_set_title (self->message_page, "Folder Is Empty");
        adw_status_page_set_description (self->message_page, NULL);
        gtk_stack_set_visible_child_name (self->stack, "message");
    } else {
        gtk_stack_set_visible_child_name (self->stack, "list");
    }
    update_commands (self);
}

static void
go_up (ZarArchiveWindow *self)
{
    if (self->archive && self->folder >= 0)
        navigate (self, zar_archive_get_entry (self->archive, (guint) self->folder)->parent);
}

static GArray *
selected_indices (ZarArchiveWindow *self)
{
    GArray *indices = g_array_new (FALSE, FALSE, sizeof (guint));
    g_autoptr (GtkBitset) selected = gtk_selection_model_get_selection (GTK_SELECTION_MODEL (self->selection));
    GtkBitsetIter iter;
    guint position;
    for (gboolean ok = gtk_bitset_iter_init_first (&iter, selected, &position); ok;
         ok = gtk_bitset_iter_next (&iter, &position)) {
        g_autoptr (ZarArchiveItem) item = g_list_model_get_item (G_LIST_MODEL (self->items), position);
        guint index = zar_archive_item_get_index (item);
        g_array_append_val (indices, index);
    }
    return indices;
}

/* The single selected folder, or -1. */
static gint64
selected_folder (ZarArchiveWindow *self)
{
    g_autoptr (GArray) indices = selected_indices (self);
    if (indices->len != 1)
        return -1;
    guint index = g_array_index (indices, guint, 0);
    return zar_archive_get_entry (self->archive, index)->is_dir ? (gint64) index : -1;
}

static void
on_activate (GtkColumnView *list, guint position, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) list;
    g_autoptr (ZarArchiveItem) item = g_list_model_get_item (G_LIST_MODEL (self->items), position);
    if (zar_archive_item_is_folder (item))
        navigate (self, zar_archive_item_get_index (item));
}

static void
set_action_enabled (ZarArchiveWindow *self, const char *name, gboolean enabled)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (self), name);
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
update_commands (ZarArchiveWindow *self)
{
    gboolean ready = self->archive && !self->job;
    guint selected = 0;
    if (self->archive) {
        g_autoptr (GtkBitset) set = gtk_selection_model_get_selection (GTK_SELECTION_MODEL (self->selection));
        selected = (guint) gtk_bitset_get_size (set);
    }
    set_action_enabled (self, "go-up", self->archive && self->folder >= 0);
    set_action_enabled (self, "extract-all", ready);
    set_action_enabled (self, "extract-selected", ready && selected > 0);
    set_action_enabled (self, "open-selected", self->archive && selected_folder (self) >= 0);

    if (self->job)
        return;
    g_autofree char *text = g_strdup_printf (g_dngettext (NULL, "%u item selected", "%u items selected", selected), selected);
    gtk_label_set_text (self->selection_label, text);
    gtk_widget_set_visible (self->extract_button, TRUE);
    gtk_widget_set_visible (self->progress_box, FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->selection_label), TRUE);
    gtk_action_bar_set_revealed (self->action_bar, selected > 0);
}

static void
on_selection_changed (GtkSelectionModel *model, guint position, guint n_items, gpointer user_data)
{
    (void) model, (void) position, (void) n_items;
    update_commands (user_data);
}

/* Extracting */

typedef struct {
    ZarArchive *archive;
    GArray *indices;
    char *destination;
    gboolean overwrite;
} ExtractRequest;

static void
extract_request_free (gpointer data)
{
    ExtractRequest *request = data;
    zar_archive_unref (request->archive);
    g_array_unref (request->indices);
    g_free (request->destination);
    g_free (request);
}

static zarpack_status
run_extract (ZarJob *job, gpointer data, char *message, gsize message_size)
{
    ExtractRequest *request = data;
    const size_t *indices = NULL;
    g_autofree size_t *converted = g_new (size_t, request->indices->len + 1);
    for (guint i = 0; i < request->indices->len; i++)
        converted[i] = g_array_index (request->indices, guint, i);
    if (request->indices->len)
        indices = converted;
    return zarpack_extract (zar_archive_get_handle (request->archive), indices, request->indices->len,
                            request->destination, request->overwrite, zar_job_progress_callback (), job, message,
                            message_size);
}

static void
on_extract_progress (guint64 done, guint64 total, const char *file, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    if (total > 0)
        gtk_progress_bar_set_fraction (self->progress, (double) done / (double) total);
    else
        gtk_progress_bar_pulse (self->progress);
    gtk_label_set_text (self->progress_label, file);
}

static void start_extraction (ZarArchiveWindow *self, gboolean overwrite);

static void
on_replace_answered (GObject *source, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (ZarArchiveWindow) self = user_data;
    if (g_str_equal (adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result), "replace"))
        start_extraction (self, TRUE);
}

static void
on_toast_show (AdwToast *toast, gpointer user_data)
{
    zar_show_in_folder (GTK_WIDGET (user_data), g_object_get_data (G_OBJECT (toast), "path"));
}

static void
on_extract_done (zarpack_status status, const char *message, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    self->job = NULL;
    update_commands (self);

    switch (status) {
    case ZARPACK_OK: {
        g_autofree char *name = g_path_get_basename (self->extract_reveal);
        g_autofree char *title = g_strdup_printf ("Extracted “%s”", name);
        AdwToast *toast = adw_toast_new (title);
        adw_toast_set_button_label (toast, "Show");
        g_object_set_data_full (G_OBJECT (toast), "path", g_strdup (self->extract_reveal), g_free);
        g_signal_connect (toast, "button-clicked", G_CALLBACK (on_toast_show), self);
        adw_toast_overlay_add_toast (self->toasts, toast);
        if (g_settings_get_boolean (self->settings, "open-folder-after-extract"))
            zar_show_in_folder (GTK_WIDGET (self), self->extract_reveal);
        break;
    }
    case ZARPACK_CANCELLED:
        break;
    case ZARPACK_ERR_OUTPUT_EXISTS: {
        g_autofree char *body =
            g_strdup_printf ("%s\n\nSome items already exist in the destination. Replacing them can’t be undone.", message);
        AdwDialog *dialog = adw_alert_dialog_new ("Replace Existing Items?", body);
        adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", "_Cancel", "replace", "_Replace", NULL);
        adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "replace", ADW_RESPONSE_DESTRUCTIVE);
        adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
        adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
        adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL, on_replace_answered,
                                 g_object_ref (self));
        break;
    }
    default: {
        AdwDialog *dialog = adw_alert_dialog_new ("Couldn’t Extract", message);
        adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", "_OK");
        adw_dialog_present (dialog, GTK_WIDGET (self));
        break;
    }
    }
}

static void
start_extraction (ZarArchiveWindow *self, gboolean overwrite)
{
    ExtractRequest *request = g_new0 (ExtractRequest, 1);
    request->archive = zar_archive_ref (self->archive);
    request->indices = g_array_ref (self->extract_indices);
    request->destination = g_strdup (self->extract_destination);
    request->overwrite = overwrite;

    gtk_progress_bar_set_fraction (self->progress, 0);
    gtk_label_set_text (self->progress_label, "");
    gtk_widget_set_visible (self->extract_button, FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->selection_label), FALSE);
    gtk_widget_set_visible (self->progress_box, TRUE);
    gtk_action_bar_set_revealed (self->action_bar, TRUE);
    self->job = zar_job_start (run_extract, request, extract_request_free, on_extract_progress, on_extract_done,
                               G_OBJECT (self));
    update_commands (self);
}

/* `roots` empty: extract everything into a new folder named after the archive. */
static void
extract_into (ZarArchiveWindow *self, const char *folder, GArray *roots)
{
    g_clear_pointer (&self->extract_indices, g_array_unref);
    g_free (self->extract_destination);
    g_free (self->extract_reveal);
    self->extract_indices = g_array_ref (roots);
    if (roots->len == 0) {
        g_autofree char *stem = zar_stem (self->path);
        self->extract_destination = zar_unique_path (folder, stem, "");
        self->extract_reveal = g_strdup (self->extract_destination);
    } else {
        self->extract_destination = g_strdup (folder);
        self->extract_reveal = roots->len == 1
            ? g_build_filename (folder, zar_archive_get_entry (self->archive, g_array_index (roots, guint, 0))->name, NULL)
            : g_strdup (folder);
    }
    start_extraction (self, FALSE);
}

typedef struct {
    ZarArchiveWindow *self;
    GArray *roots;
} DestinationRequest;

static void
on_destination_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
    DestinationRequest *request = user_data;
    g_autoptr (GFile) folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, NULL);
    g_autofree char *path = folder ? g_file_get_path (folder) : NULL;
    if (path)
        extract_into (request->self, path, request->roots);
    g_object_unref (request->self);
    g_array_unref (request->roots);
    g_free (request);
}

/* Asks for a destination, or uses the archive's folder, depending on the setting. Takes `roots`. */
static void
extract (ZarArchiveWindow *self, GArray *roots)
{
    if (self->job) {
        g_array_unref (roots);
        return;
    }
    if (g_settings_get_enum (self->settings, "extract-destination") == 1) {
        g_autofree char *folder = g_path_get_dirname (self->path);
        extract_into (self, folder, roots);
        g_array_unref (roots);
        return;
    }
    DestinationRequest *request = g_new0 (DestinationRequest, 1);
    request->self = g_object_ref (self);
    request->roots = roots;
    g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, "Extract To");
    gtk_file_dialog_set_accept_label (dialog, "Extract");
    g_autofree char *parent = g_path_get_dirname (self->path);
    g_autoptr (GFile) initial = g_file_new_for_path (parent);
    gtk_file_dialog_set_initial_folder (dialog, initial);
    gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL, on_destination_chosen, request);
}

/* Actions */

static void
action_go_up (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    go_up (user_data);
}

static void
action_extract_all (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action, (void) parameter;
    extract (user_data, g_array_new (FALSE, FALSE, sizeof (guint)));
}

static void
action_extract_selected (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) action, (void) parameter;
    g_autoptr (GArray) selected = selected_indices (self);
    extract (self, zar_archive_top_level (self->archive, selected));
}

static void
action_open_selected (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) action, (void) parameter;
    gint64 folder = selected_folder (self);
    if (folder >= 0)
        navigate (self, folder);
}

static void
action_cancel (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) action, (void) parameter;
    if (self->job)
        zar_job_cancel (self->job);
}

/* Rows: drag out, context menu */

/* Makes sure the row under the pointer is part of the selection, like the file manager. */
static void
select_row (ZarArchiveWindow *self, GtkWidget *cell)
{
    guint position = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (cell), "position"));
    if (!gtk_selection_model_is_selected (GTK_SELECTION_MODEL (self->selection), position))
        gtk_selection_model_select_item (GTK_SELECTION_MODEL (self->selection), position, TRUE);
}

static GdkContentProvider *
on_drag_prepare (GtkDragSource *source, double x, double y, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) x, (void) y;
    if (!self->archive)
        return NULL;
    select_row (self, gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (source)));
    g_autoptr (GArray) selected = selected_indices (self);
    return zar_drag_content_new (self->archive, zar_archive_top_level (self->archive, selected));
}

static void
on_drag_begin (GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{
    (void) drag, (void) user_data;
    GtkWidget *cell = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (source));
    ZarArchiveItem *item = g_object_get_data (G_OBJECT (cell), "item");
    if (!item)
        return;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display (gtk_widget_get_display (cell));
    g_autoptr (GtkIconPaintable) icon =
        gtk_icon_theme_lookup_by_gicon (theme, zar_archive_item_get_icon (item), 32,
                                        gtk_widget_get_scale_factor (cell), GTK_TEXT_DIR_NONE, 0);
    gtk_drag_source_set_icon (source, GDK_PAINTABLE (icon), 16, 16);
}

static void
on_secondary_click (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    ZarArchiveWindow *self = user_data;
    (void) n_press;
    GtkWidget *cell = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
    select_row (self, cell);
    graphene_point_t point;
    if (!gtk_widget_compute_point (cell, GTK_WIDGET (self->list), &GRAPHENE_POINT_INIT ((float) x, (float) y), &point))
        return;
    gtk_popover_set_pointing_to (GTK_POPOVER (self->context_menu),
                                 &(GdkRectangle) { (int) point.x, (int) point.y, 1, 1 });
    gtk_popover_popup (GTK_POPOVER (self->context_menu));
}

/* Every cell reacts to drags and right-clicks, so the whole row does. */
static void
add_row_controllers (ZarArchiveWindow *self, GtkWidget *cell)
{
    GtkDragSource *drag = gtk_drag_source_new ();
    gtk_drag_source_set_actions (drag, GDK_ACTION_COPY);
    g_signal_connect (drag, "prepare", G_CALLBACK (on_drag_prepare), self);
    g_signal_connect (drag, "drag-begin", G_CALLBACK (on_drag_begin), self);
    gtk_widget_add_controller (cell, GTK_EVENT_CONTROLLER (drag));

    GtkGesture *click = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_SECONDARY);
    g_signal_connect (click, "pressed", G_CALLBACK (on_secondary_click), self);
    gtk_widget_add_controller (cell, GTK_EVENT_CONTROLLER (click));
}

static void
bind_common (GtkColumnViewCell *cell, GtkWidget *widget)
{
    g_object_set_data (G_OBJECT (widget), "item", gtk_column_view_cell_get_item (cell));
    g_object_set_data (G_OBJECT (widget), "position", GUINT_TO_POINTER (gtk_column_view_cell_get_position (cell)));
}

static void
setup_name (GtkSignalListItemFactory *factory, GObject *object, gpointer user_data)
{
    (void) factory;
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_image_new ();
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 24);
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append (GTK_BOX (box), icon);
    gtk_box_append (GTK_BOX (box), label);
    add_row_controllers (user_data, box);
    gtk_column_view_cell_set_child (GTK_COLUMN_VIEW_CELL (object), box);
}

static void
bind_name (GtkSignalListItemFactory *factory, GObject *object, gpointer user_data)
{
    (void) factory, (void) user_data;
    GtkColumnViewCell *cell = GTK_COLUMN_VIEW_CELL (object);
    ZarArchiveItem *item = gtk_column_view_cell_get_item (cell);
    GtkWidget *box = gtk_column_view_cell_get_child (cell);
    gtk_image_set_from_gicon (GTK_IMAGE (gtk_widget_get_first_child (box)), zar_archive_item_get_icon (item));
    gtk_label_set_text (GTK_LABEL (gtk_widget_get_last_child (box)), zar_archive_item_get_name (item));
    bind_common (cell, box);
}

static void
setup_text (GtkSignalListItemFactory *factory, GObject *object, gpointer user_data)
{
    gboolean numeric = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (factory), "numeric"));
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), numeric ? 1 : 0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class (label, "dim-label");
    if (numeric)
        gtk_widget_add_css_class (label, "numeric");
    add_row_controllers (user_data, label);
    gtk_column_view_cell_set_child (GTK_COLUMN_VIEW_CELL (object), label);
}

static void
bind_size (GtkSignalListItemFactory *factory, GObject *object, gpointer user_data)
{
    (void) factory, (void) user_data;
    GtkColumnViewCell *cell = GTK_COLUMN_VIEW_CELL (object);
    GtkWidget *label = gtk_column_view_cell_get_child (cell);
    gtk_label_set_text (GTK_LABEL (label), zar_archive_item_get_size_text (gtk_column_view_cell_get_item (cell)));
    bind_common (cell, label);
}

static void
bind_type (GtkSignalListItemFactory *factory, GObject *object, gpointer user_data)
{
    (void) factory, (void) user_data;
    GtkColumnViewCell *cell = GTK_COLUMN_VIEW_CELL (object);
    GtkWidget *label = gtk_column_view_cell_get_child (cell);
    gtk_label_set_text (GTK_LABEL (label), zar_archive_item_get_type_text (gtk_column_view_cell_get_item (cell)));
    bind_common (cell, label);
}

static void
add_column (ZarArchiveWindow *self, const char *title, GCallback setup, GCallback bind, gboolean numeric,
            gboolean expand, int width)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_object_set_data (G_OBJECT (factory), "numeric", GINT_TO_POINTER (numeric));
    g_signal_connect (factory, "setup", setup, self);
    g_signal_connect (factory, "bind", bind, self);
    GtkColumnViewColumn *column = gtk_column_view_column_new (title, factory);
    gtk_column_view_column_set_expand (column, expand);
    gtk_column_view_column_set_resizable (column, TRUE);
    if (width > 0)
        gtk_column_view_column_set_fixed_width (column, width);
    gtk_column_view_append_column (self->list, column);
    g_object_unref (column);
}

/* Construction */

static GtkWidget *
build_menu_button (void)
{
    g_autoptr (GMenu) menu = g_menu_new ();
    g_autoptr (GMenu) section = g_menu_new ();
    g_menu_append (section, "Extract _All…", "win.extract-all");
    g_menu_append (section, "_Extract Selected…", "win.extract-selected");
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
build_list (ZarArchiveWindow *self)
{
    self->items = g_list_store_new (ZAR_TYPE_ARCHIVE_ITEM);
    self->selection = gtk_multi_selection_new (G_LIST_MODEL (g_object_ref (self->items)));
    g_signal_connect (self->selection, "selection-changed", G_CALLBACK (on_selection_changed), self);
    self->list = GTK_COLUMN_VIEW (gtk_column_view_new (GTK_SELECTION_MODEL (g_object_ref (self->selection))));
    gtk_column_view_set_show_row_separators (self->list, FALSE);
    gtk_widget_add_css_class (GTK_WIDGET (self->list), "data-table");
    g_signal_connect (self->list, "activate", G_CALLBACK (on_activate), self);
    add_column (self, "Name", G_CALLBACK (setup_name), G_CALLBACK (bind_name), FALSE, TRUE, 0);
    add_column (self, "Size", G_CALLBACK (setup_text), G_CALLBACK (bind_size), TRUE, FALSE, 100);
    add_column (self, "Type", G_CALLBACK (setup_text), G_CALLBACK (bind_type), FALSE, FALSE, 180);

    g_autoptr (GMenu) menu = g_menu_new ();
    g_menu_append (menu, "_Open", "win.open-selected");
    g_menu_append (menu, "_Extract…", "win.extract-selected");
    self->context_menu = GTK_POPOVER_MENU (gtk_popover_menu_new_from_model (G_MENU_MODEL (menu)));
    gtk_popover_set_has_arrow (GTK_POPOVER (self->context_menu), FALSE);
    gtk_widget_set_halign (GTK_WIDGET (self->context_menu), GTK_ALIGN_START);
    gtk_widget_set_parent (GTK_WIDGET (self->context_menu), GTK_WIDGET (self->list));
}

static GtkWidget *
build_action_bar (ZarArchiveWindow *self)
{
    self->action_bar = GTK_ACTION_BAR (gtk_action_bar_new ());
    self->selection_label = GTK_LABEL (gtk_label_new (NULL));
    gtk_action_bar_pack_start (self->action_bar, GTK_WIDGET (self->selection_label));

    self->extract_button = gtk_button_new_with_mnemonic ("_Extract…");
    gtk_widget_add_css_class (self->extract_button, "suggested-action");
    gtk_actionable_set_action_name (GTK_ACTIONABLE (self->extract_button), "win.extract-selected");
    gtk_action_bar_pack_end (self->action_bar, self->extract_button);

    self->progress_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *texts = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand (texts, TRUE);
    gtk_widget_set_valign (texts, GTK_ALIGN_CENTER);
    GtkWidget *heading = gtk_label_new ("Extracting…");
    gtk_label_set_xalign (GTK_LABEL (heading), 0);
    gtk_widget_add_css_class (heading, "heading");
    self->progress = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
    self->progress_label = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_xalign (self->progress_label, 0);
    gtk_label_set_ellipsize (self->progress_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class (GTK_WIDGET (self->progress_label), "caption");
    gtk_widget_add_css_class (GTK_WIDGET (self->progress_label), "dim-label");
    gtk_box_append (GTK_BOX (texts), heading);
    gtk_box_append (GTK_BOX (texts), GTK_WIDGET (self->progress));
    gtk_box_append (GTK_BOX (texts), GTK_WIDGET (self->progress_label));
    GtkWidget *cancel = gtk_button_new_with_mnemonic ("_Cancel");
    gtk_widget_set_valign (cancel, GTK_ALIGN_CENTER);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (cancel), "win.cancel");
    gtk_box_append (GTK_BOX (self->progress_box), texts);
    gtk_box_append (GTK_BOX (self->progress_box), cancel);
    gtk_widget_set_hexpand (self->progress_box, TRUE);
    gtk_action_bar_set_center_widget (self->action_bar, self->progress_box);
    gtk_widget_set_visible (self->progress_box, FALSE);
    gtk_action_bar_set_revealed (self->action_bar, FALSE);
    return GTK_WIDGET (self->action_bar);
}

static void
zar_archive_window_dispose (GObject *object)
{
    ZarArchiveWindow *self = ZAR_ARCHIVE_WINDOW (object);
    if (self->job)
        zar_job_cancel (self->job);
    if (self->context_menu) {
        gtk_widget_unparent (GTK_WIDGET (self->context_menu));
        self->context_menu = NULL;
    }
    g_clear_pointer (&self->archive, zar_archive_unref);
    g_clear_pointer (&self->extract_indices, g_array_unref);
    g_clear_pointer (&self->extract_destination, g_free);
    g_clear_pointer (&self->extract_reveal, g_free);
    g_clear_pointer (&self->path, g_free);
    g_clear_object (&self->items);
    g_clear_object (&self->selection);
    g_clear_object (&self->settings);
    G_OBJECT_CLASS (zar_archive_window_parent_class)->dispose (object);
}

static void
zar_archive_window_class_init (ZarArchiveWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = zar_archive_window_dispose;
}

static void
zar_archive_window_init (ZarArchiveWindow *self)
{
    static const GActionEntry actions[] = {
        { .name = "go-up", .activate = action_go_up },
        { .name = "extract-all", .activate = action_extract_all },
        { .name = "extract-selected", .activate = action_extract_selected },
        { .name = "open-selected", .activate = action_open_selected },
        { .name = "cancel", .activate = action_cancel },
    };
    g_action_map_add_action_entries (G_ACTION_MAP (self), actions, G_N_ELEMENTS (actions), self);
    self->settings = g_settings_new (ZAR_APP_ID);
    self->folder = -1;
    gtk_window_set_default_size (GTK_WINDOW (self), 760, 540);

    GtkWidget *header = adw_header_bar_new ();
    GtkWidget *up = gtk_button_new_from_icon_name ("go-up-symbolic");
    gtk_widget_set_tooltip_text (up, "Up (Alt+Up)");
    gtk_actionable_set_action_name (GTK_ACTIONABLE (up), "win.go-up");
    adw_header_bar_pack_start (ADW_HEADER_BAR (header), up);
    self->title = ADW_WINDOW_TITLE (adw_window_title_new ("", NULL));
    adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), GTK_WIDGET (self->title));
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), build_menu_button ());
    GtkWidget *extract_all = gtk_button_new_with_mnemonic ("Extract _All");
    gtk_widget_add_css_class (extract_all, "suggested-action");
    gtk_actionable_set_action_name (GTK_ACTIONABLE (extract_all), "win.extract-all");
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), extract_all);

    build_list (self);
    GtkWidget *scroller = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), GTK_WIDGET (self->list));
    self->message_page = ADW_STATUS_PAGE (adw_status_page_new ());
    self->stack = GTK_STACK (gtk_stack_new ());
    gtk_stack_add_named (self->stack, scroller, "list");
    gtk_stack_add_named (self->stack, GTK_WIDGET (self->message_page), "message");
    self->toasts = ADW_TOAST_OVERLAY (adw_toast_overlay_new ());
    adw_toast_overlay_set_child (self->toasts, GTK_WIDGET (self->stack));

    GtkWidget *view = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), GTK_WIDGET (self->toasts));
    adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (view), build_action_bar (self));
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), view);
}

void
zar_archive_window_open (GtkApplication *app, GFile *file)
{
    ZarArchiveWindow *self = g_object_new (ZAR_TYPE_ARCHIVE_WINDOW, "application", app, NULL);
    self->path = g_file_get_path (file);
    g_autofree char *name = g_file_get_basename (file);
    gtk_window_set_title (GTK_WINDOW (self), name);
    adw_window_title_set_title (self->title, name);

    g_autoptr (GError) error = NULL;
    self->archive = self->path ? zar_archive_open (self->path, &error) : NULL;
    if (self->archive) {
        navigate (self, -1);
    } else {
        adw_status_page_set_icon_name (self->message_page, "dialog-error-symbolic");
        adw_status_page_set_title (self->message_page, "Can’t Open Archive");
        adw_status_page_set_description (self->message_page, error ? error->message : "The file can’t be read.");
        gtk_stack_set_visible_child_name (self->stack, "message");
        update_commands (self);
    }
    gtk_window_present (GTK_WINDOW (self));
}
