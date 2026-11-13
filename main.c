// ============================================
// ASTRONOMIA QUIZ PS3
// JÁ PRONTO - NÃO PRECISA MUDAR NADA!
// ============================================
#define NOME_JOGO "ASTRONOMIA QUIZ PS3"
#define NOME_JOGADOR "ASTRONAUTA"
#define VERSAO "v1.0"
// ============================================

#include <psl1ght/lv2.h>
#include <psl1ght/lv2/io.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TOTAL_PERGUNTAS 15

typedef struct {
    char pergunta[200];
    char opA[80];
    char opB[80];
    char opC[80];
    char opD[80];
    int correta;
    char curiosidade[200];
} Pergunta;

Pergunta perguntas[TOTAL_PERGUNTAS] = {
    {"Qual planeta e o mais proximo do Sol?", "Venus", "Mercurio", "Terra", "Marte", 1, "Mercurio leva so 88 dias pra dar volta no Sol!"},
    {"Qual planeta e o Planeta Vermelho?", "Jupiter", "Venus", "Marte", "Saturno", 2, "Marte tem a maior montanha: 21km!"},
    {"Qual e a estrela mais proxima da Terra?", "Sirius", "Sol", "Alfa Centauri", "Polaris", 1, "Luz do Sol leva 8 min pra chegar aqui!"},
    {"Qual planeta tem os aneis mais famosos?", "Netuno", "Urano", "Jupiter", "Saturno", 3, "Aneis de Saturno sao de gelo!"},
    {"O que e um buraco negro?", "Planeta escuro", "Estrela que explodiu e suga tudo", "Cometa", "Nuvem", 1, "Nem a luz escapa dele!"},
    {"Qual galaxia a gente vive?", "Andromeda", "Via Lactea", "Triangulo", "Sombrero", 1, "Tem mais de 100 bilhoes de estrelas!"},
    {"Quem pisou primeiro na Lua?", "Yuri Gagarin", "Buzz Aldrin", "Neil Armstrong", "Michael Collins", 2, "Foi em 20 de julho de 1969!"},
    {"Qual e o maior planeta?", "Saturno", "Netuno", "Jupiter", "Terra", 2, "Cabem 1300 Terras dentro dele!"},
    {"O que a Lua faz pra Terra?", "Nada", "Causa as mares", "Esquenta", "Faz chover", 1, "Sem Lua o dia seria de 6 horas!"},
    {"Qual planeta e o mais quente?", "Mercurio", "Marte", "Venus", "Terra", 2, "Venus faz 465 graus!"},
    {"O que e uma supernova?", "Estrela nova", "Explosao de estrela", "Asteroide", "Eclipse", 1, "Brilha mais que uma galaxia!"},
    {"Qual planeta e o mais distante do Sol?", "Urano", "Netuno", "Plutao", "Saturno", 1, "Netuno tem ventos de 2100 km/h!"},
    {"Quantos planetas tem o Sistema Solar?", "7", "8", "9", "10", 1, "Plutao virou planeta-anao em 2006!"},
    {"O que e a NASA?", "Agencia espacial dos EUA", "Agencia do Brasil", "Um planeta", "Um foguete", 0, "NASA mandou o homem pra Lua!"},
    {"O que e um ano-luz?", "Um ano com luz", "Distancia que a luz anda em 1 ano", "Tempo da luz", "Um planeta", 1, "1 ano-luz = 9,4 trilhoes de km!"},
};

int main() {
    ioPadInit(7);
    padData pad;
    int pontos = 0;
    int vidas = 3;
    int acertos = 0;
    int idx = 0;

    printf("\n========================================\n");
    printf("  %s\n", NOME_JOGO);
    printf("  %s\n", VERSAO);
    printf("  Jogador: %s\n", NOME_JOGADOR);
    printf("========================================\n");
    printf("Controles PS3:\n");
    printf("  X = A | Quadrado = B | Bola = C | Triangulo = D\n");
    printf("  START = Comecar | SELECT = Sair\n");
    printf("Aperte START pra comecar...\n");

    while(1){
        ioPadGetData(0, &pad);
        if(pad.BTN_START) break;
        if(pad.BTN_SELECT) return 0;
        usleep(10000);
    }

    for(idx=0; idx<TOTAL_PERGUNTAS; idx++){
        if(vidas<=0) break;
        Pergunta p = perguntas[idx];
        int escolha = -1;

        printf("\n----------------------------------------\n");
        printf("PERGUNTA %d/%d - Pontos: %d - Vidas: %d\n", idx+1, TOTAL_PERGUNTAS, pontos, vidas);
        printf("%s\n\n", p.pergunta);
        printf("  [X] A) %s\n", p.opA);
        printf("  [Quadrado] B) %s\n", p.opB);
        printf("  [Bola] C) %s\n", p.opC);
        printf("  [Triangulo] D) %s\n", p.opD);
        printf("\nAperte o botao da resposta...\n");

        while(escolha==-1){
            ioPadGetData(0, &pad);
            if(pad.BTN_CROSS) escolha=0;
            else if(pad.BTN_SQUARE) escolha=1;
            else if(pad.BTN_CIRCLE) escolha=2;
            else if(pad.BTN_TRIANGLE) escolha=3;
            else if(pad.BTN_SELECT) return 0;
            usleep(10000);
        }
        usleep(300000);

        if(escolha == p.correta){
            pontos += 100;
            acertos++;
            printf("✅ ACERTOU! +100 pontos!\n");
            printf("🤯 CURIOSIDADE: %s\n", p.curiosidade);
        } else {
            vidas--;
            printf("❌ ERROU! Perdeu 1 vida!\n");
            if(p.correta==0) printf("Resposta certa era: %s\n", p.opA);
            if(p.correta==1) printf("Resposta certa era: %s\n", p.opB);
            if(p.correta==2) printf("Resposta certa era: %s\n", p.opC);
            if(p.correta==3) printf("Resposta certa era: %s\n", p.opD);
            printf("🤯 CURIOSIDADE: %s\n", p.curiosidade);
        }
        printf("Pontos: %d | Vidas: %d\n", pontos, vidas);
        usleep(800000);
    }

    printf("\n========================================\n");
    if(vidas<=0){
        printf("💔 GAME OVER!\n");
    } else if(pontos>=1300){
        printf("🏆 GENIO GALACTICO! %s\n", NOME_JOGADOR);
    } else if(pontos>=800){
        printf("🌟 ASTRONAUTA! Muito bom!\n");
    } else {
        printf("🌙 EXPLORADOR ESPACIAL! Boa!\n");
    }
    printf("Pontos finais: %d\n", pontos);
    printf("Acertos: %d/%d\n", acertos, TOTAL_PERGUNTAS);
    printf("========================================\n");
    printf("Aperte SELECT pra sair ou START pra jogar de novo\n");

    while(1){
        ioPadGetData(0, &pad);
        if(pad.BTN_SELECT) break;
        if(pad.BTN_START){ main(); break; }
        usleep(10000);
    }
    return 0;
}