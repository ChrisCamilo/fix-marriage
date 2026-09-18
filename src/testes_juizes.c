/* testes_juizes.c -- testa a camada de medidas com quadros SINTETICOS.
 *
 * O ponto: aqui a resposta e conhecida por construcao. Monta-se um quadro com
 * borrao de N linhas a partir da linha L e pergunta-se a medida onde o borrao
 * comeca -- se ela responder outra coisa, o defeito e dela, nao do arquivo.
 *
 * Isso nao existia, e a falta custou caro: as armadilhas 40 a 43 sao todas da
 * forma "a medida mede outra coisa", e cada uma so apareceu depois de horas
 * decodificando quadro real e olhando. O caso da armadilha 40 esta abaixo e
 * responde em milissegundos.
 *
 *   gcc -O2 -o testes.exe src/testes_juizes.c src/juizes.c -lm && ./testes.exe
 */
#include "juizes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1920
#define H 1080

static int falhas = 0;

static void confere(const char *nome, long obtido, long esperado) {
    if (obtido == esperado) { printf("  ok    %-46s %ld\n", nome, obtido); return; }
    printf("  FALHA %-46s obtido %ld, esperado %ld\n", nome, obtido, esperado);
    falhas++;
}

/* Quadro de teste: imagem com textura ate `ate`, depois borrao.
 *
 *   Y       destino, W*H bytes
 *   ate     linha onde a imagem acaba e o borrao comeca
 *   deriva  quanto cada linha do borrao se afasta da anterior. 0 e copia byte a
 *           byte; 1 e o borrao que DERIVA, que foi o que quebrou a medida. */
static void monta(uint8_t *Y, int ate, int deriva) {
    for (int y = 0; y < ate; y++)
        for (int x = 0; x < W; x++)
            Y[(size_t)y * W + x] = (uint8_t)(40 + ((x * 7 + y * 13) % 60));
    for (int y = ate; y < H; y++)
        for (int x = 0; x < W; x++)
            Y[(size_t)y * W + x] = (uint8_t)(Y[(size_t)(y - 1) * W + x] + deriva);
}

int main(void) {
    uint8_t *Y = malloc((size_t)W * H);
    int nt, mx, ln, lg;

    puts("borrao que COPIA byte a byte (deriva 0)");
    monta(Y, 500, 0);
    juizes_tolerancia(0);
    confere("fronteira, tolerancia 0", fronteira_borrao(Y, W, H, 160), 499);
    juizes_tolerancia(1);
    confere("fronteira, tolerancia 1", fronteira_borrao(Y, W, H, 160), 499);
    estrutura_borrao(Y, W, H, 499, &nt, &mx, &ln, &lg);
    confere("um trecho so", nt, 1);
    confere("cobre ate o fim", ln, H - 500);

    /* A ARMADILHA 40, em duas linhas de teste. O borrao deriva 1 por linha --
     * continua sendo borrao para qualquer olho -- e a tolerancia 0 nao o ve. */
    puts("\nborrao que DERIVA 1 por linha -- armadilha 40");
    monta(Y, 500, 1);
    juizes_tolerancia(0);
    confere("tolerancia 0 NAO acha o borrao", fronteira_borrao(Y, W, H, 160), H);
    juizes_tolerancia(1);
    confere("tolerancia 1 acha", fronteira_borrao(Y, W, H, 160), 499);

    puts("\ntarja: uniforme, valor livre e valor exigido");
    memset(Y, 16, (size_t)W * H);
    confere("baixo uniforme", tarja_baixo_uniforme(Y, W, H), 1);
    confere("baixo e 16", tarja_baixo_e_16(Y, W, H), 1);
    confere("topo uniforme, valor livre", tarja_topo_uniforme(Y, W, H, -1), 1);
    confere("topo uniforme, exige 16", tarja_topo_uniforme(Y, W, H, 16), 1);
    confere("desvio zero", (long)(tarja_baixo_desvio(Y, W, H) * 1000), 0);

    memset(Y, 15, (size_t)W * H);
    confere("tarja 15: uniforme sim", tarja_baixo_uniforme(Y, W, H), 1);
    confere("tarja 15: igual a 16 nao", tarja_baixo_e_16(Y, W, H), 0);
    confere("tarja 15: exigindo 16, nao", tarja_topo_uniforme(Y, W, H, 16), 0);

    /* PONTO CEGO DA AMOSTRAGEM, descoberto por este teste na primeira corrida.
     * A verificacao percorre `x += 8`, entao SETE DE CADA OITO COLUNAS nao sao
     * olhadas: um pixel errado em x=7 passa, o mesmo pixel em x=8 e pego.
     *
     * Nao e defeito -- e a amostragem escolhida, e mudar invalidaria
     * calibracao. Mas estava em lugar nenhum, e agora esta aqui: "tarja
     * uniforme" quer dizer "uniforme nas colunas multiplas de 8". */
    memset(Y, 15, (size_t)W * H);
    Y[(size_t)1000 * W + 7] = 200;
    confere("pixel em x=7 NAO e visto (passo 8)", tarja_baixo_uniforme(Y, W, H), 1);
    memset(Y, 15, (size_t)W * H);
    Y[(size_t)1000 * W + 8] = 200;
    confere("pixel em x=8 e visto", tarja_baixo_uniforme(Y, W, H), 0);

    free(Y);
    printf("\n%s\n", falhas ? "HOUVE FALHA" : "todos passaram");
    return falhas != 0;
}
