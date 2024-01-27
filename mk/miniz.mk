DIR_MINIZ := $(wildcard $(DEPDIR)/miniz)

ifneq ($(DIR_MINIZ),)
	USE_EXTERNAL_MINIZ ?= 0
else
	override USE_EXTERNAL_MINIZ := 1
endif

ifeq ($(USE_EXTERNAL_MINIZ), 0)
	CFLAGS_MINIZ := -I$(DIR_MINIZ)
	LIBS_MINIZ :=
	MKDIRS += deps/miniz
	SRCS_MINIZ := deps/miniz/miniz.c
	OBJS_MINIZ := $(SRCS_MINIZ:.c=.o)

install-docs::
	cp $(DIR_MINIZ)/LICENSE $(DESTDIR)$(DOCDIR)/LICENSE-miniz
else
	override REQUIRES_PRIVATE += miniz
	CFLAGS_MINIZ = $(shell $(PKG_CONFIG) --cflags miniz)
	LIBS_MINIZ = $(shell $(PKG_CONFIG) --libs miniz)
	OBJS_MINIZ :=
endif
