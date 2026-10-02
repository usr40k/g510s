# G510s

Graphical utility for Logitech G510 and G510s keyboards on Linux.

## Build Dependencies

### OpenSUSE

`sudo zypper in g510s libg15-devel libg15-1 libg15render3 libg15render-devel libappindicator3-devel`

## TODO

* Valgrind
* Save on system shutdown
* Change screens from GUI

## FEATURES

* libg15 and libusb debugging via cli switch
* Four profiles, selectable with M1, M2, M3 and MR keys
* Custom LED backlight colors per profile
* Custom G-Key script/command execution per profile
* Persistent sessions
* Executable as user (instead of root -- **DO NOT RUN AS ROOT**)
* GUI and AppIndicator for desktop integration
* Handles device hotplug events (on-device audio)
* DBUS profile and color control
* Advanced custom LCD configurations
* Logic flow in display scripts: `IF`/`ELIF`/`ELSE`/`ENDIF` and `WHILE`/`ENDWHILE` with `BREAK`/`CONTINUE`
* `!led` commands to drive the keyboard LED colour from a display script (without changing the UI colour)
* Colour picker with RGB spin buttons, hex input and a graphical picker
* libg15daemon-client compatibility
* Terminal emulator mode (display terminal output on LCD)
* Display effects: invert, brightness simulation, dither, scanline, color modes
* Command output caching for improved performance
* New graph types: BATCHBAR, VBATCHBAR, VBAR (vertical bars)
* !set commands for runtime display configuration

## SystemD file

```systemd
[Unit]
Description=Launch and monitor g510s program
After=default.target
Requires=default.target

[Service]
ExecStart=/bin/sh -c 'while true; do if lsusb | grep -q "046d:c22d"; then /usr/local/bin/g510s & PID=$!; while ps -p $PID > /dev/null; do CPU_USAGE=$(top -b -n1 -p $PID | awk "/$PID/ {print \$9}"); if (( $(echo "$CPU_USAGE > 90" | bc -l) )); then kill -9 $PID; fi; sleep 5; done; else sleep 10; fi; done'
Restart=always
RestartSec=1

[Install]
WantedBy=default.target
```

---

## Special Prefixes

* `!` before **x**: Draw only the left/right outline (for rectangles/bars/graphs).
* `!` before **y**: Draw only the top/bottom outline.
* `!` before **w**: Flip the X axis (draw right-to-left for bars/graphs).
* `!` before **h**: Flip the Y axis (draw bottom-to-top for bars/graphs).
* `^` removes all newlines from a command output
* `%` defines a variable
* `@` calls a variable
* `@@` is just a normal `@` sign

**You can combine these as needed.**

---

## Runtime Display Configuration (!set commands)

You can configure display rendering behavior at runtime by adding `!set` commands in your `display.txt` script. These must appear on their own line:

```g510s
!set <parameter> <value>
```

### Available Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `delay` | milliseconds | Add delay between renders (for high-FPS updates) |
| `invert` | 0 or 1 | Invert display (1=inverted, 0=normal) |
| `brightness` | 0-100 | Simulate brightness (100=full, 0=off) |
| `dither` | 0 or 1 | Enable dithering effect |
| `cache` | 0 or 1 | Enable command output caching |
| `cache_runs` | number | Cache if command runs more than this many times |
| `cache_time` | seconds | ...within this time window |
| `color_mode` | 0, 1, or 2 | 0=normal, 1=stipple, 2=checkerboard |
| `scanline` | 0 or 1 | Enable scanline effect |

**Example:**

```g510s
!set delay 50
!set invert 0
!set brightness 80
!set cache 1
!set cache_runs 5
!set cache_time 10
```

---

## Logic Flow (if / while)

The display script supports conditionals and loops. Blocks must always be
closed, and lines are trimmed of surrounding whitespace, so indentation is
free-form.

### Conditions

`IF`, `ELIF`, `ELSE`, `ENDIF`, `WHILE` and `BREAK`/`CONTINUE` all take a shell
command. Before the command runs, `@variables` are substituted, so counters and
computed values can be used directly.

How the result becomes true/false:

* **No output** -> the command's **exit status** decides (0 = true). This makes
  `test`, `[ ... ]`, `grep -q`, `pgrep`, `ping -c1`, etc. work as expected.
* **Output is a number** -> true when non-zero.
* **Output is a word** -> `true`/`yes`/`on` are true, `false`/`no`/`off` are
  false, any other text is true.

### Conditionals

```g510s
IF,ping -c1 -W1 1.1.1.1 >/dev/null 2>&1
  10,10,L,0,0,// echo "online" //
ELIF,systemctl -q is-active NetworkManager
  10,10,L,0,0,// echo "local only" //
ELSE
  10,10,L,0,0,// echo "offline" //
ENDIF
```

### Loops

```g510s
%i // echo 0 //
WHILE,test @i -lt 5
  RECT,@i,5,20,10,1            # one block per iteration
  %i // echo $(( @i + 1 )) //
ENDWHILE
```

* `WHILE,<command>` re-evaluates its command on every iteration.
* `ENDWHILE` jumps back to the `WHILE` line and re-tests the condition.
* `BREAK` leaves the innermost loop immediately.
* `CONTINUE` jumps straight back to the `WHILE` condition.
* Loops may be nested (up to 16 deep) and may contain `IF` blocks.
* A safety cap of 10000 iterations per loop stops a stuck condition from
  hanging the daemon; execution then continues after the matching `ENDWHILE`.

Assigning a variable that already exists updates it in place, which is what
makes counters work: `%i // echo $(( @i + 1 )) //`.

### Drawing inside a loop

Because the loop body is re-rendered on every pass, coordinates may use the
loop variable (`@i`) to lay out elements dynamically. Note that `RECT`, `GRAPH`
and friends expect plain numbers, so any arithmetic has to go through a `%var`:

```g510s
# A 12-bar spectrum row drawn from a pipe separated list of values
%vals // amixer get Master | grep -o '[0-9]*%' | tr -d '%' | tr '\n' ' ' //

%i // echo 0 //
WHILE,test @i -lt 12
  %v // echo @vals | awk '{ printf "%d", $(( @i + 1 )) + 0 }' //
  %x // echo $(( @i * 13 )) //
  %y // echo $(( 41 - @v )) //
  %h // echo $(( @v + 1 )) //
  RECT,@x,@y,11,@h,1
  %i // echo $(( @i + 1 )) //
ENDWHILE
```

See `display_examples/loops.txt` for a complete working script.

---

## Keyboard LED commands (`!led`)

These commands drive the physical LED backlight from the display script.
They **do not** change the colour stored in the GUI, so the colour you picked
per profile (M1/M2/M3/MR) stays untouched and is still what gets saved.

| Command | Effect |
|---------|--------|
| `!led <r> <g> <b>` | Set the LED colour (0-255 each, commas or spaces allowed) |
| `!led off` | Turn the LED off (`!led 0 0 0`) |
| `!led ui` | Restore the colour selected in the GUI for the active profile |

**Example:**

```g510s
!led 255 32 64            # pink backlight while this script runs
!led ui                   # hand control back to the GUI colour
```

Values are clamped to 0-255, so out-of-range numbers are safe.

### Backlight effects (ready-made examples)

Because the display script re-runs about once a second, `!led` can be used to
build timed effects. Three complete scripts are in `display_examples/`:

| Script | What it does |
|--------|--------------|
| `lock_fade.txt` | Ramps the backlight down to black while the screen is locked, then back up on unlock |
| `rainbow.txt` | Cycles the backlight through the full hue circle, with the hue and RGB shown on the LCD |
| `combined.txt` | Rainbow while unlocked, smooth fade to black when locked, then back to the rainbow |

All three carry their state in a small file under `~/.config/g510s/` (the
brightness level / hue have to survive between renders, because script
variables are reset on every pass):

```
$HOME/.config/g510s/fade.level     # lock_fade   -> brightness 0..255
$HOME/.config/g510s/rainbow.hue    # rainbow     -> hue 0..359
$HOME/.config/g510s/combo.state    # combined    -> "<hue> <level>"
```

Lock state comes from `loginctl` (systemd-logind), falling back to the GNOME
screensaver D-Bus interface. If neither works, set `%force` at the top of the
script to `1` to simulate a locked screen:

```g510s
%force // echo 0 //   # change to 1 to test the locked behaviour
```

Worth tuning per script:

* `%step` / `%fade` - brightness units per render. 255/step gives the seconds
  to fade at roughly one render per second.
* `%speed` - hue degrees per render, so 360/speed is the length of one cycle.
* `%br`, `%bg`, `%bb` - the base colour that `lock_fade` fades (default soft blue).

Remember that each iteration of a loop (and each `%var` line) starts a shell
process, so keep these values modest.

---

## New Graph Types

### BATCHBAR and VBATCHBAR

Display multiple bar values from a single command output (e.g., audio levels):

* `BATCHBAR` - Horizontal batch bars
* `VBATCHBAR` - Vertical batch bars

The command should output pipe-separated integer values. Each value becomes a bar segment.

Audio levels (assuming output like "`100|56|59|63|8|7|29|35|41|30|18|21|9|2|1|0`")

**Example:**

```g510s
GRAPH,BATCHBAR,0,0,160,43,// amixer get Master | grep -o '[0-9]*%' | sed 's/%//g' //
```

### VBAR (Vertical Bar)

A vertical bar graph that grows upward instead of horizontally:

**Example:**

```g510s
# Vertical CPU meter
GRAPH,VBAR,150,0,10,43,// top -bn1 | grep "Cpu(s)" | awk '{print int($2+$4)}' //
```

---

## Terminal Mode

The G510s can display a terminal emulator on the LCD screen. This feature:

*It is however, currently not finished and help is appreciated*

* Runs a shell in a PTY (pseudo-terminal)
* Filters ANSI escape sequences for clean display
* Shows the last 8 lines (53 columns wide using 3px font)
* Automatically scrolls with output

**Terminal features:**

* Supports basic shell commands
* Filters ANSI escape codes (colors, cursor movement, etc.)
* Tab completion (tabs converted to 4 spaces)
* Non-blocking I/O

**Configure terminal command:** Set `terminal_cmd` in configuration to run a specific command instead of the default shell.

---

## Display examples:

See the `display_examples` directory for the files I put together

## Text fonts

Text lines take a size as their 5th field:

```g510s
x,y,align,angle,size,// command //
```

All **40 bundled fonts** are selectable (`0`–`39`):

| Size | Font |
|------|------|
| `0` | small (3px wide) |
| `1` | medium (4px) |
| `2` | large (7px) |
| `3` | huge (11px) |
| `4`–`39` | pixel-height faces, growing from ~4px up to ~52px tall |

Sizes outside `0`–`39` are clamped, so a stray value can never crash the daemon.
Note that pixel-height faces are drawn without a background (`paint_bg` is off
above size 3), so large text overlays graphics rather than clearing them.

Because the LCD is only 160x43, sizes above ~20 will not fit much text. Keep
`align` in mind too: it is measured with an estimated character width, so `C`
and `R` alignment is approximate for the larger faces.

**Examples:**

```g510s
10,1,L,0,0,// echo "tiny (size 0)" //      # small
10,8,L,0,2,// echo "large (size 2)" //      # large
80,20,C,0,10,// echo "size 10" //           # centred, medium-large
10,30,L,0,20,// echo "size 20" //           # big, pixel-height face
```

---

## Fill Modes for Shapes

For `RECT`, `ELLIPSE`, and `POLY`, the last argument (optional) controls the fill mode:

* `0` or omitted: Outline only (default)
* `1`: Filled (white)
* `2`: Filled (black)
* `3`: (POLY only) Filled and outline both black

**Examples:**

```g510s
# Rectangle examples
RECT,5,5,30,10           # Outline rectangle at (5,5), size 30x10
RECT,40,5,30,10,1        # Filled rectangle (white)
RECT,75,5,30,10,2        # Filled rectangle (black)

# Ellipse examples
ELLIPSE,20,25,12,6       # Outline ellipse
ELLIPSE,55,25,12,6,1     # Filled ellipse (white)
ELLIPSE,90,25,12,6,2     # Filled ellipse (black)

# Polygon examples (triangle)
POLY,[(80,5);(100,20);(60,20)]           # Outline triangle
POLY,[(120,5);(140,20);(100,20)],1       # Filled triangle (white)
POLY,[(120,25);(140,40);(100,40)],2      # Filled triangle (black)
POLY,[(120,45);(140,60);(100,60)],3      # Filled and outline black (POLY only)
```

---

## Tips

* All graphing related shell commands must output a single integer value.
* For `LINE` graphs, the optional `max` parameter sets the maximum value for scaling. Use `0` for autoscale.
* Make sure your graph/shape fits on the screen: `x + w <= 160`, `y + h <= 43`.
* Use `awk`, `grep`, `cut`, etc., to extract numbers from system commands.
* You can combine as many elements as you want, up to 32 lines per script.
* Use `!` prefixes to control outlines and axis flipping as described above.

---

## Troubleshooting

* If a graph or shape is not visible, check your coordinates and sizes.
* If a command outputs nothing or an error, you'll see `[none]` or `[err]` on the display.

---

## More Stuff

* Show temperature:  
  `10,20,L,0,8,// sensors | awk '/^Package id 0:/ {print int($4)}' //`
* Show custom messages:  
  `80,10,C,0,8,// printf "Hello G510s!" //`

## sus \/

```g51-s
RECT,40,15,10,15,0
ELLIPSE,53,30,4,6,0
ELLIPSE,60,30,4,6,0
ELLIPSE,57,14,7,6,0
RECT,49,15,16,17,2
ELLIPSE,64,17,5,3,2
POLY,[(50,15);(59,15)],3
```
