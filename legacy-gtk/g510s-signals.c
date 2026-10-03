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
 *  Copyright © 2025-2026 usr40k
 */


#include <string.h>
#include <gtk-3.0/gtk/gtk.h>

#include "g510s.h"


extern GtkCheckMenuItem *menuhidden;
extern GtkCheckMenuItem *menuautosave;

// menubar actions
void on_menusave_activate(GtkMenuItem *menuitem, gpointer window) {
  save_config();
}

void on_menuhidden_toggled(GtkMenuItem *menuitem, gpointer user_data) {
  if (gtk_check_menu_item_get_active(menuhidden) == 1) {
    g510s_data.gui_hidden = 1;
  } else {
    g510s_data.gui_hidden = 0;
  }
}

void on_menuautosaveonquit_toggled(GtkMenuItem *menuitem, gpointer user_data) {
  if (gtk_check_menu_item_get_active(menuautosave) == 1) {
    g510s_data.auto_save_on_quit = 1;
  } else {
    g510s_data.auto_save_on_quit = 0;
  }
}

void on_menuabout_activate(GtkMenuItem *menuitem, gpointer aboutdialog) {
  gtk_widget_show(aboutdialog);
}

// --- Colour slider handlers -------------------------------------------------
// A single template generates the handler for every profile/channel pair,
// replacing twelve hand-written functions with one macro and twelve one-liners.
// `field` is a member path such as "m1.red" and `prof` is the profile number.
#define DEFINE_COLOR_HANDLER(fn, field, prof)          \
  void fn(GtkAdjustment *adjustment, gpointer scale) { \
    (void)adjustment;                                  \
    g510s_data.field = gtk_range_get_value(scale);     \
    update = prof;                                     \
  }

DEFINE_COLOR_HANDLER(on_red_adj_m1_value_changed,   m1.red,   1)
DEFINE_COLOR_HANDLER(on_green_adj_m1_value_changed, m1.green, 1)
DEFINE_COLOR_HANDLER(on_blue_adj_m1_value_changed,  m1.blue,  1)
DEFINE_COLOR_HANDLER(on_red_adj_m2_value_changed,   m2.red,   2)
DEFINE_COLOR_HANDLER(on_green_adj_m2_value_changed, m2.green, 2)
DEFINE_COLOR_HANDLER(on_blue_adj_m2_value_changed,  m2.blue,  2)
DEFINE_COLOR_HANDLER(on_red_adj_m3_value_changed,   m3.red,   3)
DEFINE_COLOR_HANDLER(on_green_adj_m3_value_changed, m3.green, 3)
DEFINE_COLOR_HANDLER(on_blue_adj_m3_value_changed,  m3.blue,  3)
DEFINE_COLOR_HANDLER(on_red_adj_mr_value_changed,   mr.red,   4)
DEFINE_COLOR_HANDLER(on_green_adj_mr_value_changed, mr.green, 4)
DEFINE_COLOR_HANDLER(on_blue_adj_mr_value_changed,  mr.blue,  4)

// button actions
void on_closebutton_clicked(GtkButton *button, gpointer window) {
  gtk_widget_hide(window);
}

// indicator actions
void on_indicator_menushow_activate(GtkMenuItem *menuitem, gpointer window) {
  gtk_widget_show(window);
}

void on_indicator_menuhide_activate(GtkMenuItem *menuitem, gpointer window) {
  gtk_widget_hide(window);
}

// --- G-key macro entry handlers ---------------------------------------------
// The GUI holds 72 near-identical GtkEntry widgets (4 profiles x 18 keys).
// One template plus a per-bank X-macro list generates all of their handlers.
#define DEFINE_GKEY_HANDLER(bank, idx)                                          \
  void on_entry_##bank##g##idx##_changed(GtkEntry *entry, gpointer user_data) { \
    (void)user_data;                                                            \
    memset(g510s_data.bank.g##idx, 0, sizeof(g510s_data.bank.g##idx));          \
    strncpy(g510s_data.bank.g##idx, gtk_entry_get_text(entry),                  \
            sizeof(g510s_data.bank.g##idx));                                    \
  }

#define DEFINE_GKEY_BANK(bank) \
  DEFINE_GKEY_HANDLER(bank, 1)  DEFINE_GKEY_HANDLER(bank, 2)  \
  DEFINE_GKEY_HANDLER(bank, 3)  DEFINE_GKEY_HANDLER(bank, 4)  \
  DEFINE_GKEY_HANDLER(bank, 5)  DEFINE_GKEY_HANDLER(bank, 6)  \
  DEFINE_GKEY_HANDLER(bank, 7)  DEFINE_GKEY_HANDLER(bank, 8)  \
  DEFINE_GKEY_HANDLER(bank, 9)  DEFINE_GKEY_HANDLER(bank, 10) \
  DEFINE_GKEY_HANDLER(bank, 11) DEFINE_GKEY_HANDLER(bank, 12) \
  DEFINE_GKEY_HANDLER(bank, 13) DEFINE_GKEY_HANDLER(bank, 14) \
  DEFINE_GKEY_HANDLER(bank, 15) DEFINE_GKEY_HANDLER(bank, 16) \
  DEFINE_GKEY_HANDLER(bank, 17) DEFINE_GKEY_HANDLER(bank, 18)

DEFINE_GKEY_BANK(m1)
DEFINE_GKEY_BANK(m2)
DEFINE_GKEY_BANK(m3)
DEFINE_GKEY_BANK(mr)
