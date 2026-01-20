/*
 * MoshBrosh - Datamosh Effect Plugin for Premiere Pro
 * Simplified version: deterministic block displacement (no frame history needed)
 */

#include "MoshBrosh.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>

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

    out_data->out_flags = PF_OutFlag_PIX_INDEPENDENT |
                          PF_OutFlag_USE_OUTPUT_EXTENT;

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

// Simple hash for deterministic randomness
static inline uint32_t hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// Get pixel from source with bounds checking
static inline void GetPixel(const float* srcData, int width, int height, int rowStride,
                            int x, int y, bool negativeRowbytes, float* outPixel) {
    x = Clamp(x, 0, width - 1);
    y = Clamp(y, 0, height - 1);

    const float* row;
    if (negativeRowbytes) {
        row = srcData - y * rowStride;
    } else {
        row = srcData + y * rowStride;
    }

    const float* px = row + x * 4;
    outPixel[0] = px[0];
    outPixel[1] = px[1];
    outPixel[2] = px[2];
    outPixel[3] = px[3];
}

// Main render function - stateless, deterministic block displacement
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

    // Not in mosh range - passthrough
    if (currentFrame < moshFrame || currentFrame >= moshFrame + duration) {
        // Simple passthrough
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

    // In mosh range - apply deterministic block displacement
    int framesIntoMosh = currentFrame - moshFrame;

    // Calculate row strides
    int srcAbsRowbytes = src->rowbytes < 0 ? -src->rowbytes : src->rowbytes;
    int outAbsRowbytes = output->rowbytes < 0 ? -output->rowbytes : output->rowbytes;
    int srcRowStride = srcAbsRowbytes / sizeof(float);
    int outRowStride = outAbsRowbytes / sizeof(float);
    bool srcNegative = src->rowbytes < 0;
    bool outNegative = output->rowbytes < 0;

    const float* srcData = (const float*)src->data;
    float* outData = (float*)output->data;

    int blocksX = (width + blockSize - 1) / blockSize;
    int blocksY = (height + blockSize - 1) / blockSize;

    // For each block, compute a deterministic displacement based on position and frame
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            // Hash based on block position - displacement accumulates over frames
            uint32_t blockHash = hash(bx + by * 1000 + moshFrame * 100000);

            // Base displacement direction (stays consistent for this block)
            int baseDx = ((blockHash & 0xFF) % (searchRange * 2 + 1)) - searchRange;
            int baseDy = (((blockHash >> 8) & 0xFF) % (searchRange * 2 + 1)) - searchRange;

            // Displacement grows with frames into mosh (simulating motion accumulation)
            float accumFactor = (float)framesIntoMosh / 10.0f;
            int dx = (int)(baseDx * accumFactor);
            int dy = (int)(baseDy * accumFactor);

            // Copy block with displacement
            for (int py = 0; py < blockSize; ++py) {
                int dstY = by * blockSize + py;
                if (dstY >= height) continue;

                for (int px = 0; px < blockSize; ++px) {
                    int dstX = bx * blockSize + px;
                    if (dstX >= width) continue;

                    // Source position with displacement
                    int srcX = Clamp(dstX + dx, 0, width - 1);
                    int srcY = Clamp(dstY + dy, 0, height - 1);

                    // Get source pixel
                    const float* srcPx;
                    if (srcNegative) {
                        srcPx = srcData - srcY * srcRowStride + srcX * 4;
                    } else {
                        srcPx = srcData + srcY * srcRowStride + srcX * 4;
                    }

                    // Get original pixel for blending
                    const float* origPx;
                    if (srcNegative) {
                        origPx = srcData - dstY * srcRowStride + dstX * 4;
                    } else {
                        origPx = srcData + dstY * srcRowStride + dstX * 4;
                    }

                    // Output pixel
                    float* outPx;
                    if (outNegative) {
                        outPx = outData - dstY * outRowStride + dstX * 4;
                    } else {
                        outPx = outData + dstY * outRowStride + dstX * 4;
                    }

                    // Blend displaced with original
                    outPx[0] = origPx[0] * (1.0f - blend) + srcPx[0] * blend;
                    outPx[1] = origPx[1] * (1.0f - blend) + srcPx[1] * blend;
                    outPx[2] = origPx[2] * (1.0f - blend) + srcPx[2] * blend;
                    outPx[3] = origPx[3] * (1.0f - blend) + srcPx[3] * blend;
                }
            }
        }
    }

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
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);
            break;
        default:
            break;
    }

    return err;
}
