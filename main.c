// Pou PS3 - Recriacao completa em C
// PSL1GHT + SDL 1.2 - 1280x720
// Controles: Analogico Esq = olhar / mover cesta | X = interagir | L1/R1 = trocar sala | START = sair

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>
#include <io/pad.h>
#include <sysutil/video.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define FPS 60
#define FRAME_DELAY 1000 / FPS

// Cores
#define RGB(r,g,b) SDL_MapRGB(screen->format, r,g,b)

SDL_Surface *screen = NULL;
TTF_Font *font = NULL;
TTF_Font *fontSmall = NULL;
TTF_Font *fontBig = NULL;

padInfo padinfo;
padData paddata;

// Estado do Pou
typedef struct {
    int fome;      // 0-100, 100 = cheio
    int saude;     // 0-100
    int energia;   // 0-100
    int diversao;  // 0-100
    int moedas;
    int sala; // 0 Cozinha, 1 Banheiro, 2 Quarto, 3 Jogos
    int piscar;
    int piscarTimer;
    int frame;
} PouState;

PouState pou;

// Minigame - comidas caindo
#define MAX_FOOD 8
typedef struct {
    int x, y;
    int speed;
    int tipo; // 0 maca, 1 bolo, 2 pizza, 3 cenoura
    int ativo;
} Food;

Food foods[MAX_FOOD];
int cestaX = 640;
int scoreGame = 0;
int gameTimer = 0;
int gameAtivo = 0;

// Utils desenho
void drawFilledCircle(SDL_Surface *dst, int cx, int cy, int radius, Uint32 color) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                    Uint32 *pixels = (Uint32*)dst->pixels;
                    pixels[py * dst->w + px] = color;
                }
            }
        }
    }
}

void drawFilledRect(SDL_Surface *dst, int x, int y, int w, int h, Uint32 color) {
    SDL_Rect r = {x, y, w, h};
    SDL_FillRect(dst, &r, color);
}

void drawRoundedRect(SDL_Surface *dst, int x, int y, int w, int h, int radius, Uint32 color) {
    drawFilledRect(dst, x + radius, y, w - 2*radius, h, color);
    drawFilledRect(dst, x, y + radius, w, h - 2*radius, color);
    drawFilledCircle(dst, x + radius, y + radius, radius, color);
    drawFilledCircle(dst, x + w - radius, y + radius, radius, color);
    drawFilledCircle(dst, x + radius, y + h - radius, radius, color);
    drawFilledCircle(dst, x + w - radius, y + h - radius, radius, color);
}

void drawText(SDL_Surface *dst, TTF_Font *f, int x, int y, const char *text, Uint8 r, Uint8 g, Uint8 b) {
    if (!f) return;
    SDL_Color col = {r, g, b, 255};
    SDL_Surface *t = TTF_RenderText_Blended(f, text, col);
    if (!t) return;
    SDL_Rect d = {x, y, 0, 0};
    SDL_BlitSurface(t, NULL, dst, &d);
    SDL_FreeSurface(t);
}

void drawTextCenter(SDL_Surface *dst, TTF_Font *f, int cx, int y, const char *text, Uint8 r, Uint8 g, Uint8 b) {
    if (!f) return;
    SDL_Color col = {r, g, b, 255};
    SDL_Surface *t = TTF_RenderText_Blended(f, text, col);
    if (!t) return;
    SDL_Rect d = {cx - t->w/2, y, 0, 0};
    SDL_BlitSurface(t, NULL, dst, &d);
    SDL_FreeSurface(t);
}

// Barra de status
void drawBar(int x, int y, int w, int h, int valor, const char *label, Uint8 r, Uint8 g, Uint8 b) {
    char buf[16];
    // fundo
    drawRoundedRect(screen, x, y, w, h, 8, RGB(40,40,40));
    // preenchimento
    int fillW = (w - 4) * valor / 100;
    if (fillW < 0) fillW = 0;
    if (fillW > w-4) fillW = w-4;
    if (valor > 50) drawRoundedRect(screen, x+2, y+2, fillW, h-4, 6, RGB(r,g,b));
    else if (valor > 25) drawRoundedRect(screen, x+2, y+2, fillW, h-4, 6, RGB(255,165,0));
    else drawRoundedRect(screen, x+2, y+2, fillW, h-4, 6, RGB(255,50,50));
    drawText(screen, fontSmall, x, y - 18, label, 255,255,255);
    snprintf(buf, sizeof(buf), "%d%%", valor);
    drawText(screen, fontSmall, x + w + 8, y + 2, buf, 255,255,255);
}

void drawHUD() {
    // Topo escuro
    drawFilledRect(screen, 0, 0, SCREEN_W, 80, RGB(30,30,45));
    drawBar(20, 35, 180, 18, pou.fome, "Fome", 255, 100, 100);
    drawBar(280, 35, 180, 18, pou.saude, "Saude", 100, 255, 100);
    drawBar(540, 35, 180, 18, pou.energia, "Energia", 100, 180, 255);
    drawBar(800, 35, 180, 18, pou.diversao, "Diversao", 255, 220, 80);

    // Moedas
    drawFilledCircle(screen, 1085, 44, 16, RGB(255,215,0));
    drawFilledCircle(screen, 1085, 44, 11, RGB(230,180,0));
    char coinBuf[32];
    snprintf(coinBuf, sizeof(coinBuf), "%d", pou.moedas);
    drawText(screen, font, 1110, 28, coinBuf, 255,255,255);

    // Nome sala
    const char *salas[4] = {"COZINHA", "BANHEIRO", "QUARTO", "JOGOS"};
    drawTextCenter(screen, fontSmall, 640, 68, salas[pou.sala], 200,200,200);
    drawText(screen, fontSmall, 20, 690, "L1 <- Sala | R1 Sala -> | X Interagir | START Sair", 180,180,180);
}

// Desenha o Pou com matematica
void drawPou(int cx, int cy, int pupilDX, int pupilDY) {
    int w = 340;
    int h = 420;
    int x = cx - w/2;
    int y = cy - h/2;

    // Sombra
    drawFilledCircle(screen, cx, cy + h/2 + 12, 150, RGB(0,0,0));

    // Corpo - formato de batata usando elipses sobrepostas
    // Corpo principal
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            double nx = (xx - w/2.0) / (w/2.0);
            double ny = (yy - h/2.0) / (h/2.0);
            // Equacao elipse com deformacao embaixo mais larga
            double deform = 1.0 + 0.12 * ny; // base mais larga
            double v = (nx*nx)/(deform*deform) + ny*ny;
            if (v <= 1.0) {
                int px = x + xx;
                int py = y + yy;
                if (px >=0 && px < SCREEN_W && py >=0 && py < SCREEN_H) {
                    Uint32 *pixels = (Uint32*)screen->pixels;
                    // Sombreamento: mais escuro nas bordas
                    double shade = 1.0 - 0.18 * v;
                    int r = (int)(194 * shade);
                    int g = (int)(143 * shade);
                    int b = (int)(92 * shade);
                    // Barriga mais clara
                    double dx = (xx - w/2.0)/ (w/2.0);
                    double dy = (yy - h*0.62)/ (h*0.30);
                    if (dx*dx + dy*dy < 0.9) {
                        r = 222; g = 178; b = 122;
                    }
                    pixels[py * screen->w + px] = RGB(r,g,b);
                }
            }
        }
    }

    // Olhos grandes
    int eyeY = cy - 70;
    int eyeLX = cx - 75;
    int eyeRX = cx + 75;
    int eyeR = 58;

    // Branco dos olhos
    drawFilledCircle(screen, eyeLX, eyeY, eyeR, RGB(255,255,255));
    drawFilledCircle(screen, eyeRX, eyeY, eyeR, RGB(255,255,255));
    // Contorno fino
    for (int a=0; a<360; a++) {
        double rad = a * M_PI / 180.0;
        int px = eyeLX + cos(rad)*(eyeR);
        int py = eyeY + sin(rad)*(eyeR);
        if (px>=0 && px<SCREEN_W && py>=0 && py<SCREEN_H) {
            Uint32 *pix = (Uint32*)screen->pixels;
            pix[py*screen->w+px] = RGB(120,90,50);
        }
        px = eyeRX + cos(rad)*(eyeR);
        py = eyeY + sin(rad)*(eyeR);
        if (px>=0 && px<SCREEN_W && py>=0 && py<SCREEN_H) {
            Uint32 *pix = (Uint32*)screen->pixels;
            pix[py*screen->w+px] = RGB(120,90,50);
        }
    }

    // Piscar
    if (pou.piscar > 0) {
        drawFilledRect(screen, eyeLX-eyeR, eyeY-10, eyeR*2, 20, RGB(194,143,92));
        drawFilledRect(screen, eyeRX-eyeR, eyeY-10, eyeR*2, 20, RGB(194,143,92));
    } else {
        // Pupilas que seguem analogico
        int pxL = eyeLX + pupilDX;
        int pyL = eyeY + pupilDY;
        int pxR = eyeRX + pupilDX;
        int pyR = eyeY + pupilDY;
        drawFilledCircle(screen, pxL, pyL, 26, RGB(30,30,30));
        drawFilledCircle(screen, pxR, pyR, 26, RGB(30,30,30));
        drawFilledCircle(screen, pxL+8, pyL-8, 8, RGB(255,255,255));
        drawFilledCircle(screen, pxR+8, pyR-8, 8, RGB(255,255,255));
    }

    // Boca - muda com fome / felicidade
    int mouthY = cy + 30;
    int mouthX = cx;
    if (pou.fome < 30) {
        // Boca triste / com fome: arco para baixo
        for (int a=180; a<=360; a++) {
            double rad = a * M_PI / 180.0;
            int px = mouthX + cos(rad)*42;
            int py = mouthY + 18 + sin(rad)*22;
            drawFilledCircle(screen, px, py, 5, RGB(80,40,20));
        }
        // Lingua para fora
        drawFilledCircle(screen, mouthX, mouthY+32, 12, RGB(255,120,120));
    } else if (pou.diversao > 70 && pou.fome > 50) {
        // Sorriso grande feliz
        for (int a=0; a<=180; a++) {
            double rad = a * M_PI / 180.0;
            int px = mouthX + cos(rad)*48;
            int py = mouthY + sin(rad)*32;
            drawFilledCircle(screen, px, py, 6, RGB(80,40,20));
        }
        drawFilledCircle(screen, mouthX, mouthY+22, 18, RGB(150,60,60));
    } else if (pou.fome < 60) {
        // Boca neutra / meio preocupado - linha reta
        drawFilledRect(screen, mouthX-35, mouthY, 70, 7, RGB(80,40,20));
    } else {
        // Sorriso normal
        for (int a=20; a<=160; a++) {
            double rad = a * M_PI / 180.0;
            int px = mouthX + cos(rad)*40;
            int py = mouthY + sin(rad)*24;
            drawFilledCircle(screen, px, py, 5, RGB(80,40,20));
        }
    }

    // Bochechas
    drawFilledCircle(screen, cx-115, cy+35, 18, RGB(255,150,130));
    drawFilledCircle(screen, cx+115, cy+35, 18, RGB(255,150,130));

    // Pes
    drawFilledCircle(screen, cx-70, cy+h/2-8, 32, RGB(160,110,60));
    drawFilledCircle(screen, cx+70, cy+h/2-8, 32, RGB(160,110,60));
}

void drawRoomBackground() {
    Uint32 col = RGB(180,220,255);
    if (pou.sala == 0) col = RGB(255,230,180); // Cozinha - quente
    if (pou.sala == 1) col = RGB(180,220,255); // Banheiro - azul
    if (pou.sala == 2) col = RGB(200,180,255); // Quarto - roxo
    if (pou.sala == 3) col = RGB(120,200,120); // Jogos - verde
    drawFilledRect(screen, 0, 80, SCREEN_W, SCREEN_H-80, col);

    // Chao
    drawFilledRect(screen, 0, 580, SCREEN_W, 140, RGB(150,110,70));

    // Decoracao por sala
    if (pou.sala == 0) {
        // Geladeira
        drawRoundedRect(screen, 60, 200, 160, 300, 12, RGB(220,220,230));
        drawText(screen, fontSmall, 80, 320, "GELADEIRA", 60,60,60);
        // Mesa
        drawFilledRect(screen, 950, 450, 250, 20, RGB(120,80,40));
        drawFilledRect(screen, 970, 470, 20, 110, RGB(120,80,40));
        drawFilledRect(screen, 1150, 470, 20, 110, RGB(120,80,40));
        drawText(screen, fontSmall, 1000, 410, "X = Comer", 50,50,50);
    }
    if (pou.sala == 1) {
        // Banheira
        drawRoundedRect(screen, 80, 400, 350, 150, 30, RGB(255,255,255));
        drawFilledRect(screen, 90, 410, 330, 60, RGB(120,200,255));
        drawText(screen, fontSmall, 180, 350, "X = Tomar Banho", 50,50,80);
        // Espelho
        drawRoundedRect(screen, 1000, 150, 150, 180, 20, RGB(200,230,255));
    }
    if (pou.sala == 2) {
        // Cama
        drawRoundedRect(screen, 950, 430, 260, 130, 15, RGB(255,180,180));
        drawText(screen, fontSmall, 1000, 380, "X = Dormir", 50,50,50);
        // Lampada
        drawFilledCircle(screen, 150, 180, 40, RGB(255,255,150));
    }
    if (pou.sala == 3) {
        drawTextCenter(screen, font, 640, 110, "MINIGAME: PEGUE A COMIDA!", 40,60,40);
        char buf[64];
        snprintf(buf, sizeof(buf), "Pontos: %d  |  Use Analogico para mover a cesta", scoreGame);
        drawTextCenter(screen, fontSmall, 640, 145, buf, 40,60,40);
    }
}

// Minigame
void initFoods() {
    for (int i=0;i<MAX_FOOD;i++) {
        foods[i].ativo = 0;
        foods[i].x = 100 + rand() % 1000;
        foods[i].y = -50 - rand()%400;
        foods[i].speed = 3 + rand()%4;
        foods[i].tipo = rand()%4;
    }
    // Ativa 3 iniciais
    for (int i=0;i<3;i++) foods[i].ativo = 1;
}

void updateMinigame(int anaX) {
    // Move cesta
    cestaX += anaX / 8;
    if (cestaX < 60) cestaX = 60;
    if (cestaX > 1220) cestaX = 1220;

    for (int i=0;i<MAX_FOOD;i++) {
        if (!foods[i].ativo) {
            if (rand()%100 < 2) {
                foods[i].ativo = 1;
                foods[i].x = 80 + rand()%1120;
                foods[i].y = -30;
                foods[i].speed = 3 + rand()%5;
                foods[i].tipo = rand()%4;
            }
            continue;
        }
        foods[i].y += foods[i].speed;
        // Colisao com cesta (y 620, largura 120)
        if (foods[i].y > 590 && foods[i].y < 640 && abs(foods[i].x - cestaX) < 70) {
            foods[i].ativo = 0;
            foods[i].y = -50;
            scoreGame += 10;
            pou.moedas += 5;
            pou.diversao += 3;
            if (pou.diversao > 100) pou.diversao = 100;
            pou.fome += 2;
            if (pou.fome > 100) pou.fome = 100;
        }
        if (foods[i].y > 720) {
            foods[i].ativo = 0;
            foods[i].y = -50;
        }
    }
    gameTimer++;
}

void drawMinigame() {
    // Cesta
    drawRoundedRect(screen, cestaX-60, 620, 120, 40, 10, RGB(180,120,40));
    drawFilledRect(screen, cestaX-60, 620, 120, 12, RGB(120,80,20));
    // Comidas caindo
    for (int i=0;i<MAX_FOOD;i++) {
        if (!foods[i].ativo) continue;
        int x = foods[i].x;
        int y = foods[i].y;
        if (foods[i].tipo == 0) { // maca vermelha
            drawFilledCircle(screen, x, y, 20, RGB(255,40,40));
            drawFilledRect(screen, x-2, y-28, 4, 10, RGB(80,50,20));
            drawFilledCircle(screen, x+8, y-24, 7, RGB(80,180,80));
        } else if (foods[i].tipo == 1) { // bolo
            drawFilledRect(screen, x-20, y, 40, 22, RGB(255,180,220));
            drawFilledRect(screen, x-15, y-12, 30, 12, RGB(255,220,240));
            drawFilledCircle(screen, x, y-18, 5, RGB(255,0,0));
        } else if (foods[i].tipo == 2) { // pizza
            drawFilledCircle(screen, x, y, 20, RGB(255,220,100));
            drawFilledCircle(screen, x-6, y-4, 5, RGB(255,0,0));
            drawFilledCircle(screen, x+6, y+4, 5, RGB(0,180,0));
        } else { // cenoura
            drawFilledRect(screen, x-6, y, 12, 28, RGB(255,140,0));
            drawFilledRect(screen, x-8, y-12, 16, 10, RGB(0,200,0));
        }
    }
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        printf("TTF_Init: %s\n", TTF_GetError());
    }
    screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_HWSURFACE | SDL_DOUBLEBUF);
    if (!screen) return 1;
    SDL_WM_SetCaption("Pou PS3", NULL);

    // Fonte - tenta carregar do data, se falhar usa padrao
    font = TTF_OpenFont("data/font.ttf", 24);
    fontSmall = TTF_OpenFont("data/font.ttf", 18);
    fontBig = TTF_OpenFont("data/font.ttf", 36);
    if (!font) font = TTF_OpenFont("/dev_hdd0/game/POUPS3001/USRDIR/data/font.ttf", 24);
    if (!fontSmall) fontSmall = font;
    if (!fontBig) fontBig = font;

    ioPadInit(7);
    IMG_Init(IMG_INIT_PNG);

    // Estado inicial
    pou.fome = 80;
    pou.saude = 90;
    pou.energia = 85;
    pou.diversao = 70;
    pou.moedas = 120;
    pou.sala = 0;
    pou.piscar = 0;
    pou.piscarTimer = 0;
    pou.frame = 0;
    initFoods();

    int running = 1;
    int prevL1 = 0, prevR1 = 0, prevX = 0;
    int tickCounter = 0;
    SDL_Event ev;

    while (running) {
        Uint32 frameStart = SDL_GetTicks();

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }

        ioPadGetInfo(&padinfo);
        int anaX = 0, anaY = 0;
        int btnL1 = 0, btnR1 = 0, btnX = 0, btnStart = 0;

        if (padinfo.status[0]) {
            ioPadGetData(0, &paddata);
            if (paddata.button[PAD_CTRL_START]) btnStart = 1;
            if (paddata.button[PAD_CTRL_L1]) btnL1 = 1;
            if (paddata.button[PAD_CTRL_R1]) btnR1 = 1;
            if (paddata.button[PAD_CTRL_CROSS]) btnX = 1;
            anaX = paddata.button[6] - 128; // ANA_L_X
            anaY = paddata.button[7] - 128; // ANA_L_Y
        }
        if (btnStart) running = 0;

        // Troca de sala com L1/R1 (borda de subida)
        if (btnL1 && !prevL1) {
            pou.sala--;
            if (pou.sala < 0) pou.sala = 3;
            if (pou.sala == 3) initFoods();
        }
        if (btnR1 && !prevR1) {
            pou.sala++;
            if (pou.sala > 3) pou.sala = 0;
            if (pou.sala == 3) initFoods();
        }
        prevL1 = btnL1; prevR1 = btnR1;

        // Acao X
        if (btnX && !prevX) {
            if (pou.sala == 0) { // Cozinha - alimentar
                if (pou.moedas >= 5) {
                    pou.moedas -= 5;
                    pou.fome += 18;
                    if (pou.fome > 100) pou.fome = 100;
                    pou.saude += 2;
                    if (pou.saude > 100) pou.saude = 100;
                } else {
                    pou.fome += 8;
                    if (pou.fome > 100) pou.fome = 100;
                }
            } else if (pou.sala == 1) { // Banheiro
                pou.saude += 15;
                if (pou.saude > 100) pou.saude = 100;
                pou.energia += 2;
            } else if (pou.sala == 2) { // Quarto - dormir
                pou.energia += 20;
                if (pou.energia > 100) pou.energia = 100;
                pou.saude += 3;
            } else if (pou.sala == 3) { // Jogos
                pou.diversao += 5;
                if (pou.diversao > 100) pou.diversao = 100;
            }
        }
        prevX = btnX;

        // Minigame atualiza sempre na sala Jogos
        if (pou.sala == 3) {
            updateMinigame(anaX);
        }

        // Decaimento ao longo do tempo - a cada ~60 frames (1 seg)
        tickCounter++;
        if (tickCounter >= 60) {
            tickCounter = 0;
            pou.fome -= 1;
            pou.diversao -= 1;
            pou.energia -= 1;
            if (pou.fome < 20) pou.saude -= 1;
            if (pou.fome < 0) pou.fome = 0;
            if (pou.diversao < 0) pou.diversao = 0;
            if (pou.energia < 0) pou.energia = 0;
            if (pou.saude < 0) pou.saude = 0;
            if (pou.saude > 100) pou.saude = 100;
            // Ganha moeda passiva a cada 10 seg se feliz
            if (pou.diversao > 60 && pou.fome > 60) pou.moedas += 1;
        }

        // Piscar
        pou.piscarTimer++;
        if (pou.piscarTimer > 180) {
            pou.piscar = 8;
            pou.piscarTimer = 0;
        }
        if (pou.piscar > 0) pou.piscar--;

        // Pupilas seguem analogico - limita a +/-14
        int pupilDX = anaX / 10;
        int pupilDY = anaY / 10;
        if (pupilDX > 14) pupilDX = 14;
        if (pupilDX < -14) pupilDX = -14;
        if (pupilDY > 10) pupilDY = 10;
        if (pupilDY < -10) pupilDY = -10;

        // Animacao leve de respiro
        pou.frame++;
        int bob = (int)(sin(pou.frame * 0.05) * 4);

        // RENDER
        drawRoomBackground();
        drawHUD();

        // Pou centralizado, ou escondido no minigame para dar espaco
        if (pou.sala == 3) {
            // Pou pequeno no canto assistindo
            drawPou(200, 480 + bob, pupilDX, pupilDY);
            drawMinigame();
            drawText(screen, fontSmall, 850, 650, "Pegue para ganhar moedas!", 255,255,255);
        } else {
            drawPou(640, 420 + bob, pupilDX, pupilDY);
            // Texto de ajuda por sala
            if (pou.sala == 0) drawTextCenter(screen, fontSmall, 640, 640, "Aperte X para alimentar o Pou - Custa 5 moedas", 60,40,20);
            if (pou.sala == 1) drawTextCenter(screen, fontSmall, 640, 640, "Aperte X para dar banho e recuperar Saude", 40,40,80);
            if (pou.sala == 2) drawTextCenter(screen, fontSmall, 640, 640, "Aperte X para dormir e recuperar Energia", 60,40,80);
      