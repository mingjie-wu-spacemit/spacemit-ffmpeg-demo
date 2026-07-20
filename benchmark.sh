#!/bin/bash
# Performance benchmark using real 1080p test clips.
#
# Expects the sample media (not shipped in this repo) under a directory
# passed as $1, defaulting to ~/Videos:
#   h264_*.264                       H.264 1080p elementary stream
#   hevc_*.265                       HEVC  1080p elementary stream
#   mjpeg_*.mjpeg                    MJPEG 1080p stream
#   nv12_w1920_h1080_*.yuv           raw NV12 1080p frames (encoder input)
#
# Run with sudo (or as a member of the video group).

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
MEDIA="${1:-$HOME/Videos}"
DEC="$ROOT/decode/hw_decode"
ENC="$ROOT/encode/hw_encode"
OUT="$ROOT/testdata"
mkdir -p "$OUT"

W=1920
H=1080

# Locate sample files by pattern
H264=$(ls "$MEDIA"/h264_*.264 2>/dev/null | head -1)
HEVC=$(ls "$MEDIA"/hevc_*.265 2>/dev/null | head -1)
MJPEG=$(ls "$MEDIA"/mjpeg_*.mjpeg 2>/dev/null | head -1)
NV12=$(ls "$MEDIA"/nv12_w1920_h1080_*.yuv 2>/dev/null | head -1)

echo "==========================================="
echo "SpaceMIT Codec Benchmark (1080p real clips)"
echo "  Media dir: $MEDIA"
echo "==========================================="

[ -x "$DEC" ] || (cd "$ROOT" && make)

echo ""
echo "########## DECODE ##########"
for pair in "H.264:$H264" "HEVC:$HEVC" "MJPEG:$MJPEG"; do
    name="${pair%%:*}"; file="${pair#*:}"
    if [ -z "$file" ] || [ ! -f "$file" ]; then
        echo "--- DECODE $name: SKIPPED (file not found) ---"; continue
    fi
    echo "--- DECODE $name: $(basename "$file") ---"
    "$DEC" "$file" 100000 2>/dev/null | grep -E "decoder|frames decoded|Elapsed|Average FPS"
    echo ""
done

echo "########## ENCODE ##########"
# Determine how many NV12 frames are available (1920x1080 NV12 = 3110400 bytes/frame)
if [ -n "$NV12" ] && [ -f "$NV12" ]; then
    FSIZE=$(stat -c%s "$NV12")
    NFRAMES=$(( FSIZE / (W * H * 3 / 2) ))
    echo "NV12 source: $(basename "$NV12") ($NFRAMES frames)"
    echo ""
    for c in h264 hevc mjpeg; do
        echo "--- ENCODE $c (real NV12 input) ---"
        "$ENC" "$c" "$OUT/enc_$c.out" "$W" "$H" "$NFRAMES" "$NV12" \
            | grep -E "encoder|frames encoded|Elapsed|Average FPS"
        echo ""
    done
else
    echo "NV12 source not found; encoding synthesized frames instead"
    for c in h264 hevc mjpeg; do
        echo "--- ENCODE $c (synthesized) ---"
        "$ENC" "$c" "$OUT/enc_$c.out" "$W" "$H" 120 \
            | grep -E "encoder|frames encoded|Elapsed|Average FPS"
        echo ""
    done
fi

echo "==========================================="
echo "Benchmark complete."
echo "==========================================="
