/*
 *  g510s-vars.h - persistent and predefined script variables.
 *
 *  Two kinds of variable are exposed to display scripts (via the existing
 *  "@name" substitution in g510s-clock.c):
 *
 *    Persistent  - set with `!var <name> <value>`, stored in
 *                  ~/.config/g510s/vars.conf and surviving restarts.
 *    Predefined  - computed live: %lastaction, %systemlockedduration,
 *                  %ledcolor, %time, %date, %uptime, %hostname, %mkey, ...
 *
 *  GTK-free, so both the GTK3 and Qt frontends can use it.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025 usr_40476
 */

#ifndef G510S_VARS_H
#define G510S_VARS_H

#ifdef __cplusplus
extern "C" {
#endif

#define G510S_MAX_PVARS 64
#define G510S_PVAR_NAME 32
#define G510S_PVAR_VALUE 512

typedef struct {
    char name[G510S_PVAR_NAME];
    char value[G510S_PVAR_VALUE];
} g510s_pvar_t;

void g510s_pvars_init(void);
void g510s_pvars_set(const char *name, const char *value);
const char *g510s_pvar_get(const char *name);
void g510s_pvars_save(void);
void g510s_pvars_load(void);
int  g510s_pvar_count(void);
const g510s_pvar_t *g510s_pvar_at(int i);

// Records the most recent user-visible action (macro, toast, colour change).
void g510s_note_action(const char *what);

// Total seconds the desktop session has been locked, accumulated.
long g510s_locked_seconds(void);

// Expands "%name" and "$name" references for predefined variables into dst.
// Returns 1 if anything was substituted.
int g510s_expand_predefined(const char *in, char *dst, int dst_size);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // G510S_VARS_H
