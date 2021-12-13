SOURCEDIR := $(abspath $(patsubst %/,%,$(dir $(abspath $(lastword \
	$(MAKEFILE_LIST))))))

CC ?= cc
CFLAGS ?= -O2
FLAGS := -std=c99 -Wall -Wextra -Wshadow -Wmissing-prototypes -pedantic

PKGCONF ?= pkg-config
CFLAGS_JG := $(shell $(PKGCONF) --cflags jg)

CFLAGS_EPOXY := $(shell $(PKGCONF) --cflags epoxy)
LIBS_EPOXY := $(shell $(PKGCONF) --libs epoxy)

CFLAGS_SDL2 := $(shell $(PKGCONF) --cflags sdl2)
LIBS_SDL2 := $(shell $(PKGCONF) --libs sdl2)

CFLAGS_SPEEX := $(shell $(PKGCONF) --cflags speexdsp)
LIBS_SPEEX := $(shell $(PKGCONF) --libs speexdsp)

INCLUDES := -I$(SOURCEDIR)/deps $(CFLAGS_JG) $(CFLAGS_EPOXY) $(CFLAGS_SDL2) \
	$(CFLAGS_SPEEX)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
DATADIR ?= $(DATAROOTDIR)
DOCDIR ?= $(DATAROOTDIR)/doc/jgrf
LIBDIR ?= $(PREFIX)/lib
TARGET := jollygood

UNAME := $(shell uname -s)

# Conditions for DEFS
ifneq ($(OS), Windows_NT)
	DEFS := -DDATADIR=\"$(DATADIR)\" -DLIBDIR=\"$(LIBDIR)\"
endif

LIBS := $(LIBS_EPOXY) $(LIBS_SDL2) $(LIBS_SPEEX) -lm

# Conditions for LIBS
ifeq ($(UNAME), NetBSD)
else ifeq ($(UNAME), OpenBSD)
else
	LIBS += -ldl
endif

OBJDIR := objs

CSRCS := $(OBJDIR)/jgrf.o \
	$(OBJDIR)/audio.o \
	$(OBJDIR)/cheats.o \
	$(OBJDIR)/cli.o \
	$(OBJDIR)/input.o \
	$(OBJDIR)/settings.o \
	$(OBJDIR)/video.o \
	$(OBJDIR)/video_gl.o \
	$(OBJDIR)/deps/lodepng.o \
	$(OBJDIR)/deps/md5.o \
	$(OBJDIR)/deps/miniz.o \
	$(OBJDIR)/deps/musl_memmem.o \
	$(OBJDIR)/deps/parg.o \
	$(OBJDIR)/deps/parson.o \
	$(OBJDIR)/deps/tconfig.o

.PHONY: all clean install install-strip uninstall

$(OBJDIR)/%.o: $(SOURCEDIR)/src/%.c $(OBJDIR)/.tag
	$(info $(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) $(DEFS) \
		$(subst $(SOURCEDIR)/,,$(INCLUDES) -c $<) -o $@)
	@$(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/deps/%.o: $(SOURCEDIR)/deps/%.c $(OBJDIR)/.tag
	$(info $(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) -c \
		$(subst $(SOURCEDIR)/,,$<) -o $@)
	@$(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) -c $< -o $@

OBJS := $(CSRCS:.c=.o)

all: $(TARGET)

$(OBJDIR)/.tag:
	@mkdir -p $(OBJDIR)/deps/
	@touch $@

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) $(LIBS) -o $(TARGET)

clean:
	rm -rf $(OBJDIR)/ $(TARGET)

install: all
	@mkdir -p $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(DOCDIR)
	@mkdir -p $(DESTDIR)$(DATADIR)/jollygood/jgrf/shaders
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
	cp $(TARGET) $(DESTDIR)$(BINDIR)
	cp $(SOURCEDIR)/LICENSE $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/README $(DESTDIR)$(DOCDIR)
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
	cp $(SOURCEDIR)/icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/jollygood.svg
	cp $(SOURCEDIR)/icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/pixmaps/jollygood.svg

install-strip: install
	strip $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -rf $(DESTDIR)$(DATADIR)/jollygood/jgrf
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
