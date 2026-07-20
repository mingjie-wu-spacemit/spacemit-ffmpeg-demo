/*
 * SpaceMIT H.264 Hardware Decoding Demo
 *
 * Demonstrates hardware-accelerated H.264 decoding using h264_stcodec decoder
 * with DRM PRIME zero-copy support.
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

static void print_frame_info(AVFrame *frame, int frame_count) {
    printf("Frame %4d: pts=%8ld, format=%s, size=%dx%d",
           frame_count,
           frame->pts,
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
        fprintf(stderr, "Example: %s video.mp4 100\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    int max_frames = (argc > 2) ? atoi(argv[2]) : -1;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    const AVCodec *decoder = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int video_stream_idx = -1;
    int ret = 0;
    int frame_count = 0;
    int64_t start_time, end_time;

    // Open input file
    ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input file '%s': %s\n",
                input_file, av_err2str(ret));
        return 1;
    }

    // Find stream info
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info: %s\n", av_err2str(ret));
        goto cleanup;
    }

    // Find video stream
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
           input_file,
           codecpar->width,
           codecpar->height,
           avcodec_get_name(codecpar->codec_id));

    // Find H.264 hardware decoder
    decoder = avcodec_find_decoder_by_name("h264_stcodec");
    if (!decoder) {
        fprintf(stderr, "h264_stcodec decoder not found, trying default h264 decoder\n");
        decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!decoder) {
            fprintf(stderr, "No H.264 decoder available\n");
            ret = -1;
            goto cleanup;
        }
    } else {
        printf("Using hardware decoder: h264_stcodec\n");
    }

    // Allocate decoder context
    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        fprintf(stderr, "Failed to allocate decoder context\n");
        ret = -1;
        goto cleanup;
    }

    // Copy codec parameters to context
    ret = avcodec_parameters_to_context(dec_ctx, codecpar);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters: %s\n", av_err2str(ret));
        goto cleanup;
    }

    // Open decoder
    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open decoder: %s\n", av_err2str(ret));
        goto cleanup;
    }

    printf("Decoder opened successfully\n");
    printf("Output pixel format: %s\n", av_get_pix_fmt_name(dec_ctx->pix_fmt));

    // Allocate packet and frame
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        fprintf(stderr, "Failed to allocate packet/frame\n");
        ret = -1;
        goto cleanup;
    }

    printf("\nStarting decode...\n");
    start_time = get_time_us();

    // Decode loop
    while (1) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // Flush decoder
                avcodec_send_packet(dec_ctx, NULL);
            } else {
                fprintf(stderr, "Error reading frame: %s\n", av_err2str(ret));
                break;
            }
        }

        if (ret >= 0 && pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        // Send packet to decoder
        if (ret >= 0) {
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
                av_packet_unref(pkt);
                break;
            }
            av_packet_unref(pkt);
        }

        // Receive decoded frames
        while (1) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                fprintf(stderr, "Error receiving frame: %s\n", av_err2str(ret));
                goto cleanup;
            }

            frame_count++;
            print_frame_info(frame, frame_count);

            av_frame_unref(frame);

            if (max_frames > 0 && frame_count >= max_frames) {
                goto decode_done;
            }
        }

        if (ret == AVERROR_EOF) {
            break;
        }
    }

decode_done:
    end_time = get_time_us();

    // Print statistics
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    printf("\n=== Decode Statistics ===\n");
    printf("Total frames decoded: %d\n", frame_count);
    printf("Elapsed time: %.3f seconds\n", elapsed_sec);
    if (elapsed_sec > 0) {
        printf("Average FPS: %.2f\n", frame_count / elapsed_sec);
    }
    printf("Decoder: %s\n", decoder->name);
    printf("Output format: %s\n", av_get_pix_fmt_name(dec_ctx->pix_fmt));

cleanup:
    if (frame)
        av_frame_free(&frame);
    if (pkt)
        av_packet_free(&pkt);
    if (dec_ctx)
        avcodec_free_context(&dec_ctx);
    if (fmt_ctx)
        avformat_close_input(&fmt_ctx);

    return ret < 0 ? 1 : 0;
}
