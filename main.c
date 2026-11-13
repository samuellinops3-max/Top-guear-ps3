// ASTRONOMIA QUIZ PS3 - mesma base do Cobra Arena que ja funciona
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

typedef struct {
    char pergunta[90];
    char pergunta2[90];
    char opA[60];
    char opB[60];
    char opC[60];
    char opD[60];
    int correta;
    char curiosidade[90];
} Pergunta;

static Pergunta perguntas[] = {
    {"Qual planeta e o mais", "proximo do Sol?", "Venus", "Mercurio", "Terra", "Marte", 1, "Mercurio leva 88 dias!"},
    {"Qual planeta e o", "Planeta Vermelho?", "Jupiter", "Venus", "Marte", "Saturno", 2, "Marte tem montanha de 21km!"},
    {"Qual e a estrela mais", "proxima da Terra?", "Sirius", "Sol", "Alfa Centauri", "Polaris", 1, "Luz do Sol leva 8 min!"},
    {"Qual planeta tem os", "aneis mais famosos?", "Netuno", "Urano", "Jupiter", "Saturno", 3, "Aneis sao de gelo!"},
    {"O que e um buraco negro?", "Estrela que suga tudo", "Planeta escuro", "Estrela que suga tudo", "Cometa", "Nuvem", 1, "Nem a luz escapa!"},
    {"Qual galaxia a gente vive?", "Via Lactea", "Andromeda", "Via Lactea", "Triangulo", "Sombrero", 1, "Tem 100 bilhoes de estrelas!"},
    {"Quem pisou primeiro", "na Lua?", "Yuri Gagarin", "Buzz Aldrin", "Neil Armstrong", "M Collins", 2, "Foi em 1969!"},
    {"Qual e o maior planeta", "do Sistema Solar?", "Saturno", "Netuno", "Jupiter", "Terra", 2, "Cabem 1300 Terras!"},
    {"O que a Lua faz", "pra Terra?", "Nada", "Causa as mares", "Esquenta", "Faz chover", 1, "Sem Lua dia seria 6h!"},
    {"Qual planeta e o", "mais quente?", "Mercurio", "Marte", "Venus", "Terra", 2, "Venus faz 465 graus!"},
    {"O que e uma supernova?", "Explosao de estrela", "Estrela nova", "Explosao de estrela", "Asteroide", "Eclipse", 1, "Brilha mais que galaxia!"},
    {"Qual planeta e o mais", "distante do Sol?", "Urano", "Netuno", "Plutao", "Saturno", 1, "Ventos de 2100 km/h!"},
    {"Quantos planetas tem", "o Sistema Solar?", "7", "8", "9", "10", 1, "Plutao virou anao em 2006!"},
    {"O que e a NASA?", "Agencia dos EUA", "Agencia dos EUA", "Agencia do Brasil", "Um planeta", "Um foguete", 0, "Mandou homem pra Lua!"},
    {"O que e um ano-luz?", "Distancia da luz", "Um ano com luz", "Distancia da luz", "Tempo da luz", "Um planeta", 1, "9,4 trilhoes de km!"},
};
#define TOTAL (sizeof(perguntas)/sizeof(perguntas[0]))

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata) { (void)param; (void)usrdata; if (status == SYSUTIL_EXIT_GAME) running = 0; }

static void init_screen(void) {
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st; videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0)) resid = VIDEO_RESOLUTION_720;
    videoResolution res; videoGetResolution(resid, &res); W = res.width; H = res.height;
    videoConfiguration vc; memset(&vc, 0, sizeof(vc)); vc.resolution = resid; vc.format = VIDEO_BUFFER_FORMAT_XRGB; vc.aspect = VIDEO_ASPECT_AUTO; vc.pitch = W*4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0); videoGetState(VIDEO_PRIMARY, 0, &st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for (int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64, W*H*4); gcmAddressToOffset(fb[i], &fboff[i]); gcmSetDisplayBuffer(i, fboff[i], W*4, W, H); }
    back=(u32*)memalign(64, W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){ if(!first_flip) wait_flip(); else gcmResetFlipStatus(); memcpy(fb[cur], back, W*H*4); gcmSetFlip(ctx, cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur ^= 1; first_flip = 0; }

static void fill_rect(int x,int y,int w,int h,u32 c){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
    if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
    for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
    if(r<1) r=1; for(int dy=-r;dy<=r;dy++){ int y=cy+dy; if(y<0||y>=(int)H) continue; int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw, x1=cx+hw; if(x0<0) x0=0; if(x1>=(int)W) x1=W-1; u32 *p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c; }
}
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0',"###" "#.#" "#.#" "#.#" "###"}, {'1',".#." "##." ".#." ".#." "###"},
    {'2',"###" "..#" "###" "#.." "###"}, {'3',"###" "..#" "###" "..#" "###"},
    {'4',"#.#" "#.#" "###" "..#" "..#"}, {'5',"###" "#.." "###" "..#" "###"},
    {'6',"###" "#.." "###" "#.#" "###"}, {'7',"###" "..#" "..#" "..#" "..#"},
    {'8',"###" "#.#" "###" "#.#" "###"}, {'9',"###" "#.#" "###" "..#" "###"},
    {'A',".#." "#.#" "###" "#.#" "#.#"}, {'B',"##." "#.#" "##." "#.#" "##."},
    {'C',"###" "#.." "#.." "#.." "###"}, {'D',"##." "#.#" "#.#" "#.#" "##."},
    {'E',"###" "#.." "###" "#.." "###"}, {'F',"###" "#.." "###" "#.." "#.."},
    {'G',"###" "#.." "#.#" "#.#" "###"}, {'H',"#.#" "#.#" "###" "#.#" "#.#"},
    {'I',"###" ".#." ".#." ".#." "###"}, {'J',"###" "..#" "..#" "#.#" "###"},
    {'L',"#.." "#.." "#.." "#.." "###"}, {'M',"#.#" "###" "###" "#.#" "#.#"},
    {'N',"#.#" "###" "###" "#.#" "#.#"}, {'O',"###" "#.#" "#.#" "#.#" "###"},
    {'P',"###" "#.#" "###" "#.." "#.."}, {'Q',"###" "#.#" "#.#" "##." ".#."},
    {'R',"##." "#.#" "##." "#.#" "#.#"}, {'S',"###" "#.." "###" "..#" "###"},
    {'T',"###" ".#." ".#." ".#." ".#."}, {'U',"#.#" "#.#" "#.#" "#.#" "###"},
    {'V',"#.#" "#.#" "#.#" "#.#" ".#."}, {'X',"#.#" "#.#" ".#." "#.#" "#.#"},
    {'Z',"###" "..#" ".#." "#.." "###"}, {'?',"###" "..#" ".#." "..." ".#."},
    {'!',".#." ".#." ".#." "..." ".#."}, {'.', "..." "..." "..." "..." ".#."},
    {':', "..." ".#." "..." ".#." "..."}, {'-', "..." "..." "###" "..." "..."},
    {'/', "..#" "..#" ".#." "#.." "#.."}, {'+', "..." ".#." "###" ".#." "..."},
    {'%', "#.#" "..#" ".#." "#.." "#.#"}, {' ', "..." "..." "..." "..." "..."},
};
static const char *find_glyph(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){
    for(;*s;s++){ const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc, y+r*sc, sc, sc, c); x+=4*sc; }
}
static void draw_text_center(const char *s,int y,int sc,u32 c){ draw_text(s, (int)W/2 - text_width(s,sc)/2, y, sc, c); }

// ESTADO DO JOGO
static int estado = 0; // 0=menu 1=pergunta 2=mostra resposta 3=fim
static int idx = 0, pontos = 0, vidas = 3, acertos = 0;
static int escolha = -1, timer_resposta = 0;
static int btn_prev = 0;

static void render(void){
    // fundo espacial
    u32 n = W*H; for(u32 i=0;i<n;i++) back[i]=0x000A0A2E;
    // estrelas
    for(int i=0;i<100;i++){ int sx=(i*173)%(int)W, sy=(i*97)%(int)H; fill_rect(sx,sy,3,3,0x00FFFFFF); }
    // planeta decorativo
    circle((int)W-120, 90, 45, 0x00FF8800); circle((int)W-135, 75, 10, 0x00FFAA00);
    fill_rect(0,0,W,2,0x00A855F7);
    int ui = 4;
    if(estado==0){
        draw_text_center("ASTRONOMIA", 120, 10, 0x00FFFFFF);
        draw_text_center("QUIZ PS3", 190, 10, 0x00FACC15);
        circle((int)W/2, 320, 60, 0x00FFD23F);
        circle((int)W/2-20, 300, 12, 0x00FFAA00);
        circle((int)W/2+15, 340, 8, 0x00FF8800);
        draw_text_center("15 PERGUNTAS", 420, 4, 0x00FFFFFF);
        draw_text_center("3 VIDAS - 100 PTS", 460, 4, 0x0088FFAA);
        draw_text_center("X = A  QUADRADO = B", 530, 4, 0x00FFFFFF);
        draw_text_center("BOLA = C  TRIANGULO = D", 570, 4, 0x00FFFFFF);
        draw_text_center("APERTE START", 630, 6, 0x00FACC15);
    } else if(estado==1 || estado==2){
        char buf[64];
        sprintf(buf, "PERGUNTA %d / %d", idx+1, (int)TOTAL);
        draw_text(buf, 30, 30, 4, 0x00FFFFFF);
        sprintf(buf, "PONTOS %d", pontos); draw_text(buf, 30, 65, 4, 0x00FACC15);
        sprintf(buf, "VIDAS %d", vidas); draw_text(buf, (int)W-200, 30, 4, 0x00FF4D4D);
        Pergunta *p = &perguntas[idx];
        draw_text_center(p->pergunta, 140, 5, 0x00FFFFFF);
        draw_text_center(p->pergunta2, 175, 5, 0x00FFFFFF);
        // opcoes
        const char *opts[4] = {p->opA, p->opB, p->opC, p->opD};
        const char *letras[4] = {"A - X", "B - QUADRADO", "C - BOLA", "D - TRIANGULO"};
        u32 cores[4] = {0x002563EB, 0x0000C853, 0x00FF8800, 0x00C77DFF};
        for(int i=0;i<4;i++){
            int y = 240 + i*90;
            u32 cor = cores[i];
            if(estado==2){
                if(i==p->correta) cor = 0x0022C55E;
                else if(i==escolha) cor = 0x00EF4444;
                else cor = 0x00334155;
            }
            fill_rect(100, y, (int)W-200, 70, cor);
            draw_text(letras[i], 130, y+12, 3, 0x00FFFFFF);
            draw_text(opts[i], 130, y+38, 3, 0x00FFFFFF);
        }
        if(estado==2){
            if(escolha==p->correta) draw_text_center("ACERTOU! +100", 610, 5, 0x0022C55E);
            else draw_text_center("ERROU! -1 VIDA", 610, 5, 0x00EF4444);
            draw_text_center(p->curiosidade, 650, 3, 0x00FACC15);
        }
    } else if(estado==3){
        char buf[64];
        draw_text_center("FIM DE JOGO!", 120, 8, 0x00FACC15);
        sprintf(buf, "PONTOS %d", pontos); draw_text_center(buf, 220, 6, 0x00FFFFFF);
        sprintf(buf, "ACERTOS %d / %d", acertos, (int)TOTAL); draw_text_center(buf, 300, 5, 0x00FFFFFF);
        if(vidas<=0) draw_text_center("GAME OVER!", 380, 6, 0x00FF4D4D);
        else if(pontos>=1300) draw_text_center("GENIO GALACTICO!", 380, 6, 0x0022C55E);
        else if(pontos>=800) draw_text_center("ASTRONAUTA!", 380, 6, 0x0022C55E);
        else draw_text_center("EXPLORADOR ESPACIAL!", 380, 5, 0x0022C55E);
        draw_text_center("START = JOGAR DE NOVO", 520, 4, 0x00FFFFFF);
        draw_text_center("SELECT = SAIR", 560, 4, 0x00FFFFFF);
    }
    (void)ui;
}

static int ler_botao(void){
    padInfo info; padData d;
    ioPadGetInfo(&info);
    if(!info.status[0]) return 0;
    ioPadGetData(0, &d);
    int btn = 0;
    if(d.BTN_START) btn = 10;
    else if(d.BTN_SELECT) btn = 11;
    else if(d.BTN_CROSS) btn = 1;
    else if(d.BTN_SQUARE) btn = 2;
    else if(d.BTN_CIRCLE) btn = 3;
    else if(d.BTN_TRIANGLE) btn = 4;
    int pressed = (btn!=0 && btn_prev==0) ? btn : 0;
    btn_prev = btn;
    return pressed;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261109);
    while(running){
        sysUtilCheckCallback();
        int b = ler_botao();
        if(estado==0){
            if(b==10){ estado=1; idx=0; pontos=0; vidas=3; acertos=0; }
            if(b==11) running=0;
        } else if(estado==1){
            if(b>=1 && b<=4){
                escolha = b-1;
                Pergunta *p = &perguntas[idx];
                if(escolha==p->correta){ pontos+=100; acertos++; }
                else vidas--;
                estado=2; timer_resposta=0;
            }
            if(b==11) running=0;
        } else if(estado==2){
            timer_resposta++;
            if(timer_resposta>120){
                if(vidas<=0) estado=3;
                else { idx++; if(idx>=(int)TOTAL) estado=3; else estado=1; }
            }
        } else if(estado==3){
            if(b==10){ estado=1; idx=0; pontos=0; vidas=3; acertos=0; }
            if(b==11) running=0;
        }
        render();
        present();
        usleep(16000);
    }
    return 0;
}