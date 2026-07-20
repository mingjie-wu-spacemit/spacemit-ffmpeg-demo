#!/bin/bash
# Test script for SpaceMIT FFmpeg hardware decoding demos

set -e

echo "==================================="
echo "SpaceMIT FFmpeg Hardware Decode Test"
echo "==================================="
echo ""

# Check if running as root or in video group
if [ "$EUID" -ne 0 ] && ! groups | grep -q video; then
    echo "WARNING: You may need to run with sudo or add your user to the video group:"
    echo "  sudo usermod -a -G video \$USER"
    echo "  # Then log out and log back in"
    echo ""
fi

# Generate test videos if they don't exist
if [ ! -f test_720p.mp4 ]; then
    echo "Generating H.264 test video (720p, 2 seconds)..."
    ffmpeg -f lavfi -i testsrc=duration=2:size=1280x720:rate=30 \
        -c:v libx264 -pix_fmt yuv420p test_720p.mp4 -y -loglevel warning
    echo "✓ test_720p.mp4 created"
fi

if [ ! -f test_mjpeg_420.avi ]; then
    echo "Generating MJPEG test video (480p, 1 second)..."
    ffmpeg -f lavfi -i testsrc=duration=1:size=640x480:rate=30 \
        -c:v mjpeg -pix_fmt yuvj420p -q:v 2 test_mjpeg_420.avi -y -loglevel warning
    echo "✓ test_mjpeg_420.avi created"
fi

echo ""
echo "==================================="
echo "Test 1: H.264 Hardware Decoding"
echo "==================================="
./demo_h264_decode test_720p.mp4 30

echo ""
echo "==================================="
echo "Test 2: MJPEG Hardware Decoding"
echo "==================================="
./demo_mjpeg_decode test_mjpeg_420.avi 10

echo ""
echo "==================================="
echo "All tests completed successfully!"
echo "==================================="
