// src/clod.h
#ifndef CLOD_H
#define CLOD_H

#include <gtk/gtk.h>

#ifdef WITH_PDF
#include <poppler.h>
#endif

typedef struct {
    char *archive_path;
    gboolean is_pdf;
    GList *pages;
    GdkPixbuf *current_pixbuf;
    
#ifdef WITH_PDF
    PopplerDocument *pdf_doc;
    PopplerPage *pdf_page;
#endif

    int current_page_idx;
    int total_pages;
    
    GtkWidget *drawing_area;
    GtkWidget *tab_label;
    GtkWidget *context_menu; // NEW: Tab-specific right-click menu
    
    double zoom_level;
    gboolean manga_mode;
    gboolean magnifier_active;
    double mag_x;
    double mag_y;
} ClodTab;

typedef struct {
    GtkWidget *window;
    GtkWidget *menubar;
    GtkWidget *notebook;
    GMenu *main_menu_model; // NEW: Shared menu model for Menubar and Right-Click
    gboolean minimal_mode; 
} ClodApp;

// Core Functions
void clod_open_file(ClodApp *app, const char *filepath);
void clod_tab_load_page(ClodTab *tab);

// Archive Handlers
GList* clod_get_archive_pages(const char *archive_path);
GdkPixbuf* clod_extract_page(const char *archive_path, const char *page_name);

#endif // CLOD_H