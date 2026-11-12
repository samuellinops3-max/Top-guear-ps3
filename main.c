// MOTO LOOP PS3 - Estilo das fotos: moto no deserto fazendo looping
// Controles: X = acelerar, Quadrado = frear, Analogico cima/baixo = inclinar no ar
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

#define CB_SIZE (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)
#define PI 3.14159265f
#define PATH_N 4000
#define CAM_W 2000

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{(void)param;(void)usrdata; if(status==SYSUTIL_EXIT_GAME) running=0;}

static void init_screen(void){
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st; videoGetState(VIDEO_PRIMARY,0,&st);
    u32 resid = st.displayMode.resolution;
    if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0)) resid = VIDEO_RESOLUTION_720;
    videoResolution res; videoGetResolution(resid,&res); W=res.width; H=res.height;
    videoConfiguration vc; memset(&vc,0,sizeof(vc)); vc.resolution=resid; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=W*4;
    videoConfigure(VIDEO_PRIMARY,&vc,NULL,0); videoGetState(VIDEO_PRIMARY,0,&st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++){fb[i]=(u32*)rsxMemalign(64,W*H*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],W*4,W,H);}
    back=(u32*)memalign(64,W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus();
}
static void wait_flip(void){while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus();}
static void present(void){if(!first_flip) wait_flip(); else gcmResetFlipStatus(); memcpy(fb[cur],back,W*H*4); gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1; first_flip=0;}
static void fill_rect(int x,int y,int w,int h,u32 c){int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h; if(x1>(int)W)x1=W; if(y1>(int)H)y1=H; for(int yy=y0;yy<y1;yy++){u32*p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c;}}
static void line_draw(int x0,int y0,int x1,int y1,u32 c){
    int dx=abs(x1-x0), sx=x0<x1?1:-1; int dy=-abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy;
    while(1){ if(x0>=0&&x0<(int)W&&y0>=0&&y0<(int)H) back[y0*W+x0]=c; if(x0==x1&&y0==y1) break; int e2=2*err; if(e2>=dy){err+=dy; x0+=sx;} if(e2<=dx){err+=dx; y0+=sy;} }
}
static void circle(int cx,int cy,int r,u32 c){if(r<1)r=1; if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H) return; for(int dy=-r;dy<=r;dy++){int y=cy+dy; if(y<0||y>=(int)H) continue; int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw,x1=cx+hw; if(x0<0)x0=0; if(x1>=(int)W)x1=W-1; u32*p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c;}}
static void thick_line(int x0,int y0,int x1,int y1,int th,u32 c){ for(int i=-th/2;i<=th/2;i++) line_draw(x0+i,y0,x1+i,y1,c); line_draw(x0,y0+i,x1,y1+i,c); }

typedef struct{char c; const char *p;} Glyph;
static const Glyph glyphs[]={
 {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},{'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},{'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},{'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},{'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
 {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},{'C',"###" "#.." "#.." "#.." "###"},{'E',"###" "#.." "###" "#.." "###"},{'G',"###" "#.." "#.#" "#.#" "###"},{'M',"#.#" "###" "###" "#.#" "#.#"},{'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},{'R',"##." "#.#" "##." "#.#" "#.#"},{'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},{'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},{'X',"#.#" "#.#" ".#." "#.#" "#.#"},{'I',"###" ".#." ".#." ".#." "###"},{'L',"#.." "#.." "#.." "#.." "###"},{'N',"#.#" "###" "###" "#.#" "#.#"},{'D',"##." "#.#" "#.#" "#.#" "##."},{'F',"###" "#.." "###" "#.." "#.."},{'H',"#.#" "#.#" "###" "#.#" "#.#"},{'K',"#.#" "#.#" "##." "#.#" "#.#"},{'W',"#.#" "#.#" "###" "###" "#.#"},{':',"... " ".#." "... " ".#." "..."},{'/',"..#" "..#" ".#." ".#." "#.."},
};
static const char *find_glyph(char c){for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL;}
static int text_width(const char *s,int sc){return (int)strlen(s)*4*sc-sc;}
static void draw_text(const char *s,int x,int y,int sc,u32 c){for(;*s;s++){const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc;}}

// --- PISTA COM LOOPING ---
static float path_x[PATH_N], path_y[PATH_N], path_ang[PATH_N];
static int path_len = 0;
static float bike_s = 0, bike_speed = 0, bike_ang = 0;
static float bike_x = 0, bike_y = 0, bike_vy = 0;
static int on_ground = 1, dead = 0, dead_t = 0, level = 0, score = 0, loops_done = 0;
static float camx = 0, camy = 0;
static float pad_ax = 0, pad_ay = 0;
static int pad_accel = 0, pad_brake = 0, pad_x = 0, x_prev = 0;
static int state = 0; // 0 titulo, 1 jogando, 2 fim

static void add_point(float x, float y){
    if(path_len >= PATH_N) return;
    path_x[path_len] = x; path_y[path_len] = y; path_len++;
}

static void build_track(int lvl){
    path_len = 0;
    float x = 0, y = 500, ang = 0;
    // Saida plana
    for(int i=0;i<200;i++){ add_point(x,y); x+=8; }
    // 3 loopings como nas fotos
    for(int l=0;l<3;l++){
        // Rampa subindo
        for(int i=0;i<60;i++){ add_point(x,y); x+=8; y-=1.2f; }
        // Looping completo - circulo
        float cx = x + 130, cy = y - 130;
        for(int a=0;a<=360;a+=6){
            float rad = a * PI / 180.0f;
            // Comeca embaixo e vai girando
            float px = cx + cosf(rad + PI/2) * 130;
            float py = cy + sinf(rad + PI/2) * 130;
            add_point(px, py);
        }
        x = cx + 130; y = cy + 130;
        // Descida
        for(int i=0;i<120;i++){ add_point(x,y); x+=8; y += sinf(i*0.05f)*1.5f; }
        // Morro
        for(int i=0;i<80;i++){ add_point(x,y); x+=8; y -= sinf(i*0.04f)*2.0f; }
    }
    // Chegada
    for(int i=0;i<200;i++){ add_point(x,y); x+=8; }
    // Calcula angulo de cada ponto
    for(int i=0;i<path_len-1;i++){
        path_ang[i] = atan2f(path_y[i+1]-path_y[i], path_x[i+1]-path_x[i]);
    }
    path_ang[path_len-1] = 0;
    (void)lvl; (void)ang;
}

static void reset_bike(void){
    bike_s = 10; bike_speed = 0; bike_ang = 0; dead = 0; dead_t = 0;
    bike_x = path_x[10]; bike_y = path_y[10]; bike_vy = 0; on_ground = 1;
}

static void init_game(void){
    build_track(level);
    reset_bike();
    score = 0; loops_done = 0; state = 1;
}

static void read_pad(void){
    padInfo info; padData d;
    pad_ax = pad_ay = 0; pad_accel = pad_brake = pad_x = 0;
    ioPadGetInfo(&info);
    if(info.status[0]){
        ioPadGetData(0,&d);
        float ax = ((int)d.ANA_L_H-128)/128.0f;
        float ay = ((int)d.ANA_L_V-128)/128.0f;
        if(fabsf(ax)<0.2f) ax=0; if(fabsf(ay)<0.2f) ay=0;
        if(d.BTN_LEFT) ax=-1; if(d.BTN_RIGHT) ax=1;
        if(d.BTN_UP) ay=-1; if(d.BTN_DOWN) ay=1;
        pad_ax=ax; pad_ay=ay;
        pad_accel = d.BTN_CROSS || d.BTN_UP;
        pad_brake = d.BTN_SQUARE || d.BTN_DOWN;
        pad_x = d.BTN_CROSS;
    }
}

static void update_game(void){
    if(state==0){ if(pad_x && !x_prev) init_game(); x_prev=pad_x; return; }
    if(state==2){ if(pad_x && !x_prev) state=0; x_prev=pad_x; return; }

    if(dead){
        dead_t++;
        if(dead_t>90) reset_bike();
        x_prev=pad_x; return;
    }

    // Fisica
    if(on_ground){
        if(pad_accel) bike_speed += 0.35f;
        else if(pad_brake) bike_speed -= 0.4f;
        else bike_speed -= 0.05f;
        if(bike_speed>14) bike_speed=14;
        if(bike_speed<-4) bike_speed=-4;
        // Gravidade na rampa - desce acelera, sobe freia
        float slope = path_ang[(int)bike_s];
        bike_speed -= sinf(slope)*0.18f;

        bike_s += bike_speed;
        if(bike_s<0) bike_s=0;
        if(bike_s>=path_len-10){
            // Chegou no fim!
            score += 1000; loops_done++;
            if(loops_done>=2){ state=2; }
            else { level = (level+1)%2; build_track(level); reset_bike(); }
            x_prev=pad_x; return;
        }
        bike_x = path_x[(int)bike_s];
        bike_y = path_y[(int)bike_s];
        bike_ang = path_ang[(int)bike_s];

        // Se estiver muito rapido numa descida, voa
        if(bike_speed>11 && slope < -0.3f){
            on_ground = 0; bike_vy = -6;
        }
        // Se cair do looping sem velocidade, morre
        // Detecta se esta de cabeca pra baixo e lento no looping
        if(bike_s>250 && bike_s<450){
            if(bike_speed<4 && bike_ang>1.0f && bike_ang<2.5f){
                dead=1; dead_t=0;
            }
        }
    } else {
        // No ar - controla inclinacao com analogico
        bike_ang += pad_ay * 0.06f;
        bike_x += bike_speed * 0.9f;
        bike_vy += 0.45f; // gravidade
        bike_y += bike_vy;
        // Procura ponto mais proximo da pista pra pousar
        float best=1e9f; int bi=-1;
        for(int i=(int)bike_s-20;i<(int)bike_s+120 && i<path_len;i++){
            if(i<0) continue;
            float dx=path_x[i]-bike_x, dy=path_y[i]-bike_y;
            float d=dx*dx+dy*dy;
            if(d<best){best=d; bi=i;}
        }
        if(bi>=0 && best<900){
            float target_ang = path_ang[bi];
            float diff = fabsf(target_ang - bike_ang);
            if(diff<0.6f){
                bike_s=bi; on_ground=1; bike_vy=0;
                // Bonus se pousar certinho
                score+=10;
            } else if(best<400){
                // Bateu torto = caiu
                dead=1; dead_t=0;
            }
        }
        if(bike_y>900){ dead=1; dead_t=0; }
    }

    // Camera segue a moto
    camx = bike_x - 350;
    camy = bike_y - 380;
    if(camx<0) camx=0;
    if(camy<0) camy=0;
    x_prev=pad_x;
}

static void draw_moto(int cx, int cy, float ang, int is_player){
    int r = 16;
    // Rodas
    int wx1 = cx + (int)(cosf(ang)* -18 - sinf(ang)* -6);
    int wy1 = cy + (int)(sinf(ang)* -18 + cosf(ang)* -6);
    int wx2 = cx + (int)(cosf(ang)* 18 - sinf(ang)* -6);
    int wy2 = cy + (int)(sinf(ang)* 18 + cosf(ang)* -6);
    circle(wx1, wy1, r, 0x00000000);
    circle(wx1, wy1, r-5, 0x00FFFFFF);
    circle(wx1, wy1, 4, 0x00000000);
    circle(wx2, wy2, r, 0x00000000);
    circle(wx2, wy2, r-5, 0x00FFFFFF);
    circle(wx2, wy2, 4, 0x00000000);
    // Corpo da moto
    line_draw(wx1, wy1, wx2, wy2, is_player ? 0x00FF4D4D : 0x00222222);
    line_draw(wx1, wy1, cx, cy-18, is_player ? 0x00FF4D4D : 0x00222222);
    line_draw(cx, cy-18, wx2, wy2, is_player ? 0x00FF4D4D : 0x00222222);
    // Piloto - capacete amarelo como na foto
    circle(cx, cy-28, 10, 0x00FFD23F);
    circle(cx+3, cy-28, 3, 0x00000000);
    fill_rect(cx-6, cy-22, 12, 18, 0x002E7DFF);
    // Rastro
    if(bike_speed>8 && on_ground){
        for(int i=0;i<5;i++) circle(cx - (int)(bike_speed*i*3), cy+10+i*2, 4, 0x00FFFFFF);
    }
}

static void render(void){
    float base = (float)W/1280.0f;
    // CEU - igual as fotos: laranja no level 0, azul no level 1
    for(u32 yy=0;yy<H;yy++){
        u32 c;
        if(level==0){
            // Por do sol laranja
            int t = yy*255/H;
            c = (255<<16) | ((140 + t/3)<<8) | (20 + t/2);
        } else {
            // Ceu azul
            int t = yy*100/H;
            c = (120 + t)<<16 | (180 + t)<<8 | 255;
        }
        u32 *p = back + yy*W;
        for(u32 xx=0;xx<W;xx++) p[xx]=c;
    }
    // Sol
    circle((int)(W*0.80f), 90, 42, 0x00FFD23F);
    // Montanhas / mesas do deserto - igual as fotos
    for(int i=0;i<8;i++){
        int mx = (int)((i*320.0f - camx*0.3f)) % (int)W;
        if(mx<0) mx+=W;
        int mh = 120 + (i%3)*60;
        int my = (int)(H*0.75f) - mh + (int)(camy*0.1f);
        // Mesa
        fill_rect(mx, my, 80, mh, level==0 ? 0x00D98A5A : 0x007AB8E8);
        fill_rect(mx-10, my, 100, 20, level==0 ? 0x00C47A4A : 0x006AA8D8);
        fill_rect(mx+20, my+mh, 30, 80, level==0 ? 0x00B06A3A : 0x005A98C8);
    }

    if(state==0){
        // TITULO
        draw_text("MOTO LOOP", (int)W/2 - text_width("MOTO LOOP",10)/2, (int)H/2-100, 10, 0x00FFFFFF);
        draw_text("MOTO LOOP", (int)W/2 - text_width("MOTO LOOP",10)/2+3, (int)H/2-97, 10, 0x00FF4D4D);
        draw_text("DESERTO - 3 LOOPINGS", (int)W/2 - text_width("DESERTO - 3 LOOPINGS",4)/2, (int)H/2+10, 4, 0x00FFD23F);
        draw_text("PRESS X TO START", (int)W/2 - text_width("PRESS X TO START",3)/2, (int)H/2+60, 3, 0x00FFFFFF);
        draw_text("X: ACELERAR  QUADRADO: FREAR", (int)W/2 - text_width("X: ACELERAR  QUADRADO: FREAR",3)/2, (int)H-60, 3, 0x00FFFFFF);
        return;
    }

    // Desenha pista - linha vermelha grossa igual as fotos
    float scale = (float)W / 1280.0f;
    // Converte mundo -> tela
    for(int i=0;i<path_len-1;i++){
        float wx1 = path_x[i] - camx;
        float wy1 = path_y[i] - camy;
        float wx2 = path_x[i+1] - camx;
        float wy2 = path_y[i+1] - camy;
        if(wx1<-50 || wx1>W+50) continue;
        int sx1 = (int)(wx1*scale);
        int sy1 = (int)(wy1*scale);
        int sx2 = (int)(wx2*scale);
        int sy2 = (int)(wy2*scale);
        thick_line(sx1, sy1, sx2, sy2, 5, 0x00B00000);
        thick_line(sx1, sy1+2, sx2, sy2+2, 3, 0x00FF2A2A);
    }
    // Bandeira de chegada
    {
        int fx = (int)((path_x[path_len-20]-camx)*scale);
        int fy = (int)((path_y[path_len-20]-camy)*scale);
        fill_rect(fx, fy-80, 6, 80, 0x00FFFFFF);
        fill_rect(fx+6, fy-80, 40, 26, 0x00FFFFFF);
        for(int yy=0;yy<26;yy+=8) for(int xx=0;xx<40;xx+=8) fill_rect(fx+6+xx+(yy/8%2)*8, fy-80+yy, 8, 8, 0x00000000);
    }

    // Moto
    int mx = (int)((bike_x - camx)*scale);
    int my = (int)((bike_y - camy)*scale);
    if(!dead) draw_moto(mx, my, bike_ang, 1);
    else {
        draw_text("CAIU!", mx-30, my-60, 4, 0x00FF0000);
    }

    // HUD
    char buf[64];
    draw_text("MOTO LOOP", 24, 16, 4, 0x00FFFFFF);
    sprintf(buf, "SCORE: %d", score);
    draw_text(buf, 24, 48, 3, 0x00FFD23F);
    sprintf(buf, "VELOCIDADE: %d", (int)(bike_speed*10));
    draw_text(buf, 24, 70, 3, 0x00FFFFFF);
    sprintf(buf, "LOOPINGS: %d / 3", loops_done);
    draw_text(buf, (int)W-300, 20, 3, 0x00FFFFFF);
    draw_text("X: ACELERAR  QUADRADO: FREAR  ANALOGICO: INCLINAR", 24, (int)H-30, 3, 0x00FFFFFF);

    if(state==2){
        fill_rect((int)W/2-280, (int)H/2-80, 560, 160, 0x00000000);
        draw_text("VOCE VENCEU!", (int)W/2 - text_width("VOCE VENCEU!",6)/2, (int)H/2-40, 6, 0x00FFD23F);
        draw_text("APERTE X PRA JOGAR DE NOVO", (int)W/2 - text_width("APERTE X PRA JOGAR DE NOVO",3)/2, (int)H/2+20, 3, 0x00FFFFFF);
    }
}

int main(int argc,char **argv){
    (void)argc;(void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0,sysutil_cb,NULL);
    srand(20261112);
    state=0; level=0;
    build_track(0);
    reset_bike();
    while(running){
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
        present();
    }
    return 0;
}