# SpaceMIT FFmpeg Hardware Codec Demos

Hardware-accelerated video **encoding and decoding** demos for SpaceMIT boards
using FFmpeg with the custom `stcodec` codecs. Covers **H.264, HEVC, and MJPEG**.

## Repository Layout

```
spacemit-ffmpeg-demo/
├── decode/          # generic hardware decoder
│   ├── hw_decode.c
│   └── Makefile
├── encode/          # generic hardware encoder
│   ├── hw_encode.c
│   └── Makefile
├── Makefile         # builds both
├── test.sh          # runs all encode/decode combinations, reports FPS
└── README.md
```

## Hardware Support

- **Board**: SpaceMIT K1/K3 series with hardware video codec
- **Decoders**: `h264_stcodec`, `hevc_stcodec`, `mjpeg_stcodec`
- **Encoders**: `h264_stcodec`, `hevc_stcodec`, `mjpeg_stcodec`
- **Output/Input**: NV12, YUV420P, DRM PRIME (zero-copy), and more

## Prerequisites

- SpaceMIT board running Bianbu OS
- FFmpeg with SpaceMIT hardware codec support (`ffmpeg -codecs | grep stcodec`)
- SpaceMIT MPP library (`libspacemit_mpp.so`)
- Dev libraries to build:
  ```bash
  sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev
  ```

## Device Permissions

The hardware codec accesses `/dev/video*`, which requires membership in the
`video` group (or run with `sudo`):

```bash
sudo usermod -a -G video $USER
# Log out and log back in for the change to take effect
```

## Building

```bash
make            # builds decode/hw_decode and encode/hw_encode
```

## Decode Demo (`decode/hw_decode.c`)

Auto-selects the matching `<codec>_stcodec` decoder from the input stream's
codec id, decodes every frame, and reports the frame rate.

```bash
./decode/hw_decode <input_file> [max_frames]

# Examples
./decode/hw_decode video.mp4 300      # H.264/HEVC in a container
./decode/hw_decode clip.h265          # raw HEVC elementary stream
./decode/hw_decode motion.mjpeg       # MJPEG stream
```

## Encode Demo (`encode/hw_encode.c`)

Synthesizes animated NV12 frames in-process, encodes them with the selected
hardware encoder, writes the elementary stream to disk, and reports the frame
rate.

```bash
./encode/hw_encode <h264|hevc|mjpeg> <output_file> [width height frames]

# Examples
./encode/hw_encode h264  out.h264  1280 720 300
./encode/hw_encode hevc  out.h265  1920 1080 300
./encode/hw_encode mjpeg out.mjpeg 1280 720 300
```

## Quick Test

```bash
chmod +x test.sh
./test.sh          # or: sudo ./test.sh   (if not in the video group)
```

Runs all three encoders and then decodes the produced streams, printing FPS
for each.

To benchmark against **real 1080p clips**, place sample media under a directory
and run:

```bash
sudo ./benchmark.sh /path/to/media   # defaults to ~/Videos
```

Expected files: `h264_*.264`, `hevc_*.265`, `mjpeg_*.mjpeg`, and a raw
`nv12_w1920_h1080_*.yuv` (encoder input).

## Verified Results

Tested on SpaceMIT K1 board (Bianbu OS, FFmpeg 8.0.1) with **real 1920x1080
clips** (decode) and a **real NV12 1080p source** (encode, 120 frames):

| Codec | Decode FPS | Encode FPS |
|-------|-----------:|-----------:|
| H.264 | 222 | 132 |
| HEVC  | 364 | 194 |
| MJPEG | 228 | 255 |

Decode inputs: `h264_...54f...264`, `hevc_...200f...265`, `mjpeg_...120f...mjpeg`.
Encode input: `nv12_w1920_h1080_120f...yuv`. All paths confirmed running on the
hardware `stcodec` codecs.

A smaller 1280x720 synthesized run is also available via `test.sh`.

## Implementation Notes

- Decoder: `avcodec_find_decoder_by_name()` selects the hardware decoder by a
  codec-id → name mapping, with a software fallback.
- Encoder: uses `AV_PIX_FMT_NV12` input (natively supported by stcodec) and the
  standard `avcodec_send_frame`/`avcodec_receive_packet` loop.
- Both report frames, elapsed time, and average FPS.

## Common Issues

### 1. Codec not found
**Error**: `h264_stcodec ... not found`

**Solution**: Confirm FFmpeg has the SpaceMIT codecs:
```bash
ffmpeg -codecs | grep stcodec
```

### 2. MPP library missing
**Error**: `libspacemit_mpp.so.0: cannot open shared object file`

**Solution**:
```bash
ls -l /usr/lib/libspacemit_mpp.so*
sudo ldconfig
```

### 3. Permission denied on /dev/video*
Add your user to the `video` group (see Device Permissions) or run with `sudo`.

### 4. MJPEG input pixel format
The MJPEG hardware decoder requires yuv420p input. When producing MJPEG with
FFmpeg, encode with `-pix_fmt yuvj420p` (yuv444p is not supported).

## License

MIT License - see LICENSE file.
