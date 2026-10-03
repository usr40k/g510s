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


#ifndef G510S_H
#define G510S_H

#define G510S_VERSION "1.0.0"

// Terminal display settings
#define TERMINAL_FONT_SIZE 0
#define TERMINAL_CHAR_WIDTH 3
#define TERMINAL_CHAR_HEIGHT 5
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 43
// The 40 G15 fonts bundled with libg15render (default-00.fnt .. default-39.fnt).
// Sizes 0-3 are the standard faces, 4-39 are pixel-height faces.
#define G15_MAX_FONT_SIZE 39
#define TERMINAL_COLS (DISPLAY_WIDTH / TERMINAL_CHAR_WIDTH)
#define TERMINAL_ROWS (DISPLAY_HEIGHT / TERMINAL_CHAR_HEIGHT)

#ifndef SO_PRIORITY
#define SO_PRIORITY 12
#endif

#define LISTEN_ADDR "127.0.0.1"
#define LISTEN_PORT 15550

#define MAX_CLIENTS 10

#define CLIENT_CMD_GET_KEYSTATE 'k'
#define CLIENT_CMD_SWITCH_PRIORITIES 'p'
#define CLIENT_CMD_IS_FOREGROUND 'v'
#define CLIENT_CMD_IS_USER_SELECTED 'u'
#define CLIENT_CMD_BACKLIGHT 0x80
#define CLIENT_CMD_CONTRAST 0x40
#define CLIENT_CMD_MKEY_LIGHTS 0x20
#define CLIENT_CMD_KEY_HANDLER 0x10

#define SERV_HELO "G15 daemon HELLO"

#ifndef GKEY_STRLEN
#define GKEY_STRLEN 1024
#endif

typedef struct lcd_s {
  int lcd_type;
  unsigned char buf[1048];
  int max_x;
  int max_y;
  int connection;
  long int ident;
  unsigned int backlight_state;
  unsigned int mkey_state;
  unsigned int contrast_state;
  unsigned int state_changed;
  unsigned int usr_foreground;
} lcd_t;

typedef struct lcdnode_s lcdnode_t;
typedef struct lcdlist_s lcdlist_t;

// NOTE: these used to be written as `} lcdnode_s;` which silently declared
// unused global variables of the same name. They are now plain struct
// definitions; the real globals are declared extern below.
struct lcdnode_s {
  lcdlist_t *list;
  lcdnode_t *prev;
  lcdnode_t *next;
  lcdnode_t *last_priority;
  lcd_t *lcd;
};

struct lcdlist_s {
  lcdnode_t *head;
  lcdnode_t *tail;
  lcdnode_t *current;
};

//pthread_mutex_t lcdlist_mutex;

struct m_data_s {
  int red;
  int green;
  int blue;
  char g1[GKEY_STRLEN];
  char g2[GKEY_STRLEN];
  char g3[GKEY_STRLEN];
  char g4[GKEY_STRLEN];
  char g5[GKEY_STRLEN];
  char g6[GKEY_STRLEN];
  char g7[GKEY_STRLEN];
  char g8[GKEY_STRLEN];
  char g9[GKEY_STRLEN];
  char g10[GKEY_STRLEN];
  char g11[GKEY_STRLEN];
  char g12[GKEY_STRLEN];
  char g13[GKEY_STRLEN];
  char g14[GKEY_STRLEN];
  char g15[GKEY_STRLEN];
  char g16[GKEY_STRLEN];
  char g17[GKEY_STRLEN];
  char g18[GKEY_STRLEN];
};

struct g510s_data_s {
  int gui_hidden;
  int mkey_state;
  struct m_data_s m1;
  struct m_data_s m2;
  struct m_data_s m3;
  struct m_data_s mr;
  int clock_mode;
  int show_date;
  int auto_save_on_quit;
  int color_fade; // 1 = fade, 0 = instant
  int led_red;   // Current LED color, set by DBus or threads
  int led_green;
  int led_blue;
  
  // Notification system
  char notifications[10][256];  // Queue of notifications
  int notification_count;
  int notification_display_time; // ms to display each notification
  int notification_position;     // Current position in queue
};
typedef struct g510s_data_s g510s_data_t;

// Bank config structure - stores display script + macros for a bank
typedef struct {
  char name[64];
  char display_script[4096];  // The display.txt script content (saved as <name>.txt)
  struct m_data_s macros;     // G-key macros for this bank
  int red;
  int green;
  int blue;
} bank_config_t;

#define MAX_BANK_CONFIGS 20

// These are defined once in g510s-config.c. They are declared extern here so
// that the header can also be included from the C++ (Qt) frontend, where
// tentative definitions would cause multiple-definition errors.
extern int leaving;
extern int update;
extern int device_found;
extern char *usb_id;
extern unsigned int connected_clients;
extern unsigned int current_key_state;

extern struct g510s_data_s g510s_data;

// Preview buffer shared by the renderer and whichever frontend is linked in.
// Matches libg15's G15_BUFFER_LEN (160x43 1bpp + control bytes).
#define G510S_PREVIEW_BUFFER_LEN 1048
extern unsigned char preview_buffer[G510S_PREVIEW_BUFFER_LEN];

// Dump the LCD buffer to disk every frame (--dump-display-buffer).
extern int dump_display_buffer;

// Terminal mode (declared as extern, defined in g510s-clock.c)
extern int terminal_mode;
extern char terminal_cmd[1024];

// Terminal emulator state (declared as extern, defined in g510s-clock.c)
extern int terminal_fd;  // PTY file descriptor
extern pid_t terminal_pid;  // Shell process ID
extern char terminal_buffer[1024 * 10];  // Scrollback buffer (10KB)
extern int terminal_buf_start;  // Circular buffer start
extern int terminal_buf_len;  // Current buffer length
extern int terminal_cursor_row;  // Current cursor position
extern int terminal_cursor_col;
extern pthread_mutex_t terminal_mutex;  // Protect terminal state

// Terminal keyboard mode (when active, G-keys send input to terminal)
extern int terminal_keyboard_mode;

// L1 key timing for short/long press detection
#include <sys/time.h>  // for struct timeval
extern struct timeval l1_press_time;
extern int l1_pressed;
#define L1_LONG_PRESS_MS 500  // 500ms for long press

// Terminal functions
void init_terminal();
void close_terminal();

int init_uinput();
void exit_uinput();
void process_keys(lcdlist_t *displaylist, unsigned int key, unsigned int key_state);

// Presets (defined in g510s-presets.c, shared by both frontends)
void load_presets(void);
void save_preset(const char *name);
void load_preset(const char *name);
void bind_preset_to_bank(int bank, const char *preset_name);
const char *get_bank_preset(int bank);
int preset_count(void);
const char *preset_name_at(int index);
char *macro_by_index(int mode, int idx);

void digital_clock(lcd_t *lcd);

void init_data();
int check_dir();
int load_config();
int save_config();

lcdlist_t *lcdlist_init();
lcdnode_t *lcdnode_add(lcdlist_t **display_list);
void lcdnode_remove(lcdnode_t *oldnode);
void lcdlist_destroy(lcdlist_t **displaylist);

void send_keystate(lcd_t *client, unsigned int key);
int client_connect(lcdlist_t **lcdlist, int listening_socket);
int init_sockserver();
int g15_send(int sock, char *buf, unsigned int len);
int g15_recv(lcdnode_t *lcdnode, int sock, char *buf, unsigned int len);

int is_number(char number[]);
void convert_buf(lcd_t *lcd, unsigned char * orig_buf);
void set_mkey_state(int state);
void set_color();
void run_gkey_cmd(int gkey);

// --- Frontend hooks -------------------------------------------------------
// Implemented by each UI frontend (GTK in g510s.c, Qt in qt6/backend.cpp).
// ui_set_device_attention: 1 = device missing (attention icon), 0 = ok.
// ui_request_refresh:      re-read g510s_data into the UI widgets.
// update_preview:          repaint the LCD preview from preview_buffer.
void ui_set_device_attention(int attention);
void ui_request_refresh(void);

void *lcd_client_function(void *display);
void *key_function(void *lcdlist);
void *update_function(void *lcdlist);
void *server_function(void *lcdlist);

#endif // G510S_H
