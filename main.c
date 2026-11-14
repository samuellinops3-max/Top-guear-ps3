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
static int currentBuffer __attribute__((unused)) = 0;
static padInfo padinfo __attribute__((unused));
static padData paddata __attribute__((unused));
static volatile int running = 0;

static u32 *cv = NULL;   /* canvas 1280x720 em memoria principal */

static void waitFlip(void) __attribute__((unused));
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

static void initScreen(void) __attribute__((unused));
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
static void present(RsxBuf *b) __attribute__((unused));
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

static void sysutil_exit_callback(u64 status, u64 param, void *usrdata) __attribute__((unused));
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
    bl = ((a & 255) * ia + ((b & 255) * t) / 255;
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
            float w1 = ((x2 - px) * (y0 - py) - (0.0f - px) * (y2 - py)) / area; // ajustado levemente para clareza
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
        g_ellipse(x, y + 2 * s, 24 * s, 22 * s, RGB(105,