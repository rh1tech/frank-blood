#!/bin/bash
#
# frank-blood — Blood for RP2350
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-blood
# SPDX-License-Identifier: GPL-2.0-or-later
#
# release.sh — build the release firmware.
#
# Usage: ./release.sh [VERSION] [--publish]
#   VERSION    e.g. "1.00"; asked for when omitted (default: the next minor)
#   --publish  also tag vVERSION, push the tag and create a GitHub release
#              with the UF2s (gh CLI); without it the files stay in release/
#
# Output: release/core2_frank-blood_A_BB.uf2 (FRANK Core 2, the master chip)
#         release/m2_frank-blood_A_BB.uf2    (Murmulator / FRANK M2)
# Both: 504 MHz CPU, flash 100.8 MHz; PSRAM 126 MHz (Core 2) or 168 MHz (M2);
# the PSRAM chip select (GPIO 47 on an RP2350B, 8 on an RP2350A) is detected.
# No game data is included: the player copies Blood 1.21 to /blood on the SD
# card (tools/pack_data.py makes a ZIP of the files needed).
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Build matrix: "FRANK_BOARD:prefix"
BUILD_MATRIX=(
    "CORE2:core2_"
    "M2:m2_"
)

PUBLISH=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --publish) PUBLISH=1 ;;
        *) ARGS+=("$a") ;;
    esac
done

if [ -z "$PICO_SDK_PATH" ] || [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    export PICO_SDK_PATH="$HOME/pico/pico-sdk"
fi

VERSION_FILE="version.txt"
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

echo ""
echo -e "${CYAN}frank-blood release builder${NC}"
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo -e "Variants:     ${CYAN}${#BUILD_MATRIX[@]}${NC} (Core 2, M2)"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
if [[ ${#ARGS[@]} -gt 0 ]]; then
    INPUT_VERSION="${ARGS[0]}"
    echo -e "Version (from command line): ${CYAN}${INPUT_VERSION}${NC}"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))
if [[ $MAJOR -lt 0 || $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: bad version ${INPUT_VERSION}${NC}"
    exit 1
fi

VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo -e "${GREEN}Building release version: ${VERSION_DOT}${NC}"
echo "$MAJOR $MINOR" > "$VERSION_FILE"

RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"
BUILD_DIR="$SCRIPT_DIR/build/release"

SUCCEEDED=()
FAILED=()
for ENTRY in "${BUILD_MATRIX[@]}"; do
    IFS=':' read -r BOARD PREFIX <<< "$ENTRY"
    OUTPUT_NAME="${PREFIX}frank-blood_${VERSION}.uf2"
    echo ""
    echo -e "${CYAN}Building: $OUTPUT_NAME${NC}"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    if cmake -S master -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DFRANK_BOARD="$BOARD" > "$BUILD_DIR/../release-cmake.log" 2>&1 &&
       cmake --build "$BUILD_DIR" > "$BUILD_DIR/../release-build.log" 2>&1 &&
       [[ -f "$BUILD_DIR/frank-blood-master.uf2" ]]; then
        cp "$BUILD_DIR/frank-blood-master.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
        echo -e "  ${GREEN}ok${NC} -> release/$OUTPUT_NAME"
        SUCCEEDED+=("$OUTPUT_NAME")
    else
        echo -e "  ${RED}failed${NC} (see build/release-*.log)"
        FAILED+=("$BOARD")
    fi
done
[[ ${#FAILED[@]} -eq 0 ]] && rm -rf "$BUILD_DIR"

echo ""
for FNAME in "${SUCCEEDED[@]}"; do
    ls -la "$RELEASE_DIR/$FNAME" | awk '{printf "  %-40s (%s bytes)\n", $9, $5}'
done
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo -e "${RED}${#FAILED[@]} variant(s) failed${NC}"
    exit 1
fi

if [[ $PUBLISH -eq 1 ]]; then
    TAG="v${VERSION_DOT}"
    NOTES="$RELEASE_DIR/notes_${VERSION}.md"
    cat > "$NOTES" <<NOTES_EOF
Blood (Monolith, 1997) for the FRANK Core 2 and the Murmulator / FRANK M2 (RP2350, 8 MB PSRAM, HDMI, I2S sound, USB keyboard, mouse and gamepad).

**Install**

1. Pick the firmware for your board: \`${SUCCEEDED[0]}\` (FRANK Core 2, flash the master chip)
   or \`${SUCCEEDED[1]}\` (M2). Hold BOOTSEL, plug the board in and copy it to the RP2350
   drive (or \`picotool load -f FILE.uf2\`).
2. Copy the Blood 1.21 / One Unit Whole Blood data to \`/blood\` on the SD card (FAT32 or
   exFAT): BLOOD.RFF, SOUNDS.RFF, GUI.RFF, BLOOD.INI, SURFACE.DAT, VOXEL.DAT,
   TILES000-017.ART (TABLES.DAT and the BLOOD00x.DEM demos optional).
3. Start the board.

No game data is included.
NOTES_EOF
    git add "$VERSION_FILE"
    git commit -m "Release ${VERSION_DOT}" -- "$VERSION_FILE" || true
    git tag -a "$TAG" -m "frank-blood ${VERSION_DOT}"
    git push origin HEAD "$TAG"
    gh release create "$TAG" "${SUCCEEDED[@]/#/$RELEASE_DIR/}" --title "frank-blood ${VERSION_DOT}" --notes-file "$NOTES"
    echo -e "${GREEN}Published ${TAG}${NC}"
fi
