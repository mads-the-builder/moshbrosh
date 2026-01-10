/*
 * MoshBrosh CLI - Standalone datamosh effect
 * Uses exact same algorithm as the Premiere Pro plugin
 *
 * Compile with:
 *   clang++ -std=c++17 -O2 moshbrosh_cli.cpp -o moshbrosh \
 *     -I/opt/homebrew/Cellar/ffmpeg/8.0.1/include \
 *     -L/opt/homebrew/Cellar/ffmpeg/8.0.1/lib \
 *     -lavformat -lavcodec -lavutil -lswscale
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// Configuration
struct MoshConfig {
    std::string inputFile;
    std::string outputFile;
    int moshFrame = 10;      // Frame where mosh effect starts
    int duration = 30;       // How many frames to mosh
    int blockSize = 16;      // Block size for motion estimation
    int searchRange = 16;    // Search range for motion vectors
    float blend = 1.0f;      // Blend amount (0-1)
};

// A single video frame stored as float RGBA
struct Frame {
    std::vector<float> pixels;  // RGBA interleaved, 4 floats per pixel
    int width = 0;
    int height = 0;
    bool valid = false;
};

// Motion vectors for one frame
struct FrameMotionVectors {
    std::vector<int16_t> dx;
    std::vector<int16_t> dy;
    int blocksX = 0;
    int blocksY = 0;
};

// Helper: clamp value to range
template<typename T>
inline T Clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}

// Compute luminance from RGBA pixel
inline float GetLuminance(const float* pixel) {
    return 0.299f * pixel[0] + 0.587f * pixel[1] + 0.114f * pixel[2];
}

// Compute motion vector for a single block using SAD (Sum of Absolute Differences)
// This is the EXACT same algorithm as the plugin
void ComputeBlockMotion(
    const float* current, const float* previous,
    int width, int height,
    int blockX, int blockY, int blockSize, int searchRange,
    int16_t& outDx, int16_t& outDy)
{
    int bestDx = 0, bestDy = 0;
    float bestSAD = 1e30f;

    int bx = blockX * blockSize;
    int by = blockY * blockSize;
    int rowFloats = width * 4;

    // Search in a grid pattern (step by 2 for speed, like plugin)
    for (int dy = -searchRange; dy <= searchRange; dy += 2) {
        for (int dx = -searchRange; dx <= searchRange; dx += 2) {
            float sad = 0.0f;

            for (int py = 0; py < blockSize && (by + py) < height; ++py) {
                int cy = by + py;
                int ry = cy + dy;
                if (ry < 0 || ry >= height) continue;

                for (int px = 0; px < blockSize && (bx + px) < width; ++px) {
                    int cx = bx + px;
                    int rx = cx + dx;
                    if (rx < 0 || rx >= width) continue;

                    // Compare luminance
                    float currLuma = GetLuminance(&current[(cy * rowFloats) + (cx * 4)]);
                    float prevLuma = GetLuminance(&previous[(ry * rowFloats) + (rx * 4)]);
                    sad += fabsf(currLuma - prevLuma);
                }
            }

            if (sad < bestSAD) {
                bestSAD = sad;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }

    outDx = static_cast<int16_t>(bestDx);
    outDy = static_cast<int16_t>(bestDy);
}

// Warp a frame using motion vectors (block-based, exactly like plugin)
void WarpFrameWithMotion(
    const std::vector<float>& source,
    const FrameMotionVectors& mvs,
    int width, int height, int blockSize,
    std::vector<float>& output)
{
    output.resize(width * height * 4);
    int rowFloats = width * 4;

    for (int by = 0; by < mvs.blocksY; ++by) {
        for (int bx = 0; bx < mvs.blocksX; ++bx) {
            int blockIdx = by * mvs.blocksX + bx;
            int16_t dx = mvs.dx[blockIdx];
            int16_t dy = mvs.dy[blockIdx];

            // Copy entire block with motion offset
            for (int py = 0; py < blockSize; ++py) {
                int dstY = by * blockSize + py;
                if (dstY >= height) continue;

                int srcY = Clamp(dstY + dy, 0, height - 1);

                for (int px = 0; px < blockSize; ++px) {
                    int dstX = bx * blockSize + px;
                    if (dstX >= width) continue;

                    int srcX = Clamp(dstX + dx, 0, width - 1);

                    int dstIdx = dstY * rowFloats + dstX * 4;
                    int srcIdx = srcY * rowFloats + srcX * 4;

                    output[dstIdx + 0] = source[srcIdx + 0];
                    output[dstIdx + 1] = source[srcIdx + 1];
                    output[dstIdx + 2] = source[srcIdx + 2];
                    output[dstIdx + 3] = source[srcIdx + 3];
                }
            }
        }
    }
}

// Convert AVFrame to our float RGBA format
void AVFrameToFloat(AVFrame* frame, SwsContext* swsCtx, Frame& outFrame) {
    int width = frame->width;
    int height = frame->height;

    outFrame.width = width;
    outFrame.height = height;
    outFrame.pixels.resize(width * height * 4);
    outFrame.valid = true;

    // Allocate temporary RGBA buffer
    uint8_t* rgbaData[1] = { new uint8_t[width * height * 4] };
    int rgbaLinesize[1] = { width * 4 };

    // Convert to RGBA
    sws_scale(swsCtx, frame->data, frame->linesize, 0, height,
              rgbaData, rgbaLinesize);

    // Convert to float (0-1 range)
    for (int i = 0; i < width * height * 4; ++i) {
        outFrame.pixels[i] = rgbaData[0][i] / 255.0f;
    }

    delete[] rgbaData[0];
}

// Convert our float RGBA to AVFrame
void FloatToAVFrame(const std::vector<float>& pixels, int width, int height,
                    SwsContext* swsCtx, AVFrame* outFrame) {
    // Convert float to uint8_t RGBA
    std::vector<uint8_t> rgbaData(width * height * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        rgbaData[i] = static_cast<uint8_t>(Clamp(pixels[i] * 255.0f, 0.0f, 255.0f));
    }

    uint8_t* srcData[1] = { rgbaData.data() };
    int srcLinesize[1] = { width * 4 };

    // Convert from RGBA to output format (YUV420P typically)
    sws_scale(swsCtx, srcData, srcLinesize, 0, height,
              outFrame->data, outFrame->linesize);
}

void PrintUsage(const char* progName) {
    fprintf(stderr, "MoshBrosh CLI - Datamosh Effect\n\n");
    fprintf(stderr, "Usage: %s [options] -i input.mp4 -o output.mp4\n\n", progName);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -i <file>      Input video file (required)\n");
    fprintf(stderr, "  -o <file>      Output video file (required)\n");
    fprintf(stderr, "  -f <frame>     Mosh start frame (default: 10)\n");
    fprintf(stderr, "  -d <frames>    Duration in frames (default: 30)\n");
    fprintf(stderr, "  -b <size>      Block size: 8, 16, or 32 (default: 16)\n");
    fprintf(stderr, "  -s <range>     Search range (default: 16)\n");
    fprintf(stderr, "  -m <blend>     Blend amount 0-100 (default: 100)\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s -i video.mp4 -o moshed.mp4 -f 30 -d 60 -b 16\n", progName);
}

bool ParseArgs(int argc, char* argv[], MoshConfig& config) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            config.inputFile = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            config.outputFile = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config.moshFrame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            config.duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            config.blockSize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config.searchRange = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            config.blend = atof(argv[++i]) / 100.0f;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return false;
        }
    }

    return !config.inputFile.empty() && !config.outputFile.empty();
}

int main(int argc, char* argv[]) {
    MoshConfig config;

    if (!ParseArgs(argc, argv, config)) {
        PrintUsage(argv[0]);
        return 1;
    }

    printf("MoshBrosh CLI\n");
    printf("Input:  %s\n", config.inputFile.c_str());
    printf("Output: %s\n", config.outputFile.c_str());
    printf("Mosh frame: %d, Duration: %d frames\n", config.moshFrame, config.duration);
    printf("Block size: %d, Search range: %d\n", config.blockSize, config.searchRange);
    printf("Blend: %.0f%%\n\n", config.blend * 100.0f);

    // Open input file
    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, config.inputFile.c_str(), nullptr, nullptr) < 0) {
        fprintf(stderr, "Error: Could not open input file '%s'\n", config.inputFile.c_str());
        return 1;
    }

    if (avformat_find_stream_info(inputCtx, nullptr) < 0) {
        fprintf(stderr, "Error: Could not find stream info\n");
        return 1;
    }

    // Find video stream
    int videoStreamIdx = -1;
    for (unsigned i = 0; i < inputCtx->nb_streams; ++i) {
        if (inputCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    if (videoStreamIdx < 0) {
        fprintf(stderr, "Error: No video stream found\n");
        return 1;
    }

    AVStream* inStream = inputCtx->streams[videoStreamIdx];
    AVCodecParameters* codecPar = inStream->codecpar;

    // Set up decoder
    const AVCodec* decoder = avcodec_find_decoder(codecPar->codec_id);
    if (!decoder) {
        fprintf(stderr, "Error: Could not find decoder\n");
        return 1;
    }

    AVCodecContext* decoderCtx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoderCtx, codecPar);

    if (avcodec_open2(decoderCtx, decoder, nullptr) < 0) {
        fprintf(stderr, "Error: Could not open decoder\n");
        return 1;
    }

    int width = decoderCtx->width;
    int height = decoderCtx->height;

    printf("Video: %dx%d\n", width, height);

    // Set up output file
    AVFormatContext* outputCtx = nullptr;
    avformat_alloc_output_context2(&outputCtx, nullptr, nullptr, config.outputFile.c_str());
    if (!outputCtx) {
        fprintf(stderr, "Error: Could not create output context\n");
        return 1;
    }

    // Set up encoder (H.264)
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        fprintf(stderr, "Error: Could not find H264 encoder\n");
        return 1;
    }

    AVStream* outStream = avformat_new_stream(outputCtx, nullptr);
    AVCodecContext* encoderCtx = avcodec_alloc_context3(encoder);

    encoderCtx->width = width;
    encoderCtx->height = height;
    encoderCtx->time_base = inStream->time_base;
    encoderCtx->framerate = av_guess_frame_rate(inputCtx, inStream, nullptr);
    encoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoderCtx->bit_rate = 4000000;  // 4 Mbps

    if (outputCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        encoderCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(encoderCtx, encoder, nullptr) < 0) {
        fprintf(stderr, "Error: Could not open encoder\n");
        return 1;
    }

    avcodec_parameters_from_context(outStream->codecpar, encoderCtx);
    outStream->time_base = encoderCtx->time_base;

    // Open output file
    if (!(outputCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputCtx->pb, config.outputFile.c_str(), AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Error: Could not open output file\n");
            return 1;
        }
    }

    if (avformat_write_header(outputCtx, nullptr) < 0) {
        fprintf(stderr, "Error: Could not write header\n");
        return 1;
    }

    // Set up pixel format converters
    SwsContext* toRGBA = sws_getContext(width, height, decoderCtx->pix_fmt,
                                         width, height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);

    SwsContext* fromRGBA = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                                           width, height, AV_PIX_FMT_YUV420P,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);

    // PASS 1: Read all frames into memory
    printf("Pass 1: Reading frames...\n");

    std::vector<Frame> frames;
    AVPacket* packet = av_packet_alloc();
    AVFrame* avFrame = av_frame_alloc();

    while (av_read_frame(inputCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIdx) {
            if (avcodec_send_packet(decoderCtx, packet) >= 0) {
                while (avcodec_receive_frame(decoderCtx, avFrame) >= 0) {
                    Frame f;
                    AVFrameToFloat(avFrame, toRGBA, f);
                    frames.push_back(std::move(f));

                    if (frames.size() % 30 == 0) {
                        printf("  Read %zu frames\r", frames.size());
                        fflush(stdout);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush decoder
    avcodec_send_packet(decoderCtx, nullptr);
    while (avcodec_receive_frame(decoderCtx, avFrame) >= 0) {
        Frame f;
        AVFrameToFloat(avFrame, toRGBA, f);
        frames.push_back(std::move(f));
    }

    printf("\nRead %zu frames total\n", frames.size());

    if (frames.empty()) {
        fprintf(stderr, "Error: No frames read\n");
        return 1;
    }

    // Validate mosh parameters
    int totalFrames = static_cast<int>(frames.size());
    if (config.moshFrame >= totalFrames) {
        fprintf(stderr, "Warning: moshFrame (%d) >= totalFrames (%d), adjusting\n",
                config.moshFrame, totalFrames);
        config.moshFrame = std::max(1, totalFrames - config.duration - 1);
    }
    if (config.moshFrame + config.duration > totalFrames) {
        config.duration = totalFrames - config.moshFrame;
        printf("Adjusted duration to %d frames\n", config.duration);
    }

    // PASS 2: Compute motion vectors for mosh range
    printf("\nPass 2: Computing motion vectors...\n");

    int blocksX = (width + config.blockSize - 1) / config.blockSize;
    int blocksY = (height + config.blockSize - 1) / config.blockSize;
    int numBlocks = blocksX * blocksY;

    printf("  Grid: %d x %d blocks (%d total)\n", blocksX, blocksY, numBlocks);

    std::vector<FrameMotionVectors> motionVectors(config.duration);

    for (int i = 0; i < config.duration; ++i) {
        int frameIdx = config.moshFrame + i;
        int prevIdx = frameIdx - 1;

        if (prevIdx < 0) prevIdx = 0;

        FrameMotionVectors& mvs = motionVectors[i];
        mvs.blocksX = blocksX;
        mvs.blocksY = blocksY;
        mvs.dx.resize(numBlocks);
        mvs.dy.resize(numBlocks);

        const float* curr = frames[frameIdx].pixels.data();
        const float* prev = frames[prevIdx].pixels.data();

        for (int by = 0; by < blocksY; ++by) {
            for (int bx = 0; bx < blocksX; ++bx) {
                int blockIdx = by * blocksX + bx;
                ComputeBlockMotion(curr, prev, width, height,
                                   bx, by, config.blockSize, config.searchRange,
                                   mvs.dx[blockIdx], mvs.dy[blockIdx]);
            }
        }

        printf("  Frame %d: computed motion vectors\r", frameIdx);
        fflush(stdout);
    }
    printf("\n");

    // PASS 3: Compute warped frames (accumulated)
    printf("\nPass 3: Computing warped frames (accumulated)...\n");

    int refFrameIdx = config.moshFrame - 1;
    if (refFrameIdx < 0) refFrameIdx = 0;

    printf("  Reference frame: %d\n", refFrameIdx);

    std::vector<std::vector<float>> warpedFrames(config.duration);

    // Start with reference frame
    std::vector<float> accumulated = frames[refFrameIdx].pixels;

    for (int i = 0; i < config.duration; ++i) {
        std::vector<float> warped;
        WarpFrameWithMotion(accumulated, motionVectors[i], width, height,
                           config.blockSize, warped);
        warpedFrames[i] = warped;
        accumulated = warped;  // Accumulate for next frame

        printf("  Frame %d: warped\r", config.moshFrame + i);
        fflush(stdout);
    }
    printf("\n");

    // PASS 4: Write output video
    printf("\nPass 4: Writing output video...\n");

    // Reset input to re-read for PTS
    av_seek_frame(inputCtx, videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(decoderCtx);

    AVFrame* outFrame = av_frame_alloc();
    outFrame->format = AV_PIX_FMT_YUV420P;
    outFrame->width = width;
    outFrame->height = height;
    av_frame_get_buffer(outFrame, 0);

    int frameNum = 0;
    int64_t pts = 0;

    for (size_t i = 0; i < frames.size(); ++i) {
        std::vector<float>* outputPixels = &frames[i].pixels;

        // Check if this frame is in the mosh range
        if (static_cast<int>(i) >= config.moshFrame &&
            static_cast<int>(i) < config.moshFrame + config.duration) {

            int moshIdx = static_cast<int>(i) - config.moshFrame;

            // Blend warped with original
            if (config.blend < 1.0f) {
                static std::vector<float> blended;
                blended.resize(width * height * 4);

                const auto& orig = frames[i].pixels;
                const auto& warped = warpedFrames[moshIdx];
                float b = config.blend;

                for (size_t j = 0; j < blended.size(); ++j) {
                    blended[j] = orig[j] * (1.0f - b) + warped[j] * b;
                }
                outputPixels = &blended;
            } else {
                outputPixels = &warpedFrames[moshIdx];
            }
        }

        // Convert to YUV and encode
        av_frame_make_writable(outFrame);
        FloatToAVFrame(*outputPixels, width, height, fromRGBA, outFrame);

        outFrame->pts = pts;
        pts += encoderCtx->time_base.den / encoderCtx->time_base.num /
               (encoderCtx->framerate.num / encoderCtx->framerate.den);

        if (avcodec_send_frame(encoderCtx, outFrame) >= 0) {
            while (avcodec_receive_packet(encoderCtx, packet) >= 0) {
                av_packet_rescale_ts(packet, encoderCtx->time_base, outStream->time_base);
                packet->stream_index = outStream->index;
                av_interleaved_write_frame(outputCtx, packet);
                av_packet_unref(packet);
            }
        }

        if ((i + 1) % 30 == 0) {
            printf("  Written %zu / %zu frames\r", i + 1, frames.size());
            fflush(stdout);
        }
    }

    // Flush encoder
    avcodec_send_frame(encoderCtx, nullptr);
    while (avcodec_receive_packet(encoderCtx, packet) >= 0) {
        av_packet_rescale_ts(packet, encoderCtx->time_base, outStream->time_base);
        packet->stream_index = outStream->index;
        av_interleaved_write_frame(outputCtx, packet);
        av_packet_unref(packet);
    }

    printf("\nWritten %zu frames total\n", frames.size());

    // Write trailer and cleanup
    av_write_trailer(outputCtx);

    sws_freeContext(toRGBA);
    sws_freeContext(fromRGBA);
    av_frame_free(&avFrame);
    av_frame_free(&outFrame);
    av_packet_free(&packet);
    avcodec_free_context(&decoderCtx);
    avcodec_free_context(&encoderCtx);
    if (!(outputCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outputCtx->pb);
    }
    avformat_close_input(&inputCtx);
    avformat_free_context(outputCtx);

    printf("\nDone! Output written to: %s\n", config.outputFile.c_str());

    return 0;
}
