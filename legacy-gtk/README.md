# Legacy GTK3 frontend - superseded by the Qt6/QML app in ../qt6.
#
# These files are kept for reference / in case anyone still needs the old
# GTK3 UI.  They are NOT part of any build produced by this repository
# any more: `default.nix` and `flake.nix` build only the Qt frontend.
#
# To build this by hand you would need g510s.c, g510s-signals.c and the
# shared C core from the parent directory, plus gtk+-3.0 and
# appindicator3-0.1:
#
#   cc -o g510s-legacy g510s.c g510s-signals.c ../g510s-{clock,config,keys,list,misc,net,presets,threads}.c \
#      $(pkg-config --cflags --libs gtk+-3.0 appindicator3-0.1) \
#      -lg15 -lg15render -lpthread -lm
#
# Note that g510s.c references g510s.glade at
# /usr/local/share/g510s/g510s.glade.