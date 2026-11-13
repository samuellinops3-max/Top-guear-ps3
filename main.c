// SLITHER.IO PS3 - Base VERDE RSX + Powers + Skins + Kills
// Controles: analogico = direcao, X = turbo, BOLA = escudo, TRIANGULO = ima
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

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)

#define WORLD  3200.0f
#define MAXSEG 420
#define NSN    10
#define NPEL   500
#define NPOW   8
#define SP     7.0f
#define PI     3.14159265f

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick;
    float ang, tgt;
    u32 col;
    int skin;
} Snake;

typedef struct {
    float x, y;
    int size, active;
    u32 col;
} Pellet;

typedef struct {
    float x, y;
    int type, active; // 0=escudo 1=ima 2=bonus +50
} Power;

static const u32 palette[8] = {
    0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F,
    0x00C77DFF, 0x00FF8FAB, 0x0000E5C0, 0x00FF9F43
};

// Skins - cada skin tem 2 cores que alternam
static const u32 skins[8][2] = {
    {0x00FF4D4D, 0x00FFD23F}, // 0 fogo
    {0x00FFFFFF, 0x00111111}, // 1 zebra
    {0x00FF0000, 0x00FF8800}, // 2 lava
    {0x00FF0066, 0x00FFFFFF}, // 3 rosa
    {0x0000FF00, 0x000088FF}, // 4 verde/azul
    {0x00FF8800, 0x00000000}, // 5 abelha
    {0x008800FF, 0x0000FFFF}, // 6 neon
    {0x0000FF88, 0x00003311}, // 7 menta
};

/* ---------- video - IGUAL O VERDE ---------- */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void)
{
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- desenho - IGUAL O VERDE ---------- */
static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx, int cy, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= (int)W || cy + r < 0 || cy - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

static void circle_outline(int cx, int cy, int r, u32 c)
{
    for (int a = 0; a < 360; a += 6) {
        float rad = a * PI / 180.0f;
        int x = cx + (int)(cosf(rad) * r);
        int y = cy + (int)(sinf(rad) * r);
        if (x >= 0 && x < (int)W && y >= 0 && y < (int)H) {
            back[y * W + x] = c;
            if (x + 1 < (int)W) back[y * W + x + 1] = c;
        }
    }
}

/* fonte 3x5 - COMPLETA pro novo jogo */
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'D', "##." "#.#" "#.#" "#.#" "##."},
    {'E', "###" "#.." "###" "#.." "###"}, {'F', "###" "#.." "###" "#.." "#.."},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'H', "#.#" "#.#" "###" "#.#" "#.#"},
    {'I', "###" ".#." ".#." ".#." "###"}, {'K', "#.#" "#.#" "##." "#.#" "#.#"},
    {'L', "#.." "#.." "#.." "#.." "###"}, {'M', "#.#" "###" "###" "#.#" "#.#"},
    {'N', "#.#" "###" "###" "#.#" "#.#"}, {'O', "###" "#.#" "#.#" "#.#" "###"},
    {'P', "###" "#.#" "###" "#.." "#.."}, {'R', "##." "#.#" "##." "#.#" "#.#"},
    {'S', "###" "#.." "###" "..#" "###"}, {'T', "###" ".#." ".#." ".#." ".#."},
    {'U', "#.#" "#.#" "#.#" "#.#" "###"}, {'V', "#.#" "#.#" "#.#" "#.#" ".#."},
    {'X', "#.#" "#.#" ".#." "#.#" "#.#"}, {'+', ".#." ".#." "###" ".#." ".#."},
    {'-', "..." "..." "###" "..." "..."}, {':', "..." ".#." "..." ".#." "..."},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

/* ---------- jogo - MEU SLITHER + BASE VERDE ---------- */
static Snake sn[NSN];
static Pellet pel[NPEL];
static Power powers[NPOW];
static int pelrot = 0, dead_t = 0, x_prev = 0;
static int kills = 0, foodsEaten = 0;
static int shieldOn = 0, magnetOn = 0, shieldTime = 0, magnetTime = 0;
static int playerSkin = 0;
static float camx = WORLD / 2, camy = WORLD / 2;
static float pad_ax = 0, pad_ay = 0, pad_mag = 0;
static int pad_boost = 0, pad_x = 0, pad_circle = 0, pad_tri = 0, pad_circle_prev = 0, pad_tri_prev = 0;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static float radius_of(const Snake *s)
{
    int l = s->len > 240 ? 240 : s->len;
    return 9.0f + l * 0.035f;
}

static void add_pellet(float x, float y, int size)
{
    for (int k = 0; k < NPEL; k++) {
        int i = (pelrot + k) % NPEL;
        if (!pel[i].active) {
            pel[i].x = x; pel[i].y = y; pel[i].size = size; pel[i].active = 1;
            pel[i].col = palette[rand() % 8];
            pelrot = (i + 1) % NPEL;
            return;
        }
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 400 + frand() * (WORLD - 800), y = 400 + frand() * (WORLD - 800);
    s->ang = frand() * 2 * PI;
    s->tgt = s->ang;
    s->len = 10;
    for (int j = 0; j < s->len; j++) {
        s->x[j] = x - cosf(s->ang) * SP * j;
        s->y[j] = y - sinf(s->ang) * SP * j;
    }
    s->alive = 1; s->respawn = 0; s->boost = 0; s->grow = 0; s->tick = 0;
    if (i == 0) { s->skin = playerSkin; s->col = skins[playerSkin][0]; }
    else { s->skin = rand() % 8; s->col = skins[s->skin][0]; }
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    for (int j = 0; j < s->len; j += 2)
        add_pellet(s->x[j] + (frand() - 0.5f) * 10, s->y[j] + (frand() - 0.5f) * 10, 2);
    s->alive = 0;
    s->respawn = 240;
    if (i == 0) dead_t = 0;
    else kills++;
}

static void bot_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->x[0], hy = s->y[0];
    s->tgt += (frand() - 0.5f) * 0.25f;

    if (s->tick % 6 == 0) {
        float best = 380.0f * 380.0f;
        int bi = -1;
        for (int k = 0; k < NPEL; k++) {
            if (!pel[k].active) continue;
            float dx = pel[k].x - hx, dy = pel[k].y - hy, d = dx * dx + dy * dy;
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0) s->tgt = atan2f(pel[bi].y - hy, pel[bi].x - hx);
    }

    if (hx < 250 || hx > WORLD - 250 || hy < 250 || hy > WORLD - 250)
        s->tgt = atan2f(WORLD / 2 - hy, WORLD / 2 - hx);

    float px = hx + cosf(s->ang) * 55, py = hy + sinf(s->ang) * 55;
    float rs = radius_of(s);
    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = rs + radius_of(&sn[o]) + 25;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j += 2) {
            float dx = sn[o].x[j] - px, dy = sn[o].y[j] - py;
            if (dx * dx + dy * dy < lim) { s->tgt = s->ang + 1.6f; return; }
        }
    }
}

static void update_snake(int i)
{
    Snake *s = &sn[i];
    if (!s->alive) {
        if (i > 0 && --s->respawn <= 0) spawn_snake(i);
        return;
    }
    s->tick++;
    if (i == 0) {
        if (pad_mag > 0.3f) s->tgt = atan2f(pad_ay, pad_ax);
        s->boost = pad_boost;
        // BOLA = escudo, TRIANGULO = ima - so quando aperta, nao segurando
        if (pad_circle && !pad_circle_prev && !shieldOn) { shieldOn = 1; shieldTime = 300; }
        if (pad_tri && !pad_tri_prev && !magnetOn) { magnetOn = 1; magnetTime = 400; }
    } else {
        bot_think(i);
        s->boost = (s->len > 30 && (s->tick / 90) % 5 == 0) ? 1 : 0;
    }

    float d = s->tgt - s->ang;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    if (d > 0.075f) d = 0.075f;
    if (d < -0.075f) d = -0.075f;
    s->ang += d;

    float sp = 3.0f;
    if (s->boost && s->len > 12) {
        sp = 5.4f;
        if (s->tick % 10 == 0) {
            s->len--;
            add_pellet(s->x[s->len], s->y[s->len], 1);
        }
    }
    s->x[0] += cosf(s->ang) * sp;
    s->y[0] += sinf(s->ang) * sp;
    for (int j = 1; j < s->len; j++) {
        float dx = s->x[j - 1] - s->x[j], dy = s->y[j - 1] - s->y[j];
        float dd = sqrtf(dx * dx + dy * dy);
        if (dd > SP) {
            float f = (dd - SP) / dd;
            s->x[j] += dx * f;
            s->y[j] += dy * f;
        }
    }

    // Parede - com escudo rebate, sem escudo morre
    if (s->x[0] < 0 || s->x[0] > WORLD || s->y[0] < 0 || s->y[0] > WORLD) {
        if (i == 0 && shieldOn) {
            if (s->x[0] < 0) s->x[0] = 0; if (s->x[0] > WORLD) s->x[0] = WORLD;
            if (s->y[0] < 0) s->y[0] = 0; if (s->y[0] > WORLD) s->y[0] = WORLD;
        } else { kill_snake(i); return; }
    }

    float rs = radius_of(s), er = rs + 12;
    // IMA - puxa comida de longe
    if (i == 0 && magnetOn) er = 150;
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        float dx = pel[k].x - s->x[0], dy = pel[k].y - s->y[0];
        float dist2 = dx * dx + dy * dy;
        if (i == 0 && magnetOn && dist2 < 150 * 150 && dist2 > er * er) {
            pel[k].x += (s->x[0] - pel[k].x) * 0.15f;
            pel[k].y += (s->y[0] - pel[k].y) * 0.15f;
            continue;
        }
        if (dist2 < er * er) {
            pel[k].active = 0;
            s->grow += pel[k].size;
            s->score += pel[k].size * 5;
            if (i == 0) foodsEaten++;
        }
    }
    while (s->grow >= 2 && s->len < MAXSEG) {
        s->x[s->len] = s->x[s->len - 1];
        s->y[s->len] = s->y[s->len - 1];
        s->len++;
        s->grow -= 2;
    }
    if (s->len >= MAXSEG) s->grow = 0;

    // Powers - so player pega
    if (i == 0) {
        for (int p = 0; p < NPOW; p++) {
            if (!powers[p].active) continue;
            float dx = powers[p].x - s->x[0], dy = powers[p].y - s->y[0];
            if (dx * dx + dy * dy < 625) {
                if (powers[p].type == 0) { shieldOn = 1; shieldTime = 300; }
                if (powers[p].type == 1) { magnetOn = 1; magnetTime = 400; }
                if (powers[p].type == 2) { s->score += 50; }
                powers[p].x = frand() * WORLD; powers[p].y = frand() * WORLD;
                powers[p].type = rand() % 3;
            }
        }
        if (shieldTime > 0) shieldTime--; else shieldOn = 0;
        if (magnetTime > 0) magnetTime--; else magnetOn = 0;
    }

    // Colisao com corpo - com escudo, gasta escudo e nao morre
    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = (rs + radius_of(&sn[o])) * 0.75f;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j++) {
            float dx = sn[o].x[j] - s->x[0], dy = sn[o].y[j] - s->y[0];
            if (dx * dx + dy * dy < lim) {
                if (i == 0 && shieldOn) { shieldOn = 0; shieldTime = 0; return; }
                kill_snake(i); return;
            }
        }
    }
}

static void init_game(void)
{
    memset(pel, 0, sizeof(pel));
    for (int k = 0; k < NPEL; k++) add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));
    for (int p = 0; p < NPOW; p++) {
        powers[p].x = frand() * WORLD; powers[p].y = frand() * WORLD;
        powers[p].type = p % 3; powers[p].active = 1;
    }
    for (int i = 0; i < NSN; i++) { sn[i].score = 0; spawn_snake(i); }
    dead_t = 0; kills = 0; foodsEaten = 0; shieldOn = 0; magnetOn = 0; shieldTime = 0; magnetTime = 0;
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    pad_ax = pad_ay = pad_mag = 0;
    pad_boost = pad_x = pad_circle = pad_tri = 0;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);
        float ax = ((int)d.ANA_L_H - 128) / 128.0f, ay = ((int)d.ANA_L_V - 128) / 128.0f;
        if (d.BTN_LEFT) ax = -1;
        if (d.BTN_RIGHT) ax = 1;
        if (d.BTN_UP) ay = -1;
        if (d.BTN_DOWN) ay = 1;
        pad_ax = ax; pad_ay = ay;
        pad_mag = sqrtf(ax * ax + ay * ay);
        pad_boost = d.BTN_CROSS || d.BTN_R1 || d.BTN_R2;
        pad_x = d.BTN_CROSS;
        pad_circle = d.BTN_CIRCLE;
        pad_tri = d.BTN_TRIANGLE;
    }
}

static void update_game(void)
{
    for (int i = 0; i < NSN; i++) update_snake(i);
    pad_circle_prev = pad_circle;
    pad_tri_prev = pad_tri;

    int count = 0;
    for (int k = 0; k < NPEL; k++) count += pel[k].active;
    for (int n = 0; n < 2 && count < NPEL - 40; n++, count++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));

    if (sn[0].alive) {
        camx = sn[0].x[0];
        camy = sn[0].y[0];
    } else {
        dead_t++;
        if (dead_t > 60 && pad_x && !x_prev) {
            spawn_snake(0);
            sn[0].score = 0;
        }
    }
    x_prev = pad_x;
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    float hw = W / 2.0f, hh = H / 2.0f;
    u32 n = W * H;
    for (u32 i = 0; i < n; i++) back[i] = 0x000E1420;

    int sx0 = (int)((0 - camx) * sc + hw), sx1 = (int)((WORLD - camx) * sc + hw);
    int sy0 = (int)((0 - camy) * sc + hh), sy1 = (int)((WORLD - camy) * sc + hh);
    u32 red = 0x00380E12;
    if (sx0 > 0) fill_rect(0, 0, sx0, H, red);
    if (sx1 < (int)W) fill_rect(sx1, 0, W - sx1, H, red);
    if (sy0 > 0) fill_rect(0, 0, W, sy0, red);
    if (sy1 < (int)H) fill_rect(0, sy1, W, H - sy1, red);

    float gx0 = floorf((camx - hw / sc) / 100.0f) * 100.0f;
    float gy0 = floorf((camy - hh / sc) / 100.0f) * 100.0f;
    for (float gx = gx0; gx < camx + hw / sc; gx += 100.0f) {
        if (gx < 0 || gx > WORLD) continue;
        for (float gy = gy0; gy < camy + hh / sc; gy += 100.0f) {
            if (gy < 0 || gy > WORLD) continue;
            fill_rect((int)((gx - camx) * sc + hw), (int)((gy - camy) * sc + hh), 4, 4, 0x00203047);
        }
    }

    // Comida
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        int px = (int)((pel[k].x - camx) * sc + hw), py = (int)((pel[k].y - camy) * sc + hh);
        circle(px, py, (int)((4 + pel[k].size * 2) * sc), pel[k].col);
    }

    // Powers - maior e brilhando
    for (int p = 0; p < NPOW; p++) {
        if (!powers[p].active) continue;
        int px = (int)((powers[p].x - camx) * sc + hw), py = (int)((powers[p].y - camy) * sc + hh);
        u32 c = powers[p].type == 0 ? 0x0000AAFF : powers[p].type == 1 ? 0x00FF00FF : 0x00FFD23F;
        circle(px, py, (int)(14 * sc), c);
        circle(px, py, (int)(7 * sc), 0x00FFFFFF);
    }

    // Cobras com SKINS
    for (int i = NSN - 1; i >= 0; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        int r = (int)(radius_of(s) * sc);
        for (int j = s->len - 1; j >= 0; j--) {
            int px = (int)((s->x[j] - camx) * sc + hw), py = (int)((s->y[j] - camy) * sc + hh);
            u32 col = skins[s->skin][(j / 3) % 2];
            circle(px, py, r, col);
        }
        int hx = (int)((s->x[0] - camx) * sc + hw), hy = (int)((s->y[0] - camy) * sc + hh);
        for (int e = -1; e <= 1; e += 2) {
            int ex = hx + (int)(cosf(s->ang + e * 0.7f) * r * 0.6f);
            int ey = hy + (int)(sinf(s->ang + e * 0.7f) * r * 0.6f);
            circle(ex, ey, r * 35 / 100 + 1, 0x00FFFFFF);
            circle(ex + (int)(cosf(s->ang) * r * 0.15f), ey + (int)(sinf(s->ang) * r * 0.15f), r * 18 / 100 + 1, 0x00000000);
        }
        // Escudo - circulo azul em volta do player
        if (i == 0 && shieldOn) circle_outline(hx, hy, r + 12, 0x0000AAFF);
        // Ima - circulo rosa
        if (i == 0 && magnetOn) circle_outline(hx, hy, r + 22, 0x00FF00FF);
    }

    /* HUD - MEU JOGO */
    char buf[64];
    int ui = (int)(4 * sc + 0.5f);
    if (ui < 3) ui = 3;
    draw_text("SCORE", 24, 20, ui, 0x00FFFFFF);
    sprintf(buf, "%d", sn[0].score);
    draw_text(buf, 24, 20 + 7 * ui, ui + 2, 0x00FFD23F);

    sprintf(buf, "KILLS %d", kills);
    draw_text(buf, 24, 20 + 16 * ui, ui, 0x00FF4D4D);

    sprintf(buf, "FOOD %d", foodsEaten);
    draw_text(buf, 24, 20 + 22 * ui, ui, 0x0066FF66);

    if (shieldOn) {
        sprintf(buf, "SHIELD %d", shieldTime / 60);
        draw_text(buf, 24, 20 + 28 * ui, ui, 0x0000AAFF);
    }
    if (magnetOn) {
        sprintf(buf, "MAGNET %d", magnetTime / 60);
        draw_text(buf, 24, 20 + 34 * ui, ui, 0x00FF00FF);
    }

    draw_text("X TURBO  O SHIELD  TRI MAGNET", 24, (int)H - 8 * ui, ui, 0x00809BBF);

    if (!sn[0].alive) {
        int bw = text_width("GAME OVER", ui * 2) + 80, bh = 16 * ui + 40;
        fill_rect((int)W /