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
if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0)) resid = VIDEO_RESOLUTION_720;
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
static void draw_track_point(int x,int y,u32 c){
for(int dy=-3;dy<=3;dy++) for(int dx=-3;dx<=3;dx++){ int xx=x+dx, yy=y+dy; if(xx>=0&&xx<(int)W&&yy>=0&&yy<(int)H) back[yy*W+xx]=c; }
}
static void draw_moto(int mx,int my,float ang,int is_red){
int cbody = is_red ? 0x00FF3B30 : 0x00222222;
int cacc = is_red ? 0x00FFD700 : 0x00FFD700;
float ca=cosf(ang), sa=sinf(ang);
int wx1 = mx + (int)(-30*ca - 0*sa);
int wy1 = my + (int)(-30*sa + 0*ca);
int wx2 = mx + (int)(30*ca);
int wy2 = my + (int)(30*sa);
circle(wx1, wy1, 18, 0x00000000);
circle(wx1, wy1, 8, 0x00DDDDDD);
circle(wx2, wy2, 18, 0x00000000);
circle(wx2, wy2, 8, 0x00DDDDDD);
for(int i=-30;i<=30;i++){ int x=mx+(int)(i*ca); int y=my+(int)(i*sa); if(x>=0&&x<(int)W&&y>=0&&y<(int)H) back[y*W+x]=cbody; }
circle(mx, my-22, 11, 0x00FFD700);
circle(mx-5, my-32, 8, 0x00333333);
fill_rect(mx-8, my-18, 18, 20, 0x003366FF);
}
static float px=200, py=0, vx=0, loop_ang=0;
static int score=0, in_loop=0, cam=0;
static float ground_y;
static void render(void){
ground_y = H*0.75f;
for(int y=0;y<(int)H;y++){
u32 c = 0x00A8D8FF - (y*0x30);
for(u32 x=0;x<W;x++) back[y*W+x]=c;
}
for(int x=0;x<(int)W;x++){
int h = H*0.55f + sinf(x*0.008f)*40 + sinf(x*0.02f)*20;
for(int y=h;y<(int)H;y++) back[y*W+x]=0x007AB8E8;
}
for(int x=0;x<(int)W;x++){
int h = H*0.68f + sinf(x*0.015f+2)*25;
for(int y=h;y<(int)H;y++) back[y*W+x]=0x005AA0D0;
}
int lx1 = W*0.28f - cam*0.2f;
int lx2 = W*0.78f - cam*0.2f;
int ly = H*0.45f;
for(int a=0;a<360;a++){
float r=a*PI/180;
int x=lx1+(int)(cosf(r)*130);
int y=ly+(int)(sinf(r)*130);
if(a>20 && a<340) draw_track_point(x,y,0x00C00000);
}
for(int a=0;a<360;a++){
float r=a*PI/180;
int x=lx2+(int)(cosf(r)*110);
int y=ly+(int)(sinf(r)*110);
if(a>300 || a<100) draw_track_point(x,y,0x00C00000);
}
for(int x=0;x<(int)W;x++){
int y = ground_y + sinf(x*0.01f)*10;
draw_track_point(x, y, 0x00C00000);
}
int mx = (int)px - cam;
int my = (int)py;
if(in_loop){
mx = lx1 + (int)(cosf(loop_ang)*130);
my = ly + (int)(sinf(loop_ang)*130);
draw_moto(mx, my, loop_ang+PI/2, 1);
} else {
draw_moto(mx, my, 0, 1);
draw_moto(mx+W*0.45f, my-60, -0.3f, 0);
}
}
int main(int argc, char **argv){
(void)argc; (void)argv;
init_screen();
ioPadInit(7);
sysUtilRegisterCallback(0, sysutil_cb, NULL);
ground_y = H*0.75f;
py = ground_y;
while(running){
sysUtilCheckCallback();
padInfo info; padData d;
ioPadGetInfo(&info);
if(info.status[0]){
ioPadGetData(0,&d);
if(!in_loop){
if(d.BTN_CROSS){ vx+=0.5f; if(vx>15) vx=15; } else vx*=0.985f;
if(d.BTN_SQUARE) vx*=0.88f;
}
}
if(in_loop){
loop_ang += 0.11f;
if(loop_ang >= PI*1.5f){ in_loop=0; px = W*0.28f+160; py=ground_y; vx=12; }
} else {
px+=vx; cam+= (int)vx*0.5f;
if(px > W*0.28f-20 && px < W*0.28f+20 && vx>8){ in_loop=1; loop_ang=PI*0.5f; score+=100; }
}
if(px>W+80){ px=-60; cam=0; }
render();
present();
}
return 0;
}