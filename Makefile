# g510s - top-level convenience Makefile.
#
# The Qt6/QML build is the primary frontend and is driven by CMake:
#
#     make            # build the Qt app into ./build
#     make install
#
# The original GTK3 build is preserved under legacy-gtk/ and can still be
# built explicitly with `make gtk` (requires gtk3 + libappindicator).

QT_BUILD ?= build
CMAKE    ?= cmake
NIX_CFLAGS  ?=
NIX_LDFLAGS ?=

all: qt

qt:
	$(CMAKE) -S qt6 -B $(QT_BUILD) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(QT_BUILD)

clean:
	-rm -rf $(QT_BUILD)

install: qt
	-$(CMAKE) --install $(QT_BUILD)

.PHONY: all qt clean install gtk

gtk:
	@echo "The GTK frontend now lives in legacy-gtk/."
	@echo "It is no longer built by default; see legacy-gtk/README.md."
