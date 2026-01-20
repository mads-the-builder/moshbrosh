/*
 * MoshBrosh - Datamosh Effect Plugin for Premiere Pro
 * Uses sequence_data caching for multi-frame optical flow
 */

#include "MoshBrosh.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <cstring>

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

//==============================================================================
// SEQUENCE DATA HELPERS - Uses AccumulatedFrame from header
//==============================================================================

static void CopyFrameToAccumulated(PF_LayerDef* src, AccumulatedFrame& dst) {
    if (!src || !src->data) return;

    int width = src->width;
    int height = src->height;

    dst.Allocate(width, height);
    dst.valid = true;

    // Copy pixel data row by row (handle negative rowbytes)
    int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;
    for (int y = 0; y < height; ++y) {
        const char* srcRow = (src->rowbytes < 0)
            ? (const char*)src->data - y * srcAbsRowbytes
            : (const char*)src->data + y * srcAbsRowbytes;
        float* dstRow = dst.pixelData.data() + y * width * 4;
        memcpy(dstRow, srcRow, width * 4 * sizeof(float));
    }
}

//==============================================================================
// OPTICAL FLOW - Lucas-Kanade gradient-based
//==============================================================================

static inline float GetGray(const float* data, int rowbytes, int width, int height, int x, int y) {
    x = Clamp(x, 0, width - 1);
    y = Clamp(y, 0, height - 1);
    const float* row = (const float*)((const char*)data + y * rowbytes);
    const float* px = row + x * 4;
    return 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];
}

// Compute optical flow for a block using Lucas-Kanade
static void ComputeBlockFlow(
    const float* prev, int prevRowbytes,
    const float* curr, int currRowbytes,
    int width, int height,
    int blockX, int blockY, int blockSize,
    float* outMvX, float* outMvY)
{
    double sumIxIx = 0, sumIyIy = 0, sumIxIy = 0;
    double sumIxIt = 0, sumIyIt = 0;

    int y1 = blockY;
    int y2 = (blockY + blockSize < height) ? blockY + blockSize : height;
    int x1 = blockX;
    int x2 = (blockX + blockSize < width) ? blockX + blockSize : width;

    for (int y = y1; y < y2; ++y) {
        for (int x = x1; x < x2; ++x) {
            float Ix = (GetGray(prev, prevRowbytes, width, height, x+1, y) -
                        GetGray(prev, prevRowbytes, width, height, x-1, y)) * 0.5f;
            float Iy = (GetGray(prev, prevRowbytes, width, height, x, y+1) -
                        GetGray(prev, prevRowbytes, width, height, x, y-1)) * 0.5f;
            float It = GetGray(curr, currRowbytes, width, height, x, y) -
                       GetGray(prev, prevRowbytes, width, height, x, y);

            sumIxIx += Ix * Ix;
            sumIyIy += Iy * Iy;
            sumIxIy += Ix * Iy;
            sumIxIt += Ix * It;
            sumIyIt += Iy * It;
        }
    }

    double det = sumIxIx * sumIyIy - sumIxIy * sumIxIy;

    if (fabs(det) < 1e-6) {
        *outMvX = 0;
        *outMvY = 0;
        return;
    }

    double u = (-sumIxIt * sumIyIy + sumIyIt * sumIxIy) / det;
    double v = (-sumIyIt * sumIxIx + sumIxIt * sumIxIy) / det;

    *outMvX = (float)Clamp((int)round(u), -32, 32);
    *outMvY = (float)Clamp((int)round(v), -32, 32);
}

// Warp accumulated frame using optical flow (exact Python port)
static void WarpAccumulated(
    AccumulatedFrame& accumulated,
    const float* prev, int prevRowbytes,
    const float* curr, int currRowbytes,
    int width, int height, int blockSize)
{
    // Create temp buffer for output
    std::vector<float> temp(width * height * 4, 0.0f);

    for (int by = 0; by < height; by += blockSize) {
        for (int bx = 0; bx < width; bx += blockSize) {
            int y1 = by;
            int y2 = (by + blockSize < height) ? by + blockSize : height;
            int x1 = bx;
            int x2 = (bx + blockSize < width) ? bx + blockSize : width;
            int blockH = y2 - y1;
            int blockW = x2 - x1;

            // Compute flow for this block
            float mvX, mvY;
            ComputeBlockFlow(prev, prevRowbytes, curr, currRowbytes,
                             width, height, bx, by, blockSize, &mvX, &mvY);

            int imvX = (int)round(mvX);
            int imvY = (int)round(mvY);

            // Source position in accumulated (clamped)
            int sy1 = Clamp(y1 + imvY, 0, height - blockH);
            int sx1 = Clamp(x1 + imvX, 0, width - blockW);

            // Copy block from accumulated at offset to temp
            for (int py = 0; py < blockH; ++py) {
                const float* srcRow = accumulated.pixelData.data() + (sy1 + py) * width * 4;
                float* dstRow = temp.data() + (y1 + py) * width * 4;

                for (int px = 0; px < blockW; ++px) {
                    const float* srcPx = srcRow + (sx1 + px) * 4;
                    float* dstPx = dstRow + (x1 + px) * 4;
                    dstPx[0] = srcPx[0];
                    dstPx[1] = srcPx[1];
                    dstPx[2] = srcPx[2];
                    dstPx[3] = srcPx[3];
                }
            }
        }
    }

    // Copy temp back to accumulated
    accumulated.pixelData = std::move(temp);
}

//==============================================================================
// EFFECT CALLBACKS
//==============================================================================

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    snprintf(out_data->return_msg, sizeof(out_data->return_msg),
        "%s v%d.%d\r%s", PLUGIN_NAME, PLUGIN_MAJOR_VERSION, PLUGIN_MINOR_VERSION, PLUGIN_DESCRIPTION);
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("GlobalSetup called");

    out_data->my_version = PF_VERSION(PLUGIN_MAJOR_VERSION, PLUGIN_MINOR_VERSION,
        PLUGIN_BUG_VERSION, PLUGIN_STAGE_VERSION, PLUGIN_BUILD_VERSION);

    out_data->out_flags = PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING |
                          PF_OutFlag_PIX_INDEPENDENT |
                          PF_OutFlag_USE_OUTPUT_EXTENT;

    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS;

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

static PF_Err GlobalSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    DebugLog("GlobalSetdown called");
    if (g_debugLog) {
        fclose(g_debugLog);
        g_debugLog = nullptr;
    }
    return PF_Err_NONE;
}

static PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data) {
    DebugLog("SequenceSetup called");

    // Allocate and initialize sequence data using header's struct
    MoshSequenceData* seqData = new MoshSequenceData();
    // Constructor already initializes all fields properly

    out_data->sequence_data = PF_NEW_HANDLE(sizeof(MoshSequenceData*));
    if (out_data->sequence_data) {
        *((MoshSequenceData**)(*out_data->sequence_data)) = seqData;
    }

    DebugLog("SequenceSetup complete");
    return PF_Err_NONE;
}

static PF_Err SequenceSetdown(PF_InData* in_data, PF_OutData* out_data) {
    DebugLog("SequenceSetdown called");

    if (in_data->sequence_data) {
        MoshSequenceData* seqData = *((MoshSequenceData**)(*in_data->sequence_data));
        if (seqData) {
            seqData->Clear();
            delete seqData;
        }
        PF_DISPOSE_HANDLE(in_data->sequence_data);
    }

    return PF_Err_NONE;
}

static PF_Err SequenceFlatten(PF_InData* in_data, PF_OutData* out_data) {
    // For flattening, we just clear the cache (can't serialize pixel data easily)
    DebugLog("SequenceFlatten called");

    if (in_data->sequence_data) {
        MoshSequenceData* seqData = *((MoshSequenceData**)(*in_data->sequence_data));
        if (seqData) {
            seqData->accumulatedFrames.clear();
            seqData->referenceFrame.Clear();
        }
    }

    return PF_Err_NONE;
}

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

// Key for storing pre-computed warped frames (negative keys to avoid collision with frame numbers)
static const int32_t WARPED_KEY_BASE = -10000;  // Warped frame N stored at key WARPED_KEY_BASE - N
static const int32_t INPUT_KEY_BASE = 0;  // Input frames stored at their actual frame number

static inline int32_t WarpedKey(int32_t frameNum) { return WARPED_KEY_BASE - frameNum; }

// Pre-compute all warped frames for the mosh range (called once when all inputs are cached)
static void PrecomputeWarpedFrames(
    MoshSequenceData* seqData,
    int32_t moshFrame,
    int32_t duration,
    int32_t blockSize,
    int width, int height)
{
    DebugLog("Pre-computing warped frames for mosh range [%d, %d)", moshFrame, moshFrame + duration);

    // Start with reference frame as the accumulated image
    AccumulatedFrame accumulated;
    accumulated.Allocate(width, height);
    accumulated.pixelData = seqData->referenceFrame.pixelData;  // Copy from reference
    accumulated.valid = true;

    int rowbytes = width * 4 * sizeof(float);

    // Process each frame in the mosh range sequentially
    for (int32_t f = moshFrame; f < moshFrame + duration; ++f) {
        int32_t prevFrameNum = f - 1;

        // Get cached input frames
        AccumulatedFrame& prevInput = seqData->accumulatedFrames[prevFrameNum];
        AccumulatedFrame& currInput = seqData->accumulatedFrames[f];

        // Warp the accumulated frame using optical flow between prev and current
        WarpAccumulated(
            accumulated,
            prevInput.pixelData.data(), rowbytes,
            currInput.pixelData.data(), rowbytes,
            width, height, blockSize);

        // Store a copy of the warped result for this frame
        int32_t warpedKey = WarpedKey(f);
        AccumulatedFrame& warpedResult = seqData->accumulatedFrames[warpedKey];
        warpedResult.Allocate(width, height);
        warpedResult.pixelData = accumulated.pixelData;  // Copy
        warpedResult.valid = true;
        warpedResult.frameIndex = f;

        DebugLog("Pre-computed warped frame %d", f);
    }

    // Mark pre-computation as complete
    seqData->analysisState = AnalysisState::Complete;
    DebugLog("Pre-computation complete for %d frames", duration);
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_LayerDef* src = &params[MOSH_INPUT]->u.ld;
    int width = src->width;
    int height = src->height;

    if (!src->data || !output->data || width <= 0 || height <= 0) {
        return PF_Err_NONE;
    }

    int32_t moshFrame = params[MOSH_FRAME]->u.sd.value;
    int32_t duration = params[MOSH_DURATION]->u.sd.value;
    int32_t blockSize = BlockSizeFromIndex(params[MOSH_BLOCK_SIZE]->u.pd.value);
    float blend = (float)params[MOSH_BLEND]->u.fs_d.value / 100.0f;
    int32_t currentFrame = (in_data->time_step > 0) ? (int32_t)(in_data->current_time / in_data->time_step) : 0;

    // Get sequence data
    MoshSequenceData* seqData = nullptr;
    if (in_data->sequence_data) {
        seqData = *((MoshSequenceData**)(*in_data->sequence_data));
    }

    if (!seqData) {
        // No sequence data - just passthrough
        return PF_Err_NONE;
    }

    // Lock mutex for thread-safe access to cache
    std::lock_guard<std::mutex> lock(seqData->cacheMutex);

    // Check if parameters changed - clear cache if so
    if (seqData->analyzedMoshFrame != moshFrame || seqData->analyzedDuration != duration ||
        seqData->analyzedBlockSize != blockSize) {
        DebugLog("Parameters changed, clearing cache");
        seqData->accumulatedFrames.clear();
        seqData->referenceFrame.Clear();
        seqData->analyzedMoshFrame = moshFrame;
        seqData->analyzedDuration = duration;
        seqData->analyzedBlockSize = blockSize;
        seqData->analysisState = AnalysisState::NotStarted;
    }

    // Cache current input frame (use frame number as key)
    if (seqData->accumulatedFrames.find(currentFrame) == seqData->accumulatedFrames.end()) {
        AccumulatedFrame& cached = seqData->accumulatedFrames[currentFrame];
        CopyFrameToAccumulated(src, cached);
        cached.frameIndex = currentFrame;
        DebugLog("Cached input frame %d (total cached: %zu)", currentFrame, seqData->accumulatedFrames.size());
    }

    // Store reference frame (frame before mosh starts)
    if (currentFrame == moshFrame - 1 && !seqData->referenceFrame.valid) {
        CopyFrameToAccumulated(src, seqData->referenceFrame);
        seqData->referenceFrame.frameIndex = currentFrame;
        DebugLog("Stored reference frame %d", currentFrame);
    }

    // Not in mosh range - passthrough
    if (currentFrame < moshFrame || currentFrame >= moshFrame + duration) {
        int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;
        int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;
        for (int y = 0; y < height; ++y) {
            const char* srcRow = (src->rowbytes < 0)
                ? (const char*)src->data - y * srcAbsRowbytes
                : (const char*)src->data + y * srcAbsRowbytes;
            char* dstRow = (output->rowbytes < 0)
                ? (char*)output->data - y * outAbsRowbytes
                : (char*)output->data + y * outAbsRowbytes;
            memcpy(dstRow, srcRow, width * 4 * sizeof(float));
        }
        return PF_Err_NONE;
    }

    // In mosh range - check if pre-computation is done
    int32_t warpedKey = WarpedKey(currentFrame);
    bool hasPrecomputed = seqData->accumulatedFrames.find(warpedKey) != seqData->accumulatedFrames.end();

    if (hasPrecomputed) {
        // Use pre-computed result
        AccumulatedFrame& warpedResult = seqData->accumulatedFrames[warpedKey];

        int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;
        int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;

        for (int y = 0; y < height; ++y) {
            const float* srcRow = (const float*)((src->rowbytes < 0)
                ? (const char*)src->data - y * srcAbsRowbytes
                : (const char*)src->data + y * srcAbsRowbytes);
            const float* accRow = warpedResult.pixelData.data() + y * width * 4;
            float* outRow = (float*)((output->rowbytes < 0)
                ? (char*)output->data - y * outAbsRowbytes
                : (char*)output->data + y * outAbsRowbytes);

            for (int x = 0; x < width; ++x) {
                outRow[x*4+0] = srcRow[x*4+0] * (1.0f - blend) + accRow[x*4+0] * blend;
                outRow[x*4+1] = srcRow[x*4+1] * (1.0f - blend) + accRow[x*4+1] * blend;
                outRow[x*4+2] = srcRow[x*4+2] * (1.0f - blend) + accRow[x*4+2] * blend;
                outRow[x*4+3] = srcRow[x*4+3] * (1.0f - blend) + accRow[x*4+3] * blend;
            }
        }

        DebugLog("Render frame %d using pre-computed result", currentFrame);
        return PF_Err_NONE;
    }

    // Check if we have all required input frames to do pre-computation
    bool hasAllInputs = seqData->referenceFrame.valid;
    if (hasAllInputs) {
        for (int32_t f = moshFrame - 1; f < moshFrame + duration; ++f) {
            if (seqData->accumulatedFrames.find(f) == seqData->accumulatedFrames.end()) {
                hasAllInputs = false;
                DebugLog("Missing input frame %d for pre-computation", f);
                break;
            }
        }
    }

    if (hasAllInputs && seqData->analysisState != AnalysisState::Complete) {
        // All inputs cached - do pre-computation now
        PrecomputeWarpedFrames(seqData, moshFrame, duration, blockSize, width, height);

        // Now use the pre-computed result for this frame
        warpedKey = WarpedKey(currentFrame);
        if (seqData->accumulatedFrames.find(warpedKey) != seqData->accumulatedFrames.end()) {
            AccumulatedFrame& warpedResult = seqData->accumulatedFrames[warpedKey];

            int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;
            int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;

            for (int y = 0; y < height; ++y) {
                const float* srcRow = (const float*)((src->rowbytes < 0)
                    ? (const char*)src->data - y * srcAbsRowbytes
                    : (const char*)src->data + y * srcAbsRowbytes);
                const float* accRow = warpedResult.pixelData.data() + y * width * 4;
                float* outRow = (float*)((output->rowbytes < 0)
                    ? (char*)output->data - y * outAbsRowbytes
                    : (char*)output->data + y * outAbsRowbytes);

                for (int x = 0; x < width; ++x) {
                    outRow[x*4+0] = srcRow[x*4+0] * (1.0f - blend) + accRow[x*4+0] * blend;
                    outRow[x*4+1] = srcRow[x*4+1] * (1.0f - blend) + accRow[x*4+1] * blend;
                    outRow[x*4+2] = srcRow[x*4+2] * (1.0f - blend) + accRow[x*4+2] * blend;
                    outRow[x*4+3] = srcRow[x*4+3] * (1.0f - blend) + accRow[x*4+3] * blend;
                }
            }

            DebugLog("Render frame %d after pre-computation", currentFrame);
            return PF_Err_NONE;
        }
    }

    // Still collecting input frames - output cyan tint to indicate analysis in progress
    DebugLog("Collecting input frames, outputting cyan tint for frame %d", currentFrame);
    int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;
    int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;

    for (int y = 0; y < height; ++y) {
        const float* srcRow = (const float*)((src->rowbytes < 0)
            ? (const char*)src->data - y * srcAbsRowbytes
            : (const char*)src->data + y * srcAbsRowbytes);
        float* outRow = (float*)((output->rowbytes < 0)
            ? (char*)output->data - y * outAbsRowbytes
            : (char*)output->data + y * outAbsRowbytes);

        for (int x = 0; x < width; ++x) {
            // Cyan tint: boost G and B, reduce R
            outRow[x*4+0] = srcRow[x*4+0] * 1.0f + 0.2f;  // B boosted
            outRow[x*4+1] = srcRow[x*4+1] * 1.0f + 0.2f;  // G boosted
            outRow[x*4+2] = srcRow[x*4+2] * 0.5f;          // R reduced
            outRow[x*4+3] = srcRow[x*4+3];  // A unchanged
        }
    }
    return PF_Err_NONE;
}

// Main entry point
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
    PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;

    try {
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
                err = SequenceSetup(in_data, out_data);
                break;
            case PF_Cmd_SEQUENCE_SETDOWN:
                err = SequenceSetdown(in_data, out_data);
                break;
            case PF_Cmd_SEQUENCE_FLATTEN:
                err = SequenceFlatten(in_data, out_data);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, out_data, params, output);
                break;
            default:
                break;
        }
    } catch (...) {
        DebugLog("Exception in EffectMain cmd=%d", cmd);
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return err;
}
