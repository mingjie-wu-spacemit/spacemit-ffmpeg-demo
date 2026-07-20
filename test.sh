#!/bin/bash
# Test script for SpaceMIT FFmpeg hardware encode/decode demos.
# Exercises H.264, HEVC, and MJPEG for both encoding and decoding,
# and reports the measured frame rate for each.

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEC="$ROOT/decode/hw_decode"
ENC="$ROOT/encode/hw_encode"
WORK="$ROOT/testdata"
mkdir -p "$WORK"

WIDTH=1280
HEIGHT=720
FRAMES=300

echo "==========================================="
echo "SpaceMIT FFmpeg Hardware Codec Test"
echo "  Resolution: ${WIDTH}x${HEIGHT}, ${FRAMES} frames"
echo "==========================================="

# Build if needed
[ -x "$DEC" ] || $(cd "$ROOT" && make)
[ -x "$ENC" ] || $(cd "$ROOT" && make)

run_encode() {
    local codec=$1 ext=$2
    echo ""
    echo "--- ENCODE: $codec ---"
    "$ENC" "$codec" "$WORK/out_${codec}.${ext}" "$WIDTH" "$HEIGHT" "$FRAMES" \
        | grep -E "encoder|FPS|frames|Elapsed|Output"
}

run_decode() {
    local codec=$1 file=$2
    echo ""
    echo "--- DECODE: $codec ---"
    "$DEC" "$file" "$FRAMES" 2>/dev/null \
        | grep -E "decoder|FPS|frames|Elapsed|format"
}

# --- Encode tests (produce streams we then decode) ---
run_encode h264  h264
run_encode hevc  h265
run_encode mjpeg mjpeg

# --- Decode tests (reuse the encoded elementary streams) ---
run_decode h264  "$WORK/out_h264.h264"
run_decode hevc  "$WORK/out_hevc.h265"
run_decode mjpeg "$WORK/out_mjpeg.mjpeg"

echo ""
echo "==========================================="
echo "All encode/decode tests completed."
echo "==========================================="
