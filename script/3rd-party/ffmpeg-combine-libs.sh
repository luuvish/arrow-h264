#!/bin/sh

trap exit ERR

ARCHS="armv6 armv7 i386"

BUILD_LIBS="libavcodec.a libavdevice.a libavfilter.a libavformat.a libavutil.a libswscale.a"

OUTPUT_DIR="ffmpeg-uarch"
mkdir -p $OUTPUT_DIR/lib
mkdir -p $OUTPUT_DIR/include
mkdir -p $OUTPUT_DIR/share

for LIB in $BUILD_LIBS; do 
  LIPO_CREATE=""
  for ARCH in $ARCHS; do
    LIPO_CREATE="$LIPO_CREATE-arch $ARCH ffmpeg-$ARCH/dist/lib/$LIB "
  done
  OUTPUT="$OUTPUT_DIR/lib/$LIB"
  echo "Creating: $OUTPUT"
  lipo -create $LIPO_CREATE -output $OUTPUT
  lipo -info $OUTPUT
done

echo "Copying headers from ffmpeg-i386..."
cp -R ffmpeg-i386/dist/include/* $OUTPUT_DIR/include

echo "Copying share from ffmpeg-i386..."
cp -R ffmpeg-i386/dist/share/* $OUTPUT_DIR/share

