#include "patches.h"
#include "gbi_extension.h"

/**
 * @recomp sky fix
 *
 * The original skyRender rasterises the sky/water planes on the CPU into raw
 * RDP triangle commands, which RT64's HLE cannot execute.
 * The new PORTSKY code rasterises sky polygons with real RSP geometry.
 * To achieve a result that's close to console, we draw a camera-centred ring fan
 * over the player's head (skyDrawPlaneFan). It reaches very close to zfar and
 * can be tilted according the direction the player is looking at to simulate
 * the concave sky used on the Streets level (and maybe the Runway level).
 */

#define PORTSKY 1

#define SKYDBG 0

#define SKY_PLANE_RADIUS 70000.0f

#if SKYDBG
static u32 g_SkyDbgFrame = 0;
static u8 g_SkyDbgPrint = 0;
#endif

// KSEG0 -> physical address
#define SKY_K0_TO_PHYS(p) ((u32) (p) & 0x1FFFFFFF)

#if PORTSKY

#define SKY_TEXEL_PER_WORLD 0.2f

#define SKY_CLOUD_SCALE 2.0f

#define SKY_BAND_OUTER 290000.0f

#define SKY_MIP_LEVELS 3

// 22.5-degree direction table for the sky's disc
static const f32 DIR16[16][2] = {
    { 1.0f, 0.0f },       { 0.92388f, 0.38268f },   { 0.7071f, 0.7071f },   { 0.38268f, 0.92388f },
    { 0.0f, 1.0f },       { -0.38268f, 0.92388f },  { -0.7071f, 0.7071f },  { -0.92388f, 0.38268f },
    { -1.0f, 0.0f },      { -0.92388f, -0.38268f }, { -0.7071f, -0.7071f }, { -0.38268f, -0.92388f },
    { 0.0f, -1.0f },      { 0.38268f, -0.92388f },  { 0.7071f, -0.7071f },  { 0.92388f, -0.38268f },
};

static Gfx* skyDrawPlaneFan(Gfx* gdl, f32 plane_y, bool isWater) {
    s32 i;
    s32 k;
    f32 maxc;
    f32 fit = 1.0f;

    f32 wx[25], wz[25];
    f32 tcs[25], tct[25];
    f32 radius[3];
    f32 smin, tmin;
    f32 rsp_y;
    f32 h;
    f32 uoff;
    f32 texdens;
    f32 tofft;
    f32 depth_s = 1.0f;

    f32 bx[64], bz[64];
    f32 bcx[64], bcz[64];
    f32 r_mid;
    f32 r_outer;
    s32 m;
    s32 j;

    s32 use_mips = FALSE;
    u8* miptex = NULL;
    coord3d* eye = bondviewGetCurrentPlayersPosition();

    Gfx* bp = gdl++;
    Vtx* verts = (Vtx*) gdl;
    Mtx* mtx_render;
    Mtxf fitmtx;
    SkyRelated18 cols[8];

    gdl = (Gfx*) ((u8*) gdl + 281 * sizeof(Vtx));
    mtx_render = (Mtx*) gdl;
    gdl = (Gfx*) ((u8*) gdl + sizeof(Mtx));
    if (!isWater) {
        // Room for the generated cloud mip chain: 32*32 + 16*16 + 8*8 IA8.
        miptex = (u8*) gdl;
        gdl = (Gfx*) ((u8*) gdl + 1344);
    }
    gSPBranchList(bp, SKY_K0_TO_PHYS(gdl));

    if (!isWater) {
        sImageTableEntry* simg = &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId];

        if ((simg->width == 64) && (simg->height == 64) && (simg->depth == G_IM_SIZ_8b) &&
            (simg->format == G_IM_FMT_IA) && (simg->level == 0) && (simg->index >= 0x1000)) {
            u8* msrc = (u8*) (simg->index | 0x80000000);
            u8* mdst = miptex;
            s32 mw = 64;
            s32 lvl;
            for (lvl = 0; lvl < SKY_MIP_LEVELS; lvl++) {
                s32 hw = mw >> 1;
                s32 mx, my;
                for (my = 0; my < hw; my++) {
                    for (mx = 0; mx < hw; mx++) {
                        u8 t00 = msrc[(my * 2) * mw + (mx * 2)];
                        u8 t01 = msrc[(my * 2) * mw + (mx * 2) + 1];
                        u8 t10 = msrc[(my * 2 + 1) * mw + (mx * 2)];
                        u8 t11 = msrc[(my * 2 + 1) * mw + (mx * 2) + 1];
                        s32 mi = ((t00 >> 4) + (t01 >> 4) + (t10 >> 4) + (t11 >> 4) + 2) >> 2;
                        s32 ma = ((t00 & 0xF) + (t01 & 0xF) + (t10 & 0xF) + (t11 & 0xF) + 2) >> 2;
                        mdst[my * hw + mx] = (u8) ((mi << 4) | ma);
                    }
                }
                msrc = mdst;
                mdst += hw * hw;
                mw = hw;
            }
            use_mips = TRUE;
        }
    }

    if (isWater) {
        h = plane_y - eye->y;
        if (h < 0.0f) {
            h = -h;
        }
        if (h < 50.0f) {
            h = 50.0f;
        }
    } else {
        h = plane_y;
        if (h < 500.0f) {
            h = 500.0f;
        }
    }

    radius[2] = 10.0f * h;
    if (radius[2] > SKY_PLANE_RADIUS * 0.8f) {
        radius[2] = SKY_PLANE_RADIUS * 0.8f;
    }
    radius[1] = 4.0f * h;
    if (radius[1] > radius[2] * 0.6f) {
        radius[1] = radius[2] * 0.6f;
    }
    radius[0] = 2.0f * h;
    if (radius[0] > radius[1] * 0.6f) {
        radius[0] = radius[1] * 0.6f;
    }

    wx[0] = eye->x;
    wz[0] = eye->z;
    for (k = 0; k < 3; k++) {
        f32 r = radius[k];
        f32 rd = r * 0.7071f;
        f32* px = &wx[1 + k * 8];
        f32* pz = &wz[1 + k * 8];
        px[0] = eye->x + r;   pz[0] = eye->z;
        px[1] = eye->x + rd;  pz[1] = eye->z + rd;
        px[2] = eye->x;       pz[2] = eye->z + r;
        px[3] = eye->x - rd;  pz[3] = eye->z + rd;
        px[4] = eye->x - r;   pz[4] = eye->z;
        px[5] = eye->x - rd;  pz[5] = eye->z - rd;
        px[6] = eye->x;       pz[6] = eye->z - r;
        px[7] = eye->x + rd;  pz[7] = eye->z - rd;
    }

    // Adjust disc radius to z-buffer range
    {
        f32 zrange[2];
        f32 near_floor;
        viGetZRange(zrange);
        depth_s = (0.85f * zrange[1]) / SKY_BAND_OUTER;
        if (depth_s > 1.0f) {
            depth_s = 1.0f;
        }
        near_floor = 1.5f * zrange[0];
        if (h * depth_s < near_floor) {
            depth_s = near_floor / h;
        }
        r_outer = (0.85f * zrange[1]) / depth_s;
        if (r_outer > SKY_BAND_OUTER) {
            r_outer = SKY_BAND_OUTER;
        }
        if (r_outer < SKY_PLANE_RADIUS * 1.05f) {
            r_outer = SKY_PLANE_RADIUS * 1.05f;
        }
        r_mid = (SKY_PLANE_RADIUS + r_outer) * 0.5f;
    }

    // Color the sky via fog
    {
        f32 fr[8];
        fr[0] = 0.0f;
        for (k = 0; k < 3; k++) {
            f32 f = 1.0f - (2.0f * h) / radius[k];
            if (f < 0.0f) f = 0.0f;
            fr[k + 1] = f;
        }
        fr[4] = 1.0f - (2.0f * h) / SKY_PLANE_RADIUS;
        if (fr[4] < 0.0f) fr[4] = 0.0f;
        fr[5] = 1.0f - (2.0f * h) / r_mid;
        if (fr[5] < 0.0f) fr[5] = 0.0f;
        fr[6] = 1.0f - (2.0f * h) / r_outer;
        if (fr[6] < 0.0f) fr[6] = 0.0f;
        fr[7] = 1.0f;
        for (k = 0; k < 8; k++) {
            if (isWater) {
                sub_GAME_7F093FA4(&cols[k], fr[k]);
            } else {
                skyChooseCloudVtxColour(&cols[k], fr[k]);
            }
        }
    }

    // Scrolling UVs
    uoff = isWater ? g_SkyCloudOffset : 0.0f;
    texdens = isWater ? SKY_TEXEL_PER_WORLD : SKY_TEXEL_PER_WORLD / SKY_CLOUD_SCALE;
    tofft = isWater ? g_SkyCloudOffset : g_SkyCloudOffset / SKY_CLOUD_SCALE;
    smin = 3.4e38f;
    tmin = 3.4e38f;
    for (i = 0; i < 25; i++) {
        tcs[i] = wx[i] * texdens + uoff;
        tct[i] = wz[i] * texdens + tofft;
        if (tcs[i] < smin) smin = tcs[i];
        if (tct[i] < tmin) tmin = tct[i];
    }

    smin = (f32) (((s32) (smin / 4096.0f)) * 4096);
    tmin = (f32) (((s32) (tmin / 4096.0f)) * 4096);

    rsp_y = (isWater ? (plane_y - eye->y) : h) * depth_s;
    maxc = rsp_y < 0.0f ? -rsp_y : rsp_y;
    for (i = 0; i < 25; i++) {
        f32 cx = (wx[i] - eye->x) * depth_s;
        f32 cz = (wz[i] - eye->z) * depth_s;
        f32 c;
        wx[i] = cx;
        wz[i] = cz;
        c = cx < 0.0f ? -cx : cx;
        if (c > maxc) maxc = c;
        c = cz < 0.0f ? -cz : cz;
        if (c > maxc) maxc = c;
    }

    for (m = 0; m < 16; m++) {
        f32 attach_r = (m & 1) ? radius[2] * 0.92388f : radius[2];
        bx[m] = eye->x + attach_r * DIR16[m][0];
        bz[m] = eye->z + attach_r * DIR16[m][1];
        bx[16 + m] = eye->x + SKY_PLANE_RADIUS * DIR16[m][0];
        bz[16 + m] = eye->z + SKY_PLANE_RADIUS * DIR16[m][1];
        bx[32 + m] = eye->x + r_mid * DIR16[m][0];
        bz[32 + m] = eye->z + r_mid * DIR16[m][1];
        bx[48 + m] = eye->x + r_outer * DIR16[m][0];
        bz[48 + m] = eye->z + r_outer * DIR16[m][1];
    }
    for (m = 0; m < 64; m++) {
        f32 c;
        bcx[m] = (bx[m] - eye->x) * depth_s;
        bcz[m] = (bz[m] - eye->z) * depth_s;
        c = bcx[m] < 0.0f ? -bcx[m] : bcx[m];
        if (c > maxc) maxc = c;
        c = bcz[m] < 0.0f ? -bcz[m] : bcz[m];
        if (c > maxc) maxc = c;
    }

    if (maxc > 32000.0f) {
        fit = 32000.0f / maxc;
    }

    guScaleF(fitmtx.m, 1.0f / fit, 1.0f / fit, 1.0f / fit);

    // Handle sky concavity
    {
        Mtxf viewrot;
        Mtxf composed;
        Mtxf* wts = camGetWorldToScreenMtxf();
        f32 conc = fogGetCurrentEnvironmentp()->WaterConcavity;
        for (i = 0; i < 4; i++) {
            for (k = 0; k < 4; k++) {
                viewrot.m[i][k] = wts->m[i][k];
            }
        }
        viewrot.m[3][0] = 0.0f;
        viewrot.m[3][1] = 0.0f;
        viewrot.m[3][2] = 0.0f;

        matrix_4x4_multiply(&viewrot, &fitmtx, &composed);

        if (conc != 0.0f && !isWater) {
            Mtxf pitch;
            Mtxf lifted;
            f32 t = conc / (currentPlayerGetProjectionMatrixF()->m[1][1] * (getPlayer_c_screenheight() * 0.5f));
            f32 cs = 1.0f / sqrtf(1.0f + t * t);
            f32 sn = t * cs;
            guScaleF(pitch.m, 1.0f, 1.0f, 1.0f);
            pitch.m[1][1] = cs;
            pitch.m[1][2] = sn;
            pitch.m[2][1] = -sn;
            pitch.m[2][2] = cs;
            matrix_4x4_multiply(&pitch, &composed, &lifted);
            composed = lifted;
        }

        matrix_4x4_f32_to_s32(&composed, (Mtxf*) mtx_render);
    }

    for (i = 0; i < 25; i++) {
        SkyRelated18* col;
        if (i == 0) col = &cols[0];
        else if (i < 9) col = &cols[1];
        else if (i < 17) col = &cols[2];
        else col = &cols[3];
        verts[i].v.ob[0] = wx[i] * fit;
        verts[i].v.ob[1] = rsp_y * fit;
        verts[i].v.ob[2] = wz[i] * fit;
        verts[i].v.tc[0] = skyClamp(tcs[i] - smin, -32768.f, 32767.f);
        verts[i].v.tc[1] = skyClamp(tct[i] - tmin, -32768.f, 32767.f);
#if SKYDBG
        verts[i].v.cn[0] = fogGetCurrentEnvironmentp()->Red;
        verts[i].v.cn[1] = fogGetCurrentEnvironmentp()->Green;
        verts[i].v.cn[2] = fogGetCurrentEnvironmentp()->Blue;
        verts[i].v.cn[3] = 0xff;
#else
        verts[i].v.cn[0] = col->r;
        verts[i].v.cn[1] = col->g;
        verts[i].v.cn[2] = col->b;
        verts[i].v.cn[3] = col->a;
#endif
    }

    for (j = 0; j < 3; j++) {
        f32 uvscale = texdens;
        f32 period = 4096.0f;
        f32 su = uoff;
        f32 sv = tofft;
        if (use_mips) {
            uvscale /= (f32) (2 << j);
            period = (f32) (4096 >> (j + 1));
            su = uoff / (f32) (2 << j);
            sv = tofft / (f32) (2 << j);
        }
        for (m = 0; m < 16; m++) {
            s32 m2 = (m + 1) & 15;
            s32 q[4];
            f32 qs[4], qt[4];
            f32 qsmin, qtmin;
            Vtx* v = verts + 25 + (j * 16 + m) * 4;
            q[0] = j * 16 + m;
            q[1] = j * 16 + m2;
            q[2] = (j + 1) * 16 + m;
            q[3] = (j + 1) * 16 + m2;
            for (k = 0; k < 4; k++) {
                qs[k] = bx[q[k]] * uvscale + su;
                qt[k] = bz[q[k]] * uvscale + sv;
            }
            qsmin = qs[0];
            qtmin = qt[0];
            for (k = 1; k < 4; k++) {
                if (qs[k] < qsmin) qsmin = qs[k];
                if (qt[k] < qtmin) qtmin = qt[k];
            }
            qsmin = (f32) (((s32) (qsmin / period)) * period);
            qtmin = (f32) (((s32) (qtmin / period)) * period);
            for (k = 0; k < 4; k++) {
                SkyRelated18* bcol = (k < 2) ? &cols[3 + j] : &cols[4 + j];
                v[k].v.ob[0] = bcx[q[k]] * fit;
                v[k].v.ob[1] = rsp_y * fit;
                v[k].v.ob[2] = bcz[q[k]] * fit;
                v[k].v.tc[0] = skyClamp(qs[k] - qsmin, -32768.f, 32767.f);
                v[k].v.tc[1] = skyClamp(qt[k] - qtmin, -32768.f, 32767.f);
                v[k].v.cn[0] = bcol->r;
                v[k].v.cn[1] = bcol->g;
                v[k].v.cn[2] = bcol->b;
                v[k].v.cn[3] = bcol->a;
            }
        }
    }

    for (m = 0; m < 16; m++) {
        s32 m2 = (m + 1) & 15;
        Vtx* v = verts + 217 + m * 4;
        for (k = 0; k < 4; k++) {
            s32 src = 48 + ((k & 1) ? m2 : m);
            SkyRelated18* scol = (k < 2) ? &cols[6] : &cols[7];
            v[k].v.ob[0] = bcx[src] * fit;
            v[k].v.ob[1] = (k < 2) ? (s16) (rsp_y * fit) : 0;
            v[k].v.ob[2] = bcz[src] * fit;
            v[k].v.tc[0] = 0;
            v[k].v.tc[1] = 0;
            v[k].v.cn[0] = scol->r;
            v[k].v.cn[1] = scol->g;
            v[k].v.cn[2] = scol->b;
            v[k].v.cn[3] = scol->a;
        }
    }

    // Clear culling and G_FOG because the new sky uses its own fog
    gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_FOG);

#if SKYDBG
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
#endif

    gSPMatrix(gdl++, SKY_K0_TO_PHYS(mtx_render), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

    // Batch 1: centre fan (verts 0-8).
    gSPVertex(gdl++, SKY_K0_TO_PHYS(verts), 9, 0);
    gSP4Triangles(gdl++, 0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5);
    gSP4Triangles(gdl++, 0, 5, 6, 0, 6, 7, 0, 7, 8, 0, 8, 1);

    // Batch 2: ring r1 -> r2 (verts 1-16 as slots 0-15: inner k, outer k+8).
    gSPVertex(gdl++, SKY_K0_TO_PHYS(verts + 1), 16, 0);
    for (k = 0; k < 8; k += 2) {
        s32 a = k, b = (k + 1) & 7, a2 = k + 8, b2 = ((k + 1) & 7) + 8;
        s32 c = k + 1, d = (k + 2) & 7, c2 = k + 1 + 8, d2 = ((k + 2) & 7) + 8;
        gSP4Triangles(gdl++, a, b, b2, b2, a2, a, c, d, d2, d2, c2, c);
    }

    // Batch 3: ring r2 -> r3 (verts 9-24 as slots 0-15).
    gSPVertex(gdl++, SKY_K0_TO_PHYS(verts + 9), 16, 0);
    for (k = 0; k < 8; k += 2) {
        s32 a = k, b = (k + 1) & 7, a2 = k + 8, b2 = ((k + 1) & 7) + 8;
        s32 c = k + 1, d = (k + 2) & 7, c2 = k + 1 + 8, d2 = ((k + 2) & 7) + 8;
        gSP4Triangles(gdl++, a, b, b2, b2, a2, a, c, d, d2, d2, c2, c);
    }

    // Batches 4-6: textured outer bands why mips so it looks closer to console
    for (j = 0; j < 3; j++) {
        if (use_mips) {
            s32 mw = 32 >> j;
            u8* mlvl = miptex + ((j == 0) ? 0 : (j == 1) ? 1024 : 1280);
            sImageTableEntry* simg = &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId];
            gDPPipeSync(gdl++);
            gDPLoadTextureBlock(gdl++, SKY_K0_TO_PHYS(mlvl), G_IM_FMT_IA, G_IM_SIZ_8b, mw, mw, 0,
                                simg->flagsS, simg->flagsT, 5 - j, 5 - j, G_TX_NOLOD, G_TX_NOLOD);
        }
        for (m = 0; m < 16; m++) {
            gSPVertex(gdl++, SKY_K0_TO_PHYS(verts + 25 + (j * 16 + m) * 4), 4, 0);
            gSP2Triangles(gdl++, 0, 1, 3, 0, 3, 2, 0, 0);
        }
    }

    // Batch 7: Shading
    gDPPipeSync(gdl++);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    for (k = 0; k < 16; k++) {
        gSPVertex(gdl++, SKY_K0_TO_PHYS(verts + 217 + k * 4), 4, 0);
        gSP2Triangles(gdl++, 0, 1, 3, 0, 3, 2, 0, 0);
    }

#if SKYDBG
    {
        if (g_SkyDbgPrint) {
            recomp_printf("SKYFAN water=%d planey=%d eye=(%d,%d,%d) h=%d rspy=%d fit1e6=%d tc0=%d tc1=%d tc9=%d vp=%x phys=%x w0=%x cn0=%x\n",
                          isWater, (s32) plane_y, (s32) eye->x, (s32) eye->y, (s32) eye->z, (s32) h,
                          (s32) rsp_y, (s32) (fit * 1000000.0f),
                          verts[0].v.tc[0], verts[1].v.tc[0], verts[9].v.tc[0],
                          (u32) verts, SKY_K0_TO_PHYS(verts), *(u32*) verts,
                          *(u32*) &verts[0].v.cn[0]);
            recomp_printf(" cols0=(%d,%d,%d,%d) cols2=(%d,%d,%d,%d) cols3=(%d,%d,%d,%d) env=(%d,%d,%d) cloudRGB=(%d,%d,%d)\n",
                          cols[0].r, cols[0].g, cols[0].b, cols[0].a,
                          cols[2].r, cols[2].g, cols[2].b, cols[2].a,
                          cols[3].r, cols[3].g, cols[3].b, cols[3].a,
                          fogGetCurrentEnvironmentp()->Red, fogGetCurrentEnvironmentp()->Green,
                          fogGetCurrentEnvironmentp()->Blue,
                          (s32) fogGetCurrentEnvironmentp()->CloudRed,
                          (s32) fogGetCurrentEnvironmentp()->CloudGreen,
                          (s32) fogGetCurrentEnvironmentp()->CloudBlue);
        }
    }
#endif

    // Restore the back-face culling
    gSPSetGeometryMode(gdl++, G_CULL_BACK);

    return gdl;
}
#endif

// @recomp: fill the letterbox rows
static Gfx* skyFillLetterboxBars(Gfx* gdl, s32 viewtop, s32 viewbottom) {
    if (getPlayerCount() != 1) {
        return gdl;
    }
    gEXSetRectAlign(gdl++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 0);
    if (viewtop > 0) {
        gDPFillRectangle(gdl++, 0, 0, 0, viewtop - 1);
    }
    if (viewbottom < 240) {
        gDPFillRectangle(gdl++, 0, viewbottom, 0, 239);
    }
    gEXSetRectAlign(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    return gdl;
}

#if 1
RECOMP_PATCH Gfx* skyRender(Gfx* gdl) __attribute__((optnone)) {
    coord3d sp6a4;
    coord3d sp698;
    coord3d sp68c;
    coord3d sp680;
    coord3d sp674;
    coord3d sp668;
    coord3d sp65c;
    coord3d sp650;
    coord3d sp644;
    coord3d sp638;
    coord3d sp62c;
    coord3d sp620;
    coord3d sp614;
    coord3d sp608;
    coord3d sp5fc;
    coord3d sp5f0;
    coord3d sp5e4;
    coord3d sp5d8;
    coord3d sp5cc;
    coord3d sp5c0;
    coord3d sp5b4;
    coord3d sp5a8;
    coord3d sp59c;
    coord3d sp590;
    f32 sp58c;
    f32 sp588;
    f32 sp584;
    f32 sp580;
    f32 sp57c;
    f32 sp578;
    f32 sp574;
    f32 sp570;
    f32 sp56c;
    f32 sp568;
    f32 sp564;
    f32 sp560;
    f32 sp55c;
    f32 sp558;
    f32 sp554;
    f32 sp550;
    f32 sp54c;
    f32 sp548;
    s32 s1;
    s32 j;
    s32 k;
    s32 sp538;
    s32 sp534;
    s32 sp530;
    s32 sp52c;
    SkyRelated18 sp4b4[5];
    SkyRelated18 sp43c[5];
    f32 tmp;
    f32 scale;
    bool sp430;
    struct CurrentEnvironmentRecord* env;

    scale = get_room_data_float1() / 30.0f;
    sp430 = FALSE;
    env = fogGetCurrentEnvironmentp();

#if SKYDBG
    g_SkyDbgPrint = (g_SkyDbgFrame++ & 63) == 0;
#endif

    if (!fogGetCurrentEnvironmentp()->Clouds) {
        if (getPlayerCount() == 1) {
            gDPSetCycleType(gdl++, G_CYC_FILL);

            gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

            gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(), viGetViewLeft() + viGetViewWidth() - 1,
                             viGetViewTop() + viGetViewHeight() - 1);

            // @recomp: sky-color the letterbox rows
            gdl = skyFillLetterboxBars(gdl, viGetViewTop(), viGetViewTop() + viGetViewHeight());

            gDPPipeSync(gdl++);
            return gdl;
        }

        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_FILL);

        gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);

        gDPFillRectangle(gdl++, g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop,
                         (g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx) - 1,
                         (g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy) - 1);

        gDPPipeSync(gdl++);
        return gdl;
    }

    gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

#if PORTSKY
    // Fill the view with the fog colour first
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPFillRectangle(gdl++, g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop,
                     (g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx) - 1,
                     (g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy) - 1);

    // @recomp: sky-color the letterbox rows
    gdl = skyFillLetterboxBars(gdl, g_CurrentPlayer->viewtop, g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy);

    gDPPipeSync(gdl++);
#endif

    if (&sp6a4)
        ;

    sub_GAME_7F093880(0.0f, 0.0f, &sp6a4);
    sub_GAME_7F093880(getPlayer_c_screenwidth() - 0.1f, 0.0f, &sp698);
    sub_GAME_7F093880(0.0f, getPlayer_c_screenheight() - 0.1f, &sp68c);
    sub_GAME_7F093880(getPlayer_c_screenwidth() - 0.1f, getPlayer_c_screenheight() - 0.1f, &sp680);

    sp538 = sub_GAME_7F0938FC(&sp6a4, &sp644, &sp58c);
    sp534 = sub_GAME_7F0938FC(&sp698, &sp638, &sp588);
    sp530 = sub_GAME_7F0938FC(&sp68c, &sp62c, &sp584);
    sp52c = sub_GAME_7F0938FC(&sp680, &sp620, &sp580);

    sub_GAME_7F093A78(&sp6a4, &sp5e4, &sp56c);
    sub_GAME_7F093A78(&sp698, &sp5d8, &sp568);
    sub_GAME_7F093A78(&sp68c, &sp5cc, &sp564);
    sub_GAME_7F093A78(&sp680, &sp5c0, &sp560);

    if (sp538 != sp530) {
        sp54c = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp6a4.f[1] / (sp6a4.f[1] - sp68c.f[1]));

        sub_GAME_7F093880(0.0f, sp54c, &sp65c);
        sub_GAME_7F093BFC(&sp6a4, &sp68c, &sp65c);
        sub_GAME_7F0938FC(&sp65c, &sp5fc, &sp574);
        sub_GAME_7F093A78(&sp65c, &sp59c, &sp554);
    } else {
        sp54c = 0.0f;
    }

    if (sp534 != sp52c) {
        sp548 = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp698.f[1] / (sp698.f[1] - sp680.f[1]));

        sub_GAME_7F093880(getPlayer_c_screenwidth() - 0.1f, sp548, &sp650);
        sub_GAME_7F093BFC(&sp698, &sp680, &sp650);
        sub_GAME_7F0938FC(&sp650, &sp5f0, &sp570);
        sub_GAME_7F093A78(&sp650, &sp590, &sp550);
    } else {
        sp548 = 0.0f;
    }

    if (sp538 != sp534) {
        sub_GAME_7F093880(getPlayer_c_screenleft() +
                              getPlayer_c_screenwidth() * (sp6a4.f[1] / (sp6a4.f[1] - sp698.f[1])),
                          0.0f, &sp674);
        sub_GAME_7F093BFC(&sp6a4, &sp698, &sp674);
        sub_GAME_7F0938FC(&sp674, &sp614, &sp57c);
        sub_GAME_7F093A78(&sp674, &sp5b4, &sp55c);
    }

    if (sp530 != sp52c) {
        tmp = getPlayer_c_screenleft() + getPlayer_c_screenwidth() * (sp68c.f[1] / (sp68c.f[1] - sp680.f[1]));

        sub_GAME_7F093880(tmp, getPlayer_c_screenheight() - 0.1f, &sp668);
        sub_GAME_7F093BFC(&sp68c, &sp680, &sp668);
        sub_GAME_7F0938FC(&sp668, &sp608, &sp578);
        sub_GAME_7F093A78(&sp668, &sp5a8, &sp558);
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) {
        case 15:
            s1 = 0;
            break;

        case 0:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp5c0.f[0] * scale;
            sp43c[3].unk04 = sp5c0.f[1] * scale;
            sp43c[3].unk08 = sp5c0.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5c0.f[0];
            sp43c[3].unk10 = sp5c0.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp56c);
            sub_GAME_7F093FA4(&sp43c[1], sp568);
            sub_GAME_7F093FA4(&sp43c[2], sp564);
            sub_GAME_7F093FA4(&sp43c[3], sp560);
            break;

        case 3:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp56c);
            sub_GAME_7F093FA4(&sp43c[1], sp568);
            sub_GAME_7F093FA4(&sp43c[2], sp554);
            sub_GAME_7F093FA4(&sp43c[3], sp550);
            break;

        case 12:
            s1 = 4;
            sp430 = TRUE;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp560);
            sub_GAME_7F093FA4(&sp43c[1], sp564);
            sub_GAME_7F093FA4(&sp43c[2], sp550);
            sub_GAME_7F093FA4(&sp43c[3], sp554);
            break;

        case 10:
            s1 = 4;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp568);
            sub_GAME_7F093FA4(&sp43c[1], sp560);
            sub_GAME_7F093FA4(&sp43c[2], sp55c);
            sub_GAME_7F093FA4(&sp43c[3], sp558);
            break;

        case 5:
            s1 = 4;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp564);
            sub_GAME_7F093FA4(&sp43c[1], sp56c);
            sub_GAME_7F093FA4(&sp43c[2], sp558);
            sub_GAME_7F093FA4(&sp43c[3], sp55c);
            break;

        case 14:
            s1 = 3;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5a8.f[0] * scale;
            sp43c[1].unk04 = sp5a8.f[1] * scale;
            sp43c[1].unk08 = sp5a8.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5a8.f[0];
            sp43c[1].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp560);
            sub_GAME_7F093FA4(&sp43c[1], sp558);
            sub_GAME_7F093FA4(&sp43c[2], sp550);
            break;

        case 13:
            s1 = 3;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp59c.f[0] * scale;
            sp43c[1].unk04 = sp59c.f[1] * scale;
            sp43c[1].unk08 = sp59c.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp59c.f[0];
            sp43c[1].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp564);
            sub_GAME_7F093FA4(&sp43c[1], sp554);
            sub_GAME_7F093FA4(&sp43c[2], sp558);
            break;

        case 11:
            s1 = 3;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp590.f[0] * scale;
            sp43c[1].unk04 = sp590.f[1] * scale;
            sp43c[1].unk08 = sp590.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp590.f[0];
            sp43c[1].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp568);
            sub_GAME_7F093FA4(&sp43c[1], sp550);
            sub_GAME_7F093FA4(&sp43c[2], sp55c);
            break;

        case 7:
            s1 = 3;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5b4.f[0] * scale;
            sp43c[1].unk04 = sp5b4.f[1] * scale;
            sp43c[1].unk08 = sp5b4.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5b4.f[0];
            sp43c[1].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp56c);
            sub_GAME_7F093FA4(&sp43c[1], sp55c);
            sub_GAME_7F093FA4(&sp43c[2], sp554);
            break;

        case 1:
            s1 = 5;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5d8.f[0] * scale;
            sp43c[2].unk04 = sp5d8.f[1] * scale;
            sp43c[2].unk08 = sp5d8.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[4].unk00 = sp5a8.f[0] * scale;
            sp43c[4].unk04 = sp5a8.f[1] * scale;
            sp43c[4].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5d8.f[0];
            sp43c[2].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5a8.f[0];
            sp43c[4].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp564);
            sub_GAME_7F093FA4(&sp43c[1], sp56c);
            sub_GAME_7F093FA4(&sp43c[2], sp568);
            sub_GAME_7F093FA4(&sp43c[3], sp550);
            sub_GAME_7F093FA4(&sp43c[4], sp558);
            break;

        case 2:
            s1 = 5;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5c0.f[0] * scale;
            sp43c[2].unk04 = sp5c0.f[1] * scale;
            sp43c[2].unk08 = sp5c0.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[4].unk00 = sp59c.f[0] * scale;
            sp43c[4].unk04 = sp59c.f[1] * scale;
            sp43c[4].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5c0.f[0];
            sp43c[2].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp59c.f[0];
            sp43c[4].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp56c);
            sub_GAME_7F093FA4(&sp43c[1], sp568);
            sub_GAME_7F093FA4(&sp43c[2], sp560);
            sub_GAME_7F093FA4(&sp43c[3], sp558);
            sub_GAME_7F093FA4(&sp43c[4], sp554);
            break;

        case 4:
            s1 = 5;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp5e4.f[0] * scale;
            sp43c[2].unk04 = sp5e4.f[1] * scale;
            sp43c[2].unk08 = sp5e4.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[4].unk00 = sp590.f[0] * scale;
            sp43c[4].unk04 = sp590.f[1] * scale;
            sp43c[4].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5e4.f[0];
            sp43c[2].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp590.f[0];
            sp43c[4].unk10 = sp590.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp560);
            sub_GAME_7F093FA4(&sp43c[1], sp564);
            sub_GAME_7F093FA4(&sp43c[2], sp56c);
            sub_GAME_7F093FA4(&sp43c[3], sp55c);
            sub_GAME_7F093FA4(&sp43c[4], sp550);
            break;

        case 8:
            s1 = 5;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[4].unk00 = sp5b4.f[0] * scale;
            sp43c[4].unk04 = sp5b4.f[1] * scale;
            sp43c[4].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5b4.f[0];
            sp43c[4].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            sub_GAME_7F093FA4(&sp43c[0], sp568);
            sub_GAME_7F093FA4(&sp43c[1], sp560);
            sub_GAME_7F093FA4(&sp43c[2], sp564);
            sub_GAME_7F093FA4(&sp43c[3], sp554);
            sub_GAME_7F093FA4(&sp43c[4], sp55c);
            break;

        default:
            return gdl;
    }

    if (s1 > 0) {
        Mtxf sp3cc;
        Mtxf sp38c;
        SkyRelated38 sp274[5];
        s32 i;
        s32 unused[3];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp3cc);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp3cc, &dword_CODE_bss_80079E98, &sp38c);

        for (i = 0; i < s1; i++) {
            sub_GAME_7F097388(&sp43c[i], &sp38c, 130, 65535.0f, 65535.0f, &sp274[i]);

            sp274[i].unk28 = skyClamp(sp274[i].unk28, getPlayer_c_screenleft() * 4.0f,
                                      (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f);
            sp274[i].unk2c = skyClamp(sp274[i].unk2c, getPlayer_c_screentop() * 4.0f,
                                      (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 1.0f);

            if (sp274[i].unk2c > getPlayer_c_screentop() * 4.0f + 4.0f &&
                sp274[i].unk2c < (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 4.0f) {
                sp274[i].unk2c -= 4.0f;
            }
        }

        if (!fogGetCurrentEnvironmentp()->IsWater) {
            f32 f14 = 1279.0f;
            f32 f2 = 0.0f;
            f32 f16 = 959.0f;
            f32 f12 = 0.0f;

            for (j = 0; j < s1; j++) {
                if (sp274[j].unk28 < f14) {
                    f14 = sp274[j].unk28;
                }
                if (sp274[j].unk28 > f2) {
                    f2 = sp274[j].unk28;
                }

                if (sp274[j].unk2c < f16) {
                    f16 = sp274[j].unk2c;
                }
                if (sp274[j].unk2c > f12) {
                    f12 = sp274[j].unk2c;
                }
            }

            gDPPipeSync(gdl++);
            gDPSetCycleType(gdl++, G_CYC_FILL);
            gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
            gDPSetTexturePersp(gdl++, G_TP_NONE);
            gDPFillRectangle(gdl++, (s32) (f14 * 0.25f), (s32) (f16 * 0.25f), (s32) (f2 * 0.25f), (s32) (f12 * 0.25f));
            gDPPipeSync(gdl++);
            gDPSetTexturePersp(gdl++, G_TP_PERSP);
        } else {
            gDPPipeSync(gdl++);

            texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->WaterImageId], 1, 0, 2);
            gdl = sub_GAME_7F09343C(gdl, 0); // ???
            gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

#if PORTSKY == 0
            if (s1 == 4) {
                gdl = sub_GAME_7F097818(gdl, &sp274[0], &sp274[1], &sp274[3], 130.0f, TRUE);

                if (sp430) {
                    sp274[0].unk2c++;
                    sp274[1].unk2c++;
                    sp274[2].unk2c++;
                    sp274[3].unk2c++;
                }

                gdl = sub_GAME_7F097818(gdl, &sp274[3], &sp274[2], &sp274[0], 130.0f, TRUE);
            } else if (s1 == 5) {
                gdl = sub_GAME_7F097818(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
                gdl = sub_GAME_7F097818(gdl, &sp274[0], &sp274[2], &sp274[3], 130.0f, TRUE);
                gdl = sub_GAME_7F097818(gdl, &sp274[0], &sp274[3], &sp274[4], 130.0f, TRUE);
            } else if (s1 == 3) {
                gdl = sub_GAME_7F097818(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
            }
#else
            // Recomp water plane fan
            gdl = skyDrawPlaneFan(gdl, fogGetCurrentEnvironmentp()->WaterRepeat, TRUE);
#endif
        }
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) {
        case 0:
            return gdl;

        case 15:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp620.f[0] * scale;
            sp4b4[3].unk04 = sp620.f[1] * scale;
            sp4b4[3].unk08 = sp620.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp620.f[0] * 0.1f;
            sp4b4[3].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp580);
            break;

        case 12:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            break;

        case 3:
            s1 = 4;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            break;

        case 5:
            s1 = 4;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            break;

        case 10:
            s1 = 4;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            break;

        case 1:
            s1 = 3;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp608.f[0] * scale;
            sp4b4[1].unk04 = sp608.f[1] * scale;
            sp4b4[1].unk08 = sp608.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp608.f[0] * 0.1f;
            sp4b4[1].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp578);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            break;

        case 2:
            s1 = 3;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp5fc.f[0] * scale;
            sp4b4[1].unk04 = sp5fc.f[1] * scale;
            sp4b4[1].unk08 = sp5fc.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp574);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            break;

        case 4:
            s1 = 3;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp5f0.f[0] * scale;
            sp4b4[1].unk04 = sp5f0.f[1] * scale;
            sp4b4[1].unk08 = sp5f0.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp570);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            break;

        case 8:
            s1 = 3;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp614.f[0] * scale;
            sp4b4[1].unk04 = sp614.f[1] * scale;
            sp4b4[1].unk08 = sp614.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp614.f[0] * 0.1f;
            sp4b4[1].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp57c);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            break;

        case 14:
            s1 = 5;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp638.f[0] * scale;
            sp4b4[2].unk04 = sp638.f[1] * scale;
            sp4b4[2].unk08 = sp638.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[4].unk00 = sp608.f[0] * scale;
            sp4b4[4].unk04 = sp608.f[1] * scale;
            sp4b4[4].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp638.f[0] * 0.1f;
            sp4b4[2].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp608.f[0] * 0.1f;
            sp4b4[4].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp588);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            skyChooseCloudVtxColour(&sp4b4[4], sp578);
            break;

        case 13:
            s1 = 5;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp620.f[0] * scale;
            sp4b4[2].unk04 = sp620.f[1] * scale;
            sp4b4[2].unk08 = sp620.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[4].unk00 = sp5fc.f[0] * scale;
            sp4b4[4].unk04 = sp5fc.f[1] * scale;
            sp4b4[4].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp620.f[0] * 0.1f;
            sp4b4[2].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp580);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            skyChooseCloudVtxColour(&sp4b4[4], sp574);
            break;

        case 11:
            s1 = 5;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp644.f[0] * scale;
            sp4b4[2].unk04 = sp644.f[1] * scale;
            sp4b4[2].unk08 = sp644.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[4].unk00 = sp5f0.f[0] * scale;
            sp4b4[4].unk04 = sp5f0.f[1] * scale;
            sp4b4[4].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp644.f[0] * 0.1f;
            sp4b4[2].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp58c);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            skyChooseCloudVtxColour(&sp4b4[4], sp570);
            break;

        case 7:
            s1 = 5;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[4].unk00 = sp614.f[0] * scale;
            sp4b4[4].unk04 = sp614.f[1] * scale;
            sp4b4[4].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp614.f[0] * 0.1f;
            sp4b4[4].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            skyChooseCloudVtxColour(&sp4b4[4], sp57c);
            break;

        default:
            return gdl;
    }

    gDPPipeSync(gdl++);

    texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId], 1, 0, 2);

    if (1)
        ;

    gDPSetEnvColor(gdl++, fogGetCurrentEnvironmentp()->Red, fogGetCurrentEnvironmentp()->Green,
                   fogGetCurrentEnvironmentp()->Blue, 0xff);
    gDPSetCombineLERP(gdl++, SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE, SHADE, ENVIRONMENT, TEXEL0,
                      ENVIRONMENT, 0, 0, 0, SHADE);

    {
        Mtxf sp1ec;
        Mtxf sp1ac;
        SkyRelated38 sp94[5];
        s32 i;
        s32 stack[2];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp1ec);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp1ec, &dword_CODE_bss_80079E98, &sp1ac);

        for (i = 0; i < s1; i++) {
            sub_GAME_7F097388(&sp4b4[i], &sp1ac, 130, 65535.0f, 65535.0f, &sp94[i]);

            sp94[i].unk28 = skyClamp(sp94[i].unk28, getPlayer_c_screenleft() * 4.0f,
                                     (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f);
            sp94[i].unk2c = skyClamp(sp94[i].unk2c, getPlayer_c_screentop() * 4.0f,
                                     (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 1.0f);
        }

#if PORTSKY == 0
        if (s1 == 4) {
            if (((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) == 12) {
                if (sp548 < sp54c) {
                    if (sp94[3].unk2c >= sp94[1].unk2c + 4.0f) {
                        sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[1].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;
                        sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[3].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;

                        gdl = sub_GAME_7F098A2C(gdl, &sp94[0], &sp94[1], &sp94[2], &sp94[3], 130.0f);
                    } else {
                        gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
                    }
                } else if (sp94[2].unk2c >= sp94[0].unk2c + 4.0f) {
                    sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[1].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;
                    sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[3].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;

                    gdl = sub_GAME_7F098A2C(gdl, &sp94[1], &sp94[0], &sp94[3], &sp94[2], 130.0f);
                } else {
                    gdl = sub_GAME_7F097818(gdl, &sp94[1], &sp94[0], &sp94[3], 130.0f, TRUE);
                }
            } else {
                gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[1], &sp94[3], 130.0f, TRUE);
                gdl = sub_GAME_7F097818(gdl, &sp94[3], &sp94[2], &sp94[0], 130.0f, TRUE);
            }
        } else if (s1 == 5) {
            gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
            gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[2], &sp94[3], 130.0f, TRUE);
            gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[3], &sp94[4], 130.0f, TRUE);
        } else if (s1 == 3) {
            gdl = sub_GAME_7F097818(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
        }
    }
#else
        // Recomp's cloud plane fan
        gdl = skyDrawPlaneFan(gdl, fogGetCurrentEnvironmentp()->CloudRepeat, FALSE);
    }
#endif

    return gdl;
}
#endif
