#include "patches.h"

#define WS_FULLFRAME_ROOM_SCISSOR 1
#define WS_INSTSIZE_X4 1
#define WS_LODSCALE_DIV8 1
#define WS_DISTANCE_ALWAYS_DISABLED 1

#if 0
RECOMP_PATCH Gfx* dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;

    Gfx* gdl = (Gfx*) g_GfxBuffers[g_GfxActiveBufferIndex];

    // @recomp: Enable RT64 Extended GBI
    gEXEnable(gdl++);
    gEXSetRefreshRate(gdl++, 60 / speedgraphframes);

    return gdl;
}
#endif

// Set viewports (single player)
#if 1
RECOMP_PATCH Gfx* zbufClearCurrentPlayer(Gfx* gdl) {
    s32 start_x;
    s32 end_x;

    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, z_buffer_width, OS_K0_TO_PHYSICAL(z_buffer));
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetFillColor(gdl++, (GPACK_ZDZ(G_MAXFBZ, 0) << 16 | GPACK_ZDZ(G_MAXFBZ, 0)));

    // @recomp: use gEXSetScissor instead
    // gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());
    gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);

    if (getPlayerCount() < 3) {
        start_x = 0;
        end_x = viGetX() - 1;
    } else if ((get_cur_playernum() == 0) || (get_cur_playernum() == 2)) {
        start_x = 0;
        end_x = (viGetX() / 2) - 1;
    } else {
        start_x = viGetX() / 2;
        end_x = viGetX() - 1;
    }

    gDPFillRectangle(gdl++, start_x, 0, end_x + 240, (z_buffer_height - 1));
    gDPPipeSync(gdl++);

    return gdl;
}
#endif

#if 1
RECOMP_PATCH Gfx* bgScissorCurrentPlayerView(Gfx* gdl, s32 left, s32 top, s32 width, s32 height) {
    struct player* player = g_CurrentPlayer;

    if (left < (s32) player->viewleft) {
        left = (s32) player->viewleft;
    }

    if (top < (s32) player->viewtop) {
        top = (s32) player->viewtop;
    }

    if (player->viewleft + player->viewx < width) {
        width = player->viewleft + player->viewx;
    }

    if (player->viewtop + player->viewy < height) {
        height = player->viewtop + player->viewy;
    }

#if WS_FULLFRAME_ROOM_SCISSOR
    // @recomp: use gEXSetScissor instead
    // gDPSetScissor(gdl++, G_SC_NON_INTERLACE, left, top, width, height);
    gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
#else
    // This is closer to vanilla
    gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, left, top, width, height);
#endif

    return gdl;
}
#endif

#if 1
RECOMP_PATCH void modelSetDistanceDisabled(s32 param_1) {
#if WS_DISTANCE_ALWAYS_DISABLED
    // @recomp: ModelDistance always disabled
    if ((g_StageNum == LEVELID_JUNGLE)) {
        g_ModelDistanceDisabled = 0;
    } else {
        g_ModelDistanceDisabled = 1;
    }
#else
    // Honor the level's own setting.
    g_ModelDistanceDisabled = param_1;
#endif
}
#endif

#if 1
RECOMP_PATCH Gfx* currentPlayerDrawFade(Gfx* gdl) {
    f32 frac = g_CurrentPlayer->colourscreenfrac;
    s32 r = g_CurrentPlayer->colourscreenred;
    s32 g = g_CurrentPlayer->colourscreengreen;
    s32 b = g_CurrentPlayer->colourscreenblue;
    if ((cameraFrameCounter1 != 0) || (cameraFrameCounter2 != 0)) {
        frac = 1.0f;
        b = 0;
        g = 0;
        r = 0;
    }
    if (frac > 0) {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetColorDither(gdl++, G_CD_DISABLE);
        gDPSetTexturePersp(gdl++, G_TP_NONE);
        gDPSetAlphaCompare(gdl++, G_AC_NONE);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetTextureConvert(gdl++, G_TC_FILT);
        gDPSetTextureLUT(gdl++, G_TT_NONE);
        gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(gdl++, 0, 0, r, g, b, (s32) (frac * 255.0f));

        // gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(), (viGetViewLeft() + viGetViewWidth()),
        // (viGetViewTop() + viGetViewHeight()));

        // @recomp: Remove margins
        gDPFillRectangle(gdl++, 0, 0, 320, 240);

        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_BAYER);
        gDPSetTexturePersp(gdl++, G_TP_PERSP);
        gDPSetTextureLOD(gdl++, G_TL_LOD);
    }

    return gdl;
}
#endif

// @recomp: Culling of objects on the sides.
#if 1

extern int demoMode;

RECOMP_PATCH void bgUpdateCurrentPlayerScreenMinMax(void) /* @theboy181 - WS hacks */
{
    f32 fx, fy, fwidth, fheight;

    if (demoMode != 0 || (g_StageNum == LEVELID_JUNGLE && intro_camera_index == CAMERAMODE_INTRO)) {
        /* Use global bgViewRelated values (original min/max) */
        fx = (f32) bgViewRelated[0];
        fy = (f32) bgViewRelated[1];
        fwidth = (f32) viGetX() + (f32) bgViewRelated[2];
        fheight = (f32) viGetY() + (f32) bgViewRelated[3];

        /* X min */
        g_CurrentPlayer->screenxminf = (f32) viGetViewLeft();
        if (g_CurrentPlayer->screenxminf < fx)
            g_CurrentPlayer->screenxminf = fx;
        if (fwidth < g_CurrentPlayer->screenxminf)
            g_CurrentPlayer->screenxminf = fwidth;

        /* Y min */
        g_CurrentPlayer->screenyminf = (f32) viGetViewTop();
        if (g_CurrentPlayer->screenyminf < fy)
            g_CurrentPlayer->screenyminf = fy;
        if (fheight < g_CurrentPlayer->screenyminf)
            g_CurrentPlayer->screenyminf = fheight;

        /* X max */
        g_CurrentPlayer->screenxmaxf = (f32) (viGetViewLeft() + viGetViewWidth());
        if (g_CurrentPlayer->screenxmaxf < fx)
            g_CurrentPlayer->screenxmaxf = fx;
        if (fwidth < g_CurrentPlayer->screenxmaxf)
            g_CurrentPlayer->screenxmaxf = fwidth;

        /* Y max */
        g_CurrentPlayer->screenymaxf = (f32) (viGetViewTop() + viGetViewHeight());
        if (g_CurrentPlayer->screenymaxf < fy)
            g_CurrentPlayer->screenymaxf = fy;
        if (fheight < g_CurrentPlayer->screenymaxf)
            g_CurrentPlayer->screenymaxf = fheight;
    } else {
        /* Widescreen hack/fps fix path */
        fx = -320.0f * 2.0f;
        fy = 0.0f;
        fwidth = (f32) viGetX() * 3.0f;
        fheight = (f32) viGetY();

        /* X min */
        g_CurrentPlayer->screenxminf = (f32) viGetViewLeft() + 320.0f * -4.0f;
        if (g_CurrentPlayer->screenxminf < fx)
            g_CurrentPlayer->screenxminf = fx;
        if (fwidth < g_CurrentPlayer->screenxminf)
            g_CurrentPlayer->screenxminf = fwidth;

        /* Y min */
        g_CurrentPlayer->screenyminf = (f32) viGetViewTop();
        if (g_CurrentPlayer->screenyminf < fy)
            g_CurrentPlayer->screenyminf = fy;
        if (fheight < g_CurrentPlayer->screenyminf)
            g_CurrentPlayer->screenyminf = fheight;

        /* X max */
        g_CurrentPlayer->screenxmaxf = (f32) (viGetViewLeft() + viGetViewWidth() + 320.0f * 4.0f);
        if (g_CurrentPlayer->screenxmaxf < fx)
            g_CurrentPlayer->screenxmaxf = fx;
        if (fwidth < g_CurrentPlayer->screenxmaxf)
            g_CurrentPlayer->screenxmaxf = fwidth;

        /* Y max */
        g_CurrentPlayer->screenymaxf = (f32) (viGetViewTop() + viGetViewHeight());
        if (g_CurrentPlayer->screenymaxf < fy)
            g_CurrentPlayer->screenymaxf = fy;
        if (fheight < g_CurrentPlayer->screenymaxf)
            g_CurrentPlayer->screenymaxf = fheight;
    }
}
#endif

#if 1
extern int demoMode;

RECOMP_PATCH f32 getinstsize(Model* arg0) // @theboy181
{
    f32 ret = 0.0f;

#if defined(LEFTOVERDEBUG)
    if (arg0 == NULL) {
        osSyncPrintf("getinstsize: no objinst!\n");
        return_null();
    }

    if (arg0->obj == NULL) {
        osSyncPrintf("getinstsize: no objdesc!\n");
        return_null();
    }
#endif

#if WS_INSTSIZE_X4
    if (demoMode == 0) { // @theboy181 - fps fix - Demo timing fix
        ret = arg0->obj->BoundingVolumeRadius * arg0->scale * 4;
    } else {
        ret = arg0->obj->BoundingVolumeRadius * arg0->scale;
    } // @recomp:
#else
    // Closer to vanilla
    ret = arg0->obj->BoundingVolumeRadius * arg0->scale;
#endif

    return ret;
}
#endif

#if 1
RECOMP_PATCH f32 getPlayer_c_lodscalez(void) {
#if WS_LODSCALE_DIV8
    return g_CurrentPlayer->c_lodscalez / 8; // @theboy181 - Proper LOD fix?
#else
    // Vanilla LOD scale.
    return g_CurrentPlayer->c_lodscalez;
#endif
}
#endif

#if 0
RECOMP_PATCH Gfx* bgScissorCurrentPlayerViewDefault(Gfx* gdl)
{
#if 1
    static int counterstrike = 0;

    if (counterstrike++ % 20) {
        recomp_printf("g_CurrentPlayer->viewleft: %d g_CurrentPlayer->viewx: %d g_CurrentPlayer->viewtop: %d g_CurrentPlayer->viewy: %d sum: %d\n", 
            g_CurrentPlayer->viewleft,
            g_CurrentPlayer->viewx,
            g_CurrentPlayer->viewtop, 
            g_CurrentPlayer->viewy, 
            g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy);
    }
#endif

    return bgScissorCurrentPlayerView(
        gdl,
        g_CurrentPlayer->viewleft,
        g_CurrentPlayer->viewtop,
        g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx,
        g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy);
}
#endif

#if 0
RECOMP_PATCH s16 get_curplayer_viewport_ulx(void)
{
    if (2 < getPlayerCount())
    {
        if ((get_cur_playernum() == 1) || (get_cur_playernum() == 3))
        {
                return 161 ;
        }
    }

    return 0;
}
#endif

#if 0
extern ModelRenderData D_80031FD0;
s32 fogGetPropDistColor(PropRecord* prop, struct rgba_f32* color);
f32 chrobjFogVisRangeRelated(PropRecord* prop, f32 size);
f32 getinstsize(Model* arg0);
s32 sub_GAME_7F054A64(PropRecord* prop, bbox2d* bbox);
Gfx* bgScissorCurrentPlayerViewF(Gfx* arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4);
Gfx* bgScissorCurrentPlayerViewDefault(Gfx* arg0);
s32 objGetShotsTaken(ObjectRecord* obj);
void sub_GAME_7F040384(rgba_s32* arg0, s32 arg1, rgba_f32* arg2);
void sub_GAME_7F04AC20(PropRecord* prop, ModelRenderData*, s32 arg2);

RECOMP_PATCH Gfx* chrobjRenderProp(PropRecord* prop, Gfx* gdl, s32 arg2) __attribute__((optnone)) {
    struct rgba_f32 spB0;
    s32 spAC;
    s32 spA8;
    ModelRenderData mrData;
    struct view4f sp58;
    struct rgba_s32 sp48;
    s32 sp44;
    ObjectRecord* obj;
    s32 objAlpha;
    f32 temp_f0;
    s32 temp_v0_4;
    s32 phi_a0;

    obj = prop->obj;

    mrData = D_80031FD0;

    objAlpha = 0xFF;
    spAC = fogGetPropDistColor(prop, &spB0);

    if (spAC == 0) {
        return gdl;
    }

    if ((u8) (((PropDefHeaderRecord*)obj)->type) != PROPDEF_TINTED_GLASS) {
        temp_f0 = chrobjFogVisRangeRelated(prop, getinstsize(obj->model));

        if (((s32) prop->timetoregen > 0) && ((s32) prop->timetoregen < CHROBJ_TIMETOREGEN)) {
            temp_f0 *= ((CHROBJ_TIMETOREGEN_F - (f32) prop->timetoregen) / CHROBJ_TIMETOREGEN_F);
        }

        objAlpha = (s32) (temp_f0 * 255.0f);

        if (objAlpha <= 0) {
            return gdl;
        }
    }

    if ((objAlpha < 0xFF) || (obj->flags2 & 0x10000)) {
        if (arg2 == 0) {
            return gdl;
        }

        sp44 = 3;
    } else {

        sp44 = (arg2 == 0) ? 1 : 2;
    }

    if ((sub_GAME_7F054A64(prop, &sp58) > 0) && (((s32) obj->flags2 << 5) >= 0)) {
        gdl = bgScissorCurrentPlayerViewF(gdl, sp58.left, sp58.top, sp58.width, sp58.height);
    } else {
        gdl = bgScissorCurrentPlayerViewDefault(gdl);
    }

    mrData.flags = sp44;
    mrData.zbufferenabled = (obj->flags2 & 0x10000) == 0;

    mrData.gdl = gdl;

    if (objAlpha < 0xFF) {
        mrData.PropType = 5;
        mrData.envcolour.word = objAlpha;
    } else {
        mrData.PropType = 9;

        if (((PropDefHeaderRecord*)obj)->type == PROPDEF_TINTED_GLASS) {
            mrData.envcolour.word = ((struct TintedGlassRecord*) obj)->calculatedopacity << 8;
        } else if ((((PropDefHeaderRecord*)obj)->type == PROPDEF_DOOR) && ((((struct DoorRecord*) obj)->doorFlags & 2) != 0)) {
            mrData.envcolour.word = ((struct DoorRecord*) obj)->calculatedopacity << 8;
        } else {
            mrData.envcolour.word = 0;
        }
    }

    temp_v0_4 = objGetShotsTaken(obj);
    phi_a0 = 0xFF - (temp_v0_4 * 0x15);

    if (phi_a0 < 0) {
        phi_a0 = 0;
    }

    sp48.r = (s32) (obj->shadecol.rgba[0] * phi_a0) >> 8;
    sp48.g = (s32) (obj->shadecol.rgba[1] * phi_a0) >> 8;
    sp48.b = (s32) (obj->shadecol.rgba[2] * phi_a0) >> 8;
    sp48.a = obj->shadecol.rgba[3] + temp_v0_4 * 0xF;

    if (sp48.a >= 0x100) {
        sp48.a = 0xFF;
    }

    sub_GAME_7F040384(&sp48, spAC, &spB0);

    mrData.fogcolour.word =
        (sp48.rgba[0] << 0x18) | (sp48.rgba[1] << 0x10) | (sp48.rgba[2] << 0x08) | (sp48.rgba[3] << 0x00);

    sub_GAME_7F04AC20(prop, &mrData, arg2);

    return mrData.gdl;
}

#endif

// Intro GunBarrel stretch fix
#if 1
extern f32 D_8002BB00;
extern f32 D_8002BB04;
extern f32 D_8002BB08;
extern f32 D_8002BB0C;
extern f32 D_8002BB10;
extern f32 D_8002BB14;

struct FolderSelect {
    s32 unk00;
    s32 unk04;
    s32 unk08;
};

extern f32 D_8002BB00;
extern f32 D_8002BB04;
extern f32 D_8002BB08;
extern f32 D_8002BB0C;
extern f32 D_8002BB10;
extern f32 D_8002BB14;

RECOMP_PATCH Gfx* sub_GAME_7F01B240(Gfx* gdl, s32 imgIndex, s32 x, struct FolderSelect* arg3,
                                    struct FolderSelect* arg4) {
    f32 temp_f0;
    f32 temp_f12;
    f32 temp_f14;
    f32 temp_f14_2;
    f32 temp_f16;
    f32 temp_f18;
    f32 temp_f2;
    s32 i;
    s32 var_t1;

    temp_f0 = arg3->unk00;
    temp_f2 = arg3->unk04;
    temp_f12 = arg3->unk08;
    temp_f14 = arg4->unk00;
    temp_f16 = arg4->unk04;
    temp_f18 = arg4->unk08;
    D_8002BB0C = temp_f14;
    D_8002BB10 = temp_f16;
    D_8002BB14 = temp_f18;
    gEXEnable(gdl++);
    i = 0;
    while ((i + 1) < 300) {
        gDPLoadTextureBlock(gdl++, imgIndex, G_IM_FMT_I, G_IM_SIZ_8b, 440, 1, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

        gDPSetPrimColor(gdl++, 0, 0, temp_f0 + (((temp_f14 - temp_f0) * i) / 299.0f),
                        temp_f2 + (((temp_f16 - temp_f2) * i) / 299.0f),
                        temp_f12 + (((temp_f18 - temp_f12) * i) / 299.0f), 255);

        if (x < 0) {
            gEXTextureRectangle(gdl++, G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, 0 - 880, (i + 0x10) << 2,
                                ((440 << 2) - 1) - 880, ((((i + 1) + 0x10)) << 2) - 1, G_TX_RENDERTILE, (-x) << 5, 0,
                                1 << 10, 1 << 10);
        } else {
            gEXTextureRectangle(gdl++, G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, (x << 2) - 880, (i + 0x10) << 2,
                                ((440 << 2) - 1) - 880, ((((i + 1) + 0x10)) << 2) - 1, G_TX_RENDERTILE, 0, 0, 1 << 10,
                                1 << 10);
        }
        i++;
        imgIndex += 440;
    }
    D_8002BB08 = temp_f12;
    D_8002BB04 = temp_f2;
    D_8002BB00 = temp_f0;

    return gdl;
}
#endif

#if 1
extern s32 g_ContRumblePakInitState[4];
extern OSMesgQueue g_ContInputMessageQueue;
extern OSPfs g_ContPfs[4];
extern OSContStatus g_ContStatus[4];
RECOMP_PATCH void joyRumblePakInit(s32 index) {
    s32 ret;

#if 0
   if (g_ContRumblePakInitState[index] > -1) {
       recomp_printf("if (g_ContRumblePakInitState[index] > -1) {\n");
       if ((g_ContStatus[index].type & CONT_JOYPORT) && (g_ContStatus[index].status & CONT_CARD_ON)) {
           ret = osPfsInit_recomp(&g_ContInputMessageQueue, &g_ContPfs[index], index);
           recomp_printf ("ret: %d\n", ret);
           recomp_printf("if ((g_ContStatus[index].type & CONT_JOYPORT) && (g_ContStatus[index].status & CONT_CARD_ON)) {\n");

           if ((ret == PFS_ERR_ID_FATAL) || (ret == PFS_ERR_DEVICE)) {
               if (osMotorInit_recomp(&g_ContInputMessageQueue, &g_ContPfs[index], index) == 0) {
                   g_ContRumblePakInitState[index] = 1;
                   recomp_printf("RUMBLEPAKINITSTATE_READY\n");
               } else {
                   g_ContRumblePakInitState[index] = -1;
                   recomp_printf("RUMBLEPAKINITSTATE_ERROR\n");
               }
           }
       }
   }
#endif

    // @recomp: force rumble pak plugged in.
    g_ContRumblePakInitState[index] = 1;
}
#endif