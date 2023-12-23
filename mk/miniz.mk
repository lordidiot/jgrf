USE_EXTERNAL_MINIZ ?= 0

ifeq ($(USE_EXTERNAL_MINIZ), 0)
	CFLAGS_MINIZ := -I$(DEPDIR)/miniz
	LIBS_MINIZ :=
	MKDIRS += deps/miniz
	CSRCS += deps/miniz/miniz.c
else
	override REQUIRES_PRIVATE += miniz
	CFLAGS_MINIZ = $(shell $(PKG_CONFIG) --cflags miniz)
	LIBS_MINIZ = $(shell $(PKG_CONFIG) --libs miniz)
endif
