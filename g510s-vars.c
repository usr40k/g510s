/*
 *  g510s-vars.c - persistent and predefined script variables.
 *
 *  g510s is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Copyright © 2025 usr_40476
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <pthread.h>
#include <sys/types.h>

#include "g510s.h"
#include "g510s-vars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static g510s_pvar_t s_pvars[G510S_MAX_PVARS];
static int s_pvar_count = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static char s_last_action[192] = "none";

// --- Session lock tracking --------------------------------------------------
static volatile int s_locked = 0;
static long s_locked_total = 0;
static time_t s_lock_since = 0;
static pthread_t s_lock_thread;
static int s_lock_thread_started = 0;

static long s_locked_seconds_locked(void) {
    if (!s_locked)
        return s_locked_total;
    return s_locked_total + (long)(time(NULL) - s_lock_since);
}

long g510s_locked_seconds(void) {
    pthread_mutex_lock(&s_lock);
    long v = s_locked_seconds_locked();
    pthread_mutex_unlock(&s_lock);
    return v;
}

static void set_locked(int locked) {
    pthread_mutex_lock(&s_lock);
    if (locked && !s_locked) {
        s_locked = 1;
        s_lock_since = time(NULL);
    } else if (!locked && s_locked) {
        s_locked = 0;
        s_locked_total += (long)(time(NULL) - s_lock_since);
        s_lock_since = 0;
    }
    pthread_mutex_unlock(&s_lock);
}

// Polls loginctl rather than speaking D-Bus, so this stays dependency-free.
static int query_locked_hint(void) {
    const char *sid = getenv("XDG_SESSION_ID");
    FILE *f;
    if (sid && *sid)
        f = popen("loginctl show-session \"$XDG_SESSION_ID\" -p LockedHint --value 2>/dev/null", "r");
    else
        f = popen("loginctl show-session self -p LockedHint --value 2>/dev/null", "r");
    if (!f)
        return 0;
    char buf[32] = {0};
    if (!fgets(buf, sizeof(buf), f)) { pclose(f); return 0; }
    pclose(f);
    return strncmp(buf, "yes", 3) == 0;
}

static void *lock_watcher(void *arg) {
    (void)arg;
    while (!leaving) {
        set_locked(query_locked_hint());
        for (int i = 0; i < 20 && !leaving; i++)
            usleep(100000);   // poll every ~2s
    }
    set_locked(0);
    return NULL;
}

static void ensure_lock_watcher(void) {
    if (!s_lock_thread_started) {
        s_lock_thread_started = 1;
        if (pthread_create(&s_lock_thread, NULL, lock_watcher, NULL) != 0)
            s_lock_thread_started = 0;
    }
}

// --- Persistent variables ---------------------------------------------------

static const char *vars_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    snprintf(path, sizeof(path), "%s/.config/g510s/vars.conf", home);
    return path;
}

int g510s_pvar_count(void) { return s_pvar_count; }

const g510s_pvar_t *g510s_pvar_at(int i) {
    if (i < 0 || i >= s_pvar_count) return NULL;
    return &s_pvars[i];
}

const char *g510s_pvar_get(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_pvar_count; i++)
        if (strcmp(s_pvars[i].name, name) == 0)
            return s_pvars[i].value;
    return NULL;
}

void g510s_pvars_set(const char *name, const char *value) {
    if (!name || !*name || !value) return;

    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < s_pvar_count; i++) {
        if (strcmp(s_pvars[i].name, name) == 0) {
            snprintf(s_pvars[i].value, G510S_PVAR_VALUE, "%s", value);
            pthread_mutex_unlock(&s_lock);
            g510s_pvars_save();
            return;
        }
    }
    if (s_pvar_count < G510S_MAX_PVARS) {
        snprintf(s_pvars[s_pvar_count].name, G510S_PVAR_NAME, "%s", name);
        snprintf(s_pvars[s_pvar_count].value, G510S_PVAR_VALUE, "%s", value);
        s_pvar_count++;
    }
    pthread_mutex_unlock(&s_lock);
    g510s_pvars_save();
}

void g510s_pvars_load(void) {
    const char *p = vars_path();
    if (!p) return;
    FILE *f = fopen(p, "r");
    if (!f) return;

    s_pvar_count = 0;
    char line[G510S_PVAR_VALUE + G510S_PVAR_NAME + 8];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *name = line, *val = eq + 1;
        size_t nl = strlen(name);
        while (nl > 0 && isspace((unsigned char)name[nl-1])) name[--nl] = '\0';
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl-1] == '\n' || val[vl-1] == '\r')) val[--vl] = '\0';
        if (nl == 0) continue;
        if (s_pvar_count < G510S_MAX_PVARS) {
            snprintf(s_pvars[s_pvar_count].name, G510S_PVAR_NAME, "%s", name);
            snprintf(s_pvars[s_pvar_count].value, G510S_PVAR_VALUE, "%s", val);
            s_pvar_count++;
        }
    }
    fclose(f);
}

void g510s_pvars_save(void) {
    const char *p = vars_path();
    if (!p) return;
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "# g510s persistent script variables - edited by the display script\n");
    fprintf(f, "# set from a script with:  !var <name> <value>\n");
    for (int i = 0; i < s_pvar_count; i++)
        fprintf(f, "%s=%s\n", s_pvars[i].name, s_pvars[i].value);
    fclose(f);
}

void g510s_note_action(const char *what) {
    if (!what) return;
    pthread_mutex_lock(&s_lock);
    snprintf(s_last_action, sizeof(s_last_action), "%s", what);
    pthread_mutex_unlock(&s_lock);
}

void g510s_pvars_init(void) {
    g510s_pvars_load();
    ensure_lock_watcher();
}

// --- Predefined variables ---------------------------------------------------

static int is_predefined(const char *name) {
    static const char *names[] = {
        "lastaction", "systemlockedduration", "ledcolor", "ledred", "ledgreen",
        "ledblue", "time", "date", "uptime", "hostname", "mkey", "bankname", NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(names[i], name) == 0)
            return 1;
    return 0;
}

static void render_predefined(const char *name, char *out, int out_size) {
    char tmp[128];

    if (strcmp(name, "lastaction") == 0) {
        pthread_mutex_lock(&s_lock);
        snprintf(out, out_size, "%s", s_last_action);
        pthread_mutex_unlock(&s_lock);
        return;
    }
    if (strcmp(name, "systemlockedduration") == 0) {
        long s = g510s_locked_seconds();
        if (s >= 3600) snprintf(out, out_size, "%ldh %ldm", s / 3600, (s % 3600) / 60);
        else if (s >= 60) snprintf(out, out_size, "%ldm %lds", s / 60, s % 60);
        else snprintf(out, out_size, "%lds", s);
        return;
    }
    if (strcmp(name, "ledcolor") == 0) {
        snprintf(out, out_size, "#%02x%02x%02x",
                 g510s_data.led_red & 0xff, g510s_data.led_green & 0xff,
                 g510s_data.led_blue & 0xff);
        return;
    }
    if (strcmp(name, "ledred") == 0)   { snprintf(out, out_size, "%d", g510s_data.led_red); return; }
    if (strcmp(name, "ledgreen") == 0) { snprintf(out, out_size, "%d", g510s_data.led_green); return; }
    if (strcmp(name, "ledblue") == 0)  { snprintf(out, out_size, "%d", g510s_data.led_blue); return; }
    if (strcmp(name, "mkey") == 0)     { snprintf(out, out_size, "%d", g510s_data.mkey_state); return; }
    if (strcmp(name, "bankname") == 0) {
        static const char *names4[] = {"", "M1", "M2", "M3", "MR"};
        int m = g510s_data.mkey_state;
        snprintf(out, out_size, "%s", (m >= 1 && m <= 4) ? names4[m] : "");
        return;
    }
    if (strcmp(name, "time") == 0) {
        time_t t = time(NULL);
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(tmp, sizeof(tmp), "%H:%M", &tmv);
        snprintf(out, out_size, "%s", tmp);
        return;
    }
    if (strcmp(name, "date") == 0) {
        time_t t = time(NULL);
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(tmp, sizeof(tmp), "%a %b %d", &tmv);
        snprintf(out, out_size, "%s", tmp);
        return;
    }
    if (strcmp(name, "uptime") == 0) {
        FILE *f = fopen("/proc/uptime", "r");
        double up = 0;
        if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
        long s = (long)up;
        if (s >= 3600) snprintf(out, out_size, "%ldh %ldm", s / 3600, (s % 3600) / 60);
        else           snprintf(out, out_size, "%ldm", s / 60);
        return;
    }
    if (strcmp(name, "hostname") == 0) {
        FILE *f = popen("hostname 2>/dev/null", "r");
        if (f) {
            if (fgets(tmp, sizeof(tmp), f)) {
                size_t n = strlen(tmp);
                while (n > 0 && (tmp[n-1] == '\n' || tmp[n-1] == '\r')) tmp[--n] = '\0';
            }
            pclose(f);
        }
        snprintf(out, out_size, "%s", tmp);
        return;
    }
    out[0] = '\0';
}

// Expands %name and $name for predefined variables.
// Persistent vars are *not* handled here; g510s-clock.c resolves those first.
int g510s_expand_predefined(const char *in, char *dst, int dst_size) {
    if (!in || !dst || dst_size <= 0) return 0;

    char tmp[4096];
    int di = 0;
    int changed = 0;
    const char *src = in;

    while (*src && di < (int)sizeof(tmp) - 1) {
        if ((*src == '%' || *src == '$') &&
            (isalpha((unsigned char)src[1]) || src[1] == '_')) {
            const char *start = ++src;
            char name[64];
            int ni = 0;
            while (*src && (isalnum((unsigned char)*src) || *src == '_') && ni < 63)
                name[ni++] = *src++;
            name[ni] = '\0';
            (void)start;

            char value[512];
            const char *pvar = g510s_pvar_get(name);
            if (pvar)
                snprintf(value, sizeof(value), "%s", pvar);
            else if (is_predefined(name))
                render_predefined(name, value, sizeof(value));
            else
                value[0] = '\0';

            if (value[0]) {
                size_t vl = strlen(value);
                if (vl < sizeof(tmp) - (size_t)di - 1) {
                    memcpy(tmp + di, value, vl);
                    di += (int)vl;
                    changed = 1;
                }
                continue;
            }
            // Unknown variable: drop the sigil and emit the name verbatim.
            if (ni < (int)sizeof(tmp) - (size_t)di - 1) {
                tmp[di++] = *src == '%' ? '%' : '$';
                memcpy(tmp + di, name, ni);
                di += ni;
            }
            continue;
        }
        tmp[di++] = *src++;
    }
    tmp[di] = '\0';

    if (!changed) {
        snprintf(dst, dst_size, "%s", in);
        return 0;
    }
    snprintf(dst, dst_size, "%s", tmp);
    return 1;
}
