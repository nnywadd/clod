// src/archive_handler.c
#include "clod.h"
#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#define CHUNK_SIZE 16384

static gboolean is_image_extension(const char *filename) {
    if (!filename) return FALSE;
    const char *ext = strrchr(filename, '.');
    if (!ext) return FALSE;
    return (g_ascii_strcasecmp(ext, ".jpg") == 0 ||
            g_ascii_strcasecmp(ext, ".jpeg") == 0 ||
            g_ascii_strcasecmp(ext, ".png") == 0 ||
            g_ascii_strcasecmp(ext, ".webp") == 0);
}

GList* clod_get_archive_pages(const char *archive_path) {
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    GList *pages = NULL;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, archive_path, CHUNK_SIZE) != ARCHIVE_OK) {
        g_printerr("Clod Error: Failed to open archive %s.\n", archive_path);
        archive_read_free(a);
        return NULL;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *entry_name = archive_entry_pathname(entry);
        if (is_image_extension(entry_name)) {
            pages = g_list_prepend(pages, g_strdup(entry_name));
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a); // Explicit close removed to prevent mid-stream aborts
    
    pages = g_list_sort(pages, (GCompareFunc)g_strcmp0);
    g_print("Clod Engine: Found %d valid image pages in %s\n", g_list_length(pages), archive_path);
    return pages;
}

GdkPixbuf* clod_extract_page(const char *archive_path, const char *page_name) {
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    GdkPixbuf *pixbuf = NULL;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, archive_path, CHUNK_SIZE) != ARCHIVE_OK) {
        g_printerr("Clod Error: Failed to open %s for extraction.\n", archive_path);
        archive_read_free(a);
        return NULL;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (g_strcmp0(archive_entry_pathname(entry), page_name) == 0) {
            GError *error = NULL;
            GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
            char buffer[CHUNK_SIZE];
            gssize bytes_read;
            long total_read = 0;

            while ((bytes_read = archive_read_data(a, buffer, sizeof(buffer))) > 0) {
                if (!gdk_pixbuf_loader_write(loader, (const guchar *)buffer, bytes_read, &error)) {
                    g_printerr("Clod Engine Warning: Corrupted image data: %s\n", error->message);
                    g_error_free(error);
                    error = NULL; 
                    break; 
                }
                total_read += bytes_read;
            }

            // Safely close the loader
            if (!gdk_pixbuf_loader_close(loader, &error)) {
                g_printerr("Clod Engine Warning: Loader close error: %s\n", error->message);
                g_error_free(error);
            }

            pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
            if (pixbuf) {
                g_object_ref(pixbuf); // Retain memory before destroying loader
                g_print("Clod Engine: Extracted '%s' (%ld bytes)\n", page_name, total_read);
            } else {
                g_printerr("Clod Error: Failed to generate Pixbuf for '%s'\n", page_name);
            }
            
            g_object_unref(loader);
            break;
        }
    }

    archive_read_free(a); // Safely frees without triggering end-of-file flushes
    g_print("Clod Engine: Libarchive handoff complete.\n");
    return pixbuf;
}