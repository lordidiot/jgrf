CC ?= cc
CFLAGS ?= -O2
FLAGS := -std=c99 -Wall -Wextra -pedantic
INCLUDES := -Iinclude $(shell pkg-config --cflags epoxy sdl2)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
DATADIR ?= $(DATAROOTDIR)/jollygood
LIBDIR ?= $(PREFIX)/lib/jollygood
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

objs/%.o: src/%.c
	$(CC) $(CFLAGS) $(FLAGS) $(INCLUDES) $(CPPFLAGS) $(DEFS) -c $< -o $@

objs/include/%.o: include/%.c
	$(CC) $(CFLAGS) $(FLAGS) $(CPPFLAGS) -c $< -o $@

OBJS := $(CSRCS:.c=.o)

all: maketree $(TARGET)

maketree:
	@mkdir -p objs/include/

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) $(LIBS) -o $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

install:
	@mkdir -p $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(DATADIR)/jgrf/shaders
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
	cp shaders/default.vs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/default.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/aann.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/crt-bespoke.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/crtea.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/lcd.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp shaders/sharp-bilinear.fs $(DESTDIR)$(DATADIR)/jgrf/shaders
	cp icons/jollygood96.png $(DESTDIR)$(DATADIR)/jgrf
	cp icons/jollygood1024.png $(DESTDIR)$(DATADIR)/jgrf
	cp icons/jollygood32.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps/jollygood.png
	cp icons/jollygood48.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps/jollygood.png
	cp icons/jollygood64.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps/jollygood.png
	cp icons/jollygood96.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps/jollygood.png
	cp icons/jollygood128.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps/jollygood.png
	cp icons/jollygood256.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps/jollygood.png
	cp icons/jollygood512.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps/jollygood.png
	cp icons/jollygood1024.png $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps/jollygood.png
	cp icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/jollygood.svg
	cp icons/jollygood.svg $(DESTDIR)$(DATAROOTDIR)/pixmaps/jollygood.svg

uninstall:
	rm $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DATADIR)/jgrf
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps/jollygood.png
	rm $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/jollygood.svg
	rm $(DESTDIR)$(DATAROOTDIR)/pixmaps/jollygood.svg
