/*
 * SpaceMIT Hardware Video Encoding Demo
 *
 * Generic hardware-accelerated encoder using the SpaceMIT stcodec encoders
 * (h264_stcodec / hevc_stcodec / mjpeg_stcodec). It synthesizes raw NV12
 * test frames in-process (a moving gradient), feeds them to the hardware
 * encoder, writes the elementary stream to a file, and measures the encode
 * frame rate.
 *
 * Usage: hw_encode <h264|hevc|mjpeg> <output_file> [width height frames] [raw_nv12_input]
 * Examples:
 *   hw_encode h264 out.h264 1280 720 300                 # synthesized frames
 *   hw_encode h264 out.h264 1920 1080 120 input_nv12.yuv # real NV12 input
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <sys/time.h>

static int64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* Map a short codec keyword to the SpaceMIT hardware encoder name + codec id. */
static const char *stcodec_encoder_name(const char *key, enum AVCodecID *out_id) {
    if (!strcmp(key, "h264")) { *out_id = AV_CODEC_ID_H264;  return "h264_stcodec";  }
    if (!strcmp(key, "hevc")) { *out_id = AV_CODEC_ID_HEVC;  return "hevc_stcodec";  }
    if (!strcmp(key, "mjpeg")){ *out_id = AV_CODEC_ID_MJPEG; return "mjpeg_stcodec"; }
    return NULL;
}

/* Fill an NV12 frame with an animated gradient so the encoder gets real data. */
static void fill_nv12_frame(AVFrame *frame, int frame_index) {
    int x, y;
    /* Y plane */
    for (y = 0; y < frame->height; y++) {
        uint8_t *row = frame->data[0] + y * frame->linesize[0];
        for (x = 0; x < frame->width; x++)
            row[x] = (uint8_t)(x + y + frame_index * 3);
    }
    /* Interleaved UV plane (half resolution) */
    for (y = 0; y < frame->height / 2; y++) {
        uint8_t *row = frame->data[1] + y * frame->linesize[1];
        for (x = 0; x < frame->width / 2; x++) {
            row[2 * x]     = (uint8_t)(128 + x + frame_index * 2);  /* U */
            row[2 * x + 1] = (uint8_t)(128 + y - frame_index * 2);  /* V */
        }
    }
}

/* Read one planar/interleaved NV12 frame from a raw file into an AVFrame.
 * NV12 layout: Y plane (w*h) followed by interleaved UV plane (w*h/2).
 * Returns 0 on success, 1 on clean EOF, -1 on error. */
static int read_nv12_frame(FILE *in, AVFrame *frame) {
    int w = frame->width, h = frame->height;
    /* Y plane, row by row (respects linesize/stride) */
    for (int y = 0; y < h; y++) {
        if (fread(frame->data[0] + y * frame->linesize[0], 1, w, in) != (size_t)w)
            return feof(in) ? 1 : -1;
    }
    /* Interleaved UV plane at half height, full width of chroma samples */
    for (int y = 0; y < h / 2; y++) {
        if (fread(frame->data[1] + y * frame->linesize[1], 1, w, in) != (size_t)w)
            return feof(in) ? 1 : -1;
    }
    return 0;
}

static int write_packet(FILE *f, AVPacket *pkt) {
    return fwrite(pkt->data, 1, pkt->size, f) == (size_t)pkt->size ? 0 : -1;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <h264|hevc|mjpeg> <output_file> [width height frames]\n", argv[0]);
        fprintf(stderr, "Example: %s h264 out.h264 1280 720 300\n", argv[0]);
        return 1;
    }

    const char *codec_key = argv[1];
    const char *output_file = argv[2];
    int width  = (argc > 3) ? atoi(argv[3]) : 1280;
    int height = (argc > 4) ? atoi(argv[4]) : 720;
    int frames = (argc > 5) ? atoi(argv[5]) : 300;
    const char *input_file = (argc > 6) ? argv[6] : NULL;  /* raw NV12 source */

    enum AVCodecID codec_id;
    const char *enc_name = stcodec_encoder_name(codec_key, &codec_id);
    if (!enc_name) {
        fprintf(stderr, "Unsupported codec '%s' (use h264, hevc, or mjpeg)\n", codec_key);
        return 1;
    }

    const AVCodec *encoder = NULL;
    AVCodecContext *enc_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    FILE *out = NULL;
    FILE *in = NULL;
    int ret = 0;
    int encoded_count = 0;
    int64_t start_time = 0, end_time = 0;

    encoder = avcodec_find_encoder_by_name(enc_name);
    if (!encoder) {
        fprintf(stderr, "Hardware encoder '%s' not found\n", enc_name);
        return 1;
    }
    printf("Using hardware encoder: %s\n", enc_name);
    printf("Resolution: %dx%d, frames: %d\n", width, height, frames);
    printf("Input: %s\n", input_file ? input_file : "synthesized NV12");

    if (input_file) {
        in = fopen(input_file, "rb");
        if (!in) {
            fprintf(stderr, "Failed to open raw input '%s'\n", input_file);
            return 1;
        }
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        fprintf(stderr, "Failed to allocate encoder context\n");
        return 1;
    }

    enc_ctx->width = width;
    enc_ctx->height = height;
    enc_ctx->pix_fmt = AV_PIX_FMT_NV12;      /* natively supported by stcodec */
    enc_ctx->time_base = (AVRational){1, 30};
    enc_ctx->framerate = (AVRational){30, 1};
    enc_ctx->bit_rate = 4000000;             /* 4 Mbps (ignored by mjpeg) */
    enc_ctx->gop_size = 30;

    ret = avcodec_open2(enc_ctx, encoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open encoder: %s\n", av_err2str(ret));
        goto cleanup;
    }
    printf("Encoder opened successfully\n\n");

    out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output file '%s'\n", output_file);
        ret = -1;
        goto cleanup;
    }

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        fprintf(stderr, "Failed to allocate frame/packet\n");
        ret = -1;
        goto cleanup;
    }

    frame->format = enc_ctx->pix_fmt;
    frame->width = width;
    frame->height = height;
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to allocate frame buffer: %s\n", av_err2str(ret));
        goto cleanup;
    }

    printf("Starting encode...\n");
    start_time = get_time_us();

    for (int i = 0; i < frames; i++) {
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            goto cleanup;

        if (in) {
            int r = read_nv12_frame(in, frame);
            if (r == 1) {  /* reached end of raw input early */
                printf("Reached end of input after %d frames\n", i);
                break;
            } else if (r < 0) {
                fprintf(stderr, "Error reading raw NV12 frame %d\n", i);
                ret = -1;
                goto cleanup;
            }
        } else {
            fill_nv12_frame(frame, i);
        }
        frame->pts = i;

        ret = avcodec_send_frame(enc_ctx, frame);
        if (ret < 0) {
            fprintf(stderr, "Error sending frame to encoder: %s\n", av_err2str(ret));
            goto cleanup;
        }

        while (1) {
            ret = avcodec_receive_packet(enc_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0) {
                fprintf(stderr, "Error receiving packet: %s\n", av_err2str(ret));
                goto cleanup;
            }
            if (write_packet(out, pkt) < 0) {
                fprintf(stderr, "Failed to write packet\n");
                av_packet_unref(pkt);
                ret = -1;
                goto cleanup;
            }
            encoded_count++;
            av_packet_unref(pkt);
        }
    }

    /* Flush encoder */
    avcodec_send_frame(enc_ctx, NULL);
    while (1) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
            goto cleanup;
        write_packet(out, pkt);
        encoded_count++;
        av_packet_unref(pkt);
    }
    ret = 0;

    end_time = get_time_us();

    double elapsed_sec = (end_time - start_time) / 1000000.0;
    printf("\n=== Encode Statistics ===\n");
    printf("Total frames encoded: %d\n", encoded_count);
    printf("Elapsed time: %.3f seconds\n", elapsed_sec);
    if (elapsed_sec > 0)
        printf("Average FPS: %.2f\n", encoded_count / elapsed_sec);
    printf("Encoder: %s\n", encoder->name);
    printf("Output: %s\n", output_file);

cleanup:
    if (in)      fclose(in);
    if (out)     fclose(out);
    if (frame)   av_frame_free(&frame);
    if (pkt)     av_packet_free(&pkt);
    if (enc_ctx) avcodec_free_context(&enc_ctx);
    return ret < 0 ? 1 : 0;
}
