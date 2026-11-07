#!/bin/bash
# Builds the Bambu Agent Studio icons by recoloring the official Bambu Studio icons
# from green (#00AE42) to violet (#7C3AED). Shape, padding and every size stay identical.
#
# The official icons contain only green, white and blends of the two (anti-aliased edges).
# Green has red = 0, so a pixel's red value tells how much white it has:
#   pixel = white - k * (white - green), with k = 1 - red.
# Each pixel is rebuilt with the same k toward violet, so edges stay smooth with no green fringe.
#
# Outputs (committed, rerun only when the official icons change):
#   resources/BambuAgentStudio.icns                     bundle icon (Finder, Spotlight, Dock)
#   resources/images/BambuAgentStudio-mac_256px.ico     Dock icon set at runtime by MainFrame
# Needs: ImageMagick 7 (magick), iconutil (macOS).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
RES="$REPO_DIR/resources"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# 1 - (violet channel / 255), i.e. how far each violet channel is from white.
VIOLET_R=0.513725  # 0x7C
VIOLET_G=0.772549  # 0x3A
VIOLET_B=0.070588  # 0xED

# Recolor one PNG. G and B are computed before R because both read the original red value.
recolor_png() {
    magick "$1" \
        -channel G -fx "1-(1-r)*$VIOLET_G" \
        -channel B -fx "1-(1-r)*$VIOLET_B" \
        -channel R -fx "1-(1-r)*$VIOLET_R" \
        +channel "PNG32:$2"
}

# Bundle icon: unpack every size, recolor, repack.
iconutil -c iconset "$RES/Icon.icns" -o "$WORK/official.iconset"
mkdir -p "$WORK/agent.iconset"
for png in "$WORK/official.iconset"/*.png; do
    recolor_png "$png" "$WORK/agent.iconset/$(basename "$png")"
done
iconutil -c icns "$WORK/agent.iconset" -o "$RES/BambuAgentStudio.icns"

# Runtime Dock icon: a single 256x256 PNG stored inside an .ico file, same layout as the
# official one (ImageMagick would write an uncompressed bitmap instead, so build it by hand).
recolor_png "$RES/images/BambuStudio-mac_256px.ico" "$WORK/dock.png"
png_size=$(stat -f %z "$WORK/dock.png")
le32() { printf "\\x$(printf %02x $(($1 & 255)))\\x$(printf %02x $(($1 >> 8 & 255)))\\x$(printf %02x $(($1 >> 16 & 255)))\\x$(printf %02x $(($1 >> 24 & 255)))"; }
{
    printf '\x00\x00\x01\x00\x01\x00'          # ICO header: type icon, 1 image
    printf '\x00\x00\x00\x00\x01\x00\x20\x00'  # 256x256 (stored as 0), no palette, 1 plane, 32 bpp
    le32 "$png_size"                           # image data size
    le32 22                                    # image data offset (6 + 16)
    cat "$WORK/dock.png"
} > "$RES/images/BambuAgentStudio-mac_256px.ico"

echo "Wrote $RES/BambuAgentStudio.icns"
echo "Wrote $RES/images/BambuAgentStudio-mac_256px.ico"
