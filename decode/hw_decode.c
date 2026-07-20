/*
 * SpaceMIT Hardware Video Decoding Demo
 *
 * Generic hardware-accelerated decoder that auto-selects the matching
 * SpaceMIT stcodec decoder (h264_stcodec / hevc_stcodec / mjpeg_stcodec)
 * based on the input stream's codec, then measures decode frame rate.
 *
 * Usage: hw_decode <input_file> [max_frames]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <sys/time.h>

static int64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* Map an FFmpeg codec id to its SpaceMIT hardware decoder name. */
static const char *stcodec_decoder_name(enum AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264:  return "h264_stcodec";
        case AV_CODEC_ID_HEVC:  return "hevc_stcodec";
        case AV_CODEC_ID_MJPEG: return "mjpeg_stcodec";
        default:                return NULL;
    }
}

static void print_frame_info(AVFrame *frame, int frame_count, int verbose) {
    if (!verbose)
        return;
    printf("Frame %4d: pts=%8ld, format=%s, size=%dx%d",
           frame_count,
           (long)frame->pts,
           av_get_pix_fmt_name(frame->format),
           frame->width,
           frame->height);

    if (frame->format == AV_PIX_FMT_DRM_PRIME) {
        AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
        printf(", DRM objects=%d, layers=%d",
               desc->nb_objects, desc->nb_layers);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file> [max_frames]\n", argv[0]);
        fprintf(stderr, "Supported codecs: H.264, HEVC, MJPEG\n");
        fprintf(stderr, "Example: %s video.mp4 100\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    int max_frames = (argc > 2) ? atoi(argv[2]) : -1;
    int verbose = 1;  /* per-frame log; set 0 for pure benchmark */

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    const AVCodec *decoder = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int video_stream_idx = -1;
    int ret = 0;
    int frame_count = 0;
    int64_t start_time = 0, end_time = 0;

    ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input file '%s': %s\n",
                input_file, av_err2str(ret));
        return 1;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info: %s\n", av_err2str(ret));
        goto cleanup;
    }

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx < 0) {
        fprintf(stderr, "No video stream found\n");
        ret = -1;
        goto cleanup;
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
    printf("Input video: %s, %dx%d, codec=%s\n",
           input_file, codecpar->width, codecpar->height,
           avcodec_get_name(codecpar->codec_id));

    /* Select the matching SpaceMIT hardware decoder. */
    const char *hw_name = stcodec_decoder_name(codecpar->codec_id);
    if (hw_name)
        decoder = avcodec_find_decoder_by_name(hw_name);

    if (!decoder) {
        fprintf(stderr, "%s not available, falling back to software decoder\n",
                hw_name ? hw_name : "hardware decoder");
        decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            fprintf(stderr, "No decoder available for this codec\n");
            ret = -1;
            goto cleanup;
        }
    } else {
        printf("Using hardware decoder: %s\n", hw_name);
    }

    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        fprintf(stderr, "Failed to allocate decoder context\n");
        ret = -1;
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(dec_ctx, codecpar);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters: %s\n", av_err2str(ret));
        goto cleanup;
    }

    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open decoder: %s\n", av_err2str(ret));
        goto cleanup;
    }

    printf("Decoder opened successfully\n");
    printf("Output pixel format: %s\n\n", av_get_pix_fmt_name(dec_ctx->pix_fmt));

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        fprintf(stderr, "Failed to allocate packet/frame\n");
        ret = -1;
        goto cleanup;
    }

    printf("Starting decode...\n");
    start_time = get_time_us();

    while (1) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                avcodec_send_packet(dec_ctx, NULL);  /* flush */
            } else {
                fprintf(stderr, "Error reading frame: %s\n", av_err2str(ret));
                break;
            }
        }

        if (ret >= 0 && pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        if (ret >= 0) {
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
                av_packet_unref(pkt);
                break;
            }
            av_packet_unref(pkt);
        }

        while (1) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0) {
                fprintf(stderr, "Error receiving frame: %s\n", av_err2str(ret));
                goto cleanup;
            }

            frame_count++;
            print_frame_info(frame, frame_count, verbose);
            av_frame_unref(frame);

            if (max_frames > 0 && frame_count >= max_frames)
                goto decode_done;
        }

        if (ret == AVERROR_EOF)
            break;
    }

decode_done:
    end_time = get_time_us();

    double elapsed_sec = (end_time - start_time) / 1000000.0;
    printf("\n=== Decode Statistics ===\n");
    printf("Total frames decoded: %d\n", frame_count);
    printf("Elapsed time: %.3f seconds\n", elapsed_sec);
    if (elapsed_sec > 0)
        printf("Average FPS: %.2f\n", frame_count / elapsed_sec);
    printf("Decoder: %s\n", decoder->name);
    printf("Output format: %s\n", av_get_pix_fmt_name(dec_ctx->pix_fmt));

cleanup:
    if (frame)   av_frame_free(&frame);
    if (pkt)     av_packet_free(&pkt);
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    return ret < 0 ? 1 : 0;
}
