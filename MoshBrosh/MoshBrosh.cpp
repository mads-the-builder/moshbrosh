/*
 * MoshBrosh - Datamosh Effect Plugin for Premiere Pro
 * Uses sequence data to store frames instead of checkout (avoids exceptions)
 */

#include "MoshBrosh.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

// Debug logging
static FILE* g_debugLog = nullptr;

static void DebugLog(const char* fmt, ...) {
    if (!g_debugLog) {
        g_debugLog = fopen("/Users/mads/Desktop/moshbrosh_debug.log", "a");
        if (g_debugLog) {
            fprintf(g_debugLog, "\n\n=== MoshBrosh Plugin Started ===\n");
            fflush(g_debugLog);
        }
    }
    if (g_debugLog) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_debugLog, fmt, args);
        va_end(args);
        fprintf(g_debugLog, "\n");
        fflush(g_debugLog);
    }
}

// Forward declarations
static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err GlobalSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err SequenceSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err SequenceResetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err SequenceFlatten(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);

// About dialog
static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    snprintf(out_data->return_msg, sizeof(out_data->return_msg),
        "%s v%d.%d\r%s", PLUGIN_NAME, PLUGIN_MAJOR_VERSION, PLUGIN_MINOR_VERSION, PLUGIN_DESCRIPTION);
    return PF_Err_NONE;
}

// Global setup
static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("GlobalSetup called");

    out_data->my_version = PF_VERSION(PLUGIN_MAJOR_VERSION, PLUGIN_MINOR_VERSION,
        PLUGIN_BUG_VERSION, PLUGIN_STAGE_VERSION, PLUGIN_BUILD_VERSION);

    // Remove WIDE_TIME_INPUT since we're not using frame checkout anymore
    out_data->out_flags = PF_OutFlag_PIX_INDEPENDENT |
                          PF_OutFlag_USE_OUTPUT_EXTENT |
                          PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING;

    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS;

    // Request BGRA 32f pixel format
    if (in_data->appl_id == 'PrMr') {
        AEFX_SuiteScoper<PF_PixelFormatSuite1, true> pixelFormatSuite(
            in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
        if (pixelFormatSuite.get()) {
            (*pixelFormatSuite->ClearSupportedPixelFormats)(in_data->effect_ref);
            (*pixelFormatSuite->AddSupportedPixelFormat)(in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
        }
    }

    DebugLog("GlobalSetup complete");
    return PF_Err_NONE;
}

// Global setdown
static PF_Err GlobalSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("GlobalSetdown called");
    if (g_debugLog) {
        fclose(g_debugLog);
        g_debugLog = nullptr;
    }
    return PF_Err_NONE;
}

// Params setup
static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Mosh Frame", MOSH_FRAME_MIN, MOSH_FRAME_MAX, MOSH_FRAME_MIN, MOSH_FRAME_MAX, MOSH_FRAME_DFLT, DISK_ID_MOSH_FRAME);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Duration", DURATION_MIN, DURATION_MAX, DURATION_MIN, DURATION_MAX, DURATION_DFLT, DISK_ID_DURATION);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Block Size", 3, BLOCK_SIZE_DFLT, "8|16|32", DISK_ID_BLOCK_SIZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Search Range", SEARCH_RANGE_MIN, SEARCH_RANGE_MAX, SEARCH_RANGE_MIN, SEARCH_RANGE_MAX, SEARCH_RANGE_DFLT, DISK_ID_SEARCH_RANGE);

    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_FLOAT_SLIDER;
    PF_STRCPY(def.PF_DEF_NAME, "Blend");
    def.u.fs_d.valid_min = BLEND_MIN;
    def.u.fs_d.valid_max = BLEND_MAX;
    def.u.fs_d.slider_min = BLEND_MIN;
    def.u.fs_d.slider_max = BLEND_MAX;
    def.u.fs_d.value = BLEND_DFLT;
    def.u.fs_d.dephault = BLEND_DFLT;
    def.u.fs_d.precision = 1;
    def.u.fs_d.display_flags = PF_ValueDisplayFlag_PERCENT;
    def.uu.id = DISK_ID_BLEND;
    PF_ADD_PARAM(in_data, -1, &def);

    out_data->num_params = MOSH_NUM_PARAMS;
    return err;
}

// Global pointer to sequence data (since Premiere doesn't support handles well for C++ objects)
static MoshSequenceData* g_seqData = nullptr;

// Sequence setup - allocate sequence data
static PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("SequenceSetup called");

    if (!g_seqData) {
        g_seqData = new MoshSequenceData();
    }

    // Allocate a dummy handle for the host
    PF_Handle handle = PF_NEW_HANDLE(sizeof(int32_t));
    out_data->sequence_data = handle;

    return PF_Err_NONE;
}

// Sequence setdown - free sequence data
static PF_Err SequenceSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("SequenceSetdown called");

    if (g_seqData) {
        delete g_seqData;
        g_seqData = nullptr;
    }

    if (in_data->sequence_data) {
        PF_DISPOSE_HANDLE(in_data->sequence_data);
    }

    return PF_Err_NONE;
}

// Sequence resetup - called when loading project
static PF_Err SequenceResetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("SequenceResetup called");

    if (!g_seqData) {
        g_seqData = new MoshSequenceData();
    }

    // Allocate a dummy handle for the host
    PF_Handle handle = PF_NEW_HANDLE(sizeof(int32_t));
    out_data->sequence_data = handle;

    return PF_Err_NONE;
}

// Sequence flatten - prepare for serialization (no-op since we use global)
static PF_Err SequenceFlatten(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("SequenceFlatten called");

    // Just return the existing handle
    out_data->sequence_data = in_data->sequence_data;

    return PF_Err_NONE;
}

// Copy frame from PF_LayerDef to AccumulatedFrame
static void CopyFrameToAccumulated(PF_LayerDef* src, AccumulatedFrame& dest) {
    int width = src->width;
    int height = src->height;

    dest.Allocate(width, height);
    dest.frameIndex = 0; // Will be set by caller

    // Handle potentially negative rowbytes by using absolute value for stride
    // but correctly addressing memory
    int absRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;

    for (int y = 0; y < height; ++y) {
        const float* srcRow;
        if (src->rowbytes < 0) {
            // Bottom-up: data pointer is at last row
            srcRow = reinterpret_cast<const float*>(
                reinterpret_cast<const char*>(src->data) - y * absRowbytes);
        } else {
            srcRow = reinterpret_cast<const float*>(
                reinterpret_cast<const char*>(src->data) + y * absRowbytes);
        }

        float* destRow = &dest.pixelData[y * width * 4];
        memcpy(destRow, srcRow, width * 4 * sizeof(float));
    }
}

// Simple passthrough helper
static void DoPassthrough(PF_LayerDef* src, PF_LayerDef* output) {
    int width = src->width;
    int height = src->height;

    int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;
    int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;

    for (int y = 0; y < height; ++y) {
        const char* srcRow;
        char* destRow;

        if (src->rowbytes < 0) {
            srcRow = reinterpret_cast<const char*>(src->data) - y * srcAbsRowbytes;
        } else {
            srcRow = reinterpret_cast<const char*>(src->data) + y * srcAbsRowbytes;
        }

        if (output->rowbytes < 0) {
            destRow = reinterpret_cast<char*>(output->data) - y * outAbsRowbytes;
        } else {
            destRow = reinterpret_cast<char*>(output->data) + y * outAbsRowbytes;
        }

        memcpy(destRow, srcRow, width * 4 * sizeof(float));
    }
}

// Compute luminance from BGRA pixel
inline float GetLuminance(const float* bgra) {
    return 0.114f * bgra[0] + 0.587f * bgra[1] + 0.299f * bgra[2];
}

// Compute block motion using SAD
static void ComputeBlockMotion(
    const float* current, const float* previous,
    int width, int height,
    int blockX, int blockY, int blockSize, int searchRange,
    int16_t& outDx, int16_t& outDy)
{
    int bestDx = 0, bestDy = 0;
    float bestSAD = 1e30f;

    int bx = blockX * blockSize;
    int by = blockY * blockSize;

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

                    float currLuma = GetLuminance(&current[(cy * width + cx) * 4]);
                    float prevLuma = GetLuminance(&previous[(ry * width + rx) * 4]);
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

    outDx = (int16_t)bestDx;
    outDy = (int16_t)bestDy;
}

// Main render function
static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_LayerDef* src = &params[MOSH_INPUT]->u.ld;
    int width = src->width;
    int height = src->height;

    // Safety check
    if (!src->data || !output->data || width <= 0 || height <= 0) {
        return PF_Err_NONE;
    }

    int32_t moshFrame = params[MOSH_FRAME]->u.sd.value;
    int32_t duration = params[MOSH_DURATION]->u.sd.value;
    int32_t blockSize = BlockSizeFromIndex(params[MOSH_BLOCK_SIZE]->u.pd.value);
    int32_t searchRange = params[MOSH_SEARCH_RANGE]->u.sd.value;
    float blend = (float)params[MOSH_BLEND]->u.fs_d.value / 100.0f;

    int32_t currentFrame = (in_data->time_step > 0) ? (int32_t)(in_data->current_time / in_data->time_step) : 0;

    DebugLog("Render: frame=%d mosh=%d dur=%d blend=%.2f", currentFrame, moshFrame, duration, blend);

    // Use global sequence data
    MoshSequenceData* seqData = g_seqData;

    // Before mosh range OR no sequence data - just passthrough
    if (currentFrame < moshFrame || !seqData) {
        // If this is the reference frame (moshFrame - 1), store it
        if (seqData && currentFrame == moshFrame - 1) {
            DebugLog("Storing reference frame %d", currentFrame);
            CopyFrameToAccumulated(src, seqData->referenceFrame);
            seqData->referenceFrame.frameIndex = currentFrame;
        }

        // Also store previous frame for motion estimation
        if (seqData) {
            auto& prevFrame = seqData->accumulatedFrames[currentFrame];
            CopyFrameToAccumulated(src, prevFrame);
            prevFrame.frameIndex = currentFrame;
        }

        DoPassthrough(src, output);
        return PF_Err_NONE;
    }

    // After mosh range - passthrough
    if (currentFrame >= moshFrame + duration) {
        DoPassthrough(src, output);
        return PF_Err_NONE;
    }

    // In mosh range - apply effect
    DebugLog("MOSH: frame=%d", currentFrame);

    // Check if we have a valid reference frame
    if (!seqData->referenceFrame.valid ||
        seqData->referenceFrame.width != width ||
        seqData->referenceFrame.height != height) {
        DebugLog("No valid reference frame, passthrough");
        DoPassthrough(src, output);
        return PF_Err_NONE;
    }

    // Get previous frame for motion estimation (or use reference if not available)
    AccumulatedFrame* prevFrame = &seqData->referenceFrame;
    int32_t prevFrameIdx = currentFrame - 1;
    auto it = seqData->accumulatedFrames.find(prevFrameIdx);
    if (it != seqData->accumulatedFrames.end() && it->second.valid &&
        it->second.width == width && it->second.height == height) {
        prevFrame = &it->second;
    }

    // Copy current frame to our buffer for motion estimation
    AccumulatedFrame currentFrameData;
    CopyFrameToAccumulated(src, currentFrameData);
    currentFrameData.frameIndex = currentFrame;

    // Compute motion vectors between current and previous frame
    int blocksX = (width + blockSize - 1) / blockSize;
    int blocksY = (height + blockSize - 1) / blockSize;
    std::vector<int16_t> mvX(blocksX * blocksY, 0);
    std::vector<int16_t> mvY(blocksX * blocksY, 0);

    const float* currData = currentFrameData.pixelData.data();
    const float* prevData = prevFrame->pixelData.data();
    const float* refData = seqData->referenceFrame.pixelData.data();

    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            ComputeBlockMotion(currData, prevData, width, height,
                bx, by, blockSize, searchRange,
                mvX[by * blocksX + bx], mvY[by * blocksX + bx]);
        }
    }

    // Apply motion vectors to reference frame and output
    int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;

    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            int16_t dx = mvX[by * blocksX + bx];
            int16_t dy = mvY[by * blocksX + bx];

            for (int py = 0; py < blockSize; ++py) {
                int dstY = by * blockSize + py;
                if (dstY >= height) continue;

                int warpY = Clamp(dstY + dy, 0, height - 1);

                for (int px = 0; px < blockSize; ++px) {
                    int dstX = bx * blockSize + px;
                    if (dstX >= width) continue;

                    int warpX = Clamp(dstX + dx, 0, width - 1);

                    const float* refPx = &refData[(warpY * width + warpX) * 4];
                    const float* currPx = &currData[(dstY * width + dstX) * 4];

                    float* outPx;
                    if (output->rowbytes < 0) {
                        outPx = reinterpret_cast<float*>(
                            reinterpret_cast<char*>(output->data) - dstY * outAbsRowbytes) + dstX * 4;
                    } else {
                        outPx = reinterpret_cast<float*>(
                            reinterpret_cast<char*>(output->data) + dstY * outAbsRowbytes) + dstX * 4;
                    }

                    outPx[0] = currPx[0] * (1.0f - blend) + refPx[0] * blend;
                    outPx[1] = currPx[1] * (1.0f - blend) + refPx[1] * blend;
                    outPx[2] = currPx[2] * (1.0f - blend) + refPx[2] * blend;
                    outPx[3] = currPx[3] * (1.0f - blend) + refPx[3] * blend;
                }
            }
        }
    }

    // Store current frame for next frame's motion estimation
    seqData->accumulatedFrames[currentFrame] = std::move(currentFrameData);

    // Clean up old frames to prevent memory bloat (keep only recent ones)
    if (seqData->accumulatedFrames.size() > 10) {
        auto oldest = seqData->accumulatedFrames.begin();
        while (seqData->accumulatedFrames.size() > 5 && oldest != seqData->accumulatedFrames.end()) {
            if (oldest->first < currentFrame - 5) {
                oldest = seqData->accumulatedFrames.erase(oldest);
            } else {
                ++oldest;
            }
        }
    }

    DebugLog("MOSH complete frame=%d", currentFrame);
    return PF_Err_NONE;
}

// Main entry point
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
    PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;

    switch (cmd) {
        case PF_Cmd_ABOUT:
            err = About(in_data, out_data, params, output);
            break;
        case PF_Cmd_GLOBAL_SETUP:
            err = GlobalSetup(in_data, out_data, params, output);
            break;
        case PF_Cmd_GLOBAL_SETDOWN:
            err = GlobalSetdown(in_data, out_data, params, output);
            break;
        case PF_Cmd_PARAMS_SETUP:
            err = ParamsSetup(in_data, out_data, params, output);
            break;
        case PF_Cmd_SEQUENCE_SETUP:
            err = SequenceSetup(in_data, out_data, params, output);
            break;
        case PF_Cmd_SEQUENCE_SETDOWN:
            err = SequenceSetdown(in_data, out_data, params, output);
            break;
        case PF_Cmd_SEQUENCE_RESETUP:
            err = SequenceResetup(in_data, out_data, params, output);
            break;
        case PF_Cmd_SEQUENCE_FLATTEN:
            err = SequenceFlatten(in_data, out_data, params, output);
            break;
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);
            break;
        default:
            break;
    }

    return err;
}
