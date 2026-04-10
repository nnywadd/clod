// src/main.c
#include "clod.h"
#include <libgen.h> 
#include <string.h>

// --- THEME ENGINE ---
void apply_johnny_cyan_theme() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css = 
        "window, dialog { background-color: #1E2E3E; color: #15F4EE; }\n"
        "menubar { background-color: #091018; border-bottom: 2px solid #15F4EE; padding: 4px; }\n"
        "menubar > item { color: #15F4EE; padding: 8px 16px; font-weight: bold; font-size: 13px; }\n"
        "menubar > item:hover { background-color: #2A4A5A; }\n"
        "popover > contents { padding: 12px; background-color: #1E2E3E; border: 2px solid #15F4EE; border-radius: 6px; }\n"
        "popover.menu modelbutton { min-height: 42px; padding: 8px 24px; margin-bottom: 4px; color: #15F4EE; font-size: 14px; }\n"
        "popover.menu modelbutton:hover { background-color: #15F4EE; color: #000000; }\n"
        "notebook, notebook > stack { border: none; padding: 0; margin: 0; background: transparent; }\n" // Nuke borders for Minimal Mode
        "notebook > header { background-color: #091018; }\n"
        "notebook tab { color: #15F4EE; background: rgba(9, 16, 24, 0.5); padding: 8px 16px; min-height: 30px; }\n"
        "notebook tab:checked { background-color: #1E2E3E; border: 1px solid #15F4EE; border-bottom: none; }";
    
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// --- RENDERER ---
static void draw_page_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    (void)area; 
    ClodTab *tab = (ClodTab *)data;

    cairo_set_source_rgb(cr, 0.11, 0.18, 0.24); // #1E2E3E
    cairo_paint(cr);

    if ((!tab->current_pixbuf && !tab->is_pdf) || width <= 0 || height <= 0) return; 

    double img_w = 0, img_h = 0;
    
    if (!tab->is_pdf) {
        if (!GDK_IS_PIXBUF(tab->current_pixbuf)) return;
        img_w = gdk_pixbuf_get_width(tab->current_pixbuf);
        img_h = gdk_pixbuf_get_height(tab->current_pixbuf);
    } 
#ifdef WITH_PDF
    else if (tab->is_pdf && tab->pdf_page) {
        poppler_page_get_size(tab->pdf_page, &img_w, &img_h);
    }
#endif

    if (img_w <= 0 || img_h <= 0) return;

    double scale = MIN((double)width / img_w, (double)height / img_h) * tab->zoom_level; 
    double offset_x = (width - (img_w * scale)) / 2.0;
    double offset_y = (height - (img_h * scale)) / 2.0;

    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);

    if (!tab->is_pdf) {
        gdk_cairo_set_source_pixbuf(cr, tab->current_pixbuf, 0, 0);
        cairo_paint(cr); 
    } 
#ifdef WITH_PDF
    else {
        poppler_page_render(tab->pdf_page, cr); 
    }
#endif
    cairo_restore(cr);

    if (tab->magnifier_active) {
        double lens_radius = 150.0, zoom = 2.0;
        cairo_arc(cr, tab->mag_x, tab->mag_y, lens_radius, 0, 2 * G_PI);
        cairo_clip(cr);
        cairo_translate(cr, tab->mag_x, tab->mag_y);
        cairo_scale(cr, zoom, zoom);
        cairo_translate(cr, -tab->mag_x, -tab->mag_y);
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale, scale);
        
        if (!tab->is_pdf) gdk_cairo_set_source_pixbuf(cr, tab->current_pixbuf, 0, 0);
#ifdef WITH_PDF
        else poppler_page_render(tab->pdf_page, cr);
#endif
        cairo_paint(cr);
        cairo_reset_clip(cr);
        cairo_arc(cr, tab->mag_x, tab->mag_y, lens_radius, 0, 2 * G_PI);
        cairo_set_source_rgb(cr, 0.08, 0.95, 0.93);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }
}

// --- TAB STATE MANAGEMENT ---
void clod_tab_load_page(ClodTab *tab) {
    if (tab->is_pdf) {
#ifdef WITH_PDF
        if (tab->pdf_page) g_object_unref(tab->pdf_page);
        tab->pdf_page = poppler_document_get_page(tab->pdf_doc, tab->current_page_idx);
#endif
    } else {
        if (!tab->pages) return;
        char *page_name = (char *)g_list_nth_data(tab->pages, tab->current_page_idx);
        if (tab->current_pixbuf) g_object_unref(tab->current_pixbuf);
        tab->current_pixbuf = clod_extract_page(tab->archive_path, page_name);
    }
    
    if (tab->archive_path) {
        char title[256];
        char *base = g_strdup(basename(tab->archive_path));
        snprintf(title, sizeof(title), "%s [%d/%d]", base, tab->current_page_idx + 1, tab->total_pages);
        gtk_label_set_text(GTK_LABEL(tab->tab_label), title);
        g_free(base);
    }
    gtk_widget_queue_draw(tab->drawing_area);
}

// NEW: Tracks mouse movement for the active magnifier
static void on_tab_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data) {
    (void)controller;
    ClodTab *tab = (ClodTab *)data;
    if (tab->magnifier_active) {
        tab->mag_x = x;
        tab->mag_y = y;
        gtk_widget_queue_draw(tab->drawing_area);
    }
}

static void on_tab_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    ClodTab *tab = (ClodTab *)data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    // Double Left-Click: Toggle Magnifier On/Off
    if (button == GDK_BUTTON_PRIMARY && n_press == 2) {
        tab->magnifier_active = !tab->magnifier_active;
        tab->mag_x = x;
        tab->mag_y = y;
        gtk_widget_queue_draw(tab->drawing_area);
    }
    
    // Right-Click: Spawn identical context menu at mouse coordinates
    else if (button == GDK_BUTTON_SECONDARY && tab->context_menu) {
        GdkRectangle rect = { (int)x, (int)y, 1, 1 };
        gtk_popover_set_pointing_to(GTK_POPOVER(tab->context_menu), &rect);
        gtk_popover_popup(GTK_POPOVER(tab->context_menu));
    }
}

void clod_open_file(ClodApp *app, const char *filepath) {
    ClodTab *tab = g_new0(ClodTab, 1);
    tab->archive_path = g_strdup(filepath);
    tab->zoom_level = 1.0;
    tab->manga_mode = FALSE;

    if (g_str_has_suffix(filepath, ".pdf") || g_str_has_suffix(filepath, ".PDF")) {
        tab->is_pdf = TRUE;
#ifdef WITH_PDF
        char *uri = g_filename_to_uri(filepath, NULL, NULL);
        tab->pdf_doc = poppler_document_new_from_file(uri, NULL, NULL);
        g_free(uri);
        if (tab->pdf_doc) tab->total_pages = poppler_document_get_n_pages(tab->pdf_doc);
#endif
    } else {
        tab->is_pdf = FALSE;
        tab->pages = clod_get_archive_pages(filepath);
        if (!tab->pages) { g_free(tab->archive_path); g_free(tab); return; }
        tab->total_pages = g_list_length(tab->pages);
    }
    
    if (tab->total_pages == 0) {
        g_free(tab->archive_path); g_free(tab); return;
    }

    tab->current_page_idx = 0;
    tab->drawing_area = gtk_drawing_area_new();
    
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(tab->drawing_area), 800);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(tab->drawing_area), 600);
    gtk_widget_set_vexpand(tab->drawing_area, TRUE);
    gtk_widget_set_hexpand(tab->drawing_area, TRUE);
    
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(tab->drawing_area), draw_page_cb, tab, NULL);
    g_object_set_data(G_OBJECT(tab->drawing_area), "clod-tab", tab);

    // Wire up tracking magnifier
    GtkEventController *motion_cont = gtk_event_controller_motion_new();
    g_signal_connect(motion_cont, "motion", G_CALLBACK(on_tab_motion), tab);
    gtk_widget_add_controller(tab->drawing_area, motion_cont);

    // Wire up clicks
    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 0); // Listen to ALL buttons (Left & Right)
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_tab_click_pressed), tab);
    gtk_widget_add_controller(tab->drawing_area, GTK_EVENT_CONTROLLER(click_gesture));

    // Construct Context Menu directly on the drawing area
    tab->context_menu = gtk_popover_menu_new_from_model(G_MENU_MODEL(app->main_menu_model));
    gtk_widget_set_parent(tab->context_menu, tab->drawing_area);
    gtk_popover_set_has_arrow(GTK_POPOVER(tab->context_menu), FALSE); // Cleaner look

    char *path_copy = g_strdup(filepath);
    tab->tab_label = gtk_label_new(basename(path_copy));
    g_free(path_copy);

    int new_idx = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab->drawing_area, tab->tab_label);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), new_idx);
    
    clod_tab_load_page(tab);
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller; (void)keycode; (void)state;
    ClodApp *app = (ClodApp *)user_data;
    
    if (keyval == GDK_KEY_Escape) {
        gboolean needs_restore = FALSE;
        
        if (gtk_window_is_fullscreen(GTK_WINDOW(app->window))) {
            gtk_window_unfullscreen(GTK_WINDOW(app->window));
            needs_restore = TRUE;
        }
        if (app->minimal_mode) {
            app->minimal_mode = FALSE;
            needs_restore = TRUE;
        }
        
        if (needs_restore) {
            gtk_widget_set_visible(app->menubar, TRUE);
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), TRUE);
            gtk_window_set_decorated(GTK_WINDOW(app->window), TRUE);
            return TRUE;
        }
    }
    return FALSE;
}

// NEW: Separated to act as the master layout for both bars
static GMenu* create_main_menu_model() {
    GMenu *menubar = g_menu_new();
    
    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "Open Comic/PDF...", "app.open-file");
    g_menu_append(file_menu, "Close Current Tab", "app.close-tab");
    g_menu_append(file_menu, "Quit", "app.quit");
    g_menu_append_submenu(menubar, "File", G_MENU_MODEL(file_menu));

    GMenu *edit_menu = g_menu_new();
    g_menu_append(edit_menu, "Export Screenshot (PNG)", "app.export-png");
    g_menu_append(edit_menu, "Exclusive Fullscreen", "app.fullscreen");
    g_menu_append_submenu(menubar, "Edit", G_MENU_MODEL(edit_menu));

    GMenu *view_menu = g_menu_new();
    g_menu_append(view_menu, "Toggle Menubar (Ctrl+M)", "app.toggle-menu");
    g_menu_append(view_menu, "Minimal Mode (TWM Mode)", "app.minimal-mode");
    g_menu_append(view_menu, "Comic Mode (LTR)", "app.comic-mode");
    g_menu_append(view_menu, "Manga Mode (RTL)", "app.manga-mode");
    g_menu_append(view_menu, "Zoom In", "app.zoom-in");
    g_menu_append(view_menu, "Zoom Out", "app.zoom-out");
    g_menu_append_submenu(menubar, "View", G_MENU_MODEL(view_menu));

    GMenu *go_menu = g_menu_new();
    g_menu_append(go_menu, "Next Page", "app.next-page");
    g_menu_append(go_menu, "Previous Page", "app.prev-page");
    g_menu_append_submenu(menubar, "Go", G_MENU_MODEL(go_menu));

    GMenu *bookmarks_menu = g_menu_new();
    g_menu_append(bookmarks_menu, "Add Bookmark", "app.add-bookmark");
    g_menu_append(bookmarks_menu, "Manage Bookmarks...", "app.manage-bookmarks");
    g_menu_append_submenu(menubar, "Bookmarks", G_MENU_MODEL(bookmarks_menu));

    return menubar;
}

static ClodApp* build_clod_ui(GtkApplication *app_inst) {
    apply_johnny_cyan_theme();
    ClodApp *app = g_new0(ClodApp, 1);
    app->minimal_mode = FALSE; 

    app->window = gtk_application_window_new(app_inst);
    gtk_window_set_title(GTK_WINDOW(app->window), "Clod");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1200, 800);
    g_object_set_data(G_OBJECT(app->window), "app-struct", app);

    GtkWidget *root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), root_vbox);

    // Generate the shared menu model
    app->main_menu_model = create_main_menu_model();
    app->menubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(app->main_menu_model));
    gtk_box_append(GTK_BOX(root_vbox), app->menubar);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), TRUE); 
    gtk_widget_set_hexpand(app->notebook, TRUE); 
    gtk_widget_set_vexpand(app->notebook, TRUE);
    gtk_box_append(GTK_BOX(root_vbox), app->notebook);

    GtkEventController *key_cont = gtk_event_controller_key_new();
    g_signal_connect(key_cont, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(app->window, key_cont);

    extern void setup_keybindings(GtkApplication *inst, ClodApp *data);
    setup_keybindings(app_inst, app);
    gtk_window_present(GTK_WINDOW(app->window));
    
    return app;
}

static void on_activate(GtkApplication *app_inst, gpointer user_data) {
    (void)user_data;
    build_clod_ui(app_inst);
}

static void on_open(GApplication *app_inst, GFile **files, gint n_files, const gchar *hint, gpointer user_data) {
    (void)hint; (void)user_data;
    
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app_inst));
    ClodApp *app = NULL;
    
    if (windows) {
        app = g_object_get_data(G_OBJECT(windows->data), "app-struct");
    } else {
        app = build_clod_ui(GTK_APPLICATION(app_inst));
    }

    if (app && n_files > 0) {
        for (int i = 0; i < n_files; i++) {
            char *path = g_file_get_path(files[i]);
            if (path) {
                clod_open_file(app, path);
                g_free(path);
            }
        }
    }
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.gentoo.clod", G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}