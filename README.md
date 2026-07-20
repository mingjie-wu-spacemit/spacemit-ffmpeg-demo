# SpaceMIT FFmpeg Hardware Decoding Demos

This repository contains hardware-accelerated video decoding demos for SpaceMIT boards using FFmpeg with the custom `h264_stcodec` and `mjpeg_stcodec` decoders.

## Hardware Support

- **Board**: SpaceMIT K1/M1 series with hardware video decoder
- **Decoders**: 
  - H.264 hardware decoder (`h264_stcodec`)
  - MJPEG hardware decoder (`mjpeg_stcodec`)
- **Output**: DRM PRIME (zero-copy) or software frames

## Prerequisites

- SpaceMIT board running Bianbu OS
- FFmpeg with SpaceMIT hardware decoder support
- SpaceMIT MPP library (libspacemit_mpp.so)

## Demos

### 1. H.264 Hardware Decoding (`demo_h264_decode.c`)

Decodes H.264 video using hardware acceleration and outputs decoded frames.

**Features**:
- Hardware-accelerated H.264 decoding
- DRM PRIME zero-copy support
- Frame-by-frame processing
- Statistics output (fps, frame count, decode time)

**Usage**:
```bash
./demo_h264_decode input.mp4 [output_frames]
```

### 2. MJPEG Hardware Decoding (`demo_mjpeg_decode.c`)

Decodes MJPEG video or image sequences using hardware acceleration.

**Features**:
- Hardware-accelerated MJPEG decoding
- Batch processing support
- Raw YUV output option

**Usage**:
```bash
./demo_mjpeg_decode input.mjpeg [output_frames]
```

## Building

Install FFmpeg development libraries first:
```bash
sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev
```

Then build:
```bash
make
```

This will compile both demos:
- `demo_h264_decode`
- `demo_mjpeg_decode`

## Device Permissions

The hardware decoder accesses `/dev/video*` devices, which require membership in the `video` group:

```bash
sudo usermod -a -G video $USER
# Log out and log back in for the change to take effect
```

Alternatively, run with `sudo`.

## Quick Test

Run the automated test script to verify both decoders:
```bash
chmod +x test.sh
./test.sh
```

This generates test videos and runs both demos.

## Example Workflows

### Decode H.264 video and save first 100 frames
```bash
./demo_h264_decode video.mp4 100
```

### Decode MJPEG stream
```bash
./demo_mjpeg_decode motion.mjpeg
```

### Performance Testing
```bash
# Decode entire video to measure hardware performance
./demo_h264_decode big_buck_bunny_1080p.mp4
```

## Verified Results

Tested on SpaceMIT K3 board (Bianbu OS, FFmpeg 8.0.1):

| Demo | Input | Resolution | Frames | Avg FPS |
|------|-------|-----------|--------|---------|
| H.264 | test_720p.mp4 | 1280x720 | 30 | 253 |
| MJPEG | test_mjpeg_420.avi | 640x480 | 10 | 155 |

Both decoders confirmed using hardware acceleration (`h264_stcodec` / `mjpeg_stcodec`).

> Note: MJPEG hardware decoder requires yuv420p input. yuv444p is not supported —
> encode with `-pix_fmt yuvj420p`.

## Implementation Notes

- Uses `avcodec_find_decoder_by_name()` to explicitly select hardware decoders
- Handles DRM PRIME descriptors for zero-copy output
- Properly manages MPP buffer lifecycle
- Includes pts (presentation timestamp) handling

## Common Issues

### 1. Decoder not found
**Error**: `Codec 'h264_stcodec' not found`

**Solution**: Ensure FFmpeg is built with SpaceMIT decoder support:
```bash
ffmpeg -codecs | grep stcodec
```

### 2. MPP library missing
**Error**: `libspacemit_mpp.so.0: cannot open shared object file`

**Solution**: Verify the SpaceMIT MPP library is present and registered:
```bash
ls -l /usr/lib/libspacemit_mpp.so*
sudo ldconfig
```

### 3. Slow decoding performance
Check if hardware decoder is actually being used:
```bash
# Should show h264_stcodec in use
./demo_h264_decode video.mp4 2>&1 | grep -i codec
```

## License

MIT License - See LICENSE file for details

## References

- [SpaceMIT MPP Documentation](https://github.com/spacemit)
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- SpaceMIT decoder implementation: `libavcodec/stcodecdec.c`
