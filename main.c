/*
 * BLOBI - Bichinho virtual para PS3 (PSL1GHT)
 * ------------------------------------------------------------
 * Video : RSX framebuffer XRGB (sem SDL), dois buffers + vsync.
 *         O jogo desenha num canvas 1280x720 em memoria principal
 *         e copia (com escala, se preciso) para o buffer do RSX.
 * Input : io/pad.h
 *
 * Controles:
 *   L1 / R1        : trocar de sala
 *   Analogico esq. : olhos seguem / mover no minigame
 *   Cozinha        : D-Pad Esq/Dir escolhe comida, X alimenta
 *   Banheiro       : X liga/desliga o chuveiro
 *   Quarto         : X apaga/acende a luz (dormir)
 *   Jogos          : X inicia o Food Drop, O sai do jogo
 *
 * Makefile: LIBS = -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>

#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE    0x100000
#define HOST_SIZE  (32 * 1024 * 1024)

#define VW 1280
#define VH 720
#define PI_F 3.14159265f

#define RGB(r, g, b) ((((u32)(r)) << 16) | (((u32)(g)) << 8) | ((u32)(b)))
#define MINF(a, b) ((a) < (b) ? (a) : (b))
#define MAXF(a, b) ((a) > (b) ? (a) : (b))

#ifndef MAX_PADS
#define MAX_PADS 7
#endif

/* ============================================================
 *  RSX / VIDEO
 * ============================================================ */

typedef struct {
    int height;
    int width;
    uint32_t *ptr;
    uint32_t offset;
} RsxBuf;

static gcmContextData *context = NULL;
static videoState vstate;
static videoConfiguration vconfig;
static videoResolution vres;
static RsxBuf buffers[2];
static int currentBuffer = 0;
static padInfo padinfo;
static padData paddata;
static volatile int running = 0;

static u32 *cv = NULL;   /* canvas 1280x720 em memoria principal */

static void waitFlip(void)
{
    while (gcmGetFlipStatus() != 0)
        usleep(200);
    gcmResetFlipStatus();
}

static int flipBuffer(s32 buffer)
{
    if (gcmSetFlip(context, buffer) == 0) {
        rsxFlushBuffer(context);
        gcmSetWaitFlip(context);
        return 1;
    }
    return 0;
}

static int makeBuffer(RsxBuf *buffer, u16 width, u16 height, int id)
{
    int pitch = sizeof(u32) * width;
    buffer->ptr = (uint32_t *)rsxMemalign(64, (height * pitch));
    if (buffer->ptr == NULL)
        goto error;
    if (rsxAddressToOffset(buffer->ptr, &buffer->offset) != 0)
        goto error;
    if (gcmSetDisplayBuffer(id, buffer->offset, pitch, width, height) != 0)
        goto error;
    buffer->width = width;
    buffer->height = height;
    return 1;
error:
    if (buffer->ptr != NULL)
        rsxFree(buffer->ptr);
    buffer->ptr = NULL;
    return 0;
}

static void initScreen(void)
{
    void *host_addr = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&context, CB_SIZE, HOST_SIZE, host_addr);

    videoGetState(0, 0, &vstate);
    videoGetResolution(vstate.displayMode.resolution, &vres);

    memset(&vconfig, 0, sizeof(videoConfiguration));
    vconfig.resolution = vstate.displayMode.resolution;
    vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch = vres.width * sizeof(u32);

    videoConfigure(0, &vconfig, NULL, 0);
    videoGetState(0, 0, &vstate);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    makeBuffer(&buffers[0], vres.width, vres.height, 0);
    makeBuffer(&buffers[1], vres.width, vres.height, 1);

    gcmResetFlipStatus();
    flipBuffer(1);
}

/* Copia o canvas para o framebuffer do RSX (com escala se nao for 720p) */
static void present(RsxBuf *b)
{
    static int *xmap = NULL;
    static int xmap_w = 0;
    int x, y;

    if (b->ptr == NULL)
        return;

    if (b->width == VW && b->height == VH) {
        memcpy(b->ptr, cv, VW * VH * sizeof(u32));
        return;
    }

    if (xmap == NULL || xmap_w != b->width) {
        if (xmap) free(xmap);
        xmap = (int *)malloc(b->width * sizeof(int));
        xmap_w = b->width;
        for (x = 0; x < b->width; x++)
            xmap[x] = (x * VW) / b->width;
    }

    for (y = 0; y < b->height; y++) {
        int sy = (y * VH) / b->height;
        u32 *dst = b->ptr + y * b->width;
        u32 *src = cv + sy * VW;
        for (x = 0; x < b->width; x++)
            dst[x] = src[xmap[x]];
    }
}

static void sysutil_exit_callback(u64 status, u64 param, void *usrdata)
{
    (void)param;
    (void)usrdata;
    switch (status) {
    case SYSUTIL_EXIT_GAME:
        running = 0;
        break;
    default:
        break;
    }
}

/* ============================================================
 *  GRAFICOS 2D (software)
 * ============================================================ */

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline u32 mixc(u32 a, u32 b, int t)
{
    u32 ia, r, g, bl;
    if (t <= 0) return a;
    if (t >= 255) return b;
    ia = 255 - t;
    r  = (((a >> 16) & 255) * ia + ((b >> 16) & 255) * t) / 255;
    g  = (((a >> 8) & 255) * ia + ((b >> 8) & 255) * t) / 255;
    bl = ((a & 255) * ia + (b & 255) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

static inline void px_put(int x, int y, u32 c, int a)
{
    u32 *p;
    if (a <= 0 || (unsigned)x >= VW || (unsigned)y >= VH)
        return;
    p = &cv[y * VW + x];
    if (a >= 255)
        *p = c;
    else
        *p = mixc(*p, c, a);
}

static void g_rect(int x, int y, int w, int h, u32 c, int a)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > VW ? VW : (x + w);
    int y1 = (y + h) > VH ? VH : (y + h);
    int xx, yy;
    if (a <= 0) return;
    for (yy = y0; yy < y1; yy++) {
        u32 *row = cv + yy * VW;
        if (a >= 255) {
            for (xx = x0; xx < x1; xx++) row[xx] = c;
        } else {
            for (xx = x0; xx < x1; xx++) row[xx] = mixc(row[xx], c, a);
        }
    }
}

static void g_vgrad(int x, int y, int w, int h, u32 c1, u32 c2)
{
    int i;
    for (i = 0; i < h; i++) {
        u32 c = mixc(c1, c2, h > 1 ? (i * 255) / (h - 1) : 0);
        g_rect(x, y + i, w, 1, c, 255);
    }
}

static void g_ellipse(float cx, float cy, float rx, float ry, u32 c, int a)
{
    int x0 = (int)(cx - rx - 1), x1 = (int)(cx + rx + 2);
    int y0 = (int)(cy - ry - 1), y1 = (int)(cy + ry + 2);
    int x, y;
    float m = rx < ry ? rx : ry;
    if (rx <= 0.0f || ry <= 0.0f || a <= 0) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VW) x1 = VW;
    if (y1 > VH) y1 = VH;
    for (y = y0; y < y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        float dy2 = dy * dy;
        if (dy2 > 1.3f) continue;
        for (x = x0; x < x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float d2 = dx * dx + dy2;
            float cov;
            if (d2 > 1.4f) continue;
            if (d2 < 0.8f) {
                px_put(x, y, c, a);
                continue;
            }
            cov = (1.0f - sqrtf(d2)) * m + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            px_put(x, y, c, (int)(cov * (float)a));
        }
    }
}

static void g_circle(float cx, float cy, float r, u32 c, int a)
{
    g_ellipse(cx, cy, r, r, c, a);
}

static void g_rrectg(float x, float y, float w, float h, float r, u32 c1, u32 c2, int a)
{
    int x0, x1, y0, y1, xx, yy;
    float cx, cy, hx, hy;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    x0 = (int)floorf(x) - 1;
    x1 = (int)ceilf(x + w) + 1;
    y0 = (int)floorf(y) - 1;
    y1 = (int)ceilf(y + h) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VW) x1 = VW;
    if (y1 > VH) y1 = VH;
    cx = x + w * 0.5f;
    cy = y + h * 0.5f;
    hx = w * 0.5f - r;
    hy = h * 0.5f - r;
    for (yy = y0; yy < y1; yy++) {
        float t = (((float)yy + 0.5f) - y) / (h > 1.0f ? h : 1.0f);
        float dy = fabsf((float)yy + 0.5f - cy) - hy;
        u32 col = mixc(c1, c2, (int)(clampf(t, 0.0f, 1.0f) * 255.0f));
        for (xx = x0; xx < x1; xx++) {
            float dx = fabsf((float)xx + 0.5f - cx) - hx;
            if (dx <= 0.0f && dy <= 0.0f) {
                px_put(xx, yy, col, a);
            } else {
                float ddx = dx > 0.0f ? dx : 0.0f;
                float ddy = dy > 0.0f ? dy : 0.0f;
                float d = sqrtf(ddx * ddx + ddy * ddy) - r;
                float cov = 0.5f - d;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
                px_put(xx, yy, col, (int)(cov * (float)a));
            }
        }
    }
}

static void g_rrect(float x, float y, float w, float h, float r, u32 c, int a)
{
    g_rrectg(x, y, w, h, r, c, c, a);
}

static void g_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c, int a)
{
    float minx = MINF(x0, MINF(x1, x2)), maxx = MAXF(x0, MAXF(x1, x2));
    float miny = MINF(y0, MINF(y1, y2)), maxy = MAXF(y0, MAXF(y1, y2));
    int ix0 = (int)floorf(minx), ix1 = (int)ceilf(maxx);
    int iy0 = (int)floorf(miny), iy1 = (int)ceilf(maxy);
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    int x, y;
    if (fabsf(area) < 0.01f) return;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > VW - 1) ix1 = VW - 1;
    if (iy1 > VH - 1) iy1 = VH - 1;
    for (y = iy0; y <= iy1; y++) {
        for (x = ix0; x <= ix1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
            float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                px_put(x, y, c, a);
        }
    }
}

static void g_line(float x0, float y0, float x1, float y1, float th, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len / (th * 0.35f + 0.5f)) + 1;
    int i;
    for (i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        g_circle(x0 + dx * t, y0 + dy * t, th * 0.5f, c, 255);
    }
}

/* ---------- Fonte bitmap 5x7 ---------- */

static const char gchars[] = " !+,-./0123456789:<>?ABCDEFGHIJKLMNOPQRSTUVWXYZ()";
static const unsigned char gfont[][7] = {
    { 0, 0, 0, 0, 0, 0, 0 },            /* ' ' */
    { 4, 4, 4, 4, 4, 0, 4 },            /* ! */
    { 0, 4, 4, 31, 4, 4, 0 },           /* + */
    { 0, 0, 0, 0, 6, 4, 8 },            /* , */
    { 0, 0, 0, 31, 0, 0, 0 },           /* - */
    { 0, 0, 0, 0, 0, 12, 12 },          /* . */
    { 1, 2, 2, 4, 8, 8, 16 },           /* / */
    { 14, 17, 19, 21, 25, 17, 14 },     /* 0 */
    { 4, 12, 4, 4, 4, 4, 14 },          /* 1 */
    { 14, 17, 1, 2, 4, 8, 31 },         /* 2 */
    { 30, 1, 1, 14, 1, 1, 30 },         /* 3 */
    { 2, 6, 10, 18, 31, 2, 2 },         /* 4 */
    { 31, 16, 30, 1, 1, 17, 14 },       /* 5 */
    { 6, 8, 16, 30, 17, 17, 14 },       /* 6 */
    { 31, 1, 2, 4, 8, 8, 8 },           /* 7 */
    { 14, 17, 17, 14, 17, 17, 14 },     /* 8 */
    { 14, 17, 17, 15, 1, 2, 12 },       /* 9 */
    { 0, 12, 12, 0, 12, 12, 0 },        /* : */
    { 2, 4, 8, 16, 8, 4, 2 },           /* < */
    { 8, 4, 2, 1, 2, 4, 8 },            /* > */
    { 14, 17, 1, 2, 4, 0, 4 },          /* ? */
    { 14, 17, 17, 31, 17, 17, 17 },     /* A */
    { 30, 17, 17, 30, 17, 17, 30 },     /* B */
    { 14, 17, 16, 16, 16, 17, 14 },     /* C */
    { 30, 17, 17, 17, 17, 17, 30 },     /* D */
    { 31, 16, 16, 30, 16, 16, 31 },     /* E */
    { 31, 16, 16, 30, 16, 16, 16 },     /* F */
    { 14, 17, 16, 23, 17, 17, 15 },     /* G */
    { 17, 17, 17, 31, 17, 17, 17 },     /* H */
    { 14, 4, 4, 4, 4, 4, 14 },          /* I */
    { 7, 2, 2, 2, 2, 18, 12 },          /* J */
    { 17, 18, 20, 24, 20, 18, 17 },     /* K */
    { 16, 16, 16, 16, 16, 16, 31 },     /* L */
    { 17, 27, 21, 21, 17, 17, 17 },     /* M */
    { 17, 17, 25, 21, 19, 17, 17 },     /* N */
    { 14, 17, 17, 17, 17, 17, 14 },     /* O */
    { 30, 17, 17, 30, 16, 16, 16 },     /* P */
    { 14, 17, 17, 17, 21, 18, 13 },     /* Q */
    { 30, 17, 17, 30, 20, 18, 17 },     /* R */
    { 15, 16, 16, 14, 1, 1, 30 },       /* S */
    { 31, 4, 4, 4, 4, 4, 4 },           /* T */
    { 17, 17, 17, 17, 17, 17, 14 },     /* U */
    { 17, 17, 17, 17, 17, 10, 4 },      /* V */
    { 17, 17, 17, 21, 21, 27, 17 },     /* W */
    { 17, 17, 10, 4, 10, 17, 17 },      /* X */
    { 17, 17, 10, 4, 4, 4, 4 },         /* Y */
    { 31, 1, 2, 4, 8, 16, 31 },         /* Z */
    { 2, 4, 8, 8, 8, 4, 2 },            /* ( */
    { 8, 4, 2, 2, 2, 4, 8 }             /* ) */
};

static int text_width(const char *s, int sc)
{
    return (int)strlen(s) * 6 * sc;
}

static void g_text(int x, int y, const char *s, int sc, u32 c)
{
    for (; *s; s++) {
        char ch = *s;
        const char *p;
        int gi, r, col;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        p = strchr(gchars, ch);
        if (p == NULL || ch == 0) {
            x += 6 * sc;
            continue;
        }
        gi = (int)(p - gchars);
        for (r = 0; r < 7; r++)
            for (col = 0; col < 5; col++)
                if (gfont[gi][r] & (16 >> col))
                    g_rect(x + col * sc, y + r * sc, sc, sc, c, 255);
        x += 6 * sc;
    }
}

static void g_text_sh(int x, int y, const char *s, int sc, u32 c)
{
    g_text(x + sc, y + sc, s, sc, RGB(20, 20, 40));
    g_text(x, y, s, sc, c);
}

static void g_text_center(int cx, int y, const char *s, int sc, u32 c)
{
    g_text_sh(cx - text_width(s, sc) / 2, y, s, sc, c);
}

/* ============================================================
 *  ESTADO DO JOGO
 * ============================================================ */

enum { ROOM_KITCHEN = 0, ROOM_BATH = 1, ROOM_BED = 2, ROOM_GAMES = 3 };

static const char *room_names[4] = { "COZINHA", "BANHEIRO", "QUARTO", "JOGOS" };
static const char *food_names[3] = { "MACA", "HAMBURGUER", "POCAO" };

static float hunger = 80.0f, health = 90.0f, energy = 85.0f, fun = 70.0f;
static int coins = 25;
static int room = ROOM_KITCHEN;
static int selFood = 0;
static int lightOn = 1;
static int showerOn = 0;
static float animTime = 0.0f;
static float fadeAlpha = 0.0f;

/* olhar / piscar */
static float lookX = 0.0f, lookY = 0.0f;
static float blinkTimer = 2.5f, blinkPhase = -1.0f, blinkF = 1.0f;

/* comer */
static float eatTimer = 0.0f;
static int eatItem = 0;

/* mensagens */
static char msgText[64] = "";
static float msgTimer = 0.0f;

/* sujeira */
#define N_DIRT 5
static float dirt[N_DIRT];
static float dirtTimer = 35.0f;
static const float dsx[N_DIRT] = { -70.0f, 80.0f, -15.0f, 85.0f, -95.0f };
static const float dsy[N_DIRT] = { -60.0f, -75.0f, -155.0f, -35.0f, -20.0f };

/* chuveiro */
#define N_DROPS 90
typedef struct { float x, y, vx, vy; } Drop;
static Drop drops[N_DROPS];

/* minigame Food Drop */
#define N_FALL 16
typedef struct { int on; float x, y, v; int type; } Faller;
static Faller fl[N_FALL];
typedef struct { float x, y, life; char t[12]; u32 col; } Popup;
static Popup popups[8];
static int gstate = 0;            /* 0 menu, 1 jogando, 2 fim */
static float gx = 640.0f;
static int glives = 3;
static float gtime = 45.0f;
static int gcoins = 0;
static int ghi = 0;
static float gspawn = 0.5f;
static float gelapsed = 0.0f;
static float gcatch = 0.0f;
static float ghurt = 0.0f;

/* entrada */
typedef struct {
    int l1, r1, x, left, right, circle;
    int pl1, pr1, px, pleft, pright, pcircle;
    float ax, ay;
} Input;
static Input in;
#define PRESSED(n) (in.n && !in.p##n)

static void set_msg(const char *s)
{
    snprintf(msgText, sizeof(msgText), "%s", s);
    msgTimer = 2.2f;
}

static void add_popup(float x, float y, const char *t, u32 col)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (popups[i].life <= 0.0f) {
            popups[i].x = x;
            popups[i].y = y;
            popups[i].life = 1.0f;
            snprintf(popups[i].t, sizeof(popups[i].t), "%s", t);
            popups[i].col = col;
            return;
        }
    }
}

static void clamp_stats(void)
{
    hunger = clampf(hunger, 0.0f, 100.0f);
    health = clampf(health, 0.0f, 100.0f);
    energy = clampf(energy, 0.0f, 100.0f);
    fun = clampf(fun, 0.0f, 100.0f);
}

static int dirt_count(void)
{
    int i, n = 0;
    for (i = 0; i < N_DIRT; i++)
        if (dirt[i] > 0.0f) n++;
    return n;
}

static void add_dirt(void)
{
    int tries;
    for (tries = 0; tries < 12; tries++) {
        int i = rand() % N_DIRT;
        if (dirt[i] <= 0.0f) {
            dirt[i] = 1.0f;
            return;
        }
    }
}

static void reset_drop(Drop *d, int initial)
{
    float x0 = 590.0f + (float)(rand() % 120);
    d->x = x0;
    d->y = 168.0f + (float)(rand() % 10) + (initial ? (float)(rand() % 440) : 0.0f);
    d->vx = (x0 - 640.0f) * 0.55f;
    d->vy = 650.0f + (float)(rand() % 250);
}

static void init_game(void)
{
    int i;
    srand(20260713);
    for (i = 0; i < N_DIRT; i++) dirt[i] = 0.0f;
    dirt[1] = 1.0f;
    for (i = 0; i < N_DROPS; i++) reset_drop(&drops[i], 1);
    for (i = 0; i < N_FALL; i++) fl[i].on = 0;
    for (i = 0; i < 8; i++) popups[i].life = 0.0f;
    memset(&in, 0, sizeof(in));
}

/* ============================================================
 *  OBJETOS (comidas)
 * ============================================================ */

/* type: 0 maca, 1 hamburguer, 2 pocao, 3 podre */
static void draw_item(int type, float x, float y, float s)
{
    if (type == 0) {
        g_ellipse(x, y + 2 * s, 25 * s, 23 * s, RGB(205, 35, 45), 255);
        g_ellipse(x + 7 * s, y + 7 * s, 17 * s, 14 * s, RGB(160, 20, 38), 110);
        g_ellipse(x - 9 * s, y - 8 * s, 6 * s, 9 * s, RGB(255, 255, 255), 130);
        g_line(x, y - 21 * s, x + 3 * s, y - 31 * s, 3 * s, RGB(110, 70, 40));
        g_ellipse(x + 13 * s, y - 27 * s, 11 * s, 5 * s, RGB(90, 190, 80), 255);
    } else if (type == 1) {
        g_ellipse(x, y - 14 * s, 31 * s, 20 * s, RGB(228, 160, 78), 255);
        g_ellipse(x - 8 * s, y - 20 * s, 14 * s, 8 * s, RGB(255, 210, 140), 120);
        g_ellipse(x - 12 * s, y - 26 * s, 3 * s, 1.6f * s, RGB(255, 245, 210), 255);
        g_ellipse(x + 2 * s, y - 30 * s, 3 * s, 1.6f * s, RGB(255, 245, 210), 255);
        g_ellipse(x + 14 * s, y - 24 * s, 3 * s, 1.6f * s, RGB(255, 245, 210), 255);
        g_ellipse(x - 2 * s, y - 20 * s, 3 * s, 1.6f * s, RGB(255, 245, 210), 255);
        g_rrect(x - 34 * s, y - 9 * s, 68 * s, 9 * s, 4 * s, RGB(105, 190, 70), 255);
        g_rrect(x - 31 * s, y - 1 * s, 62 * s, 5 * s, 2 * s, RGB(255, 205, 60), 255);
        g_rrect(x - 32 * s, y + 3 * s, 64 * s, 11 * s, 5 * s, RGB(110, 60, 35), 255);
        g_rrect(x - 30 * s, y + 12 * s, 60 * s, 13 * s, 6 * s, RGB(222, 155, 75), 255);
    } else if (type == 2) {
        g_rrect(x - 8 * s, y - 32 * s, 16 * s, 24 * s, 4 * s, RGB(205, 232, 255), 255);
        g_circle(x, y + 6 * s, 24 * s, RGB(205, 232, 255), 255);
        g_circle(x, y + 8 * s, 20 * s, RGB(190, 60, 205), 255);
        g_ellipse(x + 6 * s, y + 14 * s, 13 * s, 8 * s, RGB(150, 30, 175), 140);
        g_rect((int)(x - 2 * s), (int)(y + 1 * s), (int)(4 * s) + 1, (int)(14 * s), RGB(255, 255, 255), 230);
        g_rect((int)(x - 7 * s), (int)(y + 6 * s), (int)(14 * s) + 1, (int)(4 * s) + 1, RGB(255, 255, 255), 230);
        g_ellipse(x - 10 * s, y - 2 * s, 4 * s, 9 * s, RGB(255, 255, 255), 150);
        g_rrect(x - 10 * s, y - 40 * s, 20 * s, 10 * s, 3 * s, RGB(170, 120, 70), 255);
    } else {
        g_ellipse(x, y + 2 * s, 24 * s, 22 * s, RGB(105, 120, 50), 255);
        g_ellipse(x + 6 * s, y + 6 * s, 15 * s, 13 * s, RGB(70, 85, 35), 130);
        g_circle(x - 9 * s, y + 6 * s, 4 * s, RGB(60, 70, 30), 255);
        g_circle(x + 10 * s, y + 9 * s, 3 * s, RGB(60, 70, 30), 255);
        g_line(x - 12 * s, y - 8 * s, x - 5 * s, y - 1 * s, 2 * s, RGB(35, 40, 20));
        g_line(x - 5 * s, y - 8 * s, x - 12 * s, y - 1 * s, 2 * s, RGB(35, 40, 20));
        g_line(x + 5 * s, y - 8 * s, x + 12 * s, y - 1 * s, 2 * s, RGB(35, 40, 20));
        g_line(x + 12 * s, y - 8 * s, x + 5 * s, y - 1 * s, 2 * s, RGB(35, 40, 20));
        g_line(x, y - 20 * s, x + 3 * s, y - 29 * s, 3 * s, RGB(90, 60, 35));
        g_line(x - 18 * s, y - 30 * s, x - 14 * s, y - 40 * s, 2 * s, RGB(140, 200, 70));
        g_line(x + 18 * s, y - 32 * s, x + 22 * s, y - 42 * s, 2 * s, RGB(140, 200, 70));
    }
}

static void draw_heart(float x, float y, float s, u32 c)
{
    g_circle(x - 7 * s, y - 4 * s, 8 * s, c, 255);
    g_circle(x + 7 * s, y - 4 * s, 8 * s, c, 255);
    g_tri(x - 14.5f * s, y - 1 * s, x + 14.5f * s, y - 1 * s, x, y + 15 * s, c, 255);
}

/* ============================================================
 *  O BICHINHO "BLOBI" (triangulo arredondado com luz e sombra)
 * ============================================================ */

static void draw_curve(float cx, float my, float w, float curv, float th, u32 c)
{
    int i;
    float px = 0, py = 0;
    for (i = 0; i <= 18; i++) {
        float u = -1.0f + 2.0f * (float)i / 18.0f;
        float x = cx + u * w;
        float y = my + curv * (1.0f - u * u);
        if (i > 0) g_line(px, py, x, y, th, c);
        px = x;
        py = y;
    }
}

static void draw_mouth(float cx, float my, float s, int mouth, float mopen)
{
    u32 dark = RGB(85, 35, 50);
    int i;
    switch (mouth) {
    case 0: /* triste */
        draw_curve(cx, my + 8 * s, 28 * s, -12 * s, 3.5f * s, dark);
        break;
    case 1: /* neutro */
        g_line(cx - 22 * s, my + 2 * s, cx + 22 * s, my + 2 * s, 3.5f * s, dark);
        break;
    case 2: /* sorriso */
        draw_curve(cx, my - 6 * s, 34 * s, 14 * s, 3.5f * s, dark);
        break;
    case 3: { /* sorrisao aberto */
        float w = 36 * s, top = my - 8 * s, depth = 32 * s;
        for (i = (int)(-w); i <= (int)w; i++) {
            float u = (float)i / w;
            float yb = top + depth * sqrtf(1.0f - u * u);
            g_rect((int)(cx + i), (int)top, 1, (int)(yb - top) + 1, dark, 255);
        }
        g_ellipse(cx, top + depth * 0.78f, w * 0.45f, depth * 0.22f, RGB(235, 110, 125), 255);
        g_line(cx - w, top, cx + w, top, 3 * s, dark);
        break;
    }
    case 4: { /* mastigando / boca aberta */
        float ry = (6.0f + 22.0f * mopen) * s;
        g_ellipse(cx, my + 4 * s, 25 * s, ry, dark, 255);
        if (mopen > 0.4f)
            g_ellipse(cx, my + 4 * s + ry * 0.45f, 14 * s, ry * 0.4f, RGB(235, 110, 125), 255);
        break;
    }
    default: /* dormindo */
        g_ellipse(cx, my + 2 * s, 9 * s, (6.0f + 2.0f * sinf(animTime * 1.6f)) * s, dark, 255);
        break;
    }
}

static int mood_mouth(void)
{
    float avg = (hunger + health + energy + fun) * 0.25f;
    float mn = MINF(MINF(hunger, health), MINF(energy, fun));
    if (mn < 20.0f || avg < 35.0f) return 0;
    if (avg < 55.0f) return 1;
    if (avg > 82.0f && mn > 50.0f) return 3;
    return 2;
}

static void draw_zzz(float cx, float top, float s)
{
    int i;
    for (i = 0; i < 3; i++) {
        float ph = animTime * 1.2f + (float)i * 1.1f;
        float fy = fmodf(ph, 3.3f);
        float x = cx + 70 * s + fy * 22 * s + (float)i * 6;
        float y = top + 40 * s - fy * 34 * s;
        g_text_sh((int)x, (int)y, "Z", 3 + i, RGB(235, 240, 255));
    }
}

static void draw_pet(float cx, float by, float s, int sleeping, int mouth, float mopen)
{
    float breathe = sleeping ? sinf(animTime * 1.6f) * 0.025f : sinf(animTime * 2.4f) * 0.012f;
    float BW = 140.0f * s * (1.0f - breathe * 0.6f);
    float BH = 250.0f * s * (1.0f + breathe);
    float top = by - BH;
    u32 base = RGB(86, 196, 168);
    int br, bg, bb, y, x, i, side;
    float ey, ex, erx, ery, my;

    /* doente: tom esverdeado/palido */
    if (health < 30.0f)
        base = mixc(base, RGB(150, 175, 110), (int)((30.0f - health) * 6.0f));
    br = (base >> 16) & 255;
    bg = (base >> 8) & 255;
    bb = base & 255;

    /* sombra no chao */
    g_ellipse(cx, by + 8 * s, BW * 1.0f, 16 * s, RGB(0, 0, 0), 70);
    g_ellipse(cx, by + 6 * s, BW * 0.75f, 10 * s, RGB(0, 0, 0), 50);

    /* corpo */
    for (y = (int)top; y <= (int)by; y++) {
        float t = ((float)y + 0.5f - top) / BH;
        float hb, inset = 0.0f, hw, vshade;
        int xs, xe;
        if (t <= 0.0f) continue;
        if (t > 1.0f) t = 1.0f;
        hb = powf(t, 0.72f);
        if (t > 0.78f) {
            float k = (t - 0.78f) / 0.22f;
            inset = 0.28f * (1.0f - sqrtf(MAXF(0.0f, 1.0f - k * k)));
        }
        hw = (hb - inset) * BW;
        if (hw < 1.0f) continue;
        xs = (int)(cx - hw - 1.0f);
        xe = (int)(cx + hw + 1.0f);
        vshade = 1.10f - 0.28f * t;
        for (x = xs; x <= xe; x++) {
            float dx = (float)x + 0.5f - cx;
            float adx = fabsf(dx);
            float cov = hw - adx + 0.5f;
            float u, e2, ea, light, hx, hy, d2, hl = 0.0f;
            int r, g, b;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            u = dx / hw;
            e2 = u * u;
            ea = fabsf(u);
            light = vshade * (1.0f - 0.30f * e2 - 0.14f * u);
            if (ea > 0.88f) light *= 1.0f - (ea - 0.88f) * 2.2f;
            if (light < 0.2f) light = 0.2f;
            hx = ((float)x - (cx - 0.40f * BW)) / (0.24f * BW);
            hy = ((float)y - (top + 0.32f * BH)) / (0.14f * BH);
            d2 = hx * hx + hy * hy;
            if (d2 < 1.0f) {
                hl = 1.0f - d2;
                hl = hl * hl * 0.5f;
            }
            r = (int)((float)br * light + 255.0f * hl);
            g = (int)((float)bg * light + 255.0f * hl);
            b = (int)((float)bb * light + 255.0f * hl);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            px_put(x, y, RGB(r, g, b), (int)(cov * 255.0f));
        }
    }

    /* manchas de sujeira */
    for (i = 0; i < N_DIRT; i++) {
        if (dirt[i] > 0.0f) {
            float lv = dirt[i] > 1.0f ? 1.0f : dirt[i];
            int al = (int)(60.0f + 150.0f * lv);
            float dx = cx + dsx[i] * s, dy = by + dsy[i] * s;
            g_ellipse(dx, dy, (8.0f + 14.0f * lv) * s, (6.0f + 10.0f * lv) * s, RGB(112, 80, 42), al);
            g_ellipse(dx + 10 * s * lv, dy + 6 * s * lv, (5.0f + 6.0f * lv) * s, (4.0f + 5.0f * lv) * s, RGB(90, 62, 32), al);
        }
    }

    /* olhos */
    ey = by - 125.0f * s;
    ex = 40.0f * s;
    erx = 29.0f * s;
    ery = 33.0f * s;
    for (side = -1; side <= 1; side += 2) {
        float exc = cx + (float)side * ex;
        if (sleeping || blinkF < 0.15f) {
            draw_curve(exc, ey - 3 * s, erx * 0.85f, 7 * s, 3.2f * s, RGB(30, 70, 70));
        } else {
            float ry = ery * blinkF;
            float pr = 13.0f * s;
            float pxo = lookX * (erx - pr - 3 * s);
            float pyo = lookY * MAXF(0.0f, (ry - pr) * 0.8f);
            g_ellipse(exc, ey, erx + 2.5f * s, ry + 2.5f * s, RGB(30, 70, 70), 255);
            g_ellipse(exc, ey, erx, ry, RGB(255, 255, 255), 255);
            g_ellipse(exc, ey + ry * 0.45f, erx * 0.85f, ry * 0.4f, RGB(225, 232, 240), 120);
            g_circle(exc + pxo, ey + pyo, pr, RGB(28, 30, 45), 255);
            g_circle(exc + pxo - 4.5f * s, ey + pyo - 5 * s, 4.6f * s, RGB(255, 255, 255), 255);
            g_circle(exc + pxo + 4 * s, ey + pyo + 4 * s, 2.2f * s, RGB(255, 255, 255), 200);
        }
    }

    /* sobrancelhas tristes */
    if (mouth == 0 && !sleeping) {
        g_line(cx - ex - 22 * s, ey - ery - 4 * s, cx - ex + 22 * s, ey - ery - 15 * s, 3.5f * s, RGB(30, 90, 85));
        g_line(cx + ex - 22 * s, ey - ery - 15 * s, cx + ex + 22 * s, ey - ery - 4 * s, 3.5f * s, RGB(30, 90, 85));
    }

    /* bochechas */
    my = by - 62.0f * s;
    if (mouth >= 2 && mouth != 5) {
        g_ellipse(cx - 70 * s, my - 8 * s, 14 * s, 8 * s, RGB(255, 120, 140), 110);
        g_ellipse(cx + 70 * s, my - 8 * s, 14 * s, 8 * s, RGB(255, 120, 140), 110);
    }

    draw_mouth(cx, my, s, mouth, mopen);

    /* balao de pensamento quando com fome */
    if (hunger < 25.0f && !sleeping && s > 0.9f) {
        float bx = cx + BW * 0.95f, byy = top - 10.0f;
        g_circle(bx - 36, byy + 52, 6, RGB(255, 255, 255), 230);
        g_circle(bx - 22, byy + 36, 9, RGB(255, 255, 255), 230);
        g_ellipse(bx + 14, byy, 46, 36, RGB(255, 255, 255), 235);
        draw_item(0, bx + 14, byy + 2 + sinf(animTime * 4.0f) * 2.0f, 0.8f);
    }
}

/* ============================================================
 *  CENARIOS
 * ============================================================ */

static const float itemX[3] = { 830.0f, 970.0f, 1110.0f };
static const float itemY[3] = { 497.0f, 496.0f, 490.0f };
#define K_PET_X 420.0f
#define K_PET_Y 620.0f

static void scene_kitchen(void)
{
    int tx, ty, i;
    float mopen = 0.0f;
    int mouth = mood_mouth();

    /* parede */
    g_vgrad(0, 0, VW, 560, RGB(255, 236, 178), RGB(255, 214, 140));
    /* azulejos */
    for (ty = 0; ty < 5; ty++)
        for (tx = 0; tx < 32; tx++) {
            u32 c = ((tx + ty) & 1) ? RGB(250, 250, 244) : RGB(226, 240, 248);
            g_rect(tx * 40, 330 + ty * 46, 40, 46, c, 255);
        }
    g_rect(0, 328, VW, 4, RGB(180, 150, 100), 255);

    /* geladeira */
    g_rrectg(40, 120, 160, 440, 14, RGB(228, 235, 242), RGB(186, 198, 212), 255);
    g_rect(44, 300, 152, 4, RGB(150, 160, 175), 255);
    g_rrect(172, 170, 10, 90, 5, RGB(130, 140, 155), 255);
    g_rrect(172, 330, 10, 120, 5, RGB(130, 140, 155), 255);
    g_rect(70, 200, 34, 26, RGB(250, 120, 120), 255);
    g_circle(130, 240, 11, RGB(110, 170, 250), 255);
    g_rect(75, 380, 40, 30, RGB(255, 220, 90), 255);

    /* armarios */
    for (i = 0; i < 4; i++) {
        float x = 250.0f + (float)i * 118.0f;
        g_rrectg(x, 110, 112, 150, 10, RGB(192, 132, 82), RGB(158, 104, 64), 255);
        g_rrect(x + 10, 120, 92, 130, 8, RGB(120, 76, 44), 70);
        g_circle(x + 92, 238, 5, RGB(240, 220, 160), 255);
    }

    /* janela */
    g_rrect(826, 108, 308, 178, 10, RGB(255, 255, 255), 255);
    g_vgrad(840, 122, 280, 150, RGB(130, 204, 250), RGB(205, 236, 255));
    g_circle(1070, 170, 28, RGB(255, 232, 100), 255);
    g_circle(1070, 170, 38, RGB(255, 240, 150), 60);
    g_ellipse(900, 190, 40, 14, RGB(255, 255, 255), 230);
    g_ellipse(930, 178, 30, 14, RGB(255, 255, 255), 230);
    g_rect(976, 122, 6, 150, RGB(255, 255, 255), 255);
    g_rect(840, 192, 280, 6, RGB(255, 255, 255), 255);

    /* bancada + piso */
    g_rect(0, 548, VW, 18, RGB(120, 80, 50), 255);
    g_vgrad(0, 566, VW, 154, RGB(214, 160, 104), RGB(168, 118, 74));
    for (i = 0; i < 5; i++)
        g_rect(0, 566 + i * 38, VW, 2, RGB(120, 84, 50), 110);

    /* mesa */
    g_rect(770, 548, 26, 100, RGB(120, 78, 46), 255);
    g_rect(1144, 548, 26, 100, RGB(120, 78, 46), 255);
    g_rrectg(740, 520, 460, 30, 10, RGB(176, 122, 74), RGB(140, 94, 56), 255);
    g_rect(752, 548, 436, 6, RGB(0, 0, 0), 50);

    /* bichinho */
    if (eatTimer > 0.0f) {
        mouth = 4;
        mopen = (eatTimer > 0.45f) ? 1.0f : fabsf(sinf(eatTimer * 22.0f));
    }
    draw_pet(K_PET_X, K_PET_Y, 1.0f, 0, mouth, mopen);

    /* comidas */
    for (i = 0; i < 3; i++) {
        float sc = 1.0f, yo = 0.0f;
        if (i == selFood) {
            sc = 1.25f;
            yo = -fabsf(sinf(animTime * 5.0f)) * 6.0f;
            g_ellipse(itemX[i], 522, 58, 10, RGB(255, 240, 120), 200);
        }
        draw_item(i, itemX[i], itemY[i] + yo - (sc - 1.0f) * 22.0f, sc);
    }
    {
        float bounce = sinf(animTime * 6.0f) * 5.0f;
        float ax = itemX[selFood];
        g_tri(ax - 13, 418 + bounce, ax + 13, 418 + bounce, ax, 440 + bounce, RGB(255, 240, 120), 255);
        g_rrect(970 - 120, 372, 240, 38, 14, RGB(20, 25, 50), 170);
        g_text_center(970, 380, food_names[selFood], 3, RGB(255, 255, 255));
    }

    /* comida voando para a boca */
    if (eatTimer > 0.45f) {
        float p = (0.95f - eatTimer) / 0.5f;
        float fx, fy, sc;
        p = clampf(p, 0.0f, 1.0f);
        fx = lerpf(itemX[eatItem], K_PET_X, p);
        fy = lerpf(itemY[eatItem], K_PET_Y - 62.0f, p) - sinf(p * PI_F) * 90.0f;
        sc = lerpf(1.25f, 0.6f, p);
        draw_item(eatItem, fx, fy, sc);
    }
}

static void scene_bath(void)
{
    int tx, ty, i;
    int mouth = mood_mouth();

    /* parede de azulejos */
    g_vgrad(0, 0, VW, 560, RGB(196, 234, 247), RGB(158, 208, 234));
    for (tx = 0; tx <= VW; tx += 80)
        g_rect(tx, 0, 3, 560, RGB(255, 255, 255), 150);
    for (ty = 0; ty <= 560; ty += 80)
        g_rect(0, ty, VW, 3, RGB(255, 255, 255), 150);
    g_rect(0, 556, VW, 8, RGB(120, 170, 200), 255);

    /* piso */
    for (ty = 0; ty < 2; ty++)
        for (tx = 0; tx < 16; tx++) {
            u32 c = ((tx + ty) & 1) ? RGB(236, 240, 246) : RGB(170, 190, 215);
            g_rect(tx * 80, 564 + ty * 80, 80, 80, c, 255);
        }

    /* espelho + pia */
    g_rrect(80, 160, 210, 230, 16, RGB(225, 228, 236), 255);
    g_rrectg(92, 172, 186, 206, 10, RGB(200, 232, 245), RGB(150, 200, 225), 255);
    g_tri(110, 372, 150, 372, 200, 182, RGB(255, 255, 255), 70);
    g_rrect(70, 430, 230, 40, 16, RGB(246, 249, 252), 255);
    g_rect(150, 468, 70, 100, RGB(230, 236, 242), 255);
    g_rrect(176, 408, 14, 26, 5, RGB(170, 180, 195), 255);
    g_rrect(160, 404, 46, 8, 4, RGB(170, 180, 195), 255);

    /* toalha */
    g_rect(1040, 258, 170, 8, RGB(150, 150, 165), 255);
    g_rrectg(1060, 266, 110, 170, 10, RGB(245, 125, 140), RGB(220, 95, 115), 255);
    g_rect(1060, 330, 110, 12, RGB(255, 255, 255), 200);

    /* registro do chuveiro */
    g_circle(920, 310, 22, RGB(180, 185, 195), 255);
    g_circle(920, 310, 14, showerOn ? RGB(80, 160, 255) : RGB(230, 90, 90), 255);
    g_rect(906, 306, 28, 8, RGB(255, 255, 255), 230);

    /* cano e cabeca do chuveiro */
    g_rect(634, 0, 12, 130, RGB(196, 202, 212), 255);
    g_rect(630, 118, 20, 12, RGB(160, 168, 180), 255);
    g_ellipse(640, 140, 72, 22, RGB(206, 212, 222), 255);
    g_ellipse(640, 152, 64, 12, RGB(150, 158, 172), 255);
    for (i = -3; i <= 3; i++)
        g_circle(640 + i * 16, 156, 2.5f, RGB(60, 66, 80), 255);

    /* tapete */
    g_ellipse(640, 632, 220, 28, RGB(250, 205, 120), 255);
    g_ellipse(640, 632, 190, 20, RGB(255, 225, 160), 255);

    /* poca d'agua */
    if (showerOn)
        g_ellipse(640, 636, 200 + sinf(animTime * 3.0f) * 6.0f, 24, RGB(120, 190, 255), 110);

    draw_pet(640.0f, 620.0f, 1.0f, 0, showerOn ? 3 : mouth, 0.0f);

    /* espuma */
    if (showerOn) {
        for (i = 0; i < 12; i++) {
            float bx = 640.0f + sinf((float)i * 1.7f) * 95.0f * (1.0f - (float)i * 0.03f);
            float byy = 600.0f - (float)i * 19.0f + sinf(animTime * 2.0f + (float)i) * 4.0f;
            float r = 7.0f + (float)(i % 3) * 4.0f;
            g_circle(bx, byy, r, RGB(255, 255, 255), 170);
            g_circle(bx - r * 0.3f, byy - r * 0.3f, r * 0.3f, RGB(255, 255, 255), 230);
        }
        /* agua */
        for (i = 0; i < N_DROPS; i++) {
            float x = drops[i].x, y = drops[i].y;
            g_rrect(x - 1.5f, y, 3.0f, 16.0f, 1.5f, RGB(130, 200, 255), 210);
            g_rrect(x - 0.5f, y + 2.0f, 1.0f, 8.0f, 0.5f, RGB(255, 255, 255), 200);
        }
        /* vapor */
        for (i = 0; i < 6; i++) {
            float fx = 300.0f + (float)i * 140.0f;
            float fy = 110.0f + sinf(animTime * 0.8f + (float)i) * 18.0f;
            g_ellipse(fx, fy, 90, 36, RGB(255, 255, 255), 35);
        }
    }
}

static void scene_bed(void)
{
    int i;
    int sleeping = !lightOn;
    int mouth = sleeping ? 5 : mood_mouth();
    u32 skyTop = RGB(18, 24, 70), skyBot = RGB(50, 60, 120);

    /* parede */
    g_vgrad(0, 0, VW, 560, RGB(70, 78, 140), RGB(102, 102, 168));
    for (i = 0; i < 46; i++)
        g_circle((float)((i * 97) % VW), (float)(90 + (i * 53) % 440), 2.5f, RGB(255, 255, 255), 60);
    g_rect(0, 556, VW, 8, RGB(60, 45, 80), 255);

    /* piso + tapete */
    g_vgrad(0, 564, VW, 156, RGB(112, 82, 122), RGB(80, 55, 95));
    g_ellipse(560, 640, 320, 40, RGB(182, 112, 142), 255);
    g_ellipse(560, 640, 270, 30, RGB(206, 140, 168), 255);

    /* cama */
    g_rrectg(850, 300, 400, 200, 22, RGB(160, 106, 66), RGB(120, 78, 46), 255);
    g_rect(860, 500, 380, 26, RGB(134, 88, 54), 255);
    g_rect(868, 526, 24, 52, RGB(110, 72, 42), 255);
    g_rect(1208, 526, 24, 52, RGB(110, 72, 42), 255);
    g_rrectg(858, 410, 384, 92, 18, RGB(240, 242, 252), RGB(214, 220, 240), 255);
    g_rrectg(858, 440, 384, 72, 18, RGB(110, 150, 228), RGB(80, 116, 200), 255);
    for (i = 0; i < 5; i++)
        g_rect(900 + i * 70, 440, 14, 72, RGB(255, 255, 255), 70);
    g_ellipse(930, 420, 62, 28, RGB(252, 252, 255), 255);
    g_ellipse(922, 414, 40, 12, RGB(255, 255, 255), 160);

    /* criado-mudo e abajur */
    g_rrectg(70, 470, 160, 110, 8, RGB(170, 114, 70), RGB(132, 88, 54), 255);
    g_rect(80, 520, 140, 3, RGB(90, 58, 34), 255);
    g_circle(150, 495, 5, RGB(240, 215, 150), 255);
    g_circle(150, 548, 5, RGB(240, 215, 150), 255);
    g_rect(146, 400, 8, 70, RGB(200, 200, 215), 255);
    g_ellipse(150, 468, 30, 7, RGB(180, 182, 200), 255);
    {
        u32 shade = lightOn ? RGB(255, 222, 140) : RGB(122, 112, 94);
        g_tri(120, 340, 180, 340, 200, 400, shade, 255);
        g_tri(120, 340, 200, 400, 100, 400, shade, 255);
    }
    if (lightOn) {
        g_ellipse(150, 380, 210, 170, RGB(255, 232, 160), 28);
        g_ellipse(150, 380, 140, 110, RGB(255, 232, 160), 36);
        g_ellipse(150, 380, 80, 64, RGB(255, 240, 180), 50);
    }

    /* bichinho */
    draw_pet(560.0f, 620.0f, 1.0f, sleeping, mouth, 0.0f);

    /* escuro */
    if (!lightOn) {
        int n;
        for (n = 0; n < VW * VH; n++) {
            u32 c = cv[n];
            cv[n] = (((c >> 2) & 0x3F3F3F) + ((c >> 3) & 0x1F1F1F)) + 0x04081A;
        }
        draw_zzz(560.0f, 370.0f, 1.0f);
    }

    /* janela (sempre noite, fica brilhante) */
    g_rrect(290, 108, 240, 200, 10, RGB(235, 238, 250), 255);
    g_vgrad(302, 120, 216, 176, skyTop, skyBot);
    for (i = 0; i < 12; i++)
        g_circle((float)(312 + (i * 37) % 196), (float)(130 + (i * 23) % 100), 1.8f + (float)(i % 2), RGB(255, 255, 220), 230);
    g_circle(400, 190, 32, RGB(255, 250, 210), 255);
    g_circle(416, 182, 28, RGB(34, 42, 95), 255);
    g_rect(406, 120, 6, 176, RGB(235, 238, 250), 255);
    g_rect(302, 204, 216, 6, RGB(235, 238, 250), 255);
    if (!lightOn) {
        g_tri(290, 308, 530, 308, 420, 610, RGB(170, 190, 255), 26);
        g_tri(530, 308, 420, 610, 760, 610, RGB(170, 190, 255), 26);
    }
}

static const char *game_label = "FOOD DROP";

static void scene_gameroom(void)
{
    int tx, ty, i;
    int mouth = mood_mouth();

    g_vgrad(0, 0, VW, 560, RGB(74, 44, 124), RGB(116, 66, 168));
    g_rect(0, 120, VW, 10, RGB(255, 80, 200), 200);
    g_rect(0, 124, VW, 2, RGB(255, 200, 240), 255);
    g_rect(0, 150, VW, 5, RGB(80, 220, 255), 200);
    g_rect(0, 556, VW, 8, RGB(40, 25, 80), 255);

    /* piso xadrez */
    for (ty = 0; ty < 2; ty++)
        for (tx = 0; tx < 16; tx++) {
            u32 c = ((tx + ty) & 1) ? RGB(42, 32, 84) : RGB(64, 48, 110);
            g_rect(tx * 80, 564 + ty * 80, 80, 80, c, 255);
        }

    /* poster */
    g_rrect(110, 300, 160, 210, 8, RGB(250, 240, 220), 255);
    g_rrectg(120, 310, 140, 190, 6, RGB(255, 120, 160), RGB(255, 190, 90), 255);
    g_circle(190, 380, 34, RGB(255, 255, 255), 230);
    draw_item(0, 190, 380, 0.9f);
    g_text_center(190, 450, "FOOD", 3, RGB(255, 255, 255));

    /* fliperama */
    g_rrectg(800, 200, 240, 370, 18, RGB(64, 74, 196), RGB(34, 40, 122), 255);
    g_rrect(812, 212, 216, 50, 10, RGB(255, 200, 50), 255);
    g_text_center(920, 226, "ARCADE", 3, RGB(120, 40, 20));
    g_rrect(826, 278, 188, 132, 10, RGB(8, 12, 28), 255);
    for (i = 0; i < 6; i++) {
        float fx = 846.0f + (float)((i * 31) % 150);
        float fy = 290.0f + fmodf(animTime * 70.0f + (float)(i * 47), 110.0f);
        g_circle(fx, fy, 7, (i & 1) ? RGB(255, 90, 90) : RGB(255, 220, 90), 255);
    }
    g_rect(900, 392, 40, 8, RGB(90, 220, 120), 255);
    g_rrect(812, 428, 216, 62, 8, RGB(38, 44, 150), 255);
    g_circle(860, 462, 10, RGB(230, 50, 50), 255);
    g_circle(940, 458, 9, RGB(255, 220, 60), 255);
    g_circle(975, 458, 9, RGB(80, 220, 120), 255);
    g_circle(1008, 458, 9, RGB(90, 150, 255), 255);
    g_rect(850, 570, 140, 10, RGB(20, 24, 70), 255);

    draw_pet(K_PET_X, K_PET_Y, 1.0f, 0, mouth, 0.0f);

    /* menu */
    g_text_center(520, 176, game_label, 7, RGB(255, 235, 120));
    g_text_center(520, 244, "COMIDA CAI DO CEU!", 3, RGB(255, 255, 255));
    if (((int)(animTime * 2.0f)) & 1)
        g_text_center(520, 290, "PRESSIONE X PARA JOGAR", 3, RGB(140, 255, 200));
    {
        char buf[40];
        snprintf(buf, sizeof(buf), "RECORDE: %d", ghi);
        g_text_center(520, 330, buf, 3, RGB(255, 210, 255));
    }
}

static void draw_clouds(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        float x = fmodf(animTime * 18.0f + (float)i * 380.0f, 1600.0f) - 150.0f;
        float y = 150.0f + (float)(i * 53 % 160);
        g_ellipse(x, y, 70, 22, RGB(255, 255, 255), 220);
        g_ellipse(x - 30, y - 12, 38, 20, RGB(255, 255, 255), 220);
        g_ellipse(x + 30, y - 10, 42, 22, RGB(255, 255, 255), 220);
    }
}

static void scene_minigame(void)
{
    int i;
    float s = 0.72f;
    float by = 672.0f;
    int mouth = 3;
    float mopen = 0.0f;

    g_vgrad(0, 0, VW, 640, RGB(110, 190, 255), RGB(255, 236, 190));
    g_circle(1130, 170, 48, RGB(255, 240, 130), 255);
    g_circle(1130, 170, 66, RGB(255, 245, 170), 60);
    draw_clouds();
    g_ellipse(260, 640, 420, 90, RGB(110, 190, 100), 255);
    g_ellipse(980, 650, 480, 100, RGB(90, 170, 90), 255);
    g_vgrad(0, 636, VW, 84, RGB(120, 205, 100), RGB(60, 140, 70));

    if (gstate == 1 || gstate == 2) {
        if (gcatch > 0.0f) {
            mouth = 4;
            mopen = 1.0f;
        } else if (glives <= 1) {
            mouth = 1;
        }
        draw_pet(gx, by, s, 0, mouth, mopen);
    }

    for (i = 0; i < N_FALL; i++)
        if (fl[i].on)
            draw_item(fl[i].type, fl[i].x, fl[i].y, 0.9f);

    for (i = 0; i < 8; i++)
        if (popups[i].life > 0.0f)
            g_text_sh((int)popups[i].x, (int)popups[i].y, popups[i].t, 4, popups[i].col);

    if (ghurt > 0.0f)
        g_rect(0, 0, VW, VH, RGB(255, 40, 40), (int)(ghurt * 160.0f));

    /* informacoes */
    {
        char buf[40];
        g_rrect(10, 80, 380, 36, 12, RGB(20, 25, 50), 160);
        snprintf(buf, sizeof(buf), "TEMPO: %d", (int)(gtime + 0.99f));
        g_text_sh(22, 88, buf, 3, RGB(255, 255, 255));
        snprintf(buf, sizeof(buf), "MOEDAS: %d", gcoins);
        g_text_sh(190, 88, buf, 3, RGB(255, 225, 90));
        for (i = 0; i < 3; i++)
            draw_heart(1170.0f + (float)i * 34.0f, 98.0f, 0.9f, i < glives ? RGB(235, 55, 75) : RGB(90, 70, 90));
    }

    if (gstate == 2) {
        char buf[40];
        g_rrect(340, 190, 600, 320, 26, RGB(20, 25, 60), 225);
        g_text_center(640, 215, "FIM DE JOGO", 6, RGB(255, 235, 120));
        snprintf(buf, sizeof(buf), "MOEDAS GANHAS: %d", gcoins);
        g_text_center(640, 300, buf, 4, RGB(255, 255, 255));
        snprintf(buf, sizeof(buf), "RECORDE: %d", ghi);
        g_text_center(640, 352, buf, 3, RGB(255, 210, 255));
        if (((int)(animTime * 2.0f)) & 1)
            g_text_center(640, 430, "PRESSIONE X PARA VOLTAR", 3, RGB(140, 255, 200));
    }
}

/* ============================================================
 *  HUD
 * ============================================================ */

static void draw_bar(int x, int y, int w, int h, float val, u32 c1, u32 c2)
{
    int fw = (int)((float)w * val / 100.0f);
    g_rrect((float)x, (float)y, (float)w, (float)h, (float)h * 0.5f, RGB(15, 18, 36), 230);
    if (fw < h) fw = (val > 0.5f) ? h : 0;
    if (fw > 0) {
        if (val < 25.0f && (((int)(animTime * 3.0f)) & 1)) {
            c1 = RGB(255, 80, 70);
            c2 = RGB(200, 30, 40);
        }
        g_rrectg((float)x + 2, (float)y + 2, (float)fw - 4, (float)h - 4, (float)(h - 4) * 0.5f, c1, c2, 255);
        g_rrect((float)x + 5, (float)y + 4, (float)fw - 10, 3, 1.5f, RGB(255, 255, 255), 90);
    }
}

static void draw_hud(void)
{
    int i;
    char buf[24];
    float vals[4];
    const char *names[4] = { "FOME", "SAUDE", "ENERGIA", "DIVERSAO" };
    u32 c1[4] = { RGB(255, 190, 70), RGB(255, 110, 120), RGB(255, 235, 90), RGB(200, 130, 255) };
    u32 c2[4] = { RGB(230, 120, 30), RGB(210, 50, 80), RGB(230, 170, 30), RGB(150, 80, 230) };

    vals[0] = hunger;
    vals[1] = health;
    vals[2] = energy;
    vals[3] = fun;

    g_rrect(8, 6, 1264, 66, 18, RGB(18, 22, 46), 175);
    for (i = 0; i < 4; i++) {
        int x = 16 + i * 250;
        float ix = (float)x + 18.0f, iy = 40.0f;
        g_text_sh(x + 42, 12, names[i], 2, RGB(255, 255, 255));
        draw_bar(x + 42, 36, 160, 20, vals[i], c1[i], c2[i]);
        if (i == 0) {
            draw_item(0, ix, iy, 0.45f);
        } else if (i == 1) {
            draw_heart(ix, iy - 2, 1.0f, RGB(240, 60, 85));
        } else if (i == 2) {
            g_tri(ix + 5, iy - 16, ix - 8, iy + 3, ix + 2, iy + 3, RGB(255, 225, 60), 255);
            g_tri(ix - 5, iy, ix + 9, iy - 3, ix - 3, iy + 17, RGB(255, 225, 60), 255);
        } else {
            g_circle(ix, iy, 15, RGB(255, 220, 70), 255);
            g_circle(ix - 5, iy - 4, 2.5f, RGB(60, 40, 20), 255);
            g_circle(ix + 5, iy - 4, 2.5f, RGB(60, 40, 20), 255);
            draw_curve(ix, iy + 2, 7, 5, 2, RGB(60, 40, 20));
        }
    }
    /* moedas */
    g_circle(1098, 40, 19, RGB(214, 150, 20), 255);
    g_circle(1098, 40, 16, RGB(255, 216, 70), 255);
    g_circle(1098, 40, 11, RGB(240, 180, 30), 255);
    g_rect(1096, 31, 4, 18, RGB(255, 236, 140), 255);
    snprintf(buf, sizeof(buf), "%d", coins);
    g_text_sh(1128, 24, buf, 4, RGB(255, 235, 140));
}

static void draw_bottom(void)
{
    int i;
    const char *hint = NULL;
    g_rect(0, 684, VW, 36, RGB(10, 12, 30), 170);

    g_rrect(16, 690, 56, 26, 8, RGB(70, 80, 140), 255);
    g_text_center(44, 696, "L1", 2, RGB(255, 255, 255));
    g_rrect(VW - 72, 690, 56, 26, 8, RGB(70, 80, 140), 255);
    g_text_center(VW - 44, 696, "R1", 2, RGB(255, 255, 255));

    for (i = 0; i < 4; i++) {
        int x = 300 + i * 160;
        if (i == room)
            g_rrect((float)x, 689, 150, 28, 10, RGB(255, 190, 70), 255);
        else
            g_rrect((float)x, 689, 150, 28, 10, RGB(50, 58, 100), 255);
        g_text_center(x + 75, 696, room_names[i], 2, i == room ? RGB(60, 30, 10) : RGB(210, 215, 240));
    }

    if (room == ROOM_KITCHEN) hint = "< > ESCOLHER    X: ALIMENTAR";
    else if (room == ROOM_BATH) hint = showerOn ? "X: DESLIGAR CHUVEIRO" : "X: LIGAR CHUVEIRO";
    else if (room == ROOM_BED) hint = lightOn ? "X: APAGAR A LUZ E DORMIR" : "X: ACENDER A LUZ";
    else if (gstate == 0) hint = "X: JOGAR FOOD DROP";

    if (hint != NULL) {
        int w = text_width(hint, 2);
        g_rrect((float)(640 - w / 2 - 14), 650, (float)(w + 28), 28, 12, RGB(18, 22, 46), 170);
        g_text_sh(640 - w / 2, 657, hint, 2, RGB(255, 255, 255));
    }
}

static void draw_message(void)
{
    if (msgTimer > 0.0f && msgText[0]) {
        int w = text_width(msgText, 3);
        float y = (room == ROOM_GAMES && gstate != 0) ? 124.0f : 90.0f;
        g_rrect((float)(640 - w / 2 - 16), y, (float)(w + 32), 38, 14, RGB(18, 22, 46), 190);
        g_text_sh(640 - w / 2, (int)y + 8, msgText, 3, RGB(255, 240, 150));
    }
}

/* ============================================================
 *  LOGICA
 * ============================================================ */

static void read_input(void)
{
    int i, connected = 0;

    in.pl1 = in.l1;
    in.pr1 = in.r1;
    in.px = in.x;
    in.pleft = in.left;
    in.pright = in.right;
    in.pcircle = in.circle;

    memset(&paddata, 0, sizeof(paddata));
    ioPadGetInfo(&padinfo);
    for (i = 0; i < MAX_PADS; i++) {
        if (padinfo.status[i]) {
            ioPadGetData(i, &paddata);
            connected = 1;
            break;
        }
    }

    if (connected) {
        in.l1 = paddata.BTN_L1 ? 1 : 0;
        in.r1 = paddata.BTN_R1 ? 1 : 0;
        in.x = paddata.BTN_CROSS ? 1 : 0;
        in.left = paddata.BTN_LEFT ? 1 : 0;
        in.right = paddata.BTN_RIGHT ? 1 : 0;
        in.circle = paddata.BTN_CIRCLE ? 1 : 0;
        in.ax = ((float)paddata.ANA_L_H - 128.0f) / 128.0f;
        in.ay = ((float)paddata.ANA_L_V - 128.0f) / 128.0f;
        if (fabsf(in.ax) < 0.18f) in.ax = 0.0f;
        if (fabsf(in.ay) < 0.18f) in.ay = 0.0f;
        in.ax = clampf(in.ax, -1.0f, 1.0f);
        in.ay = clampf(in.ay, -1.0f, 1.0f);
    } else {
        in.l1 = in.r1 = in.x = in.left = in.right = in.circle = 0;
        in.ax = in.ay = 0.0f;
    }
}

static void feed(void)
{
    if (eatTimer > 0.0f) return;
    if (selFood == 0) {
        if (hunger > 97.0f) { set_msg("ESTOU CHEIO!"); return; }
        hunger += 18.0f; health += 2.0f; fun += 2.0f;
        set_msg("NHAC! QUE DELICIA!");
    } else if (selFood == 1) {
        if (hunger > 97.0f) { set_msg("ESTOU CHEIO!"); return; }
        hunger += 35.0f; fun += 8.0f; health -= 2.0f;
        add_dirt();
        set_msg("NHAM! HAMBURGUER!");
    } else {
        if (health > 97.0f && energy > 97.0f) { set_msg("ESTOU SAUDAVEL!"); return; }
        health += 30.0f; energy += 12.0f;
        set_msg("GLUB GLUB! ENERGIA!");
    }
    eatItem = selFood;
    eatTimer = 0.95f;
    clamp_stats();
}

static void change_room(int dir)
{
    room = (room + dir + 4) % 4;
    lightOn = 1;
    showerOn = 0;
    fadeAlpha = 210.0f;
    if (room != ROOM_GAMES) gstate = 0;
}

static void start_game(void)
{
    int i;
    gstate = 1;
    gx = 640.0f;
    glives = 3;
    gtime = 45.0f;
    gcoins = 0;
    gelapsed = 0.0f;
    gspawn = 0.6f;
    gcatch = 0.0f;
    ghurt = 0.0f;
    for (i = 0; i < N_FALL; i++) fl[i].on = 0;
}

static void end_game(void)
{
    char buf[40];
    gstate = 2;
    coins += gcoins;
    if (gcoins > ghi) ghi = gcoins;
    fun += MINF(30.0f, (float)gcoins * 1.5f);
    energy -= 8.0f;
    clamp_stats();
    snprintf(buf, sizeof(buf), "+%d MOEDAS!", gcoins);
    set_msg(buf);
}

static void update_minigame(float dt)
{
    int i;
    float BW = 140.0f * 0.72f, BH = 250.0f * 0.72f;
    float by = 672.0f, top = by - BH;

    if (gstate == 0) {
        if (PRESSED(x)) start_game();
        return;
    }
    if (gstate == 2) {
        if (PRESSED(x)) gstate = 0;
        return;
    }

    /* jogando */
    gx += in.ax * 680.0f * dt;
    if (in.left) gx -= 680.0f * dt;
    if (in.right) gx += 680.0f * dt;
    gx = clampf(gx, BW * 0.9f, (float)VW - BW * 0.9f);

    gtime -= dt;
    gelapsed += dt;
    energy -= 0.8f * dt;
    if (gcatch > 0.0f) gcatch -= dt;
    if (ghurt > 0.0f) ghurt -= dt * 2.0f;

    gspawn -= dt;
    if (gspawn <= 0.0f) {
        for (i = 0; i < N_FALL; i++) {
            if (!fl[i].on) {
                int r = rand() % 100;
                fl[i].on = 1;
                fl[i].x = 80.0f + (float)(rand() % 1120);
                fl[i].y = -40.0f;
                fl[i].v = 170.0f + gelapsed * 3.0f + (float)(rand() % 80);
                fl[i].type = r < 40 ? 0 : (r < 70 ? 1 : (r < 85 ? 2 : 3));
                break;
            }
        }
        gspawn = MAXF(0.35f, 0.9f - gelapsed * 0.012f) + (float)(rand() % 30) / 100.0f;
    }

    for (i = 0; i < N_FALL; i++) {
        if (!fl[i].on) continue;
        fl[i].y += fl[i].v * dt;
        if (fl[i].y > top - 10.0f && fl[i].y < by - BH * 0.30f && fabsf(fl[i].x - gx) < BW * 0.85f) {
            if (fl[i].type == 3) {
                glives--;
                ghurt = 1.0f;
                add_popup(fl[i].x, fl[i].y - 40.0f, "ECA!", RGB(150, 255, 100));
            } else {
                int v = fl[i].type == 0 ? 1 : (fl[i].type == 1 ? 2 : 3);
                char b[12];
                gcoins += v;
                fun += 1.5f;
                snprintf(b, sizeof(b), "+%d", v);
                add_popup(fl[i].x, fl[i].y - 40.0f, b, RGB(255, 235, 90));
            }
            gcatch = 0.25f;
            fl[i].on = 0;
        } else if (fl[i].y > 650.0f) {
            fl[i].on = 0;
        }
    }

    for (i = 0; i < 8; i++) {
        if (popups[i].life > 0.0f) {
            popups[i].life -= dt * 1.2f;
            popups[i].y -= 50.0f * dt;
        }
    }

    lookX = in.ax;
    lookY = 0.0f;
    clamp_stats();

    if (PRESSED(circle)) end_game();
    else if (gtime <= 0.0f || glives <= 0) end_game();
}

static void update(float dt)
{
    int sleeping, i, dcount;
    float hd = 0.55f, ed = 0.35f, fd = 0.40f, drain = 0.0f;
    int inGame = (room == ROOM_GAMES && gstate == 1);

    animTime += dt;
    if (msgTimer > 0.0f) msgTimer -= dt;
    if (fadeAlpha > 0.0f) fadeAlpha -= 700.0f * dt;
    if (fadeAlpha < 0.0f) fadeAlpha = 0.0f;

    /* troca de sala */
    if (!inGame) {
        if (PRESSED(l1)) change_room(-1);
        else if (PRESSED(r1)) change_room(1);
    }

    /* olhos seguem o analogico */
    if (!inGame) {
        lookX += (in.ax - lookX) * 0.2f;
        lookY += (in.ay - lookY) * 0.2f;
    } else {
        lookX += (in.ax - lookX) * 0.2f;
        lookY += (0.0f - lookY) * 0.2f;
    }

    /* piscar */
    if (blinkPhase < 0.0f) {
        blinkTimer -= dt;
        blinkF = 1.0f;
        if (blinkTimer <= 0.0f) blinkPhase = 0.0f;
    } else {
        blinkPhase += dt / 0.18f;
        if (blinkPhase >= 1.0f) {
            blinkPhase = -1.0f;
            blinkTimer = 2.0f + (float)(rand() % 300) / 100.0f;
            blinkF = 1.0f;
        } else {
            blinkF = fabsf(cosf(blinkPhase * PI_F));
        }
    }

    if (eatTimer > 0.0f) {
        eatTimer -= dt;
        if (eatTimer < 0.0f) eatTimer = 0.0f;
    }

    /* acoes por sala */
    if (room == ROOM_KITCHEN) {
        if (PRESSED(left)) selFood = (selFood + 2) % 3;
        if (PRESSED(right)) selFood = (selFood + 1) % 3;
        if (PRESSED(x)) feed();
    } else if (room == ROOM_BATH) {
        if (PRESSED(x)) {
            showerOn = !showerOn;
            if (showerOn) {
                for (i = 0; i < N_DROPS; i++) reset_drop(&drops[i], 1);
                set_msg("CHUVEIRO LIGADO!");
            } else {
                set_msg("CHUVEIRO DESLIGADO");
            }
        }
    } else if (room == ROOM_BED) {
        if (PRESSED(x)) {
            lightOn = !lightOn;
            set_msg(lightOn ? "BOM DIA!" : "BOA NOITE... ZZZ");
        }
    } else {
        update_minigame(dt);
    }

    /* agua do chuveiro e limpeza */
    if (room == ROOM_BATH && showerOn) {
        int before = dirt_count();
        for (i = 0; i < N_DROPS; i++) {
            drops[i].x += drops[i].vx * dt;
            drops[i].y += drops[i].vy * dt;
            if (drops[i].y > 640.0f) reset_drop(&drops[i], 0);
        }
        for (i = 0; i < N_DIRT; i++) {
            if (dirt[i] > 0.0f) {
                dirt[i] -= 0.45f * dt;
                if (dirt[i] < 0.0f) dirt[i] = 0.0f;
            }
        }
        fun += 0.6f * dt;
        if (before > 0 && dirt_count() == 0) {
            set_msg("TUDO LIMPINHO!");
            health += 5.0f;
        }
    }

    /* status */
    sleeping = (room == ROOM_BED && !lightOn);
    if (sleeping) {
        energy += 7.0f * dt;
        hd = 0.15f;
        fd = 0.05f;
        if (energy >= 100.0f && msgTimer <= 0.0f)
            set_msg("ENERGIA CHEIA!");
    } else {
        energy -= ed * dt;
    }
    if (!inGame) {
        hunger -= hd * dt;
        fun -= fd * dt;
    } else {
        hunger -= hd * 0.5f * dt;
    }

    dcount = dirt_count();
    if (dcount >= 3) fun -= 0.3f * dt;
    if (hunger < 15.0f) drain += 0.6f;
    if (energy < 10.0f) drain += 0.4f;
    if (dcount >= 3) drain += 0.25f;
    if (fun < 10.0f) drain += 0.2f;
    if (drain > 0.0f) health -= drain * dt;
    else if (hunger > 40.0f && energy > 30.0f) health += 0.25f * dt;

    /* sujeira surge com o tempo */
    if (!sleeping) {
        dirtTimer -= dt;
        if (dirtTimer <= 0.0f) {
            add_dirt();
            dirtTimer = 30.0f + (float)(rand() % 25);
        }
    }

    clamp_stats();
}

/* ============================================================
 *  RENDER
 * ============================================================ */

static void render(void)
{
    switch (room) {
    case ROOM_KITCHEN: scene_kitchen(); break;
    case ROOM_BATH:    scene_bath(); break;
    case ROOM_BED:     scene_bed(); break;
    default:
        if (gstate == 0) scene_gameroom();
        else scene_minigame();
        break;
    }

    draw_hud();
    if (!(room == ROOM_GAMES && gstate != 0))
        draw_bottom();
    else
        g_rect(0, 684, VW, 36, RGB(10, 12, 30), 120);
    draw_message();

    if (room == ROOM_GAMES && gstate == 1)
        g_text_center(640, 692, "ANALOGICO: MOVER    O: SAIR", 2, RGB(255, 255, 255));

    if (fadeAlpha > 0.0f)
        g_rect(0, 0, VW, VH, RGB(0, 0, 0), (int)fadeAlpha);
}

/* ============================================================
 *  MAIN
 * ============================================================ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    initScreen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_exit_callback, NULL);

    cv = (u32 *)memalign(128, VW * VH * sizeof(u32));
    if (cv == NULL)
        return 1;
    memset(cv, 0, VW * VH * sizeof(u32));

    init_game();
    running = 1;

    while (running) {
        sysUtilCheckCallback();
        read_input();

        waitFlip();

        update(1.0f / 60.0f);
        render();
        present(&buffers[currentBuffer]);

        flipBuffer(currentBuffer);
        currentBuffer = !currentBuffer;
    }

    gcmSetWaitFlip(context);
    if (buffers[0].ptr) rsxFree(buffers[0].ptr);
    if (buffers[1].ptr) rsxFree(buffers[1].ptr);
    rsxFinish(context, 1);
    ioPadEnd();
    free(cv);

    return 0;
}
