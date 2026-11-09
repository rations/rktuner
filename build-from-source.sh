#!/bin/sh
# One-shot build + install ON A RENKUOS MACHINE (run this inside RenkuOS, not
# on the dev host). Mirrors the sibling ports' build-from-source scripts.
#
#   ./build-from-source.sh
#
# Builds rktuner.lv2 and the rktuner app against the sibling LV2-renku
# checkout's stage, installs the bundle into the user's non-packaged add-ons
# dir (the default LV2_PATH picks it up) and the app, with its JACK add-on,
# into the user's non-packaged apps and add-ons dirs. Build LV2-renku first.

set -eu

LV2_HAIKU=${LV2_HAIKU:-$HOME/LV2-haiku}
export LV2_HAIKU

pkgman install -y pkgconfig gcc make

make
make install

APPS=$HOME/config/non-packaged/apps
mkdir -p "$APPS"
cp build/rktuner "$APPS/rktuner"
if [ -f build/backends/jack.so ]; then
	mkdir -p "$HOME/config/non-packaged/add-ons/rktuner"
	cp build/backends/jack.so "$HOME/config/non-packaged/add-ons/rktuner/jack.so"
fi

echo "done: bundle in $HOME/config/non-packaged/add-ons/media/LV2/rktuner.lv2"
echo "      app in $APPS/rktuner"
