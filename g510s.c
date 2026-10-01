/*
 *  This file is part of g510s.
 *
 *  g510s is  free  software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the  Free Software Foundation; either version 3 of the License, or (at your
 *  option)  any later version.
 *
 *  g510s is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with g510s; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335  USA
 *
 *  Copyright © 2015 John Augustine
 *  Copyright © 2025 usr_40476
 */


#include <stdlib.h>
#include <pthread.h>
#include <libg15.h>
#include <libg15render.h>
#include <gtk/gtk.h>
#include <libappindicator3-0.1/libappindicator/app-indicator.h>
#include <glib-2.0/gio/gio.h>
#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#include "g510s.h"

// Terminal mode variables (referenced in this file)
int terminal_mode = 0;
char terminal_cmd[1024] = {0};

// Display buffer for preview
unsigned char preview_buffer[G15_BUFFER_LEN];

// Dump display buffer flag
int dump_display_buffer = 0;

// Bank configs array
static bank_config_t bank_configs[MAX_BANK_CONFIGS];
static int bank_config_count = 0;

// DBus interface and object path
#define G510S_DBUS_NAME "org.g510s.control"
#define G510S_DBUS_PATH "/org/g510s/control"
#define G510S_DBUS_INTERFACE "org.g510s.control"

// DBus property names
#define G510S_DBUS_PROP_MODE "Mode"
#define G510S_DBUS_PROP_COLOR "Color"
#define G510S_DBUS_PROP_COLORFADE "ColorFade"
#define G510S_DBUS_PROP_AUTOSAVE "AutoSaveOnQuit"
#define G510S_DBUS_PROP_GUIHIDDEN "GuiHidden"

// Forward declarations for DBus handlers
static gboolean on_handle_set_mode(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data);

static gboolean on_handle_set_color(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data);

static gboolean on_handle_set_colorfade(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_set_autosave(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_set_guihidden(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data);

static void emit_properties_changed(GDBusConnection *connection);
static gboolean on_handle_save_config(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data);

// Forward declarations for macro DBus handlers
static gboolean on_handle_get_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_set_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_run_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data);

// Forward declarations for preset DBus handlers
static gboolean on_handle_save_preset(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_load_preset(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data);
static gboolean on_handle_list_presets(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                      const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                      GDBusMethodInvocation *invocation, gpointer user_data);

// Forward declaration for display_notification (defined in g510s-clock.c)
extern void display_notification(const char *text, int duration_ms, int priority);

// Forward declaration for display_editor (declared below as global widget pointer)
extern GtkTextView *display_editor;

// --- GUI refresh support ---
static void refresh_gui_internal();
static gboolean refresh_gui_idle(gpointer data) {
    refresh_gui_internal();
    return G_SOURCE_REMOVE;
}
void refresh_gui() {
    g_idle_add(refresh_gui_idle, NULL);
}

// --- Preset management ---
// Helper: get display.txt path
static void get_display_path(char *buf, size_t bufsize) {
    char home[255];
    strncpy(home, getenv("HOME"), sizeof(home));
    if (home[0]) {
        snprintf(buf, bufsize, "%s/.config/g510s/display.txt", home);
    } else {
        strncpy(buf, "display.txt", bufsize);
    }
}

void load_presets() {
    char configs_dir[512];
    
    bank_config_count = 0;
    memset(bank_configs, 0, sizeof(bank_configs));
    
    char home_path[255];
    strncpy(home_path, getenv("HOME"), sizeof(home_path));
    if (home_path[0] == '\0') return;
    
    snprintf(configs_dir, sizeof(configs_dir), "%s/.config/g510s/presets/", home_path);
    
    DIR *dir = opendir(configs_dir);
    if (!dir) return;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && bank_config_count < MAX_BANK_CONFIGS) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            char *dot = strrchr(entry->d_name, '.');
            if (dot && strcmp(dot, ".txt") == 0) {
                char fullpath[512];
                snprintf(fullpath, sizeof(fullpath), "%s%s", configs_dir, entry->d_name);
                
                FILE *f = fopen(fullpath, "r");
                if (f) {
                    char name_no_ext[64];
                    strncpy(name_no_ext, entry->d_name, strlen(entry->d_name) - 4);
                    name_no_ext[strlen(entry->d_name) - 4] = '\0';
                    strncpy(bank_configs[bank_config_count].name, name_no_ext, sizeof(bank_configs[bank_config_count].name) - 1);
                    
                    // Read the display script content
                    size_t nread = fread(bank_configs[bank_config_count].display_script, 1, sizeof(bank_configs[bank_config_count].display_script) - 1, f);
                    bank_configs[bank_config_count].display_script[nread] = '\0';
                    
                    fclose(f);
                    bank_config_count++;
                }
            }
        }
    }
    closedir(dir);
}

void save_preset(const char *name) {
    char presets_dir[512];
    char fullpath[512];
    
    char home_path[255];
    strncpy(home_path, getenv("HOME"), sizeof(home_path));
    if (home_path[0] == '\0') return;
    
    snprintf(presets_dir, sizeof(presets_dir), "%s/.config/g510s/presets/", home_path);
    mkdir(presets_dir, 0777);
    
    snprintf(fullpath, sizeof(fullpath), "%s%s.txt", presets_dir, name);
    
    // Read current display.txt content
    char display_path[512];
    get_display_path(display_path, sizeof(display_path));
    
    FILE *src = fopen(display_path, "r");
    FILE *f = fopen(fullpath, "w");
    if (f) {
        if (src) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                fwrite(buf, 1, n, f);
            }
            fclose(src);
        }
        fclose(f);
        
        // Update bank config list
        int found = 0;
        for (int i = 0; i < bank_config_count; i++) {
            if (strcmp(bank_configs[i].name, name) == 0) {
                found = 1;
                FILE *r = fopen(fullpath, "r");
                if (r) {
                    size_t n = fread(bank_configs[i].display_script, 1, sizeof(bank_configs[i].display_script) - 1, r);
                    bank_configs[i].display_script[n] = '\0';
                    fclose(r);
                }
                break;
            }
        }
        if (!found && bank_config_count < MAX_BANK_CONFIGS) {
            strncpy(bank_configs[bank_config_count].name, name, sizeof(bank_configs[bank_config_count].name) - 1);
            FILE *r = fopen(fullpath, "r");
            if (r) {
                size_t n = fread(bank_configs[bank_config_count].display_script, 1, sizeof(bank_configs[bank_config_count].display_script) - 1, r);
                bank_configs[bank_config_count].display_script[n] = '\0';
                fclose(r);
            }
            bank_config_count++;
        }
    }
}

void load_preset(const char *name) {
    for (int i = 0; i < bank_config_count; i++) {
        if (strcmp(bank_configs[i].name, name) == 0) {
            char display_path[512];
            get_display_path(display_path, sizeof(display_path));
            FILE *f = fopen(display_path, "w");
            if (f) {
                fprintf(f, "%s", bank_configs[i].display_script);
                fclose(f);
                printf("G510s: Loaded display preset '%s'\n", name);
            }
            
            // Update the editor if it exists
            if (display_editor) {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(display_editor);
                gtk_text_buffer_set_text(buffer, bank_configs[i].display_script, -1);
            }
            
            refresh_gui();
            break;
        }
    }
}

// Bind display preset to macro bank
void bind_preset_to_bank(int bank, const char *preset_name) {
    if (bank < 1 || bank > 4) return;
    char home_path[255];
    char bind_path[512];
    
    strncpy(home_path, getenv("HOME"), sizeof(home_path));
    if (home_path[0] == '\0') return;
    
    snprintf(bind_path, sizeof(bind_path), "%s/.config/g510s/bank%d_preset.txt", home_path, bank);
    FILE *f = fopen(bind_path, "w");
    if (f) {
        fprintf(f, "%s\n", preset_name);
        fclose(f);
        printf("G510s: Bound preset '%s' to bank M%d\n", preset_name, bank);
    }
}

// Read preset binding for a bank
const char* get_bank_preset(int bank) {
    static char preset_name[64];
    char home_path[255];
    char bind_path[512];
    
    strncpy(home_path, getenv("HOME"), sizeof(home_path));
    if (home_path[0] == '\0') return "";
    
    snprintf(bind_path, sizeof(bind_path), "%s/.config/g510s/bank%d_preset.txt", home_path, bank);
    FILE *f = fopen(bind_path, "r");
    if (f) {
        if (fgets(preset_name, sizeof(preset_name), f)) {
            size_t len = strlen(preset_name);
            if (len > 0 && preset_name[len-1] == '\n') preset_name[len-1] = '\0';
        } else {
            preset_name[0] = '\0';
        }
        fclose(f);
        return preset_name;
    }
    return "";
}


// DBus property getter
static GVariant* on_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                 const gchar *interface_name, const gchar *property_name, GError **error, gpointer user_data) {
    if (g_strcmp0(property_name, G510S_DBUS_PROP_MODE) == 0) {
        return g_variant_new_int32(g510s_data.mkey_state);
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_COLOR) == 0) {
        int color[3] = {g510s_data.led_red, g510s_data.led_green, g510s_data.led_blue};
        return g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, color, 3, sizeof(int));
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_COLORFADE) == 0) {
        return g_variant_new_boolean(g510s_data.color_fade);
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_AUTOSAVE) == 0) {
        return g_variant_new_boolean(g510s_data.auto_save_on_quit);
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_GUIHIDDEN) == 0) {
        return g_variant_new_boolean(g510s_data.gui_hidden);
    }
    return NULL;
}

// DBus property setter
static gboolean on_set_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                const gchar *interface_name, const gchar *property_name, GVariant *value, GError **error, gpointer user_data) {
    if (g_strcmp0(property_name, G510S_DBUS_PROP_COLORFADE) == 0) {
        g510s_data.color_fade = g_variant_get_boolean(value);
        save_config();
        emit_properties_changed(connection);
        refresh_gui();
        return TRUE;
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_AUTOSAVE) == 0) {
        g510s_data.auto_save_on_quit = g_variant_get_boolean(value);
        save_config();
        emit_properties_changed(connection);
        refresh_gui();
        return TRUE;
    } else if (g_strcmp0(property_name, G510S_DBUS_PROP_GUIHIDDEN) == 0) {
        g510s_data.gui_hidden = g_variant_get_boolean(value);
        save_config();
        emit_properties_changed(connection);
        refresh_gui();
        return TRUE;
    }
    return FALSE;
}

// Helper to emit PropertiesChanged
static void emit_properties_changed(GDBusConnection *connection) {
    GVariantBuilder builder;
    GVariantBuilder invalidated;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE_STRING_ARRAY);
    g_variant_builder_add(&builder, "{sv}", G510S_DBUS_PROP_MODE, g_variant_new_int32(g510s_data.mkey_state));
    int color[3] = {g510s_data.led_red, g510s_data.led_green, g510s_data.led_blue};
    g_variant_builder_add(&builder, "{sv}", G510S_DBUS_PROP_COLOR, g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, color, 3, sizeof(int)));
    g_variant_builder_add(&builder, "{sv}", G510S_DBUS_PROP_COLORFADE, g_variant_new_boolean(g510s_data.color_fade));
    g_variant_builder_add(&builder, "{sv}", G510S_DBUS_PROP_AUTOSAVE, g_variant_new_boolean(g510s_data.auto_save_on_quit));
    g_variant_builder_add(&builder, "{sv}", G510S_DBUS_PROP_GUIHIDDEN, g_variant_new_boolean(g510s_data.gui_hidden));
    g_dbus_connection_emit_signal(
        connection,
        NULL,
        G510S_DBUS_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)", G510S_DBUS_INTERFACE, &builder, &invalidated),
        NULL
    );
}

// DBus method handlers for preset operations
static gboolean on_handle_save_preset(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data) {
    const char *name;
    g_variant_get(parameters, "(s)", &name);
    save_preset(name);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

static gboolean on_handle_load_preset(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data) {
    const char *name;
    g_variant_get(parameters, "(s)", &name);
    load_preset(name);
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

static gboolean on_handle_list_presets(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                      const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                      GDBusMethodInvocation *invocation, gpointer user_data) {
    load_presets();
    const char **names = g_new(const char *, bank_config_count + 1);
    for (int i = 0; i < bank_config_count; i++) {
        names[i] = bank_configs[i].name;
    }
    names[bank_config_count] = NULL;
    g_dbus_method_invocation_return_value(invocation, g_variant_new_strv(names, bank_config_count));
    g_free(names);
    return TRUE;
}

// DBus method handler
static void on_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                           const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                           GDBusMethodInvocation *invocation, gpointer user_data) {
    if (g_strcmp0(method_name, "SetMode") == 0) {
        on_handle_set_mode(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SetColor") == 0) {
        on_handle_set_color(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SetColorFade") == 0) {
        on_handle_set_colorfade(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SetAutoSaveOnQuit") == 0) {
        on_handle_set_autosave(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SetGuiHidden") == 0) {
        on_handle_set_guihidden(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "GetMacro") == 0) {
        on_handle_get_macro(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SetMacro") == 0) {
        on_handle_set_macro(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "RunMacro") == 0) {
        on_handle_run_macro(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SaveConfig") == 0) {
        on_handle_save_config(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "SavePreset") == 0) {
        on_handle_save_preset(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "LoadPreset") == 0) {
        on_handle_load_preset(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else if (g_strcmp0(method_name, "ListPresets") == 0) {
        on_handle_list_presets(connection, sender, object_path, interface_name, method_name, parameters, invocation, user_data);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unknown method: %s", method_name);
    }
}

// SetMode handler: expects (i) for mode (1,2,3,4)
static gboolean on_handle_set_mode(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data) {
    int mode;
    g_variant_get(parameters, "(i)", &mode);
    if (mode < 1 || mode > 4) {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid mode: %d", mode);
        return TRUE;
    }
    g510s_data.mkey_state = mode;
    set_mkey_state(mode);
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// SetColor handler: expects (iii b) for red, green, blue, save (bool)
static gboolean on_handle_set_color(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data) {
    int r, g, b;
    gboolean save;
    g_variant_get(parameters, "(iiib)", &r, &g, &b, &save);
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid color values");
        return TRUE;
    }
    setG510LEDColor(r, g, b);
    g510s_data.led_red = r;
    g510s_data.led_green = g;
    g510s_data.led_blue = b;
    if (save) {
        if (g510s_data.mkey_state == 1) {
            g510s_data.m1.red = r;
            g510s_data.m1.green = g;
            g510s_data.m1.blue = b;
        } else if (g510s_data.mkey_state == 2) {
            g510s_data.m2.red = r;
            g510s_data.m2.green = g;
            g510s_data.m2.blue = b;
        } else if (g510s_data.mkey_state == 3) {
            g510s_data.m3.red = r;
            g510s_data.m3.green = g;
            g510s_data.m3.blue = b;
        } else if (g510s_data.mkey_state == 4) {
            g510s_data.mr.red = r;
            g510s_data.mr.green = g;
            g510s_data.mr.blue = b;
        }
        save_config();
    }
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// SetColorFade handler: expects (b)
static gboolean on_handle_set_colorfade(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data) {
    gboolean fade;
    g_variant_get(parameters, "(b)", &fade);
    g510s_data.color_fade = fade;
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// SetAutoSaveOnQuit handler: expects (b)
static gboolean on_handle_set_autosave(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data) {
    gboolean autosave;
    g_variant_get(parameters, "(b)", &autosave);
    g510s_data.auto_save_on_quit = autosave;
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// SetGuiHidden handler: expects (b)
static gboolean on_handle_set_guihidden(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                       const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                       GDBusMethodInvocation *invocation, gpointer user_data) {
    gboolean hidden;
    g_variant_get(parameters, "(b)", &hidden);
    g510s_data.gui_hidden = hidden;
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// Implementation for on_handle_save_config
static gboolean on_handle_save_config(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                     GDBusMethodInvocation *invocation, gpointer user_data) {
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// Macro helpers
static char* get_macro_by_index(int mode, int idx) {
    switch (mode) {
        case 1:
            switch (idx) {
                case 1: return g510s_data.m1.g1;
                case 2: return g510s_data.m1.g2;
                case 3: return g510s_data.m1.g3;
                case 4: return g510s_data.m1.g4;
                case 5: return g510s_data.m1.g5;
                case 6: return g510s_data.m1.g6;
                case 7: return g510s_data.m1.g7;
                case 8: return g510s_data.m1.g8;
                case 9: return g510s_data.m1.g9;
                case 10: return g510s_data.m1.g10;
                case 11: return g510s_data.m1.g11;
                case 12: return g510s_data.m1.g12;
                case 13: return g510s_data.m1.g13;
                case 14: return g510s_data.m1.g14;
                case 15: return g510s_data.m1.g15;
                case 16: return g510s_data.m1.g16;
                case 17: return g510s_data.m1.g17;
                case 18: return g510s_data.m1.g18;
                default: return NULL;
            }
        case 2:
            switch (idx) {
                case 1: return g510s_data.m2.g1;
                case 2: return g510s_data.m2.g2;
                case 3: return g510s_data.m2.g3;
                case 4: return g510s_data.m2.g4;
                case 5: return g510s_data.m2.g5;
                case 6: return g510s_data.m2.g6;
                case 7: return g510s_data.m2.g7;
                case 8: return g510s_data.m2.g8;
                case 9: return g510s_data.m2.g9;
                case 10: return g510s_data.m2.g10;
                case 11: return g510s_data.m2.g11;
                case 12: return g510s_data.m2.g12;
                case 13: return g510s_data.m2.g13;
                case 14: return g510s_data.m2.g14;
                case 15: return g510s_data.m2.g15;
                case 16: return g510s_data.m2.g16;
                case 17: return g510s_data.m2.g17;
                case 18: return g510s_data.m2.g18;
                default: return NULL;
            }
        case 3:
            switch (idx) {
                case 1: return g510s_data.m3.g1;
                case 2: return g510s_data.m3.g2;
                case 3: return g510s_data.m3.g3;
                case 4: return g510s_data.m3.g4;
                case 5: return g510s_data.m3.g5;
                case 6: return g510s_data.m3.g6;
                case 7: return g510s_data.m3.g7;
                case 8: return g510s_data.m3.g8;
                case 9: return g510s_data.m3.g9;
                case 10: return g510s_data.m3.g10;
                case 11: return g510s_data.m3.g11;
                case 12: return g510s_data.m3.g12;
                case 13: return g510s_data.m3.g13;
                case 14: return g510s_data.m3.g14;
                case 15: return g510s_data.m3.g15;
                case 16: return g510s_data.m3.g16;
                case 17: return g510s_data.m3.g17;
                case 18: return g510s_data.m3.g18;
                default: return NULL;
            }
        case 4:
            switch (idx) {
                case 1: return g510s_data.mr.g1;
                case 2: return g510s_data.mr.g2;
                case 3: return g510s_data.mr.g3;
                case 4: return g510s_data.mr.g4;
                case 5: return g510s_data.mr.g5;
                case 6: return g510s_data.mr.g6;
                case 7: return g510s_data.mr.g7;
                case 8: return g510s_data.mr.g8;
                case 9: return g510s_data.mr.g9;
                case 10: return g510s_data.mr.g10;
                case 11: return g510s_data.mr.g11;
                case 12: return g510s_data.mr.g12;
                case 13: return g510s_data.mr.g13;
                case 14: return g510s_data.mr.g14;
                case 15: return g510s_data.mr.g15;
                case 16: return g510s_data.mr.g16;
                case 17: return g510s_data.mr.g17;
                case 18: return g510s_data.mr.g18;
                default: return NULL;
            }
        default: return NULL;
    }
}

// GetMacro handler: expects (ii) for mode, index
static gboolean on_handle_get_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data) {
    int mode, idx;
    g_variant_get(parameters, "(ii)", &mode, &idx);
    char *macro = get_macro_by_index(mode, idx);
    if (!macro) {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid mode or index");
        return TRUE;
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", macro));
    return TRUE;
}
// SetMacro handler: expects (ii s) for mode, index, value
static gboolean on_handle_set_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data) {
    int mode, idx;
    const char *value;
    g_variant_get(parameters, "(iis)", &mode, &idx, &value);
    char *macro = get_macro_by_index(mode, idx);
    if (!macro) {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid mode or index");
        return TRUE;
    }
    strncpy(macro, value, 63);
    macro[63] = '\0';
    save_config();
    emit_properties_changed(connection);
    refresh_gui();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}
// RunMacro handler: expects (ii) for mode, index
static gboolean on_handle_run_macro(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                   const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                                   GDBusMethodInvocation *invocation, gpointer user_data) {
    int mode, idx;
    g_variant_get(parameters, "(ii)", &mode, &idx);
    char *macro = get_macro_by_index(mode, idx);
    if (!macro) {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid mode or index");
        return TRUE;
    }
    system(macro);
    printf("RunMacro: mode=%d idx=%d macro=%s\n", mode, idx, macro);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return TRUE;
}

// DBus registration callback
static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    static GDBusNodeInfo *introspection_data = NULL;
    static const gchar introspection_xml[] =
        "<node>"
        "  <interface name='org.g510s.control'>"
        "    <method name='SetMode'>"
        "      <arg type='i' name='mode' direction='in'/>"
        "    </method>"
        "    <method name='SetColor'>"
        "      <arg type='i' name='red' direction='in'/>"
        "      <arg type='i' name='green' direction='in'/>"
        "      <arg type='i' name='blue' direction='in'/>"
        "      <arg type='b' name='save' direction='in'/>"
        "    </method>"
        "    <method name='SetColorFade'>"
        "      <arg type='b' name='fade' direction='in'/>"
        "    </method>"
        "    <method name='SetAutoSaveOnQuit'>"
        "      <arg type='b' name='autosave' direction='in'/>"
        "    </method>"
        "    <method name='SetGuiHidden'>"
        "      <arg type='b' name='hidden' direction='in'/>"
        "    </method>"
        "    <method name='GetMacro'>"
        "      <arg type='i' name='mode' direction='in'/>"
        "      <arg type='i' name='index' direction='in'/>"
        "      <arg type='s' name='macro' direction='out'/>"
        "    </method>"
        "    <method name='SetMacro'>"
        "      <arg type='i' name='mode' direction='in'/>"
        "      <arg type='i' name='index' direction='in'/>"
        "      <arg type='s' name='macro' direction='in'/>"
        "    </method>"
        "    <method name='RunMacro'>"
        "      <arg type='i' name='mode' direction='in'/>"
        "      <arg type='i' name='index' direction='in'/>"
        "    </method>"
        "    <method name='SaveConfig'/>"
        "    <method name='SavePreset'>"
        "      <arg type='s' name='name' direction='in'/>"
        "    </method>"
        "    <method name='LoadPreset'>"
        "      <arg type='s' name='name' direction='in'/>"
        "    </method>"
        "    <method name='ListPresets'>"
        "      <arg type='as' name='presets' direction='out'/>"
        "    </method>"
        "    <property name='Mode' type='i' access='readwrite'/>"
        "    <property name='Color' type='ai' access='readwrite'/>"
        "    <property name='ColorFade' type='b' access='readwrite'/>"
        "    <property name='AutoSaveOnQuit' type='b' access='readwrite'/>"
        "    <property name='GuiHidden' type='b' access='readwrite'/>"
        "  </interface>"
        "</node>";

    if (!introspection_data)
        introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);

    guint registration_id = g_dbus_connection_register_object(
        connection,
        G510S_DBUS_PATH,
        introspection_data->interfaces[0],
        &(GDBusInterfaceVTable){
            .method_call = on_method_call,
            .get_property = on_get_property,
            .set_property = on_set_property
        },
        NULL, NULL, NULL);

    if (registration_id == 0) {
        g_warning("Failed to register DBus object");
    }
}

// Widget pointers for GUI access
GtkRange *redscale_m1;
GtkRange *greenscale_m1;
GtkRange *bluescale_m1;
GtkRange *redscale_m2;
GtkRange *greenscale_m2;
GtkRange *bluescale_m2;
GtkRange *redscale_m3;
GtkRange *greenscale_m3;
GtkRange *bluescale_m3;
GtkRange *redscale_mr;
GtkRange *greenscale_mr;
GtkRange *bluescale_mr;
// Macro entries live in per-profile arrays; index 1..18 maps to G1..G18.
#define GKEY_COUNT 18
GtkEntry *entry_m1[GKEY_COUNT + 1];
GtkEntry *entry_m2[GKEY_COUNT + 1];
GtkEntry *entry_m3[GKEY_COUNT + 1];
GtkEntry *entry_mr[GKEY_COUNT + 1];
GtkCheckMenuItem *menuhidden;
GtkCheckMenuItem *menuautosave;
GtkCheckMenuItem *menucolorfade;
GtkTextView *display_editor;       // In-GUI display editor
GtkWidget *display_preview;        // Display preview drawing area
GtkComboBoxText *preset_combo;     // Preset selector
GtkComboBoxText *bank1_preset_combo; // Preset binding for bank 1
GtkComboBoxText *bank2_preset_combo; // Preset binding for bank 2
GtkComboBoxText *bank3_preset_combo; // Preset binding for bank 3
GtkComboBoxText *bank4_preset_combo; // Preset binding for bank 4
AppIndicator *indicator;

// --- Colour picker ----------------------------------------------------------
// A single reusable picker (R/G/B spin buttons + hex entry + graphical
// GtkColorButton chooser + live swatch) is built once per profile in code, so
// the UI needs no repeated slider widgets or per-channel signal handlers.
typedef struct {
    int profile;          // 1..4  (M1, M2, M3, MR)
    GtkWidget *red;       // GtkSpinButton 0..255
    GtkWidget *green;
    GtkWidget *blue;
    GtkWidget *hex;       // GtkEntry, "#RRGGBB"
    GtkWidget *chooser;   // GtkColorButton (graphical picker)
    GtkWidget *swatch;    // GtkDrawingArea preview
    int updating;         // Re-entrancy guard
} color_picker_t;

static color_picker_t color_pickers[4];

static struct m_data_s *profile_data(int profile) {
    switch (profile) {
        case 1: return &g510s_data.m1;
        case 2: return &g510s_data.m2;
        case 3: return &g510s_data.m3;
        case 4: return &g510s_data.mr;
    }
    return &g510s_data.m1;
}

// Return the storage for G-key `idx` (1..18) of a profile.
static const char *gkey_field(struct m_data_s *m, int idx) {
    switch (idx) {
        case 1:  return m->g1;   case 2:  return m->g2;
        case 3:  return m->g3;   case 4:  return m->g4;
        case 5:  return m->g5;   case 6:  return m->g6;
        case 7:  return m->g7;   case 8:  return m->g8;
        case 9:  return m->g9;   case 10: return m->g10;
        case 11: return m->g11;  case 12: return m->g12;
        case 13: return m->g13;  case 14: return m->g14;
        case 15: return m->g15;  case 16: return m->g16;
        case 17: return m->g17;  case 18: return m->g18;
    }
    return "";
}

static gboolean on_swatch_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    color_picker_t *cp = data;
    int r = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->red));
    int g = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->green));
    int b = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->blue));
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
    cairo_rectangle(cr, 0.5, 0.5, w - 1, h - 1);
    cairo_stroke(cr);
    return FALSE;
}

// Push the picker's colour into the profile data and request an LED update.
static void picker_apply(color_picker_t *cp) {
    if (cp->updating) return;
    int r = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->red));
    int g = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->green));
    int b = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cp->blue));
    struct m_data_s *m = profile_data(cp->profile);
    m->red = r; m->green = g; m->blue = b;
    update = cp->profile;   // ask the update thread to push the new colour

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
    GdkRGBA rgba = { r / 255.0, g / 255.0, b / 255.0, 1.0 };

    cp->updating = 1;
    gtk_entry_set_text(GTK_ENTRY(cp->hex), hex);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cp->chooser), &rgba);
    cp->updating = 0;

    gtk_widget_queue_draw(cp->swatch);
}

// Refresh the picker widgets from the profile data (no LED update).
static void picker_sync(color_picker_t *cp) {
    struct m_data_s *m = profile_data(cp->profile);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", m->red, m->green, m->blue);
    GdkRGBA rgba = { m->red / 255.0, m->green / 255.0, m->blue / 255.0, 1.0 };

    cp->updating = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->red), m->red);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->green), m->green);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->blue), m->blue);
    gtk_entry_set_text(GTK_ENTRY(cp->hex), hex);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cp->chooser), &rgba);
    cp->updating = 0;

    gtk_widget_queue_draw(cp->swatch);
}

static void on_picker_spin_changed(GtkSpinButton *spin, gpointer data) {
    picker_apply((color_picker_t *)data);
}

static void on_picker_hex_activate(GtkEntry *entry, gpointer data) {
    color_picker_t *cp = data;
    const char *text = gtk_entry_get_text(entry);
    while (*text == ' ' || *text == '#') text++;
    unsigned int r, g, b;
    if (sscanf(text, "%2x%2x%2x", &r, &g, &b) == 3) {
        cp->updating = 1;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->red), r);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->green), g);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->blue), b);
        cp->updating = 0;
        picker_apply(cp);
    }
}

static void on_picker_color_set(GtkColorButton *button, gpointer data) {
    color_picker_t *cp = data;
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    cp->updating = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->red), (int)(rgba.red * 255 + 0.5));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->green), (int)(rgba.green * 255 + 0.5));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cp->blue), (int)(rgba.blue * 255 + 0.5));
    cp->updating = 0;
    picker_apply(cp);
}

static void build_color_picker(GtkBuilder *builder, const char *box_id,
                               const char *f1, const char *f2, const char *f3,
                               int profile) {
    GtkWidget *box = GTK_WIDGET(gtk_builder_get_object(builder, box_id));
    if (!box) return;

    // Hide the legacy slider frames this picker replaces.
    const char *frames[3] = { f1, f2, f3 };
    for (int i = 0; i < 3; i++) {
        GtkWidget *fr = GTK_WIDGET(gtk_builder_get_object(builder, frames[i]));
        if (fr) {
            gtk_widget_set_no_show_all(fr, TRUE);
            gtk_widget_hide(fr);
        }
    }

    color_picker_t *cp = &color_pickers[profile - 1];
    cp->profile = profile;
    cp->updating = 0;

    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 6);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    cp->red = gtk_spin_button_new_with_range(0, 255, 1);
    cp->green = gtk_spin_button_new_with_range(0, 255, 1);
    cp->blue = gtk_spin_button_new_with_range(0, 255, 1);
    cp->hex = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(cp->hex), 8);
    gtk_entry_set_max_length(GTK_ENTRY(cp->hex), 7);
    gtk_entry_set_placeholder_text(GTK_ENTRY(cp->hex), "#RRGGBB");
    cp->chooser = gtk_color_button_new();
    gtk_color_button_set_title(GTK_COLOR_BUTTON(cp->chooser), "LED colour");
    cp->swatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(cp->swatch, 40, 24);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("R"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->red,              1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("G"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->green,            3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("B"), 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->blue,             5, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Hex"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->hex,              1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->chooser,          3, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cp->swatch,           4, 1, 2, 1);

    g_signal_connect(cp->red,     "value-changed", G_CALLBACK(on_picker_spin_changed), cp);
    g_signal_connect(cp->green,   "value-changed", G_CALLBACK(on_picker_spin_changed), cp);
    g_signal_connect(cp->blue,    "value-changed", G_CALLBACK(on_picker_spin_changed), cp);
    g_signal_connect(cp->hex,     "activate",      G_CALLBACK(on_picker_hex_activate), cp);
    g_signal_connect(cp->chooser, "color-set",     G_CALLBACK(on_picker_color_set), cp);
    g_signal_connect(cp->swatch,  "draw",          G_CALLBACK(on_swatch_draw), cp);

    gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 4);
    gtk_box_reorder_child(GTK_BOX(box), frame, 0);
    gtk_widget_show_all(frame);

    picker_sync(cp);
}

// Replace the slider frames on each profile page with a colour picker.
static void setup_color_pickers(GtkBuilder *builder) {
    build_color_picker(builder, "box2", "frame1",  "frame2",  "frame3",  1);
    build_color_picker(builder, "box3", "frame4",  "frame5",  "frame6",  2);
    build_color_picker(builder, "box4", "frame7",  "frame8",  "frame9",  3);
    build_color_picker(builder, "box5", "frame10", "frame11", "frame12", 4);
}

static void sync_color_pickers(void) {
    for (int i = 0; i < 4; i++) {
        if (color_pickers[i].profile) picker_sync(&color_pickers[i]);
    }
}

// Display preview rendering function  
static gboolean on_preview_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    
    // The LCD uses 32-byte offset before pixel data, and pixels are 160*43/8 = 860 bytes
    #define PREVIEW_LCD_OFFSET 0
    int preview_pitch = DISPLAY_WIDTH / 8; // 20 bytes per row
    
    // Calculate scaling to fit the 160x43 display into the widget
    double scale_x = (double)width / DISPLAY_WIDTH;
    double scale_y = (double)height / DISPLAY_HEIGHT;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;
    
    int offset_x = (width - (int)(DISPLAY_WIDTH * scale)) / 2;
    int offset_y = (height - (int)(DISPLAY_HEIGHT * scale)) / 2;
    
    // Clear background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    
    // Draw border
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_rectangle(cr, offset_x - 1, offset_y - 1, 
                    (int)(DISPLAY_WIDTH * scale) + 2, (int)(DISPLAY_HEIGHT * scale) + 2);
    cairo_stroke(cr);
    
    // Draw pixels - use correct byte index with LCD offset
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            int byte_idx = PREVIEW_LCD_OFFSET + y * preview_pitch + x / 8;
            int bit_idx = x % 8;
            int pixel = preview_buffer[byte_idx] & (0x80 >> bit_idx);            
            if (pixel) {
                cairo_set_source_rgb(cr, 0.168, 0.878, 0.298);
            } else {
                cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
            }
            
            cairo_rectangle(cr, offset_x + (int)(x * scale), offset_y + (int)(y * scale),
                          ceil(scale), ceil(scale));
            cairo_fill(cr);
        }
    }
    
    return FALSE;
}

// Update preview from LCD buffer
void update_preview() {
    if (display_preview) {
        gtk_widget_queue_draw(display_preview);
    }
}

void on_menucolorfade_toggled(GtkCheckMenuItem *menuitem, gpointer user_data) {
    g510s_data.color_fade = gtk_check_menu_item_get_active(menuitem) ? 1 : 0;
    save_config();
}

// Save current preset from UI
void on_save_preset_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *entry;
    
    dialog = gtk_dialog_new_with_buttons("Save Preset",
        GTK_WINDOW(user_data),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Save", GTK_RESPONSE_ACCEPT,
        "_Cancel", GTK_RESPONSE_REJECT,
        NULL);
    
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter preset name (e.g. gaming_stats)...");
    gtk_entry_set_text(GTK_ENTRY(entry), "my_preset");
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && name[0]) {
            save_preset(name);
            
            // Update presets combo
            load_presets();
            gtk_combo_box_text_remove_all(preset_combo);
            gtk_combo_box_text_append_text(preset_combo, "None");
            for (int i = 0; i < bank_config_count; i++) {
                gtk_combo_box_text_append_text(preset_combo, bank_configs[i].name);
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(preset_combo), 0);
            
            // Update bank combos
            for (int b = 1; b <= 4; b++) {
                GtkComboBoxText *bank_combo = NULL;
                switch (b) {
                    case 1: bank_combo = bank1_preset_combo; break;
                    case 2: bank_combo = bank2_preset_combo; break;
                    case 3: bank_combo = bank3_preset_combo; break;
                    case 4: bank_combo = bank4_preset_combo; break;
                }
                if (bank_combo) {
                    const char *current = get_bank_preset(b);
                    gtk_combo_box_text_remove_all(bank_combo);
                    gtk_combo_box_text_append_text(bank_combo, "None");
                    int active_idx = 0;
                    for (int i = 0; i < bank_config_count; i++) {
                        gtk_combo_box_text_append_text(bank_combo, bank_configs[i].name);
                        if (strcmp(bank_configs[i].name, current) == 0) {
                            active_idx = i + 1;
                        }
                    }
                    gtk_combo_box_set_active(GTK_COMBO_BOX(bank_combo), active_idx);
                }
            }
            
            display_notification("Preset saved!", 2000, 0);
        }
    }
    gtk_widget_destroy(dialog);
}

// Load preset from UI
void on_load_preset_clicked(GtkButton *button, gpointer user_data) {
    const char *name = gtk_combo_box_text_get_active_text(preset_combo);
    if (name && strcmp(name, "None") != 0) {
        load_preset(name);
        save_config();
        display_notification("Preset loaded!", 2000, 0);
    }
}

// Bind preset to bank from UI
void on_bank_preset_changed(GtkComboBox *combo, gpointer user_data) {
    int bank = GPOINTER_TO_INT(user_data);
    const char *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (name) {
        if (strcmp(name, "None") == 0) {
            bind_preset_to_bank(bank, "");
        } else {
            bind_preset_to_bank(bank, name);
        }
    }
}

// Display editor text changed - saves to display.txt
void on_display_editor_changed(GtkTextBuffer *buffer, gpointer user_data) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    // Save to display.txt
    char home_path[255];
    strncpy(home_path, getenv("HOME"), sizeof(home_path));
    if (home_path[0]) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/.config/g510s/display.txt", home_path);
        FILE *f = fopen(filepath, "w");
        if (f) {
            fprintf(f, "%s", text);
            fclose(f);
        }
    }
    g_free(text);
}

// Refresh preview from display buffer
gboolean on_preview_refresh(gpointer data) {
    update_preview();
    return G_SOURCE_CONTINUE;
}

// --- GUI refresh implementation ---
static void refresh_gui_internal() {
    // Update colour pickers (the visible controls; the legacy sliders are hidden)
    sync_color_pickers();
    // Update macro entries
    struct m_data_s *profiles[4] = { &g510s_data.m1, &g510s_data.m2, &g510s_data.m3, &g510s_data.mr };
    GtkEntry **entries[4] = { entry_m1, entry_m2, entry_m3, entry_mr };
    for (int b = 0; b < 4; b++) {
        for (int g = 1; g <= GKEY_COUNT; g++) {
            if (entries[b][g]) {
                gtk_entry_set_text(entries[b][g], gkey_field(profiles[b], g));
            }
        }
    }
    // Update menu toggles
    gtk_check_menu_item_set_active(menuautosave, g510s_data.auto_save_on_quit ? TRUE : FALSE);
    gtk_check_menu_item_set_active(menucolorfade, g510s_data.color_fade ? TRUE : FALSE);
    gtk_check_menu_item_set_active(menuhidden, g510s_data.gui_hidden ? TRUE : FALSE);
}

int main(int argc, char *argv[]) {
  int i = 1;
  int help = 0;
  int debug = 0;
  int opt_invalid = 0;
  int dflag = 0;
  
  // Initialize terminal mode
  terminal_mode = 0;
  terminal_cmd[0] = '\0';
  
  pthread_t key_thread;
  pthread_t update_thread;
  pthread_t server_thread;
  
  GtkBuilder *builder;
  GtkWidget *window;
  GtkAboutDialog *aboutdialog;
  GtkWidget *indicator_menu;
  
  leaving = 0;
  update = 0;
  device_found = 0;
  connected_clients = 0;
  current_key_state = 0;
  
  // parse command line options
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) {
      help = 1;
      break;
    } else if (!strcmp(argv[i],"--debug") || !strcmp(argv[i],"-d")) {
      if (argv[i+1] && !is_number(argv[i+1])) {
        i++;
        debug = atoi(argv[i]);
        dflag++;
      } else {
        opt_invalid = 1;
        break;
      }
    } else if (!strcmp(argv[i],"--dump-display-buffer")) {
      dump_display_buffer = 1;
    } else if (!strcmp(argv[i],"--terminal")) {
      terminal_mode = 1;
      if (argv[i+1]) {
        i++;
        strncpy(terminal_cmd, argv[i], sizeof(terminal_cmd)-1);
        terminal_cmd[sizeof(terminal_cmd)-1] = '\0';
        i++;
        while (i < argc) {
          strncat(terminal_cmd, " ", sizeof(terminal_cmd)-strlen(terminal_cmd)-1);
          strncat(terminal_cmd, argv[i], sizeof(terminal_cmd)-strlen(terminal_cmd)-1);
          i++;
        }
      } else {
        opt_invalid = 1;
        break;
      }
    } else {
      opt_invalid = 1;
      break;
    }
    if (dflag > 1) {
      opt_invalid = 1;
      break;
    }
  }
  
  if (opt_invalid) {
    printf("G510s: invalid option specified!\n\n");
    help = 1;
  }

  if (help) {
    printf("Usage: g510s <option> <value>\n\n");
    printf("Options:\n");
    printf("  --help|-h\tShow this help\n");
    printf("  --debug|-d\tSet debug level (default 0)\n");
    printf("  --terminal <cmd>\tRun command and display output on LCD using small font\n");
    return 0;
  }

  if (debug != 0) {
    libg15Debug(debug);
    printf("G510s: debugging enabled, level %i\n", debug);
  }
  
  // init libg15
  if (setupLibG15(0x46d, 0xc22d, 0) == G15_NO_ERROR) {
    printf("G510s: found device 046d:c22d\n");
    device_found = 1;
    usb_id="046d:c22d";
  } else if (setupLibG15(0x46d, 0xc22e, 0) == G15_NO_ERROR) {
    printf("G510s: found device 046d:c22e\n");
    device_found = 1;
    usb_id="046d:c22e";
  } else {
    printf("G510s: failed to initialize libg15\n");
    device_found = 0;
  }
  
  if (device_found) {
    if (init_uinput() != 0) {
      printf("G510s: failed to initialize uinput, media keys not available\n");
    }
  }
  
  // init data structure
  init_data();
  
  // init lcd list
  lcdlist_t *lcdlist = lcdlist_init();
  
  // try to create user save directory
  check_dir();
  
  // try to load previously saved data
  load_config();
  
  // Load presets
  load_presets();
  
  // start threads
  pthread_create(&server_thread, NULL, server_function, lcdlist);
  pthread_create(&update_thread, NULL, update_function, lcdlist);
  pthread_create(&key_thread, NULL, key_function, lcdlist);
  
  // init gtk
  gtk_init(&argc, &argv);
  
  builder = gtk_builder_new();
  gtk_builder_add_from_file(builder, "/usr/local/share/g510s/g510s.glade", NULL);
  
  window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
  aboutdialog = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "aboutdialog"));
  
  // scales
  redscale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m1"));
  greenscale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m1"));
  bluescale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m1"));
  
  redscale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m2"));
  greenscale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m2"));
  bluescale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m2"));
  
  redscale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m3"));
  greenscale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m3"));
  bluescale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m3"));
  
  redscale_mr = GTK_RANGE(gtk_builder_get_object(builder, "redscale_mr"));
  greenscale_mr = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_mr"));
  bluescale_mr = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_mr"));
  
  // text entries (macro G-keys), stored per profile in arrays
  {
    GtkEntry **banks[4] = { entry_m1, entry_m2, entry_m3, entry_mr };
    const char *bank_names[4] = { "m1", "m2", "m3", "mr" };
    for (int b = 0; b < 4; b++) {
      for (int g = 1; g <= GKEY_COUNT; g++) {
        char id[32];
        snprintf(id, sizeof(id), "entry_%sg%d", bank_names[b], g);
        banks[b][g] = GTK_ENTRY(gtk_builder_get_object(builder, id));
      }
    }
  }
  
  // New UI elements for editor, presets, preview
  display_editor = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "display_editor"));
  display_preview = GTK_WIDGET(gtk_builder_get_object(builder, "display_preview"));
  preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "preset_combo"));
  bank1_preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "bank1_preset_combo"));
  bank2_preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "bank2_preset_combo"));
  bank3_preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "bank3_preset_combo"));
  bank4_preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "bank4_preset_combo"));
  
  // Setup preview drawing area
  if (display_preview) {
      g_signal_connect(display_preview, "draw", G_CALLBACK(on_preview_draw), NULL);
      g_timeout_add(200, on_preview_refresh, NULL);
      gtk_widget_set_size_request(display_preview, 320, 86);
  }
  
  // Setup display editor
  if (display_editor) {
      GtkTextBuffer *buffer = gtk_text_view_get_buffer(display_editor);
      
      // Load current display.txt content
      char home_path[255];
      strncpy(home_path, getenv("HOME"), sizeof(home_path));
      if (home_path[0]) {
          char filepath[512];
          snprintf(filepath, sizeof(filepath), "%s/.config/g510s/display.txt", home_path);
          FILE *f = fopen(filepath, "r");
          if (f) {
              fseek(f, 0, SEEK_END);
              long fsize = ftell(f);
              fseek(f, 0, SEEK_SET);
              char *content = malloc(fsize + 1);
              fread(content, 1, fsize, f);
              content[fsize] = '\0';
              gtk_text_buffer_set_text(buffer, content, -1);
              free(content);
              fclose(f);
          }
      }
      
      g_signal_connect(buffer, "changed", G_CALLBACK(on_display_editor_changed), NULL);
  }
  
  // Setup preset combos
  if (preset_combo) {
      gtk_combo_box_text_append_text(preset_combo, "None");
      for (int i = 0; i < bank_config_count; i++) {
          gtk_combo_box_text_append_text(preset_combo, bank_configs[i].name);
      }
      gtk_combo_box_set_active(GTK_COMBO_BOX(preset_combo), 0);
  }
  
  // Setup bank preset binding combos
  GtkComboBoxText *bank_combos[] = {bank1_preset_combo, bank2_preset_combo, bank3_preset_combo, bank4_preset_combo};
  for (int b = 0; b < 4; b++) {
      if (bank_combos[b]) {
          int bank = b + 1;
          const char *current = get_bank_preset(bank);
          gtk_combo_box_text_append_text(bank_combos[b], "None");
          int active_idx = 0;
          for (int i = 0; i < bank_config_count; i++) {
              gtk_combo_box_text_append_text(bank_combos[b], bank_configs[i].name);
              if (strcmp(bank_configs[i].name, current) == 0) {
                  active_idx = i + 1;
              }
          }
          gtk_combo_box_set_active(GTK_COMBO_BOX(bank_combos[b]), active_idx);
          g_signal_connect(bank_combos[b], "changed", G_CALLBACK(on_bank_preset_changed), GINT_TO_POINTER(bank));
      }
  }
  
  menuhidden = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(builder, "menuhidden"));
  menuautosave = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(builder, "menuautosaveonquit"));
  menucolorfade = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(builder, "menucolorfade"));
  
  // indicator
  indicator_menu = GTK_WIDGET(gtk_builder_get_object(builder, "indicator_menu"));
  indicator = app_indicator_new("G510s", "/usr/local/share/g510s/g510s.svg", APP_INDICATOR_CATEGORY_HARDWARE);
  app_indicator_set_attention_icon(indicator, "/usr/local/share/g510s/g510s-alert.svg");
  app_indicator_set_menu(indicator, GTK_MENU(indicator_menu));
  
  // Replace the colour sliders on each profile page with the new colour picker
  setup_color_pickers(builder);

  gtk_builder_connect_signals(builder, NULL);
  g_object_unref(G_OBJECT(builder));
  
  // set program version
  gtk_about_dialog_set_version(aboutdialog, G510S_VERSION);
  
  // apply the loaded configuration to the UI (colours, macros and menus)
  refresh_gui_internal();
  
  // set whether we want to hide the window
  if (g510s_data.gui_hidden) {
    gtk_widget_hide(window);
    gtk_check_menu_item_set_active(menuhidden, TRUE);
  } else {
    gtk_widget_show(window);
    gtk_check_menu_item_set_active(menuhidden, FALSE);
  }

  if (g510s_data.auto_save_on_quit == 1) {
      gtk_check_menu_item_set_active(menuautosave, TRUE);
  } else {
      gtk_check_menu_item_set_active(menuautosave, FALSE);
  }
  
  if (g510s_data.color_fade == 1) {
      gtk_check_menu_item_set_active(menucolorfade, TRUE);
  } else {
      gtk_check_menu_item_set_active(menucolorfade, FALSE);
  }
  
  if (device_found) {
    set_mkey_state(g510s_data.mkey_state);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  } else {
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ATTENTION);
  }
  
  // --- DBus setup ---
  guint owner_id = g_bus_own_name(
      G_BUS_TYPE_SESSION,
      G510S_DBUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_NONE,
      on_bus_acquired,
      NULL,
      NULL,
      NULL,
      NULL
  );

  gtk_main();
  
  // notify threads to exit
  leaving = 1;
  
  if (menuautosave) {
      g510s_data.auto_save_on_quit = gtk_check_menu_item_get_active(menuautosave) ? 1 : 0;
  }
  if (g510s_data.auto_save_on_quit) {
      save_config();
  }

  g_bus_unown_name(owner_id);
  
  if (device_found) {
    exit_uinput();
    exitLibG15();
    if (usb_id) {
      char cmd[128];
      snprintf(cmd, sizeof(cmd), "usbreset %s", usb_id);
      system(cmd);
    }
  }
  
  lcdlist_destroy(&lcdlist);
  
  return 0;
}