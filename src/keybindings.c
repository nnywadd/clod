// src/keybindings.c
#include "clod.h"
#include <sqlite3.h>
#include <stdio.h>

static sqlite3 *bookmark_db = NULL;

void init_bookmarks_db() {
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "%s/.clod_bookmarks.db", g_get_home_dir());
    
    if (sqlite3_open(db_path, &bookmark_db) == SQLITE_OK) {
        const char *schema = "CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY, path TEXT, page INTEGER);";
        sqlite3_exec(bookmark_db, schema, NULL, NULL, NULL);
    } else {
        g_printerr("Failed to initialize SQLite bookmarks.\n");
    }
}

static ClodTab* get_active_tab(ClodApp *app) {
    int page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (page_num < 0) return NULL; 
    GtkWidget *active_da = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page_num);
    return (ClodTab *)g_object_get_data(G_OBJECT(active_da), "clod-tab");
}

static void on_file_opened(GObject *source, GAsyncResult *res, gpointer data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, res, &error);
    if (file) {
        char *path = g_file_get_path(file);
        clod_open_file((ClodApp*)data, path);
        g_free(path);
        g_object_unref(file);
    }
}

static void action_open_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_open(dialog, GTK_WINDOW(((ClodApp*)data)->window), NULL, on_file_opened, data);
}

static void action_close_tab(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodApp *app = (ClodApp *)data;
    int current = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (current >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), current);
}

static void action_quit(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(((ClodApp*)data)->window));
    g_application_quit(G_APPLICATION(app));
}

// --- BOOKMARK LOGIC ---
static void action_add_bookmark(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (!tab || !tab->archive_path || !bookmark_db) return;
    
    char sql[1024];
    snprintf(sql, sizeof(sql), "INSERT INTO bookmarks (path, page) VALUES ('%s', %d);", 
             tab->archive_path, tab->current_page_idx);
    sqlite3_exec(bookmark_db, sql, NULL, NULL, NULL);
    g_print("Clod: Bookmark saved: %s at Page %d\n", tab->archive_path, tab->current_page_idx + 1);
}

static void on_bookmark_go_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    const char *path = g_object_get_data(G_OBJECT(btn), "bm-path");
    int page = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "bm-page"));
    ClodApp *app = g_object_get_data(G_OBJECT(btn), "bm-app");
    GtkWidget *dialog = g_object_get_data(G_OBJECT(btn), "bm-dialog");

    clod_open_file(app, path);
    ClodTab *new_tab = get_active_tab(app); 
    if (new_tab) {
        new_tab->current_page_idx = page;
        clod_tab_load_page(new_tab);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_bookmark_delete_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "bm-id"));
    GtkWidget *row_box = g_object_get_data(G_OBJECT(btn), "bm-row");

    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM bookmarks WHERE id = %d;", id);
    sqlite3_exec(bookmark_db, sql, NULL, NULL, NULL);

    GtkWidget *list_row = gtk_widget_get_parent(row_box);
    GtkWidget *list_box = gtk_widget_get_parent(list_row);
    gtk_list_box_remove(GTK_LIST_BOX(list_box), list_row);
}

static void action_manage_bookmarks(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodApp *app = (ClodApp *)data;
    if (!bookmark_db) return;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), "Manage Bookmarks");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(dialog), scroll);

    GtkWidget *listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), listbox);

    sqlite3_stmt *res;
    const char *sql = "SELECT id, path, page FROM bookmarks ORDER BY id DESC;";
    
    if (sqlite3_prepare_v2(bookmark_db, sql, -1, &res, 0) == SQLITE_OK) {
        while (sqlite3_step(res) == SQLITE_ROW) {
            int id = sqlite3_column_int(res, 0);
            const char *path = (const char *)sqlite3_column_text(res, 1);
            int page = sqlite3_column_int(res, 2);

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_set_margin_start(row, 10);
            gtk_widget_set_margin_end(row, 10);
            gtk_widget_set_margin_top(row, 5);
            gtk_widget_set_margin_bottom(row, 5);

            char *base = g_strdup(g_path_get_basename(path));
            char label_text[512];
            snprintf(label_text, sizeof(label_text), "%s (Page %d)", base, page + 1);
            g_free(base);

            GtkWidget *lbl = gtk_label_new(label_text);
            gtk_widget_set_hexpand(lbl, TRUE);
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(row), lbl);

            GtkWidget *btn_go = gtk_button_new_with_label("Open");
            g_object_set_data_full(G_OBJECT(btn_go), "bm-path", g_strdup(path), g_free);
            g_object_set_data(G_OBJECT(btn_go), "bm-page", GINT_TO_POINTER(page));
            g_object_set_data(G_OBJECT(btn_go), "bm-app", app);
            g_object_set_data(G_OBJECT(btn_go), "bm-dialog", dialog);
            g_signal_connect(btn_go, "clicked", G_CALLBACK(on_bookmark_go_clicked), NULL);
            gtk_box_append(GTK_BOX(row), btn_go);

            GtkWidget *btn_del = gtk_button_new_with_label("Remove");
            g_object_set_data(G_OBJECT(btn_del), "bm-id", GINT_TO_POINTER(id));
            g_object_set_data(G_OBJECT(btn_del), "bm-row", row);
            g_signal_connect(btn_del, "clicked", G_CALLBACK(on_bookmark_delete_clicked), NULL);
            gtk_box_append(GTK_BOX(row), btn_del);

            gtk_list_box_append(GTK_LIST_BOX(listbox), row);
        }
    }
    sqlite3_finalize(res);
    gtk_window_present(GTK_WINDOW(dialog));
}

// --- EDIT/VIEW ACTIONS ---
static void action_export_png(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab && !tab->is_pdf && tab->current_pixbuf) {
        char path[256];
        snprintf(path, sizeof(path), "%s/clod_screenshot_page_%d.png", g_get_home_dir(), tab->current_page_idx + 1);
        gdk_pixbuf_save(tab->current_pixbuf, path, "png", NULL, NULL);
        g_print("Screenshot saved: %s\n", path);
    }
}

static void action_minimal_mode(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodApp *app = (ClodApp *)data;
    
    if (app->minimal_mode) {
        app->minimal_mode = FALSE;
        gtk_widget_set_visible(app->menubar, TRUE);
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), TRUE);
        gtk_window_set_decorated(GTK_WINDOW(app->window), TRUE); // Restore WM borders
    } else {
        app->minimal_mode = TRUE;
        gtk_widget_set_visible(app->menubar, FALSE);
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), FALSE);
        gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE); // Strip WM Borders
    }
}

static void action_fullscreen(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodApp *app = (ClodApp *)data;
    if (gtk_window_is_fullscreen(GTK_WINDOW(app->window))) {
        gtk_window_unfullscreen(GTK_WINDOW(app->window));
        if (!app->minimal_mode) {
            gtk_widget_set_visible(app->menubar, TRUE);
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), TRUE);
            gtk_window_set_decorated(GTK_WINDOW(app->window), TRUE);
        }
    } else {
        gtk_window_fullscreen(GTK_WINDOW(app->window));
        gtk_widget_set_visible(app->menubar, FALSE);
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), FALSE);
    }
}

static void action_toggle_menu(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodApp *app = (ClodApp *)data;
    gtk_widget_set_visible(app->menubar, !gtk_widget_get_visible(app->menubar));
}

static void action_comic_mode(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) tab->manga_mode = FALSE; 
}

static void action_manga_mode(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) tab->manga_mode = TRUE; 
}

static void action_zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) { tab->zoom_level += 0.25; gtk_widget_queue_draw(tab->drawing_area); }
}

static void action_zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) { tab->zoom_level -= 0.25; gtk_widget_queue_draw(tab->drawing_area); }
}

static void advance_page(ClodTab *tab, int direction) {
    int actual_dir = tab->manga_mode ? -direction : direction;
    if (actual_dir > 0 && tab->current_page_idx < tab->total_pages - 1) {
        tab->current_page_idx++;
        clod_tab_load_page(tab);
    } else if (actual_dir < 0 && tab->current_page_idx > 0) {
        tab->current_page_idx--;
        clod_tab_load_page(tab);
    }
}

static void action_next_page(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) advance_page(tab, 1);
}

static void action_prev_page(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    ClodTab *tab = get_active_tab((ClodApp *)data);
    if (tab) advance_page(tab, -1);
}

static void action_dummy(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param; (void)data;
}

static const GActionEntry app_entries[] = {
    { .name = "open-file", .activate = action_open_file },
    { .name = "close-tab", .activate = action_close_tab },
    { .name = "add-bookmark", .activate = action_add_bookmark },
    { .name = "manage-bookmarks", .activate = action_manage_bookmarks },
    { .name = "quit", .activate = action_quit },
    { .name = "export-png", .activate = action_export_png },
    { .name = "fullscreen", .activate = action_fullscreen },
    { .name = "toggle-menu", .activate = action_toggle_menu },
    { .name = "minimal-mode", .activate = action_minimal_mode },
    { .name = "comic-mode", .activate = action_comic_mode },
    { .name = "manga-mode", .activate = action_manga_mode },
    { .name = "zoom-in", .activate = action_zoom_in },
    { .name = "zoom-out", .activate = action_zoom_out },
    { .name = "next-page", .activate = action_next_page },
    { .name = "prev-page", .activate = action_prev_page },
    { .name = "goto-page", .activate = action_dummy }
};

void setup_keybindings(GtkApplication *app_inst, ClodApp *app_data) {
    init_bookmarks_db();
    g_action_map_add_action_entries(G_ACTION_MAP(app_inst), app_entries, G_N_ELEMENTS(app_entries), app_data);

    const gchar *acc_next[] = { "Right", "space", NULL }; 
    gtk_application_set_accels_for_action(app_inst, "app.next-page", acc_next);
    
    const gchar *acc_prev[] = { "Left", "BackSpace", NULL };
    gtk_application_set_accels_for_action(app_inst, "app.prev-page", acc_prev);
    
    const gchar *acc_close[] = { "<Primary>w", NULL }; 
    gtk_application_set_accels_for_action(app_inst, "app.close-tab", acc_close);

    const gchar *acc_menu[] = { "<Primary>m", NULL }; 
    gtk_application_set_accels_for_action(app_inst, "app.toggle-menu", acc_menu);
    
    const gchar *acc_quit[] = { "<Primary>q", NULL }; 
    gtk_application_set_accels_for_action(app_inst, "app.quit", acc_quit);
}