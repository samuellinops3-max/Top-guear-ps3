// TOP GEAR PS3 - Las Vegas - 3 voltas - Gasolina, Pit, Nitro
// Controles: Analogico/Setas = dirigir, X = acelerar, Quadrado = freio, R1 = nitro, L1 = pit
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
#define NSEG 240
#define NCAR 15
typedef struct { float curve; int color; } Seg;
typedef struct { float z; float off; float speed; u32 col; } Car;
static const u32 palette[6] = {0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F, 0x00C77DFF, 0x00FF8FAB};
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;
static void sysutil_cb(u64 status, u64 param, void *usrdata){(void)param;(void)usrdata;if(status==SYSUTIL_EXIT_GAME) running=0;}
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
static void fill_trap(int y0,int y1,int x0a,int x1a,int x0b,int x1b,u32 c){if(y1<=y0)return; for(int y=y0;y<y1;y++){float t=(float)(y-y0)/(float)(y1-y0); int xa=(int)(x0a+(x0b-x0a)*t); int xb=(int)(x1a+(x1b-x1a)*t); if(xa>xb){int tmp=xa;xa=xb;xb=tmp;} if(xa<0)xa=0; if(xb>=(int)W)xb=W-1; u32*p=back+y*W; for(int x=xa;x<=xb;x++) p[x]=c;}}
static void circle(int cx,int cy,int r,u32 c){if(r<1)r=1; if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H)return; for(int dy=-r;dy<=r;dy++){int y=cy+dy; if(y<0||y>=(int)H)continue; int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw,x1=cx+hw; if(x0<0)x0=0; if(x1>=(int)W)x1=W-1; u32*p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c;}}
typedef struct{char c; const char *p;} Glyph;
static const Glyph glyphs[]={
 {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},{'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},{'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},{'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},{'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
 {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},{'C',"###" "#.." "#.." "#.." "###"},{'E',"###" "#.." "###" "#.." "###"},{'G',"###" "#.." "#.#" "#.#" "###"},{'M',"#.#" "###" "###" "#.#" "#.#"},{'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},{'R',"##." "#.#" "##." "#.#" "#.#"},{'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},{'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},{'X',"#.#" "#.#" ".#." "#.#" "#.#"},{'I',"###" ".#." ".#." ".#." "###"},{'L',"#.." "#.." "#.." "#.." "###"},{'N',"#.#" "###" "###" "#.#" "#.#"},{'D',"##." "#.#" "#.#" "#.#" "##."},{'F',"###" "#.." "###" "#.." "#.."},{'H',"#.#" "#.#" "###" "#.#" "#.#"},{'K',"#.#" "#.#" "##." "#.#" "#.#"},{'W',"#.#" "#.#" "###" "###" "#.#"},{':',"... " ".#." "... " ".#." "..."},{'/',"..#" "..#" ".#." ".#." "#.."},{'-',"... " "..." "###" "..." "..."},{'!',".#." ".#." ".#." "..." ".#."},
};
static const char *find_glyph(char c){for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL;}
static int text_width(const char *s,int sc){return (int)strlen(s)*4*sc-sc;}
static void draw_text(const char *s,int x,int y,int sc,u32 c){for(;*s;s++){const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc;}}
static Seg track[NSEG];
static Car cars[NCAR];
static float pos=0,speed=0,player_x=0,fuel=100,nitro=100;
static int lap=1,position=16,time_left=99,frame=0,state=0,count_t=0,pit=0;
static float pad_ax=0; static int pad_accel=0,pad_brake=0,pad_nitro=0,pad_x=0,pad_l1=0,x_prev=0;
static float frand(void){return (float)rand()/(float)RAND_MAX;}
static void build_track_las_vegas(void){for(int i=0;i<NSEG;i++){track[i].color=(i/4)%2; if(i<50)track[i].curve=0; else if(i<80)track[i].curve=2.2f; else if(i<110)track[i].curve=0; else if(i<140)track[i].curve=-3.0f; else if(i<170)track[i].curve=0; else if(i<200)track[i].curve=3.8f; else if(i<220)track[i].curve=-2.0f; else track[i].curve=0;}}
static void init_game(void){build_track_las_vegas(); pos=0;speed=0;player_x=0;lap=1;position=16;time_left=99;frame=0;fuel=100;nitro=100;pit=0;state=1;count_t=0; for(int i=0;i<NCAR;i++){cars[i].z=500+i*1000+frand()*300; cars[i].off=(frand()-0.5f)*1.4f; cars[i].speed=70+frand()*30; cars[i].col=palette[i%6];}}
static void read_pad(void){padInfo info; padData d; pad_ax=0; pad_accel=pad_brake=pad_nitro=pad_x=pad_l1=0; ioPadGetInfo(&info); if(info.status[0]){ioPadGetData(0,&d); float ax=((int)d.ANA_L_H-128)/128.0f; if(fabsf(ax)<0.2f)ax=0; if(d.BTN_LEFT)ax=-1; if(d.BTN_RIGHT)ax=1; pad_ax=ax; pad_accel=d.BTN_CROSS||d.BTN_UP; pad_brake=d.BTN_SQUARE||d.BTN_DOWN; pad_nitro=d.BTN_R1||d.BTN_R2; pad_x=d.BTN_CROSS; pad_l1=d.BTN_L1;}}
static void update_game(void){
 frame++; if(state==0){if(pad_x&&!x_prev)init_game(); x_prev=pad_x; return;}
 if(state==1){count_t++; if(count_t>200) state=2; x_prev=pad_x; return;}
 if(state==3){if(pad_x&&!x_prev) state=0; x_prev=pad_x; return;}
 if(frame%60==0) time_left--; if(time_left<=0||fuel<=0){state=3; return;}
 if(pad_l1&&speed<5){pit=1; fuel+=1.5f; if(fuel>100)fuel=100; speed=0;} else pit=0;
 float max_speed=175; if(pad_nitro&&nitro>0){max_speed=235; nitro-=0.7f;} else if(nitro<100) nitro+=0.12f;
 if(pad_accel&&!pit) speed+=1.3f; else if(pad_brake) speed-=2.8f; else speed-=0.45f; if(speed>max_speed)speed=max_speed; if(speed<0)speed=0;
 if(speed>10) fuel-=0.012f+(pad_nitro?0.02f:0);
 int seg_idx=(int)(pos/200)%NSEG; float curve=track[seg_idx].curve; player_x+=pad_ax*(0.02f+speed*0.00012f); player_x-=curve*speed*0.00009f; if(player_x<-1.7f){player_x=-1.7f; speed*=0.95f;} if(player_x>1.7f){player_x=1.7f; speed*=0.95f;}
 pos+=speed; float track_len=NSEG*200.0f; if(pos>=track_len){pos-=track_len; lap++; time_left+=35; if(time_left>99)time_left=99; if(lap>3){state=3; return;}}
 int pos_count=1; for(int i=0;i<NCAR;i++){cars[i].z+=cars[i].speed; if(cars[i].z>=track_len)cars[i].z-=track_len; float dz=cars[i].z-pos; if(dz<0)dz+=track_len; if(dz>track_len/2)dz-=track_len; if(fabsf(dz)<60&&fabsf(cars[i].off-player_x)<0.35f){speed*=0.45f; if(player_x<cars[i].off)player_x-=0.25f; else player_x+=0.25f;} float rel=cars[i].z-pos; if(rel<0)rel+=track_len; if(rel>0&&rel<track_len/2)pos_count++;} position=pos_count; x_prev=pad_x;
}
static void draw_car(int cx,int cy,int w,int h,u32 col){fill_rect(cx-w/2,cy-h/2,w,h,col); fill_rect(cx-w/2-w/8,cy,w/4,h/3,0x00000000); fill_rect(cx+w/2-w/8,cy,w/4,h/3,0x00000000); fill_rect(cx-w/3,cy-h/2+h/6,w*2/3,h/3,0x00111111); fill_rect(cx-w/4,cy+h/4,w/5,h/6,0x00FFFF00); fill_rect(cx+w/20,cy+h/4,w/5,h/6,0x00FFFF00); fill_rect(cx-w/6,cy-h/2-4,w/3,6,col); fill_rect(cx-w/12,cy-h/2,w/6,h,0x00FFFFFF);}
static void render(void){
 float base=(float)W/1280.0f;
 if(state==0){for(u32 yy=0;yy<H;yy++){u32 c=0x00000080+yy*0x10/H; u32 *p=back+yy*W; for(u32 xx=0;xx<W;xx++) p[xx]=c;} for(int i=0;i<20;i++){fill_rect(i*(int)W/20,(int)H/2-90,(int)W/20,20,(i%2)?0x00FFFFFF:0x00000000); fill_rect(i*(int)W/20,(int)H/2+70,(int)W/20,20,(i%2)?0x00000000:0x00FFFFFF);} draw_text("TOP GEAR",(int)W/2-text_width("TOP GEAR",12)/2,(int)H/2-70,12,0x00FF0000); draw_text("TOP GEAR",(int)W/2-text_width("TOP GEAR",12)/2+4,(int)H/2-66,12,0x00FFFFFF); draw_text("LAS VEGAS",(int)W/2-text_width("LAS VEGAS",5)/2,(int)H/2+20,5,0x00FFD23F); draw_text("PRESS X TO START",(int)W/2-text_width("PRESS X TO START",3)/2,(int)H/2+60,3,0x00FFFFFF); draw_text("CHAMPIONSHIP - 3 LAPS",(int)W/2-text_width("CHAMPIONSHIP - 3 LAPS",3)/2,(int)H-60,3,0x0000E5C0); return;}
 for(u32 yy=0;yy<H;yy++){u32 c; if(yy<H*0.42f) c=0x00FF8C00-(yy*0x20/H); else c=0x00FFD23F; u32 *p=back+yy*W; for(u32 xx=0;xx<W;xx++) p[xx]=c;}
 circle((int)(W*0.78f),100,48,0x00FFD23F); circle((int)(W*0.78f),100,36,0x00FFF176);
 for(int i=0;i<12;i++){int mx=(int)((i*280.0f-pos*0.05f))%(int)W; if(mx<0)mx+=W; fill_rect(mx,(int)H/2-80-(i%4)*25,90,200,0x00222222); for(int wy=0;wy<5;wy++) for(int wx=0;wx<3;wx++) fill_rect(mx+10+wx*22,(int)H/2-60-(i%4)*25+wy*18,10,10,0x00FFFF00);}
 float cam_h=1000,x=0,dx=0; int start=(int)(pos/200); float horizon=H*0.42f;
 for(int n=0;n<45;n++){int idx=(start+n)%NSEG; float z1=n*200.0f-fmodf(pos,200); float z2=(n+1)*200.0f-fmodf(pos,200); if(z1<1)z1=1; float scale1=cam_h/z1; float scale2=cam_h/z2; int y1=(int)(horizon+(H-horizon)*(1-scale1/5.0f)); int y2=(int)(horizon+(H-horizon)*(1-scale2/5.0f)); if(n==0)y1=H; x+=dx; dx+=track[idx].curve*0.12f; int w1=(int)(W*0.55f*scale1/5.0f); int w2=(int)(W*0.55f*scale2/5.0f); if(w1<2)w1=2; if(w2<2)w2=2; int cx1=(int)(W/2+x*scale1/2.0f-player_x*W*0.5f*scale1/5.0f); int cx2=(int)(W/2+(x+dx)*scale2/2.0f-player_x*W*0.5f*scale2/5.0f); u32 desert=track[idx].color?0x00C2A265:0x00B89A55; fill_rect(0,y2,W,y1-y2+1,desert); u32 road=track[idx].color?0x00333333:0x00444444; fill_trap(y2,y1,cx1-w1,cx1+w1,cx2-w2,cx2+w2,road); u32 edge=track[idx].color?0x00FF0000:0x00FFFFFF; fill_trap(y2,y1,cx1-w1-8,cx1-w1,cx2-w2-8,cx2-w2,edge); fill_trap(y2,y1,cx1+w1,cx1+w1+8,cx2+w2,cx2+w2+8,edge); if(track[idx].color) fill_trap(y2,y1,cx1-3,cx1+3,cx2-3,cx2+3,0x00FFD23F);}
 for(int i=NCAR-1;i>=0;i--){float dz=cars[i].z-pos; float track_len=NSEG*200.0f; if(dz<0)dz+=track_len; if(dz>6000||dz<0)continue; float scale=1000.0f/(dz+1); int sx=(int)(W/2+(cars[i].off-player_x)*W*0.4f*scale/5.0f); int sy=(int)(H-120-scale*18); int cw=(int)(90*base*scale/5.0f+20); int ch=(int)(55*base*scale/5.0f+14); if(cw<8)cw=8; if(ch<6)ch=6; draw_car(sx,sy,cw,ch,cars[i].col);}
 int pcx=(int)(W/2),pcy=(int)(H-110); int pw=(int)(150*base),ph=(int)(95*base); draw_car(pcx,pcy,pw,ph,0x00FF0000); if(pad_nitro&&speed>150){fill_rect(pcx-20,pcy+ph/2,12,30,0x00FF9F1C); fill_rect(pcx+8,pcy+ph/2,12,30,0x00FF9F1C);}
 char buf[64]; draw_text("LAS VEGAS",24,16,4,0x00FFFFFF); sprintf(buf,"POS: %d / 16",position); draw_text(buf,24,48,3,0x00FFD23F); sprintf(buf,"LAP: %d / 3",lap); draw_text(buf,24,70,3,0x00FFFFFF); sprintf(buf,"TIME: %d",time_left); draw_text(buf,24,92,3,time_left<15?0x00FF4D4D:0x00FFFFFF); sprintf(buf,"KM/H: %d",(int)speed); draw_text(buf,(int)W-240,20,4,0x00FFFFFF); draw_text("FUEL",(int)W-240,60,3,0x00FFD23F); fill_rect((int)W-240,82,200,14,0x00000000); fill_rect((int)W-240,82,(int)(fuel*2),14,fuel<25?0x00FF0000:0x00FF9F1C); draw_text("NITRO",(int)W-240,102,3,0x0000E5C0); fill_rect((int)W-240,124,200,14,0x00000000); fill_rect((int)W-240,124,(int)(nitro*2),14,0x0000E5C0);
 if(pit) draw_text("PIT STOP - ABASTECENDO!",(int)W/2-text_width("PIT STOP - ABASTECENDO!",3)/2,(int)H/2-80,3,0x00FFD23F); if(fuel<25) draw_text("GASOLINA BAIXA! APERTE L1 PARADO!",(int)W/2-text_width("GASOLINA BAIXA! APERTE L1 PARADO!",3)/2,(int)H/2-50,3,0x00FF0000); draw_text("X: ACELERAR  QUADRADO: FREIO  R1: NITRO  L1: PIT",24,(int)H-30,3,0x00FFFFFF);
 if(state==1){const char *txt="3"; if(count_t>60)txt="2"; if(count_t>120)txt="1"; if(count_t>170)txt="GO!"; draw_text(txt,(int)W/2-text_width(txt,10)/2,(int)H/2-50,10,0x00FF0000);}
 if(state==3){fill_rect((int)W/2-280,(int)H/2-80,560,160,0x00000000); if(lap>3){draw_text("VOCE VENCEU!",(int)W/2-text_width("VOCE VENCEU!",6)/2,(int)H/2-40,6,0x00FFD23F); draw_text("CAMPEAO DE LAS VEGAS!",(int)W/2-text_width("CAMPEAO DE LAS VEGAS!",3)/2,(int)H/2+20,3,0x00FFFFFF);} else {draw_text("FIM DE CORRIDA",(int)W/2-text_width("FIM DE CORRIDA",5)/2,(int)H/2-40,5,0x00FF4D4D); draw_text("APERTE X PRA VOLTAR",(int)W/2-text_width("APERTE X PRA VOLTAR",3)/2,(int)H/2+20,3,0x00FFFFFF);}}
}
int main(int argc,char **argv){(void)argc;(void)argv; init_screen(); ioPadInit(7); sysUtilRegisterCallback(0,sysutil_cb,NULL); srand(20261112); state=0; build_track_las_vegas(); while(running){sysUtilCheckCallback(); read_pad(); update_game(); render(); present();} return 0;}