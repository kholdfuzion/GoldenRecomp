//
// RT64
//

#pragma once

#define G_MDSFT_ALPHACOMPARE 0
#define G_AC_NONE (0 << G_MDSFT_ALPHACOMPARE)
#define G_AC_THRESHOLD (1 << G_MDSFT_ALPHACOMPARE)
#define G_AC_DITHER (3 << G_MDSFT_ALPHACOMPARE)

#define G_MDSFT_ZSRCSEL 2
#define G_ZS_PIXEL (0 << G_MDSFT_ZSRCSEL)
#define G_ZS_PRIM (1 << G_MDSFT_ZSRCSEL)

#define	G_MDSFT_ALPHADITHER 4
#define	G_AD_PATTERN (0 << G_MDSFT_ALPHADITHER)
#define	G_AD_NOTPATTERN (1 << G_MDSFT_ALPHADITHER)
#define	G_AD_NOISE (2 << G_MDSFT_ALPHADITHER)
#define	G_AD_DISABLE (3 << G_MDSFT_ALPHADITHER)

#define	G_MDSFT_RGBDITHER 6
#define G_CD_MAGICSQ (0 << G_MDSFT_RGBDITHER)
#define G_CD_BAYER (1 << G_MDSFT_RGBDITHER)
#define G_CD_NOISE (2 << G_MDSFT_RGBDITHER)
#define G_CD_DISABLE (3 << G_MDSFT_RGBDITHER)

#define G_MDSFT_COMBKEY 8
#define G_CK_NONE (0 << G_MDSFT_COMBKEY)
#define G_CK_KEY (1 << G_MDSFT_COMBKEY)

#define G_MDSFT_TEXTFILT 12
#define G_TF_POINT (0 << G_MDSFT_TEXTFILT)
#define G_TF_AVERAGE (3 << G_MDSFT_TEXTFILT)
#define G_TF_BILERP (2 << G_MDSFT_TEXTFILT)

#define G_MDSFT_TEXTLUT 14
#define G_TT_NONE (0 << G_MDSFT_TEXTLUT)
#define G_TT_RGBA16 (2 << G_MDSFT_TEXTLUT)
#define G_TT_IA16 (3 << G_MDSFT_TEXTLUT)

#define G_MDSFT_TEXTLOD 16
#define G_TL_TILE (0 << G_MDSFT_TEXTLOD)
#define G_TL_LOD (1 << G_MDSFT_TEXTLOD)

#define G_MDSFT_TEXTDETAIL 17
#define G_TD_CLAMP (0 << G_MDSFT_TEXTDETAIL)
#define G_TD_SHARPEN (1 << G_MDSFT_TEXTDETAIL)
#define G_TD_DETAIL (2 << G_MDSFT_TEXTDETAIL)

#define G_MDSFT_TEXTPERSP 19
#define G_TP_NONE (0 << G_MDSFT_TEXTPERSP)
#define G_TP_PERSP (1 << G_MDSFT_TEXTPERSP)

#define G_MDSFT_CYCLETYPE 20
#define G_CYC_1CYCLE (0 << G_MDSFT_CYCLETYPE)
#define G_CYC_2CYCLE (1 << G_MDSFT_CYCLETYPE)
#define G_CYC_COPY (2 << G_MDSFT_CYCLETYPE)
#define G_CYC_FILL (3 << G_MDSFT_CYCLETYPE)

#define G_MAXZ 0x03ff
#define G_TX_RENDERTILE 0
#define G_TX_LOADTILE 7
#define G_TX_NOMIRROR 0
#define G_TX_WRAP 0
#define G_TX_MIRROR 1
#define G_TX_CLAMP 2
#define G_TX_NOMASK 0
#define G_TX_NOLOD  0
#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3
#define G_IM_FMT_RGBA 0
#define G_IM_FMT_YUV 1
#define G_IM_FMT_CI 2
#define G_IM_FMT_IA 3
#define G_IM_FMT_I 4
#define G_IM_FMT_DEPTH 5 // Added to simplify some code paths.
#define G_MWO_POINT_RGBA 0x10
#define G_MWO_POINT_ST 0x14
#define G_MWO_POINT_XYSCREEN 0x18
#define G_MWO_POINT_ZSCREEN 0x1C
#define AA_EN 0x8
#define Z_CMP 0x10
#define Z_UPD 0x20
#define IM_RD 0x40
#define CLR_ON_CVG 0x80
#define CVG_DST_CLAMP 0
#define CVG_DST_WRAP 0x100
#define CVG_DST_FULL 0x200
#define CVG_DST_SAVE 0x300
#define ZMODE_OPA 0
#define ZMODE_INTER 0x400
#define ZMODE_XLU 0x800
#define ZMODE_DEC 0xc00
#define ZMODE_MASK ZMODE_DEC
#define CVG_X_ALPHA 0x1000
#define ALPHA_CVG_SEL 0x2000
#define FORCE_BL 0x4000
#define G_SHADE 0x00000004
#define G_FOG 0x00010000
#define G_LIGHTING 0x00020000
#define G_POINT_LIGHTING 0x00400000
#define G_TEXTURE_GEN 0x00040000
#define G_TEXTURE_GEN_LINEAR 0x00080000
#define G_CLIPPING 0x00800000
#define G_MW_MATRIX 0x00
#define G_MW_NUMLIGHT 0x02
#define G_MW_CLIP 0x04
#define G_MW_SEGMENT 0x06
#define G_MW_FOG 0x08
#define G_MW_GENSTAT 0x08
#define G_MW_LIGHTCOL 0x0a
#define G_MW_PERSPNORM 0x0e
#define G_MWO_NUMLIGHT 0x00
#define G_MWO_CLIP_RNX 0x04
#define G_MWO_CLIP_RNY 0x0c
#define G_MWO_CLIP_RPX 0x14
#define G_MWO_CLIP_RPY 0x1c
#define G_MWO_SEGMENT_0 0x00
#define G_MWO_SEGMENT_1 0x01
#define G_MWO_SEGMENT_2 0x02
#define G_MWO_SEGMENT_3 0x03
#define G_MWO_SEGMENT_4 0x04
#define G_MWO_SEGMENT_5 0x05
#define G_MWO_SEGMENT_6 0x06
#define G_MWO_SEGMENT_7 0x07
#define G_MWO_SEGMENT_8 0x08
#define G_MWO_SEGMENT_9 0x09
#define G_MWO_SEGMENT_A 0x0a
#define G_MWO_SEGMENT_B 0x0b
#define G_MWO_SEGMENT_C 0x0c
#define G_MWO_SEGMENT_D 0x0d
#define G_MWO_SEGMENT_E 0x0e
#define G_MWO_SEGMENT_F 0x0f
#define G_MWO_FOG 0x00
#define G_MWO_aLIGHT_1 0x00
#define G_MWO_bLIGHT_1 0x04
#define G_NOOP 0x00
#define G_SETCIMG 0xff
#define G_SETZIMG 0xfe
#define G_SETTIMG 0xfd
#define G_SETCOMBINE 0xfc
#define G_SETENVCOLOR 0xfb
#define G_SETPRIMCOLOR 0xfa
#define G_SETBLENDCOLOR 0xf9
#define G_SETFOGCOLOR 0xf8
#define G_SETFILLCOLOR 0xf7
#define G_FILLRECT 0xf6
#define G_SETTILE 0xf5
#define G_LOADTILE 0xf4
#define G_LOADBLOCK 0xf3
#define G_SETTILESIZE 0xf2
#define G_LOADTLUT 0xf0
#define G_RDPSETOTHERMODE 0xef
#define G_SETPRIMDEPTH 0xee
#define G_SETSCISSOR 0xed
#define G_SETCONVERT 0xec
#define G_SETKEYR 0xeb
#define G_SETKEYGB 0xea
#define G_TEXRECT 0xe4
#define G_TEXRECTFLIP 0xe5
#define G_RDPLOADSYNC 0xe6
#define G_RDPPIPESYNC 0xe7
#define G_RDPTILESYNC 0xe8
#define G_RDPFULLSYNC 0xe9
#define G_RDPNOOP 0xC0
#define G_RDPTRI_BASE 0x08
