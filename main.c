// SLITHER.IO PS3 - SEM LIBFONT - COMPILA 100%
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <ppu-lv2.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <tiny3d.h>

#define MAX_FOODS 100
#define MAX_BOTS 15
#define MAX_BODY 120

struct Body { float x, y; };
struct Snake {
    float x, y, angle;
    int score;
    int alive;
    int skin;
    int bodyLen;
    struct Body body[MAX_BODY];
    float tx, ty;
};
struct Food { float x, y; int color; };
struct Power { float x, y; int type; };

static struct Snake player;
static struct Snake bots[MAX_BOTS];
static struct Food foods[MAX_FOODS];
static struct Power powers[8];
static int foodsEaten = 0, kills = 0, dead = 0, deadReason = 0;
static int boostOn = 0, shieldOn = 0, magnetOn = 0;
static int shieldTime = 0, magnetTime = 0;
static float joyX = 1, joyY = 0;

static unsigned int skinColors[8][6] = {
    {0xFF0000FF,0xFF8800FF,0xFFFF00FF,0x00FF00FF,0x0088FFFF,0xAA00FFFF},
    {0xFFFFFFFF,0x111111FF,0xFFFFFFFF,0x111111FF,0xFFFFFFFF,0x111111FF},
    {0xFF0000FF,0xFF6600FF,0xFFCC00FF,0xFF0000FF,0xFF6600FF,0xFFCC00FF},
    {0xFF0066FF,0xFFFFFFFF,0xFF0066FF,0xFFFFFFFF,0xFF0066FF,0xFFFFFFFF},
    {0x00FF00FF,0xFFFF00FF,0x0088FFFF,0x00FF00FF,0xFFFF00FF,0x0088FFFF},
    {0xFF8800FF,0x000000FF,0x000000FF,0xFF8800FF,0x000000FF,0x000000FF},
    {0x220044FF,0x8800FFFF,0x00FFFFFF,0xFF00FFFF,0x220044FF,0x8800FFFF},
    {0x00FF88FF,0x003311FF,0x00FF88FF,0x003311FF,0x00FF88FF,0x003311FF},
};

float frand(float a, float b){
    return a + ((float)rand() / (float)RAND_MAX) * (b - a);
}

void initGame(){
    srand((unsigned int)time(NULL));
    player.x=0; player.y=0; player.angle=0; player.score=10; player.alive=1; player.skin=0; player.bodyLen=20;
    for(int i=0;i<20;i++){ player.body[i].x=-i*11; player.body[i].y=0; }
    for(int i=0;i<MAX_FOODS;i++){ foods[i].x=frand(-1500,1500); foods[i].y=frand(-1500,1500); foods[i].color=rand()%8; }
    for(int i=0;i<8;i++){ powers[i].x=frand(-1500,1500); powers[i].y=frand(-1500,1500); powers[i].type=i%3; }
    for(int i=0;i<MAX_BOTS;i++){
        struct Snake *b=&bots[i];
        b->x=frand(-1300,1300); b->y=frand(-1300,1300); b->angle=frand(0,6.28f);
        b->score=12+rand()%35; b->alive=1; b->skin=i%8; b->bodyLen=22;
        for(int j=0;j<22;j++){ b->body[j].x=b->x-j*11; b->body[j].y=b->y; }
        b->tx=b->x; b->ty=b->y;
    }
    foodsEaten=0; kills=0; dead=0; boostOn=0; shieldOn=0; magnetOn=0; shieldTime=0; magnetTime=0;
}

void killSnake(struct Snake *s, int reason){
    if(!s->alive) return;
    s->alive=0;
    if(s==&player){ dead=1; deadReason=reason; }
    else kills++;
}

void moveSnake(struct Snake *s, int isPlayer){
    float target;
    if(isPlayer){
        if(fabsf(joyX)>0.15f || fabsf(joyY)>0.15f) target=atan2f(joyY, joyX);
        else target=s->angle;
        if(fabsf(s->x)>=1490 || fabsf(s->y)>=1490){
            if(shieldOn){ s->x=fmaxf(-1480,fminf(1480,s->x)); s->y=fmaxf(-1480,fminf(1480,s->y)); }
            else { killSnake(s,1); return; }
        }
    } else {
        if(rand()%100<2){
            float best=1e9f; int bi=-1;
            for(int i=0;i<MAX_FOODS;i++){ float dx=foods[i].x-s->x, dy=foods[i].y-s->y; float d=dx*dx+dy*dy; if(d<best){best=d; bi=i;} }
            if(bi>=0){ s->tx=foods[bi].x; s->ty=foods[bi].y; }
        }
        target=atan2f(s->ty-s->y, s->tx-s->x);
        if(fabsf(s->x)>1350 || fabsf(s->y)>1350) target=atan2f(0-s->y, 0-s->x);
    }
    float diff=target-s->angle;
    while(diff>3.14159f) diff-=6.28318f; while(diff<-3.14159f) diff+=6.28318f;
    if(diff>0.09f) diff=0.09f; if(diff<-0.09f) diff=-0.09f;
    s->angle+=diff;
    float sp=3.5f; if(isPlayer && boostOn) sp=6.0f;
    s->x+=cosf(s->angle)*sp; s->y+=sinf(s->angle)*sp;
    for(int i=s->bodyLen-1;i>0;i--) s->body[i]=s->body[i-1];
    s->body[0].x=s->x; s->body[0].y=s->y;
    int maxLen=12+s->score; if(maxLen>MAX_BODY) maxLen=MAX_BODY;
    if(s->bodyLen<maxLen) s->bodyLen++;
    for(int i=0;i<MAX_FOODS;i++){
        float dx=foods[i].x-s->x, dy=foods[i].y-s->y;
        float range = (isPlayer && magnetOn)? 14400.0f : 600.0f;
        if(dx*dx+dy*dy < range){
            if(isPlayer && magnetOn && dx*dx+dy*dy>600){ foods[i].x+=(s->x-foods[i].x)*0.2f; foods[i].y+=(s->y-foods[i].y)*0.2f; continue; }
            s->score++; if(isPlayer) foodsEaten++;
            foods[i].x=frand(-1500,1500); foods[i].y=frand(-1500,1500); foods[i].color=rand()%8;
            break;
        }
    }
    if(isPlayer){
        for(int i=0;i<8;i++){
            float dx=powers[i].x-s->x, dy=powers[i].y-s->y;
            if(dx*dx+dy*dy < 625){
                if(powers[i].type==0){ shieldOn=1; shieldTime=300; }
                if(powers[i].type==1){ magnetOn=1; magnetTime=400; }
                if(powers[i].type==2){ s->score+=10; }
                powers[i].x=frand(-1500,1500); powers[i].y=frand(-1500,1500); powers[i].type=rand()%3;
            }
        }
        if(shieldTime>0) shieldTime--; else shieldOn=0;
        if(magnetTime>0) magnetTime--; else magnetOn=0;
    }
}

void drawCircle(float sx, float sy, float r, unsigned int color){
    tiny3d_SetPolygon(TINY3D_POLYGON);
    for(int i=0;i<12;i++){
        float a1=i*6.28318f/12, a2=(i+1)*6.28318f/12;
        tiny3d_VertexPos(sx, sy, 0);
        tiny3d_VertexColor(color);
        tiny3d_VertexPos(sx+cosf(a1)*r, sy+sinf(a1)*r, 0);
        tiny3d_VertexColor(color);
        tiny3d_VertexPos(sx+cosf(a2)*r, sy+sinf(a2)*r, 0);
        tiny3d_VertexColor(color);
    }
    tiny3d_End();
}

void drawSnake(struct Snake *s, float camX, float camY, int isPlayer){
    if(!s->alive) return;
    for(int i=s->bodyLen-1;i>=0;i--){
        float sx=s->body[i].x-camX, sy=s->body[i].y-camY;
        if(sx<-30||sx>1280+30||sy<-30||sy>720+30) continue;
        float r = (i==0)?15:11;
        unsigned int col = skinColors[s->skin][i%6];
        if(isPlayer) col = skinColors[0][i%6];
        drawCircle(sx, sy, r, col);
        if(i==0){
            float ax=cosf(s->angle), ay=sinf(s->angle);
            float px=-ay, py=ax;
            drawCircle(sx+ax*7+px*6, sy+ay*7+py*6, 5, 0xFFFFFFFF);
            drawCircle(sx+ax*7-px*6, sy+ay*7-py*6, 5, 0xFFFFFFFF);
            drawCircle(sx+ax*9+px*6, sy+ay*9+py*6, 2, 0x000000FF);
            drawCircle(sx+ax*9-px*6, sy+ay*9-py*6, 2, 0x000000FF);
            if(isPlayer && shieldOn){
                tiny3d_SetPolygon(TINY3D_LINE_LOOP);
                for(int k=0;k<20;k++){ float a=k*6.28318f/20; tiny3d_VertexPos(sx+cosf(a)*24, sy+sinf(a)*24, 0); tiny3d_VertexColor(0x00AAFFFF); }
                tiny3d_End();
            }
        }
    }
}

void drawRect(float x, float y, float w, float h, unsigned int col){
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x,y,0); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x+w,y,0); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x+w,y+h,0); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x,y+h,0); tiny3d_VertexColor(col);
    tiny3d_End();
}

int main(){
    tiny3d_Init(1024*1024);
    ioPadInit(7);
    initGame();
    padInfo padinfo;
    padData paddata;
    while(1){
        ioPadGetInfo(&padinfo);
        if(padinfo.status[0]){
            ioPadGetData(0, &paddata);
            int lx = paddata.ANA_L_H - 128;
            int ly = paddata.ANA_L_V - 128;
            if(abs(lx)>20 || abs(ly)>20){ joyX=lx/128.0f; joyY=ly/128.0f; }
            boostOn = (paddata.BTN_CROSS) ? 1 : 0;
            if(paddata.BTN_CIRCLE && !shieldOn){ shieldOn=1; shieldTime=300; }
            if(paddata.BTN_TRIANGLE && !magnetOn){ magnetOn=1; magnetTime=400; }
            if(dead && (paddata.BTN_CROSS || paddata.BTN_START)){ initGame(); }
            if(paddata.BTN_SELECT) break;
        }
        if(!dead){
            moveSnake(&player, 1);
            for(int i=0;i<MAX_BOTS;i++) if(bots[i].alive) moveSnake(&bots[i], 0);
            for(int i=0;i<MAX_BOTS;i++){
                if(!bots[i].alive) continue;
                for(int j=6;j<bots[i].bodyLen;j+=2){
                    float dx=player.x-bots[i].body[j].x, dy=player.y-bots[i].body[j].y;
                    if(dx*dx+dy*dy<200){ if(shieldOn) shieldOn=0; else { killSnake(&player,0); break; } }
                }
                if(!player.alive) break;
                for(int j=6;j<player.bodyLen;j+=2){
                    float dx=bots[i].x-player.body[j].x, dy=bots[i].y-player.body[j].y;
                    if(dx*dx+dy*dy<200){ killSnake(&bots[i],0); break; }
                }
            }
            for(int i=0;i<MAX_BOTS;i++) if(!bots[i].alive && rand()%100<1){ bots[i].alive=1; bots[i].x=frand(-1300,1300); bots[i].y=frand(-1300,1300); bots[i].score=12; bots[i].bodyLen=20; }
        }
        tiny3d_Clear(0x151528FF, TINY3D_CLEAR_ALL);
        tiny3d_Project2D();
        float camX=player.x-640, camY=player.y-360;
        tiny3d_SetPolygon(TINY3D_LINE_LOOP);
        tiny3d_VertexPos(-1500-camX, -1500-camY, 0); tiny3d_VertexColor(0xFF0000FF);
        tiny3d_VertexPos(1500-camX, -1500-camY, 0); tiny3d_VertexColor(0xFF0000FF);
        tiny3d_VertexPos(1500-camX, 1500-camY, 0); tiny3d_VertexColor(0xFF0000FF);
        tiny3d_VertexPos(-1500-camX, 1500-camY, 0); tiny3d_VertexColor(0xFF0000FF);
        tiny3d_End();
        unsigned int foodCols[8]={0xFF0000FF,0xFF8800FF,0xFFFF00FF,0x00FF00FF,0x0088FFFF,0xAA00FFFF,0xFF44FFFF,0x00FFFFFF};
        for(int i=0;i<MAX_FOODS;i++){ float sx=foods[i].x-camX, sy=foods[i].y-camY; if(sx<-20||sx>1300||sy<-20||sy>740) continue; drawCircle(sx,sy,6,foodCols[foods[i].color]); }
        for(int i=0;i<8;i++){ float sx=powers[i].x-camX, sy=powers[i].y-camY; if(sx<-20||sx>1300||sy<-20||sy>740) continue; unsigned int c=powers[i].type==0?0x00AAFFFF:powers[i].type==1?0xFF00FFFF:0xFFD23FFF; drawCircle(sx,sy,10,c); }
        for(int i=0;i<MAX_BOTS;i++) drawSnake(&bots[i], camX, camY, 0);
        drawSnake(&player, camX, camY, 1);
        drawRect(10,10, 200, 18, 0x000000AA);
        int bar = player.score>100?100:player.score;
        drawRect(10,10, bar*2, 18, 0x00FF88FF);
        if(dead){
            drawRect(0,0,1280,720, 0x000000CC);
            drawRect(540, 250, 200, 60, 0xFF0000FF);
            drawRect(480, 330, 320, 40, 0x00FF88FF);
        }
        tiny3d_Flip();
        sysUtilCheckCallback();
    }
    ioPadEnd();
    return 0;
}