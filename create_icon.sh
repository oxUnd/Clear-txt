#!/bin/bash
# Create icon for Clear app using ImageMagick
# Generate .icns file for macOS

set -e

ICONSET_DIR="Clear.iconset"
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# Create base 1024x1024 icon following Apple HIG guidelines
# Background must fill entire canvas (1024x1024) - no transparency
# Main content (checkmark) should be in safe area (10-20% margin from edges)
# According to Apple: https://developer.apple.com/design/human-interface-guidelines/app-icons
TEMP_ICON="$ICONSET_DIR/icon_1024.png"
magick -size 1024x1024 \
  gradient:"rgb(255,70,70)-rgb(255,220,60)" \
  -alpha off \
  -fill white \
  -stroke white \
  -strokewidth 200 \
  -draw "path 'M 250,450 L 512,750 L 800,250'" \
  -alpha off \
  "$TEMP_ICON"

# Generate all required sizes for iconset
echo "Generating icon sizes..."

# 16x16
magick "$TEMP_ICON" -resize 16x16 "$ICONSET_DIR/icon_16x16.png"

# 16x16@2x (32x32)
magick "$TEMP_ICON" -resize 32x32 "$ICONSET_DIR/icon_16x16@2x.png"

# 32x32
magick "$TEMP_ICON" -resize 32x32 "$ICONSET_DIR/icon_32x32.png"

# 32x32@2x (64x64)
magick "$TEMP_ICON" -resize 64x64 "$ICONSET_DIR/icon_32x32@2x.png"

# 128x128
magick "$TEMP_ICON" -resize 128x128 "$ICONSET_DIR/icon_128x128.png"

# 128x128@2x (256x256)
magick "$TEMP_ICON" -resize 256x256 "$ICONSET_DIR/icon_128x128@2x.png"

# 256x256
magick "$TEMP_ICON" -resize 256x256 "$ICONSET_DIR/icon_256x256.png"

# 256x256@2x (512x512)
magick "$TEMP_ICON" -resize 512x512 "$ICONSET_DIR/icon_256x256@2x.png"

# 512x512
magick "$TEMP_ICON" -resize 512x512 "$ICONSET_DIR/icon_512x512.png"

# 512x512@2x (1024x1024)
cp "$TEMP_ICON" "$ICONSET_DIR/icon_512x512@2x.png"

# Remove temporary 1024x1024 file
rm -f "$TEMP_ICON"

# Convert iconset to .icns
echo "Converting to .icns format..."
iconutil -c icns "$ICONSET_DIR" -o "Clear.icns"

# Clean up temporary iconset directory
rm -rf "$ICONSET_DIR"

echo "Icon file created: Clear.icns"
echo "✓ Icon generation complete"

