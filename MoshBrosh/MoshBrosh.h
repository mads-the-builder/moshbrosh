/*
 * MoshBrosh - Datamosh Effect Plugin for Premiere Pro
 * CPU-based I-frame deletion simulation with block-based motion compensation
 */

#pragma once

#define PF_TABLE_BITS   12
#define PF_TABLE_SZ_16  4096
#define PF_DEEP_COLOR_AWARE 1

#include "AEConfig.h"

#ifdef AE_OS_WIN
    typedef unsigned short PixelType;
    #include <Windows.h>
#endif

#include "PrSDKTypes.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "SPTypes.h"
#include "PrSDKAESupport.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AEFX_SuiteHelper.h"
#include "AEGP_SuiteHandler.h"

#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <mutex>

// Plugin info
#define PLUGIN_NAME         "MoshBrosh"
#define PLUGIN_DESCRIPTION  "Datamosh effect - I-frame deletion simulation"
#define PLUGIN_CATEGORY     "Stylize"
#define PLUGIN_MATCH_NAME   "MoshBrosh Datamosh"

// Version
#define PLUGIN_MAJOR_VERSION    1
#define PLUGIN_MINOR_VERSION    0
#define PLUGIN_BUG_VERSION      0
#define PLUGIN_STAGE_VERSION    PF_Stage_DEVELOP
#define PLUGIN_BUILD_VERSION    1

// Parameter IDs
enum {
    MOSH_INPUT = 0,
    MOSH_FRAME,
    MOSH_DURATION,
    MOSH_BLOCK_SIZE,
    MOSH_SEARCH_RANGE,
    MOSH_BLEND,
    MOSH_NUM_PARAMS
};

// Disk IDs for parameter persistence
enum {
    DISK_ID_MOSH_FRAME = 1,
    DISK_ID_DURATION,
    DISK_ID_BLOCK_SIZE,
    DISK_ID_SEARCH_RANGE,
    DISK_ID_BLEND
};

// Parameter defaults and ranges
#define MOSH_FRAME_DFLT         10
#define MOSH_FRAME_MIN          1
#define MOSH_FRAME_MAX          10000

#define DURATION_DFLT           30
#define DURATION_MIN            1
#define DURATION_MAX            1000

#define SEARCH_RANGE_DFLT       16
#define SEARCH_RANGE_MIN        4
#define SEARCH_RANGE_MAX        64

#define BLEND_DFLT              100.0f
#define BLEND_MIN               0.0f
#define BLEND_MAX               100.0f

// Block size options (popup indices are 1-based)
#define BLOCK_SIZE_8            1
#define BLOCK_SIZE_16           2
#define BLOCK_SIZE_32           3
#define BLOCK_SIZE_DFLT         BLOCK_SIZE_16

// Pixel format structures
typedef struct {
    A_u_char blue, green, red, alpha;
} PF_Pixel_BGRA_8u;

typedef struct {
    A_u_char Pr, Pb, luma, alpha;
} PF_Pixel_VUYA_8u;

typedef struct {
    PF_FpShort blue, green, red, alpha;
} PF_Pixel_BGRA_32f;

typedef struct {
    PF_FpShort Pr, Pb, luma, alpha;
} PF_Pixel_VUYA_32f;

// Motion vector for a single macroblock
struct MotionVector {
    int16_t dx;
    int16_t dy;
    uint32_t sad;  // Sum of Absolute Differences (match quality)
};

// Motion field for entire frame (grid of MVs)
struct MotionField {
    int32_t frameIndex;
    int32_t width;
    int32_t height;
    int32_t blockSize;
    int32_t blocksX;
    int32_t blocksY;
    std::vector<MotionVector> vectors;

    size_t GetVectorIndex(int32_t bx, int32_t by) const {
        return static_cast<size_t>(by * blocksX + bx);
    }

    void Clear() {
        vectors.clear();
        frameIndex = 0;
        width = height = 0;
        blockSize = blocksX = blocksY = 0;
    }
};

// Accumulated frame buffer
struct AccumulatedFrame {
    int32_t frameIndex;
    int32_t width;
    int32_t height;
    int32_t rowBytes;
    std::vector<float> pixelData;  // BGRA 32f format
    bool valid;

    AccumulatedFrame() : frameIndex(0), width(0), height(0), rowBytes(0), valid(false) {}

    void Allocate(int32_t w, int32_t h) {
        width = w;
        height = h;
        rowBytes = w * 4 * sizeof(float);
        pixelData.resize(static_cast<size_t>(w * h * 4), 0.0f);
        valid = true;
    }

    void Clear() {
        pixelData.clear();
        valid = false;
        frameIndex = 0;
        width = height = rowBytes = 0;
    }
};

// Analysis state
enum class AnalysisState : int32_t {
    NotStarted = 0,
    InProgress = 1,
    Complete = 2,
    Invalid = 3
};

// Sequence data - persists with the project
struct MoshSequenceData {
    uint32_t version;
    AnalysisState analysisState;

    // Parameters at time of analysis (for invalidation detection)
    int32_t analyzedMoshFrame;
    int32_t analyzedDuration;
    int32_t analyzedBlockSize;
    int32_t analyzedSearchRange;
    int32_t analyzedWidth;
    int32_t analyzedHeight;

    // Cached motion fields: frameIndex -> MotionField
    std::unordered_map<int32_t, MotionField> motionFields;

    // Accumulated frames for mosh range
    std::unordered_map<int32_t, AccumulatedFrame> accumulatedFrames;

    // Reference frame (frozen at mosh_frame - 1)
    AccumulatedFrame referenceFrame;

    // Mutex for thread-safe access (Premiere renders frames in parallel)
    std::mutex cacheMutex;

    MoshSequenceData() : version(1), analysisState(AnalysisState::NotStarted),
        analyzedMoshFrame(0), analyzedDuration(0), analyzedBlockSize(16),
        analyzedSearchRange(16), analyzedWidth(0), analyzedHeight(0) {}

    bool IsValidForParams(int32_t moshFrame, int32_t duration,
                          int32_t blockSize, int32_t searchRange,
                          int32_t width, int32_t height) const {
        return analysisState == AnalysisState::Complete &&
               analyzedMoshFrame == moshFrame &&
               analyzedDuration == duration &&
               analyzedBlockSize == blockSize &&
               analyzedSearchRange == searchRange &&
               analyzedWidth == width &&
               analyzedHeight == height;
    }

    void Invalidate() {
        analysisState = AnalysisState::Invalid;
    }

    void Clear() {
        analysisState = AnalysisState::NotStarted;
        motionFields.clear();
        accumulatedFrames.clear();
        referenceFrame.Clear();
    }
};

// Flattened version for project serialization
struct MoshSequenceDataFlat {
    uint32_t version;
    int32_t analysisState;
    int32_t analyzedMoshFrame;
    int32_t analyzedDuration;
    int32_t analyzedBlockSize;
    int32_t analyzedSearchRange;
    int32_t analyzedWidth;
    int32_t analyzedHeight;
};

#define MOSH_SEQUENCE_DATA_VERSION 1

// Entry point declaration
#ifdef __cplusplus
extern "C" {
#endif

#ifdef AE_OS_WIN
#define DllExport __declspec(dllexport)
#else
#define DllExport __attribute__((visibility("default")))
#endif

DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra);

#ifdef __cplusplus
}
#endif

// Helper functions
inline int32_t BlockSizeFromIndex(int32_t index) {
    switch (index) {
        case BLOCK_SIZE_8:  return 8;
        case BLOCK_SIZE_32: return 32;
        default:            return 16;
    }
}

inline float ComputeLuminance(float b, float g, float r) {
    return 0.114f * b + 0.587f * g + 0.299f * r;
}

template<typename T>
inline T Clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}
