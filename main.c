// Moto Loop - mesmo estilo do Cobra Arena, pra compilar sem erro
// X = acelerar, Quadrado = frear
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

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata){
    (void)param; (void)usrdata;
    if(status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void){
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;
    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width; H = res.height;
    videoConfiguration vc;
    memset(&vc,0,sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W*4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++){
        fb[i]=(u32*)rsxMemalign(64, W*H*4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W*4, W, H);
    }
    back=(u32*)memalign(64, W*H*4);
    memset(back,0,W*H*4);
    gcmResetFlipStatus();
}

static void wait_flip(void){
    while(gcmGetFlipStatus()!=0) usleep(200);
    gcmResetFlipStatus();
}
static void present(void){
    if(!first_flip) wait_flip(); else gcmResetFlipStatus();
    memcpy(fb[cur], back, W*H*4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1; first_flip=0;
}

static void fill_rect(int x,int y,int w,int h,u32 c){
    int x0=x<0?0:x, y0=y<0?0:y;
    int x1=x+w, y1=y+h;
    if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
    for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
    if(r<1) r=1;
    for(int dy=-r;dy<=r;dy++){
        int y=cy+dy; if(y<0||y>=(int)H) continue;
        int hw=(int)sqrtf((float)(r*r-dy*dy));
        int x0=cx-hw, x1=cx+hw;
        if(x0<0) x0=0; if(x1>=(int)W) x1=W-1;
        u32 *p=back+y*W;
        for(int x=x0;x<=x1;x++) p[x]=c;
    }
}
static void circle_outline(int cx,int cy,int r,u32 c){
    for(int a=0;a<360;a++){ float rad=a*PI/180; int x=cx+cosf(rad)*r; int y=cy+sinf(rad)*r; if(x>=0&&x<(int)W&&y>=0&&y<(int)H) back[y*W+x]=c; }
}

// JOGO
static float px=150, py=0, vx=0, vy=0;
static int on_ground=1, loops=0, score=0;
static float ground_y;

static void render(void){
    ground_y = H*0.83f;
    // ceu
    for(u32 i=0;i<W*H;i++) back[i]=0x0087CEEB;
    fill_rect(0, H*0.45f, W, H*0.38f, 0x00FFA500);
    // sol
    circle(W-120, 90, 45, 0x00FFFF00);
    // montanhas
    for(int x=0;x<(int)W;x++){
        int h = H*0.55f + sinf(x*0.01f)*30;
        for(int y=h;y<(int)ground_y;y++) back[y*W+x]=0x00C68E4A;
    }
    // chao
    fill_rect(0, ground_y, W, H-ground_y, 0x00DEB887);
    // pista vermelha
    fill_rect(0, ground_y, W, 10, 0x00FF0000);
    // looping vermelho
    int lx = W*0.64f, ly = ground_y-80;
    circle_outline(lx, ly, 70, 0x00FF0000);
    circle_outline(lx, ly, 69, 0x00FF0000);
    circle_outline(lx, ly, 68, 0x00FF0000);
    circle_outline(lx, ly, 60, 0x00FFA500);
    // moto vermelha
    int mx=(int)px, my=(int)py;
    circle(mx-22, my, 16, 0x00000000);
    circle(mx-22, my, 8, 0x00FFFFFF);
    circle(mx+22, my, 16, 0x00000000);
    circle(mx+22, my, 8, 0x00FFFFFF);
    // corpo vermelho
    for(int i=-22;i<=22;i++) back[my*W+mx+i]=0x00FF0000;
    circle(mx, my-18, 10, 0x00FF0000);
    // capacete vermelho
    circle(mx, my-30, 10, 0x00FF0000);
    // placar - quadrado amarelo simples pra mostrar score
    char buf[32]; sprintf(buf, "%d", score);
    fill_rect(20, 20, 120, 30, 0x00000000);
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    ground_y = H*0.83f;
    py = ground_y;
    while(running){
        sysUtilCheckCallback();
        padInfo info; padData d;
        ioPadGetInfo(&info);
        if(info.status[0]){
            ioPadGetData(0,&d);
            if(d.BTN_CROSS){ vx+=0.35f; if(vx>12) vx=12; }
            else vx*=0.98f;
            if(d.BTN_SQUARE) vx*=0.9f;
        }
        px+=vx;
        if(!on_ground){
            vy+=0.6f; py+=vy;
            if(py>=ground_y){ py=ground_y; vy=0; on_ground=1; }
        }
        // looping
        if(px > W*0.64f-40 && px < W*0.64f+40 && on_ground){
            if(vx>7){ loops++; score+=100; py=ground_y-120; on_ground=0; vy=-8; }
        }
        if(px>W+50) px=-50;
        render();
        present();
    }
    return 0;
}