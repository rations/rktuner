# rktuner — chromatic tuner for RenkuOS: an LV2 plugin, and the rktuner app.
#
# Builds against the sibling LV2-renku checkout's stage (pkg-config against
# build/stage): the plugin needs only the lv2 headers; the app links that
# checkout's hosting core and audio backends (liblv2host.a, libjdbackend.a)
# and the lilv stack. The
# development loop runs entirely on the Haiku laptop:
#
#   make remote        # rsync to haikulaptop, build over ssh, install
#
# Never treat a Linux build as authoritative; the target is Haiku.

LV2_HAIKU ?= $(HOME)/LV2-haiku
STAGE := $(LV2_HAIKU)/build/stage
PKGCFG := PKG_CONFIG_PATH=$(STAGE)/lib/pkgconfig

BUILD_DIR ?= build
OUT := $(BUILD_DIR)/rktuner.lv2
LV2_INSTALL_DIR ?= $(HOME)/config/non-packaged/add-ons/media/LV2

CC ?= gcc
CXX ?= g++
WARN = -Wall -Wextra
UNAME := $(shell uname)

GUI_PNGS := $(wildcard gui/*.png)
BUNDLE_PNGS := $(patsubst gui/%.png,$(OUT)/gui/%.png,$(GUI_PNGS))
TTLS := $(OUT)/manifest.ttl $(OUT)/rktuner.ttl

VERSION := $(shell cat VERSION)
VERSION_PARTS := $(subst ., ,$(VERSION))
APP := $(BUILD_DIR)/rktuner
APP_SRCS := standalone/main.cpp standalone/mainwindow.cpp standalone/settings.cpp
APP_HDRS := standalone/mainwindow.h standalone/settings.h
APP_RSRC := $(BUILD_DIR)/rktuner.rsrc
HOST_LIBS := $(STAGE)/lib/liblv2host.a $(STAGE)/lib/libjdbackend.a
# The JACK add-on LV2-renku stages (when it found JACK's headers); copied to
# backends/ beside the app, where the backend loader looks in a build tree.
JACK_ADDON := $(if $(wildcard $(STAGE)/lib/jdbackend/jack.so),$(BUILD_DIR)/backends/jack.so)

all: build

ifeq ($(UNAME),Haiku)
# The native BView UI needs the Interface Kit (Haiku only).
build: check-stage $(OUT)/rktuner.so $(OUT)/rktuner_ui.so $(TTLS) $(BUNDLE_PNGS) $(APP) $(JACK_ADDON)
else
build: check-stage $(OUT)/rktuner.so $(TTLS) $(BUNDLE_PNGS)
endif

# The stage must exist before anything compiles; fail with a clear message
# instead of a cryptic pkg-config one.
check-stage:
	@test -f $(STAGE)/lib/pkgconfig/lv2.pc || { \
		echo "error: $(STAGE)/lib/pkgconfig/lv2.pc not found;" \
		     "build the LV2-haiku stack first (make in $(LV2_HAIKU))" >&2; \
		exit 1; }

$(OUT)/rktuner.so: src/rktuner.c
	@mkdir -p $(OUT)
	$(CC) -std=c99 $(WARN) -O2 -fPIC -shared -o $@ $< \
		`$(PKGCFG) pkg-config --cflags lv2` -lm

# Haiku only: the plugin's native BView UI (second .so in the bundle).
# Translation Kit (PNG loading) lives in libtranslation.
$(OUT)/rktuner_ui.so: src/rktuner_ui.cpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 $(WARN) -O2 -fPIC -shared -o $@ $< \
		`$(PKGCFG) pkg-config --cflags lv2` -lbe -ltranslation

# ---- the app (Haiku only) ---------------------------------------------------

$(HOST_LIBS):
	@echo "error: $@ not found; build LV2-renku first (make in $(LV2_HAIKU))," \
	     "which stages the hosting core and audio backends" >&2; exit 1

# Resources (signature, launch flags, icon): the .rdef goes through the C
# preprocessor for its #include, then rc, as Haiku's makefile-engine does.
$(APP_RSRC): standalone/rktuner.rdef standalone/icons/rktuner_icon.rdef
	@mkdir -p $(BUILD_DIR)
	$(CC) -E -P -Istandalone -x c - < $< | grep -av '^#' | rc -o $@ -

$(APP): $(APP_SRCS) $(APP_HDRS) $(HOST_LIBS) $(APP_RSRC) VERSION
	@mkdir -p $(BUILD_DIR)
	$(CXX) -std=c++17 $(WARN) -O2 -g -DRKTUNER_VERSION='"$(VERSION)"' \
		-I$(STAGE)/develop/headers `$(PKGCFG) pkg-config --cflags lilv-0 zix-0 lv2` \
		-o $@ $(APP_SRCS) $(HOST_LIBS) `$(PKGCFG) pkg-config --static --libs lilv-0 zix-0` \
		-lbe -lmedia -lmidi2 -lsamplerate
	xres -o $@ $(APP_RSRC)
	setversion $@ -app $(word 1,$(VERSION_PARTS)) $(word 2,$(VERSION_PARTS)) \
		$(word 3,$(VERSION_PARTS)) -short $(VERSION) -long "rktuner $(VERSION)"
	mimeset -f $@

$(BUILD_DIR)/backends/jack.so: $(STAGE)/lib/jdbackend/jack.so
	@mkdir -p $(BUILD_DIR)/backends
	cp $< $@

$(OUT)/%.ttl: src/%.ttl
	@mkdir -p $(OUT)
	cp $< $@

$(OUT)/gui/%.png: gui/%.png
	@mkdir -p $(OUT)/gui
	cp $< $@

# ---- tests -----------------------------------------------------------------

# Offline detection test: links the plugin translation unit directly and
# drives it through the LV2 descriptor API with a synchronous worker.
$(BUILD_DIR)/test_detect: tests/test_detect.c src/rktuner.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c99 $(WARN) -O2 -o $@ tests/test_detect.c src/rktuner.c \
		`$(PKGCFG) pkg-config --cflags lv2` -lm

test: check-stage $(BUILD_DIR)/test_detect
	$(BUILD_DIR)/test_detect

# ---- install / validate ----------------------------------------------------

install: build
	mkdir -p $(LV2_INSTALL_DIR)
	rm -rf $(LV2_INSTALL_DIR)/rktuner.lv2
	cp -r $(OUT) $(LV2_INSTALL_DIR)/rktuner.lv2

validate:
	@echo "== lv2ls =="
	$(STAGE)/bin/lv2ls
	@echo "== lv2info rktuner =="
	$(STAGE)/bin/lv2info https://github.com/rations/rktuner

# ---- art (dev host, one-shot; outputs are committed) -----------------------

art:
	gui/make_background.sh
	gui/make_led_cells.sh
	gui/make_dots.sh
	gui/make_glyphs.sh

# ---- dev-host loop ---------------------------------------------------------

sync:
	rsync -a --delete --exclude build --exclude .git ./ haikulaptop:rktuner/

remote: sync
	ssh haikulaptop 'cd rktuner && make && make install'

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all build check-stage install validate art sync remote clean
