#!/bin/sh
# make-hpkg.sh — build the `rktuner` .hpkg on a running RenkuOS (x86_64): the
# rktuner app (with its Deskbar entry and its JACK add-on) and the plug-in
# bundle (DSP .so, native HaikuUI .so, TTLs and the GUI sprite PNGs) in
# add-ons/media/LV2/rktuner.lv2.
#
# Needs the sibling LV2-renku checkout built first (LV2_HAIKU, default
# $HOME/LV2-haiku): the plug-in compiles against its staged lv2 headers, the
# app links its hosting core and audio backends, and its JACK add-on is the
# one it stages. The LV2 spec bundles a host needs at runtime ship in the
# lv2_renku package.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=$(cat "$ROOT/VERSION")   # the only place the version lives
REVISION=1
STAGE_PKG="$HERE/stage"

# The system this is built on is the oldest the package may run on (as
# haikuporter does): the version of the installed haiku package.
HAIKU_PKG=$(ls /boot/system/packages/haiku-r*-x86_64.hpkg | head -n 1)
HAIKU_MIN=$(basename "$HAIKU_PKG" | sed -e 's/^haiku-//' -e 's/-[0-9]*-x86_64\.hpkg$//')

LV2_HAIKU=${LV2_HAIKU:-$HOME/LV2-haiku}
export LV2_HAIKU

pkgman install -y gcc make pkgconfig || true

cd "$ROOT"
rm -rf build "$STAGE_PKG"
make

# The UI is Haiku-only and built conditionally; a bundle without it would load
# but silently fall back to a generic UI, so fail loudly here instead.
test -f build/rktuner.lv2/rktuner_ui.so || {
    echo "error: build/rktuner.lv2/rktuner_ui.so missing — the native GUI did not build" >&2
    exit 1
}

test -f build/backends/jack.so || {
    echo "error: build/backends/jack.so missing — rebuild LV2-renku with JACK's headers" >&2
    exit 1
}

mkdir -p "$STAGE_PKG/add-ons/media/LV2" "$STAGE_PKG/apps" \
    "$STAGE_PKG/data/deskbar/menu/Applications" "$STAGE_PKG/add-ons/rktuner"
cp -r build/rktuner.lv2 "$STAGE_PKG/add-ons/media/LV2/"
cp build/rktuner "$STAGE_PKG/apps/rktuner"
ln -s ../../../../apps/rktuner "$STAGE_PKG/data/deskbar/menu/Applications/rktuner"
# The app looks for its JACK add-on in add-ons/rktuner/.
cp build/backends/jack.so "$STAGE_PKG/add-ons/rktuner/"

sed -e "s/@VERSION@/$VERSION/g" -e "s/@REVISION@/$REVISION/g" \
    -e "s/@HAIKU_MIN@/$HAIKU_MIN/g" \
    "$HERE/rktuner.PackageInfo" > "$STAGE_PKG/.PackageInfo"
OUT="$HERE/rktuner-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE_PKG" "$OUT"
echo ">> built $OUT"
