#!/usr/bin/env bash
set -e

# Always work from the script's own directory
cd "$(dirname "$0")"

IGRAPHICS_DEPS_DIR="$PWD"
BUILD_DIR="$IGRAPHICS_DEPS_DIR/../Build"
DL_DIR="$BUILD_DIR/tmp"
SRC_DIR="$BUILD_DIR/src"
LOG_PATH="$BUILD_DIR"
LOG_NAME="download.log"

# Basename part of tarballs to download
FREETYPE_VERSION=freetype-2.13.3
PKGCONFIG_VERSION=pkg-config-0.28
EXPAT_VERSION=expat-2.2.5
PNG_VERSION=v1.6.35
ZLIB_VERSION=zlib-1.3.1
SKIA_VERSION=chrome/m130
#SKIA_VERSION=main

# URLs where tarballs of releases can be downloaded - no trailing slash
PNG_URL=https://github.com/glennrp/libpng/archive
ZLIB_URL=https://www.zlib.net
FREETYPE_URL=https://download.savannah.gnu.org/releases/freetype
SKIA_URL=https://github.com/google/skia.git

echo "IGRAPHICS_DEPS_DIR: $IGRAPHICS_DEPS_DIR"
echo "BUILD_DIR: $BUILD_DIR"
echo "DL_DIR: $DL_DIR"
echo "LOG_PATH: $LOG_PATH"
echo "LOG_NAME: $LOG_NAME"

err_report() {
    echo
    echo "*******************************************************************************"
    echo "Error: something went wrong during the download process, printing $LOG_NAME"
    echo "*******************************************************************************"
    echo
    cat "$LOG_PATH/$LOG_NAME"
}
trap err_report ERR

spin() {
    pid=$!
    spin='-\|/'
    while kill -0 $pid 2>/dev/null; do
        printf " [%c]  " "$spin"
        spin=${spin#?}${spin%"${spin#?}"}
        sleep .1
        printf "\b\b\b\b\b\b"
    done
    printf "    \b\b\b\b"
}

echo
echo "###################################################################################"
echo
echo "     This script will download source packages and repos for the libraries required"
echo "     for IGraphics. Please relax and have a cup of tea—it'll take a while..."
echo
echo "###################################################################################"
echo

mkdir -p "$BUILD_DIR" "$DL_DIR" "$SRC_DIR"
cd "$DL_DIR"

if [ -e "$LOG_PATH/$LOG_NAME" ]; then
    rm "$LOG_PATH/$LOG_NAME"
else
    touch "$LOG_PATH/$LOG_NAME"
fi

#######################################################################
# zlib
if [ -d "$SRC_DIR/zlib" ]; then
  echo "Found zlib"
else
  echo "Downloading zlib"
  curl -L --progress-bar -O $ZLIB_URL/$ZLIB_VERSION.tar.gz
  echo "Unpacking..."
  tar -xf $ZLIB_VERSION.tar.gz
  mv $ZLIB_VERSION "$SRC_DIR/zlib"
fi

#######################################################################
# libpng
if [ -d "$SRC_DIR/libpng" ]; then
  echo "Found libpng"
else
  echo "Downloading libpng"
  curl -L --progress-bar -O $PNG_URL/$PNG_VERSION.tar.gz
  echo "Unpacking..."
  tar -xf $PNG_VERSION.tar.gz
  mv libpng* "$SRC_DIR/libpng"
  echo "Copying pnglibconf.h"
  cp "$SRC_DIR/libpng/scripts/pnglibconf.h.prebuilt" "$SRC_DIR/libpng/pnglibconf.h"
fi

#######################################################################
# freetype
if [ -d "$SRC_DIR/freetype" ]; then
  echo "Found freetype"
else
  echo "Downloading freetype"
  curl --progress-bar -OL --disable-epsv $FREETYPE_URL/$FREETYPE_VERSION.tar.gz
  echo "Unpacking..."
  tar -xf $FREETYPE_VERSION.tar.gz
  mv $FREETYPE_VERSION "$SRC_DIR/freetype"
fi

#######################################################################
# skia
if [ -d "$SRC_DIR/skia" ]; then
  if git -C "$SRC_DIR/skia" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Found skia"
  else
    echo "Existing $SRC_DIR/skia is not a git repository."
    read -p "Delete and re-clone Skia? [y/N] " yn
    case $yn in
      [Yy]* ) rm -rf "$SRC_DIR/skia";;
      * ) echo "Cannot proceed without a clean Skia directory."; exit 1;;
    esac
  fi
fi

if [ ! -d "$SRC_DIR/skia" ]; then
  echo "Cloning Skia"
  git clone --depth 1 --branch $SKIA_VERSION $SKIA_URL "$SRC_DIR/skia"
  echo "Syncing Skia dependencies..."
  (
    cd "$SRC_DIR/skia"
    python tools/git-sync-deps
    # rm -rf .git  # Keep the Skia checkout as a git repository for easier updates
  )
fi

# Optional: remove temporary download directory
# rm -r "$DL_DIR"
