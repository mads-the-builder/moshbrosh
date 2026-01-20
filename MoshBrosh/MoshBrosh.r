/*
 * MoshBrosh - Datamosh Effect Plugin for Premiere Pro
 * PiPL Resource Definition
 */

#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"

resource 'PiPL' (16000) {
    {
        /* [1] Kind */
        Kind {
            AEEffect
        },

        /* [2] Name - displayed in Effects panel */
        Name {
            "MoshBrosh"
        },

        /* [3] Category - folder in Effects panel */
        Category {
            "Stylize"
        },

        /* [4] Code entry points */
#ifdef AE_OS_WIN
        CodeWin64X86 {"EffectMain"},
#else
        CodeMacARM64 {"EffectMain"},
        CodeMacIntel64 {"EffectMain"},
#endif

        /* [5] AE PiPL Version */
        AE_PiPL_Version {
            2,
            0
        },

        /* [6] Effect Spec Version */
        AE_Effect_Spec_Version {
            PF_PLUG_IN_VERSION,
            PF_PLUG_IN_SUBVERS
        },

        /* [7] Effect Version (major.minor in high word, revision in low) */
        AE_Effect_Version {
            0x00010000  /* 1.0.0 */
        },

        /* [8] Effect Info Flags */
        AE_Effect_Info_Flags {
            0
        },

        /* [9] Effect Global OutFlags
         * PF_OutFlag_PIX_INDEPENDENT = 0x00000040
         * PF_OutFlag_USE_OUTPUT_EXTENT = 0x00000400
         * Combined = 0x00000440
         */
        AE_Effect_Global_OutFlags {
            0x00000440
        },

        /* [10] Effect Global OutFlags2
         * PF_OutFlag2_FLOAT_COLOR_AWARE = 0x00000400
         * PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS = 0x00001000
         * Combined = 0x00001400
         */
        AE_Effect_Global_OutFlags_2 {
            0x00001400
        },

        /* [11] Match Name - unique identifier */
        AE_Effect_Match_Name {
            "MoshBrosh Datamosh"
        },

        /* [12] Reserved Info */
        AE_Reserved_Info {
            8
        }
    }
};
