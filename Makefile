SOURCEDIR := $(abspath $(patsubst %/,%,$(dir $(abspath $(lastword \
	$(MAKEFILE_LIST))))))

CC ?= cc
CFLAGS ?= -O2
FLAGS := -std=c99 -Wall -Wextra -Wshadow -pedantic
INCLUDES := -I$(SOURCEDIR)/include $(shell pkg-config --cflags epoxy sdl2)

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

# Conditions for LIBS
ifeq ($(UNAME), NetBSD)
	LIBS := $(shell pkg-config --libs epoxy sdl2) -lm
else ifeq ($(UNAME), OpenBSD)
	LIBS := $(shell pkg-config --libs epoxy sdl2) -lm
else
	LIBS := $(shell pkg-config --libs epoxy sdl2) -lm -ldl
endif

CSRCS := objs/jgrf.o \
	objs/audio.o \
	objs/cli.o \
	objs/input.o \
	objs/settings.o \
	objs/video.o \
	objs/video_gl.o \
	objs/include/lodepng.o \
	objs/include/md5.o \
	objs/include/miniz.o \
	objs/include/musl_memmem.o \
	objs/include/parg.o \
	objs/include/resampler.o \
	objs/include/tconfig.o \

objs/%.o: $(SOURCEDIR)/src/%.c
	$(CC) $(CFLAGS) $(FLAGS) $(INCLUDES) $(CPPFLAGS) $(DEFS) -c $< -o $@

objs/include/%.o: $(SOURCEDIR)/include/%.c
	$(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) -c $< -o $@

OBJS := $(CSRCS:.c=.o)

all: maketree $(TARGET)

maketree:
	@mkdir -p objs/include/

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) $(LIBS) -o $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

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
