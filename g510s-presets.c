/*
 *  g510s-presets.c - display preset handling, shared by both UI frontends.
 *
 *  Extracted from g510s.c so that it carries no GTK or Qt dependency and can
 *  be linked into either the GTK3 build or the Qt6/QML build (qt6/). The UI is
 *  notified through the ui_* hooks declared in g510s.h.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025-2026 usr40k
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "g510s.h"

// Bank configs array
static bank_config_t bank_configs[MAX_BANK_CONFIGS];
static int bank_config_count = 0;

// Optional hook: lets a frontend push new display script text into its editor
void (*ui_display_script_changed)(const char *) = NULL;

// Helper: get display.txt path
static void get_display_path(char *buf, size_t bufsize) {
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(buf, bufsize, "%s/.config/g510s/display.txt", home);
  } else {
    snprintf(buf, bufsize, "display.txt");
  }
}

int preset_count(void) {
  return bank_config_count;
}

const char *preset_name_at(int index) {
  if (index < 0 || index >= bank_config_count) return "";
  return bank_configs[index].name;
}

void load_presets() {
  char configs_dir[512];

  bank_config_count = 0;
  memset(bank_configs, 0, sizeof(bank_configs));

  const char *home_path = getenv("HOME");
  if (!home_path || home_path[0] == '\0') return;

  snprintf(configs_dir, sizeof(configs_dir), "%s/.config/g510s/presets/", home_path);

  DIR *dir = opendir(configs_dir);
  if (!dir) return;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && bank_config_count < MAX_BANK_CONFIGS) {
    if (entry->d_type == DT_REG || entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
      char *dot = strrchr(entry->d_name, '.');
      if (dot && strcmp(dot, ".txt") == 0) {
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s%s", configs_dir, entry->d_name);

        FILE *f = fopen(fullpath, "r");
        if (f) {
          size_t len = strlen(entry->d_name);
          if (len < 4) { fclose(f); continue; }
          if (len - 4 >= sizeof(bank_configs[0].name))
            len = 4 + sizeof(bank_configs[0].name) - 1;
          memcpy(bank_configs[bank_config_count].name, entry->d_name, len - 4);
          bank_configs[bank_config_count].name[len - 4] = '\0';

          size_t nread = fread(bank_configs[bank_config_count].display_script, 1,
                               sizeof(bank_configs[bank_config_count].display_script) - 1, f);
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
  if (!name || !name[0]) return;

  char presets_dir[512];
  char fullpath[512];

  const char *home_path = getenv("HOME");
  if (!home_path || home_path[0] == '\0') return;

  snprintf(presets_dir, sizeof(presets_dir), "%s/.config/g510s/presets/", home_path);
  mkdir(presets_dir, 0777);

  snprintf(fullpath, sizeof(fullpath), "%s%s.txt", presets_dir, name);

  char display_path[512];
  get_display_path(display_path, sizeof(display_path));

  FILE *src = fopen(display_path, "r");
  FILE *f = fopen(fullpath, "w");
  if (f) {
    if (src) {
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, f);
      fclose(src);
    }
    fclose(f);

    // Refresh the in-memory bank config list
    int found = 0;
    for (int i = 0; i < bank_config_count; i++) {
      if (strcmp(bank_configs[i].name, name) == 0) { found = 1; break; }
    }
    if (!found && bank_config_count < MAX_BANK_CONFIGS) {
      strncpy(bank_configs[bank_config_count].name, name,
              sizeof(bank_configs[bank_config_count].name) - 1);
      bank_config_count++;
    }
    for (int i = 0; i < bank_config_count; i++) {
      if (strcmp(bank_configs[i].name, name) == 0) {
        FILE *r = fopen(fullpath, "r");
        if (r) {
          size_t n = fread(bank_configs[i].display_script, 1,
                           sizeof(bank_configs[i].display_script) - 1, r);
          bank_configs[i].display_script[n] = '\0';
          fclose(r);
        }
      }
    }
  }
}

void load_preset(const char *name) {
  if (!name) return;
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

      if (ui_display_script_changed)
        ui_display_script_changed(bank_configs[i].display_script);

      ui_request_refresh();
      break;
    }
  }
}

// Bind display preset to macro bank
void bind_preset_to_bank(int bank, const char *preset_name) {
  if (bank < 1 || bank > 4 || !preset_name) return;

  const char *home_path = getenv("HOME");
  if (!home_path || home_path[0] == '\0') return;

  char bind_path[512];
  snprintf(bind_path, sizeof(bind_path), "%s/.config/g510s/bank%d_preset.txt", home_path, bank);
  FILE *f = fopen(bind_path, "w");
  if (f) {
    fprintf(f, "%s\n", preset_name);
    fclose(f);
    printf("G510s: Bound preset '%s' to bank M%d\n", preset_name, bank);
  }
}

// Read preset binding for a bank
const char *get_bank_preset(int bank) {
  static char preset_name[64];
  preset_name[0] = '\0';

  if (bank < 1 || bank > 4) return "";

  const char *home_path = getenv("HOME");
  if (!home_path || home_path[0] == '\0') return "";

  char bind_path[512];
  snprintf(bind_path, sizeof(bind_path), "%s/.config/g510s/bank%d_preset.txt", home_path, bank);
  FILE *f = fopen(bind_path, "r");
  if (f) {
    if (fgets(preset_name, sizeof(preset_name), f)) {
      size_t len = strlen(preset_name);
      if (len > 0 && preset_name[len-1] == '\n') preset_name[len-1] = '\0';
    }
    fclose(f);
  }
  return preset_name;
}

// Macro helpers - returns a pointer into the live g510s_data structure.
// m_data_s stores the 18 command strings contiguously after red/green/blue.
char *macro_by_index(int mode, int idx) {
  struct m_data_s *m = NULL;
  switch (mode) {
    case 1: m = &g510s_data.m1; break;
    case 2: m = &g510s_data.m2; break;
    case 3: m = &g510s_data.m3; break;
    case 4: m = &g510s_data.mr; break;
    default: return NULL;
  }
  if (idx < 1 || idx > 18) return NULL;

  return (char *)m + 3 * sizeof(int) + (size_t)(idx - 1) * GKEY_STRLEN;
}