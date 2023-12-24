SOURCEDIR := $(abspath $(patsubst %/,%,$(dir $(abspath $(lastword \
	$(MAKEFILE_LIST))))))

BUILD_STATIC ?= 0
USE_EXTERNAL_MD5 ?= 0

CC ?= cc
CFLAGS ?= -O2
PKG_CONFIG ?= pkg-config

PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= $(EXEC_PREFIX)/bin
LIBDIR ?= $(EXEC_PREFIX)/lib
DATAROOTDIR ?= $(PREFIX)/share
DATADIR ?= $(DATAROOTDIR)
DOCDIR ?= $(DATAROOTDIR)/doc/jgrf
MANDIR ?= $(DATAROOTDIR)/man

FLAGS := -std=c99 -Wall -Wextra -Wshadow -Wmissing-prototypes -pedantic
DEFINES :=
DEPDIR := $(SOURCEDIR)/deps

CFLAGS_JG = $(shell $(PKG_CONFIG) --cflags jg)

CFLAGS_EPOXY = $(shell $(PKG_CONFIG) --cflags epoxy)
LIBS_EPOXY = $(shell $(PKG_CONFIG) --libs epoxy)

CFLAGS_SDL2 = $(shell $(PKG_CONFIG) --cflags sdl2)
LIBS_SDL2 = $(shell $(PKG_CONFIG) --libs sdl2)

CFLAGS_SPEEX = $(shell $(PKG_CONFIG) --cflags speexdsp)
LIBS_SPEEX = $(shell $(PKG_CONFIG) --libs speexdsp)

INCLUDES := -I$(DEPDIR) $(CFLAGS_JG) $(CFLAGS_EPOXY) $(CFLAGS_SDL2) \
	$(CFLAGS_SPEEX)

LIBS := $(LIBS_EPOXY) $(LIBS_SDL2) $(LIBS_SPEEX) -lm

UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
	LIBS += -Wl,-undefined,error
else
	LIBS += -Wl,--no-undefined
endif

# Conditions for DEFINES
ifneq ($(OS), Windows_NT)
	DEFINES += -DDATADIR="\"$(DATADIR)\"" -DLIBDIR="\"$(LIBDIR)\""
endif

ifneq ($(BUILD_STATIC), 0)
	include $(BUILD_STATIC)/jg-static.mk
	BASE := $(NAME:%-jg=%)
	INSTALLDIR := $(NAME)
	SHARE_DEST := $(BASE)
	DEFINES += -DJGRF_STATIC
	LIBS += -L$(BUILD_STATIC) -l$(BASE)-jg $(LIBS_STATIC)
	EXE := $(NAME)/$(NAME)
	TARGET := $(EXE)
	ifneq ($(ASSETS),)
		ASSETS_PATH := $(ASSETS:%=$(BUILD_STATIC)/%)
		ASSETS_TARGET := $(ASSETS:%=$(NAME)/%)
		TARGET += $(ASSETS_TARGET)
	endif
	DESKTOP_TARGET := $(NAME)/$(NAME).desktop
	ifneq ($(ICONS),)
		ICONS_DIR := $(BUILD_STATIC)/icons
		ICONS_NAME := $(BASE)
		ICONS_PATH := $(ICONS:%=$(ICONS_DIR)/%)
	else
		ICONS_DIR := $(SOURCEDIR)/icons
		ICONS_NAME := jollygood
		ICONS_PATH := $(wildcard $(ICONS_DIR)/*.png) \
			$(ICONS_DIR)/$(ICONS_NAME).svg
		ICONS_BASE := $(notdir $(ICONS_PATH))
		ICONS := $(subst $(ICONS_NAME),$(BASE),$(ICONS_BASE))
	endif
	ICONS_SRC = $(subst icons/$(BASE),icons/$(ICONS_NAME),$@)
	ICONS_CPY = $(subst $(NAME)/icons,$(ICONS_DIR),$(ICONS_SRC))
	ICONS_TARGET := $(ICONS:%=$(NAME)/icons/%)
	SHADERS := $(wildcard $(SOURCEDIR)/shaders/*.fs) \
		$(wildcard $(SOURCEDIR)/shaders/*.vs)
	SHADERS_BASE := $(notdir $(SHADERS))
	SHADERS_TARGET := $(SHADERS_BASE:%=$(NAME)/shaders/%)
	TARGET += $(DESKTOP_TARGET) $(ICONS_TARGET) $(SHADERS_TARGET)
else
	NAME := jollygood
	BASE := $(NAME)
	EXE := $(NAME)
	INSTALLDIR := $(SOURCEDIR)
	SHARE_DEST := jgrf
	TARGET := $(NAME)
endif

ICONS_INSTALL_DIR = $(DATAROOTDIR)/icons/hicolor/$${i}x$${i}/apps
SHADER_INSTALL_DIR := $(DATADIR)/jollygood/$(SHARE_DEST)/shaders

MKDIRS :=

CSRCS := jgrf.c \
	audio.c \
	cheats.c \
	cli.c \
	input.c \
	menu.c \
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
	CFLAGS_MD5 = $(shell $(PKG_CONFIG) --cflags libcrypto)
	LIBS_MD5 = $(shell $(PKG_CONFIG) --libs libcrypto)
endif

include $(SOURCEDIR)/mk/miniz.mk

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

.PHONY: all clean install install-bin install-data install-docs install-man \
	install-strip uninstall

all: $(TARGET)

$(OBJDIR)/%.o: $(SOURCEDIR)/src/%.c $(OBJDIR)/.tag
	$(call COMPILE_INFO,$(BUILD_MAIN))
	@$(BUILD_MAIN)

$(OBJDIR)/deps/%.o: $(DEPDIR)/%.c $(OBJDIR)/.tag
	$(call COMPILE_INFO,$(BUILD_DEPS))
	@$(BUILD_DEPS)

$(OBJDIR)/.tag:
	@mkdir -p -- $(if $(MKDIRS),$(MKDIRS:%=$(OBJDIR)/%),$(OBJDIR)/deps)
	@touch $@

$(EXE): $(OBJS)
ifneq ($(BUILD_STATIC), 0)
	@mkdir -p $(NAME)
endif
	$(strip $(CC) $^ $(LDFLAGS) $(LIBS) -o $@)

ifneq ($(BUILD_STATIC), 0)
ifneq ($(ASSETS),)
$(ASSETS_TARGET): $(ASSETS_PATH)
	@mkdir -p $(NAME)
	@cp $(subst $(NAME),$(BUILD_STATIC),$@) $(NAME)/
endif

$(DESKTOP_TARGET): $(BUILD_STATIC)/$(NAME).desktop
	@mkdir -p $(NAME)
	@cp $< $@

$(ICONS_TARGET): $(ICONS_PATH)
	@mkdir -p $(NAME)/icons
	@cp $(ICONS_CPY) $(NAME)/icons/$(notdir $@)

$(SHADERS_TARGET): $(SHADERS)
	@mkdir -p $(NAME)/shaders
	@cp $(subst $(NAME),$(SOURCEDIR),$@) $(NAME)/shaders
endif

clean:
	rm -rf $(OBJDIR) $(NAME)

install-bin: all
	@mkdir -p $(DESTDIR)$(BINDIR)
	cp $(EXE) $(DESTDIR)$(BINDIR)

install-data: all
	@mkdir -p $(DESTDIR)$(SHADER_INSTALL_DIR)
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/applications
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps
	@mkdir -p $(DESTDIR)$(DATAROOTDIR)/pixmaps
	cp $(INSTALLDIR)/$(NAME).desktop $(DESTDIR)$(DATAROOTDIR)/applications
	cp $(INSTALLDIR)/shaders/default.vs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/default.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/aann.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/crt-yee64.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/crtea.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/lcd.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	cp $(INSTALLDIR)/shaders/sharp-bilinear.fs $(DESTDIR)$(SHADER_INSTALL_DIR)
	for i in 32 48 64 96 128 256 512 1024; do \
		mkdir -p $(DESTDIR)$(ICONS_INSTALL_DIR); \
		if test "$$i" = '96' || test "$$i" = '1024'; then \
			cp $(INSTALLDIR)/icons/$(BASE)$$i.png \
				$(DESTDIR)$(DATADIR)/jollygood/$(SHARE_DEST)/; \
		fi; \
		cp $(INSTALLDIR)/icons/$(BASE)$$i.png \
			$(DESTDIR)$(ICONS_INSTALL_DIR)/$(BASE).png; \
	done
	cp $(INSTALLDIR)/icons/$(BASE).svg $(DESTDIR)$(DATAROOTDIR)/pixmaps/
	cp $(INSTALLDIR)/icons/$(BASE).svg \
		$(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/
ifneq ($(BUILD_STATIC), 0)
ifneq ($(ASSETS),)
	@mkdir -p $(DESTDIR)$(DATADIR)/jollygood/$(SHARE_DEST)
	for a in $(ASSETS_TARGET); do \
		cp $$a $(DESTDIR)$(DATADIR)/jollygood/$(SHARE_DEST)/; \
	done
endif
endif

install-docs:: all
	@mkdir -p $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/ChangeLog $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/LICENSE $(DESTDIR)$(DOCDIR)
	cp $(SOURCEDIR)/README $(DESTDIR)$(DOCDIR)
ifeq ($(USE_EXTERNAL_MINIZ), 0)
	cp $(DEPDIR)/miniz/LICENSE $(DESTDIR)$(DOCDIR)/LICENSE-miniz
endif

install-man: all
	@mkdir -p $(DESTDIR)$(MANDIR)/man6
	cp $(SOURCEDIR)/jollygood.6 $(DESTDIR)$(MANDIR)/man6

install: install-bin install-data install-docs install-man

install-strip: install
	strip $(DESTDIR)$(BINDIR)/$(NAME)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -rf $(DESTDIR)$(DATADIR)/jollygood/$(SHARE_DEST)
	rm -f $(DESTDIR)$(DATAROOTDIR)/applications/$(NAME).desktop
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/32x32/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/48x48/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/64x64/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/96x96/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/128x128/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/256x256/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/512x512/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/1024x1024/apps/$(BASE).png
	rm -f $(DESTDIR)$(DATAROOTDIR)/icons/hicolor/scalable/apps/$(BASE).svg
	rm -f $(DESTDIR)$(DATAROOTDIR)/pixmaps/$(BASE).svg
	rm -f $(DESTDIR)$(MANDIR)/man6/jollygood.6
