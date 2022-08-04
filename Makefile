SOURCEDIR := $(abspath $(patsubst %/,%,$(dir $(abspath $(lastword \
	$(MAKEFILE_LIST))))))

CC ?= cc
CFLAGS ?= -O2
FLAGS := -std=c99 -Wall -Wextra -Wshadow -Wmissing-prototypes -pedantic

PKG_CONFIG ?= pkg-config
CFLAGS_JG := $(shell $(PKG_CONFIG) --cflags jg)

CFLAGS_EPOXY := $(shell $(PKG_CONFIG) --cflags epoxy)
LIBS_EPOXY := $(shell $(PKG_CONFIG) --libs epoxy)

CFLAGS_SDL2 := $(shell $(PKG_CONFIG) --cflags sdl2)
LIBS_SDL2 := $(shell $(PKG_CONFIG) --libs sdl2)

CFLAGS_SPEEX := $(shell $(PKG_CONFIG) --cflags speexdsp)
LIBS_SPEEX := $(shell $(PKG_CONFIG) --libs speexdsp)

DEFINES :=
INCLUDES := -I$(SOURCEDIR)/deps $(CFLAGS_JG) $(CFLAGS_EPOXY) $(CFLAGS_SDL2) \
	$(CFLAGS_SPEEX)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
DATADIR ?= $(DATAROOTDIR)
DOCDIR ?= $(DATAROOTDIR)/doc/jgrf
LIBDIR ?= $(PREFIX)/lib
MANDIR ?= $(DATAROOTDIR)/man
TARGET := jollygood

USE_EXTERNAL_MD5 ?= 0
USE_EXTERNAL_MINIZ ?= 0

UNAME := $(shell uname -s)

# Conditions for DEFINES
ifneq ($(OS), Windows_NT)
	DEFINES += -DDATADIR="\"$(DATADIR)\"" -DLIBDIR="\"$(LIBDIR)\""
endif

LIBS := $(LIBS_EPOXY) $(LIBS_SDL2) $(LIBS_SPEEX) -lm

# Conditions for LIBS
ifeq ($(UNAME), NetBSD)
else ifeq ($(UNAME), OpenBSD)
else
	LIBS += -ldl
endif

CSRCS := jgrf.c \
	audio.c \
	cheats.c \
	cli.c \
	input.c \
	settings.c \
	video.c \
	video_gl.c \
	deps/lodepng.c \
	deps/musl_memmem.c \
	deps/parg.c \
	deps/parson.c \
	deps/tconfig.c \
	deps/wave_writer.c

ifeq ($(USE_EXTERNAL_MD5), 0)
	CFLAGS_MD5 :=
	LIBS_MD5 :=
	CSRCS += deps/md5.c
else
	DEFINES += -DHAVE_OPENSSL
	CFLAGS_MD5 := $(shell $(PKG_CONFIG) --cflags libcrypto)
	LIBS_MD5 := $(shell $(PKG_CONFIG) --libs libcrypto)
endif

ifeq ($(USE_EXTERNAL_MINIZ), 0)
	Q_MINIZ :=
	CFLAGS_MINIZ := -I$(SOURCEDIR)/deps/miniz
	LIBS_MINIZ :=
	CSRCS += deps/miniz/miniz.c
else
	Q_MINIZ := @
	CFLAGS_MINIZ := $(shell $(PKG_CONFIG) --cflags miniz)
	LIBS_MINIZ := $(shell $(PKG_CONFIG) --libs miniz)
endif

INCLUDES += $(CFLAGS_MD5) $(CFLAGS_MINIZ)
LIBS += $(LIBS_MD5) $(LIBS_MINIZ)

OBJDIR := objs

# List of object files
OBJS := $(patsubst %,$(OBJDIR)/%,$(CSRCS:.c=.o))

# Compiler command
COMPILE_C = $(strip $(CC) $(CFLAGS) $(CPPFLAGS) $(1) -c $< -o $@)

# Info command
COMPILE_INFO = $(info $(subst $(SOURCEDIR)/,,$(1)))

# Dependencies command
BUILD_DEPS = $(call COMPILE_C, $(FLAGS))

# Core command
BUILD_MAIN = $(call COMPILE_C, $(FLAGS) $(DEFINES) $(INCLUDES))

.PHONY: all clean install install-strip uninstall

all: $(TARGET)

$(OBJDIR)/%.o: $(SOURCEDIR)/src/%.c $(OBJDIR)/.tag
	$(call COMPILE_INFO, $(BUILD_MAIN))
	@$(BUILD_MAIN)

$(OBJDIR)/deps/%.o: $(SOURCEDIR)/deps/%.c $(OBJDIR)/.tag
	$(call COMPILE_INFO, $(BUILD_DEPS))
	@$(BUILD_DEPS)

$(OBJDIR)/.tag:
	@mkdir -p $(OBJDIR)/deps/miniz
	@touch $@

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -rf $(OBJDIR)/ $(TARGET)

install: all
	@mkdir -p $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(DOCDIR)
	@mkdir -p $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/applications
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/pixmaps
	@mkdir -p $(DESTDIR)$(MANDIR)/man6
	cp $(TARGET) $(DESTDIR)$(BINDIR)
	cp $(SOURCEDIR)/LICENSE $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/README $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/jollygood.6 $(DESTDIR)$(MANDIR)/man6
	cp $(SOURCEDIR)/jollygood.desktop $(DESTDIR)$(DATAROOTDIR)/applications
	cp $(SOURCEDIR)/shaders/default.vs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/default.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/aann.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/crt-bespoke.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/crtea.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/lcd.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/shaders/sharp-bilinear.fs $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
	cp $(SOURCEDIR)/icons/jollygood96.png $(DESTDIR)$(DATADIR)/jollygood/jgrf
	cp $(SOURCEDIR)/icons/jollygood1024.png $(DESTDIR)$(DATADIR)/jollygood/jgrf
	cp $(SOURCEDIR)/icons/jollygood32.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood48.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood64.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood96.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood128.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood256.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood512.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood1024.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps/jollygood.png
	cp $(SOURCEDIR)/icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps
	cp $(SOURCEDIR)/icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/pixmaps
	$(Q_MINIZ)if test $(USE_VENDORED_MINIZ) != 0; then \
		cp $(SOURCEDIR)/deps/miniz/LICENSE \
			$(DESTDIR)$(DOCDIR)/LICENSE-miniz; \
	fi

install-strip: install
	strip $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -rf $(DESTDIR)$(DATADIR)/jollygood/jgrf
	rm -f $(DESTDIR)$(DATAROOTDIR)/applications/jollygood.desktop
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps/jollygood.png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/jollygood.svg
	rm -f $(DESTDIR)$(DATAROOTDIR)/pixmaps/jollygood.svg
	rm -f $(DESTDIR)$(MANDIR)/man6/jollygood.6
