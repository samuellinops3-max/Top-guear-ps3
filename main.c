// SLITHER.IO - PS3 - PSL1GHT / Tiny3D
// Controles: L3 = Analógico | X = Boost | O = Escudo | Triangulo = Imã | START = Reiniciar
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ppu-lv2.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <tiny3d.h>
#include <libfont.h>

#define MAX_FOODS 130
#define MAX_BOTS 15
#define WORLD 1500.0f
#define MAX_BODY 120

struct Body { float x, y; };
struct Snake {
    float x, y, angle;
    int score;
    int alive;
    int skin;
    int bodyLen;
    Body body[MAX_BODY];
    float tx, ty;
    char name[16];
};
struct Food { float x, y; int color; };
struct Power { float x, y; int type; }; // 0 shield, 1 magnet, 2 boost

static Snake player;
static Snake bots[MAX_BOTS];
static Food foods[MAX_FOODS];
static Power powers[8];
static int foodsEaten = 0, kills = 0, dead = 0, deadReason = 0; // 0 cobra, 1 borda
static int boostOn = 0, shieldOn = 0, magnetOn = 0;
static int shieldTime = 0, magnetTime = 0;
static float joyX = 1, joyY = 0;
static padInfo padinfo;
static padData paddata;

// Cores skins - igual HTML
static int skinColors[8][6] = {
    {0xFF0000,0xFF8800,0xFFFF00,0x00FF00,0x0088FF,0xAA00FF}, // rainbow
    {0xFFFFFF,0x111111,0xFFFFFF,0x111111,0xFFFFFF,0x111111}, // zebra
    {0xFF0000,0xFF6600,0xFFCC00,0xFF0000,0xFF6600,0xFFCC00}, // fire
    {0xFF0066,0xFFFFFF,0xFF0066,0xFFFFFF,0xFF0066,0xFFFFFF}, // candy
    {0x00FF00,0xFFFF00,0x0088FF,0x00FF00,0xFFFF00,0x0088FF}, // brasil
    {0xFF8800,0x000000,0x000000,0xFF8800,0x000000,0x000000}, // tiger
    {0x220044,0x8800FF,0x00FFFF,0xFF00FF,0x220044,0x8800FF}, // galaxy
    {0x00FF88,0x003311,0x00FF88,0x003311,0x00FF88,0x003311}, // neon
};
static char *botNames[15] = {"Noob","Pro","King","Turbo","Brabo","Ghost","Lenda","Dark","Fome","Snake","Ze","Loko","Fera","Top1","Mata"};

float frand(float a, float b){ return a + (rand()/(float)RAND_MAX)*(b-a); }

void initGame(){
    srand(sysGetTime());
    player.x=0; player.y=0; player.angle=0; player.score=10; player.alive=1; player.skin=0; player.bodyLen=20;
    for(int i=0;i<20;i++){ player.body[i].x=-i*11; player.body[i].y=0; }
    for(int i=0;i<MAX_FOODS;i++){ foods[i].x=frand(-1500,1500); foods[i].y=frand(-1500,1500); foods[i].color=rand()%8; }
    for(int i=0;i<8;i++){ powers[i].x=frand(-1500,1500); powers[i].y=frand(-1500,1500); powers[i].type=i%3; }
    for(int i=0;i<MAX_BOTS;i++){
        Snake *b=&bots[i];
        b->x=frand(-1300,1300); b->y=frand(-1300,1300); b->angle=frand(0,6.28);
        b->score=12+rand()%35; b->alive=1; b->skin=i%8; b->bodyLen=22;
        strcpy(b->name, botNames[i]);
        for(int j=0;j<22;j++){ b->body[j].x=b->x-j*11; b->body[j].y=b->y; }
        b->tx=b->x; b->ty=b->y;
    }
    foodsEaten=0; kills=0; dead=0; boostOn=0; shieldOn=0; magnetOn=0; shieldTime=0; magnetTime=0;
}

void killSnake(Snake *s, int reason){
    if(!s->alive) return;
    s->alive=0;
    // vira comida
    for(int i=0;i<s->bodyLen;i+=3){
        if(i<MAX_FOODS){ foods[i].x=s->body[i].x; foods[i].y=s->body[i].y; foods[i].color=rand()%8; }
    }
    if(s==&player){ dead=1; deadReason=reason; }
    else kills++;
}

void moveSnake(Snake *s, int isPlayer){
    float target;
    if(isPlayer){
        if(fabs(joyX)>0.15 || fabs(joyY)>0.15) target=atan2f(joyY, joyX);
        else target=s->angle;
        // BORDA MATA - PS3
        if(fabs(s->x)>=1490 || fabs(s->y)>=1490){
            if(shieldOn){ s->x=fmaxf(-1480,fminf(1480,s->x)); s->y=fmaxf(-1480,fminf(1480,s->y)); }
            else { killSnake(s,1); return; }
        }
    } else {
        // BOT - vai na comida mais perto
        if(rand()%100<2){
            float best=1e9; int bi=-1;
            for(int i=0;i<MAX_FOODS;i++){ float dx=foods[i].x-s->x, dy=foods[i].y-s->y; float d=dx*dx+dy*dy; if(d<best){best=d; bi=i;} }
            if(bi>=0){ s->tx=foods[bi].x; s->ty=foods[bi].y; }
        }
        target=atan2f(s->ty-s->y, s->tx-s->x);
        if(fabs(s->x)>1350 || fabs(s->y)>1350) target=atan2f(0-s->y, 0-s->x);
    }
    float diff=target-s->angle;
    while(diff>M_PI) diff-=2*M_PI; while(diff<-M_PI) diff+=2*M_PI;
    if(diff>0.09) diff=0.09; if(diff<-0.09) diff=-0.09;
    s->angle+=diff;
    float sp=3.5f; if(isPlayer && boostOn) sp=6.0f;
    s->x+=cosf(s->angle)*sp; s->y+=sinf(s->angle)*sp;
    // corpo segue cabeça
    for(int i=s->bodyLen-1;i>0;i--) s->body[i]=s->body[i-1];
    s->body[0].x=s->x; s->body[0].y=s->y;
    int maxLen=12+s->score; if(maxLen>MAX_BODY) maxLen=MAX_BODY;
    if(s->bodyLen<maxLen) s->bodyLen++;
    // comer
    for(int i=0;i<MAX_FOODS;i++){
        float dx=foods[i].x-s->x, dy=foods[i].y-s->y;
        float range = (isPlayer && magnetOn)? 120*120 : 600;
        if(dx*dx+dy*dy < range){
            if(isPlayer && magnetOn && dx*dx+dy*dy>600){ foods[i].x+=(s->x-foods[i].x)*0.2; foods[i].y+=(s->y-foods[i].y)*0.2; continue; }
            s->score++; if(isPlayer) foodsEaten++;
            foods[i].x=frand(-1500,1500); foods[i].y=frand(-1500,1500); foods[i].color=rand()%8;
            break;
        }
    }
    if(isPlayer){
        for(int i=0;i<8;i++){
            if(fabs(powers[i].x-s->x)<25 && fabs(powers[i].y-s->y)<25){
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

// Desenha circulo com Tiny3D
void drawCircle(float sx, float sy, float r, int color){
    float cr=((color>>16)&0xFF)/255.0f, cg=((color>>8)&0xFF)/255.0f, cb=(color&0xFF)/255.0f;
    tiny3d_SetPolygon(TINY3D_POLYGON);
    for(int i=0;i<16;i++){
        float a1=i*6.28f/16, a2=(i+1)*6.28f/16;
        tiny3d_VertexPos(sx, sy, 0);
        tiny3d_VertexColor(tiny3d_Blend(cr,cg,cb,1));
        tiny3d_VertexPos(sx+cosf(a1)*r, sy+sinf(a1)*r, 0);
        tiny3d_VertexColor(tiny3d_Blend(cr,cg,cb,1));
        tiny3d_VertexPos(sx+cosf(a2)*r, sy+sinf(a2)*r, 0);
        tiny3d_VertexColor(tiny3d_Blend(cr,cg,cb,1));
    }
    tiny3d_End();
}

// CORRIGIDO: cabeça do bot com olhos!
void drawSnake(Snake *s, float camX, float camY, int isPlayer){
    if(!s->alive) return;
    for(int i=s->bodyLen-1;i>=0;i--){
        float sx=s->body[i].x-camX, sy=s->body[i].y-camY;
        if(sx<-30||sx>1280+30||sy<-30||sy>720+30) continue;
        float r = (i==0)?15:11;
        int col = skinColors[s->skin][i%6];
        if(isPlayer) col = skinColors[0][i%6]; // player arco-iris
        drawCircle(sx, sy, r, col);
        if(i==0){
            // CABEÇA - OLHOS BRANCOS + PRETOS - AGORA BOT TEM CABEÇA!
            float ax=cosf(s->angle), ay=sinf(s->angle);
            float px=-ay, py=ax;
            drawCircle(sx+ax*7+px*6, sy+ay*7+py*6, 5, 0xFFFFFF);
            drawCircle(sx+ax*7-px*6, sy+ay*7-py*6, 5, 0xFFFFFF);
            drawCircle(sx+ax*9+px*6, sy+ay*9+py*6, 2.5, 0x000000);
            drawCircle(sx+ax*9-px*6, sy+ay*9-py*6, 2.5, 0x000000);
            // escudo
            if(isPlayer && shieldOn){
                tiny3d_SetPolygon(TINY3D_LINE_LOOP);
                for(int k=0;k<20;k++){ float a=k*6.28f/20; tiny3d_VertexPos(sx+cosf(a)*22, sy+sinf(a)*22, 0); tiny3d_VertexColor(tiny3d_Blend(0,0.6,1,1)); }
                tiny3d_End();
            }
        }
    }
}

int main(){
    tiny3d_Init(1024*1024);
    ioPadInit(7);
    libfont_Init();
    initGame();
    sysUtilRegisterCallback(0, NULL, NULL);

    while(1){
        // ===== LE CONTROLE PS3 =====
        ioPadGetInfo(&padinfo);
        if(padinfo.status[0]){
            ioPadGetData(0, &paddata);
            // L3 ANALOGICO - CORRIGIDO: 128 = centro, 0-255
            int lx = paddata.ANA_L_H - 128;
            int ly = paddata.ANA_L_V - 128;
            if(abs(lx)>20 || abs(ly)>20){ joyX=lx/128.0f; joyY=ly/128.0f; }
            // X = BOOST (BTN_CROSS)
            boostOn = (paddata.BTN_CROSS) ? 1 : 0;
            // O = ESCUDO (BTN_CIRCLE) - aperta e ativa
            if(paddata.BTN_CIRCLE && !shieldOn){ shieldOn=1; shieldTime=300; }
            // TRIANGULO = IMÃ
            if(paddata.BTN_TRIANGLE && !magnetOn){ magnetOn=1; magnetTime=400; }
            // START = reinicia se morreu
            if(dead && paddata.BTN_START){ initGame(); }
            // X TAMBEM VOLTA SE MORREU - TELA DE MORTE SIMPLES!
            if(dead && paddata.BTN_CROSS){ initGame(); }
            // SELECT = sair
            if(paddata.BTN_SELECT) break;
        }

        if(!dead){
            moveSnake(&player, 1);
            for(int i=0;i<MAX_BOTS;i++) if(bots[i].alive) moveSnake(&bots[i], 0);
            // colisão player x bots
            for(int i=0;i<MAX_BOTS;i++){
                if(!bots[i].alive) continue;
                for(int j=6;j<bots[i].bodyLen;j+=2){
                    float dx=player.x-bots[i].body[j].x, dy=player.y-bots[i].body[j].y;
                    if(dx*dx+dy*dy<200){ if(shieldOn) shieldOn=0; else { killSnake(&player,0); break; } }
                }
                if(!player.alive) break;
                // bot bate no player -> bot morre
                for(int j=6;j<player.bodyLen;j+=2){
                    float dx=bots[i].x-player.body[j].x, dy=bots[i].y-player.body[j].y;
                    if(dx*dx+dy*dy<200){ killSnake(&bots[i],0); break; }
                }
            }
            // respawn bots
            for(int i=0;i<MAX_BOTS;i++) if(!bots[i].alive && rand()%100<1){ bots[i].alive=1; bots[i].x=frand(-1300,1300); bots[i].y=frand(-1300,1300); bots[i].score=12; bots[i].bodyLen=20; }
        }

        // ===== DESENHA =====
        tiny3d_Clear(0x151528FF, TINY3D_CLEAR_ALL);
        tiny3d_Project2D();
        float camX=player.x-640, camY=player.y-360;
        // fundo
        // borda vermelha
        tiny3d_SetPolygon(TINY3D_LINE_LOOP);
        tiny3d_VertexPos(-1500-camX, -1500-camY, 0); tiny3d_VertexColor(tiny3d_Blend(1,0,0,1));
        tiny3d_VertexPos(1500-camX, -1500-camY, 0); tiny3d_VertexColor(tiny3d_Blend(1,0,0,1));
        tiny3d_VertexPos(1500-camX, 1500-camY, 0); tiny3d_VertexColor(tiny3d_Blend(1,0,0,1));
        tiny3d_VertexPos(-1500-camX, 1500-camY, 0); tiny3d_VertexColor(tiny3d_Blend(1,0,0,1));
        tiny3d_End();
        // comidas
        int foodCols[8]={0xFF0000,0xFF8800,0xFFFF00,0x00FF00,0x0088FF,0xAA00FF,0xFF44FF,0x00FFFF};
        for(int i=0;i<MAX_FOODS;i++){ float sx=foods[i].x-camX, sy=foods[i].y-camY; if(sx<-20||sx>1300||sy<-20||sy>740) continue; drawCircle(sx,sy,6,foodCols[foods[i].color]); }
        // powers
        for(int i=0;i<8;i++){ float sx=powers[i].x-camX, sy=powers[i].y-camY; if(sx<-20||sx>1300||sy<-20||sy>740) continue; int c=powers[i].type==0?0x00AAFF:powers[i].type==1?0xFF00FF:0xFFD23F; drawCircle(sx,sy,10,c); }
        // cobras - bots primeiro, player por cima
        for(int i=0;i<MAX_BOTS;i++) drawSnake(&bots[i], camX, camY, 0);
        drawSnake(&player, camX, camY, 1);

        // HUD - SEM BOTÃO INVISÍVEL, SÓ TEXTO!
        libfont_DrawString(10, 10, "SLITHER.IO PS3", 0xFFFFFFFF);
        char buf[64]; sprintf(buf, "Tamanho: %d  Cobras: %d", player.score, 16);
        libfont_DrawString(10, 30, buf, 0xFFFFFFFF);
        sprintf(buf, "Comidas: %d  Matou: %d", foodsEaten, kills);
        libfont_DrawString(10, 50, buf, 0xFFFFFFFF);
        if(shieldOn) libfont_DrawString(10, 70, "ESCUDO ATIVO!", 0x00AAFFFF);
        if(magnetOn) libfont_DrawString(10, 90, "IMA ATIVO!", 0xFF00FFFF);

        // TELA DE MORTE - SIMPLES! SÓ APERTA X!
        if(dead){
            // fundo escuro
            tiny3d_SetPolygon(TINY3D_QUADS);
            tiny3d_VertexPos(0,0,0); tiny3d_VertexColor(tiny3d_Blend(0,0,0,0.8));
            tiny3d_VertexPos(1280,0,0); tiny3d_VertexColor(tiny3d_Blend(0,0,0,0.8));
            tiny3d_VertexPos(1280,720,0); tiny3d_VertexColor(tiny3d_Blend(0,0,0,0.8));
            tiny3d_VertexPos(0,720,0); tiny3d_VertexColor(tiny3d_Blend(0,0,0,0.8));
            tiny3d_End();
            libfont_DrawString(500, 250, "VOCE MORREU!", 0xFF4444FF);
            if(deadReason==1) libfont_DrawString(460, 290, "BATEU NA BORDA!", 0xFFFFFFFF);
            else libfont_DrawString(460, 290, "BATEU NA COBRA!", 0xFFFFFFFF);
            sprintf(buf, "Tamanho: %d  Comidas: %d  Matou: %d", player.score, foodsEaten, kills);
            libfont_DrawString(430, 330, buf, 0xFFFFFFFF);
            libfont_DrawString(400, 400, "APERTE X PARA JOGAR DE NOVO", 0x00FF88FF);
            libfont_DrawString(440, 430, "SELECT PARA SAIR", 0x888888FF);
        }

        tiny3d_Flip();
        sysUtilCheckCallback();
    }
    ioPadEnd();
    return 0;
}