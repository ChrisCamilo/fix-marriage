/* ancora3.c -- a tarja de IDR com UMA coluna de modo variante: existe
 * explicacao sem troca de bit?
 *
 * O ancora2.c supoe todo MB da tarja em I16x16 DC com croma DC. O IDR integro
 * 3348 tem a coluna 1 em croma modo 2 em todas as fileiras de tarja (armadilha
 * 61), e o modelo puro fica a 23 bits dele. Este programa recodifica a tarja
 * com uma coluna `col` em (modo I16x16 `i16`, modo de croma `cm`) em todas as
 * fileiras, com a selecao de contexto do JM (ver cabac_enc2.py, validado com
 * distancia 0 no 3348, 2333 e 3319 usando os estados reais).
 *
 * Incognitas: codIRange da entrada (256-510), estado de mb_type[0][1] (k1, o
 * bin 0 da coluna 0) e de bcbp[0][1] (k20, o CBF do DC da coluna 0) --
 * exaustivas, como no ancora2.c -- e, se cm != 0, os estados de cipr[1] e
 * cipr[3], que so a coluna variante e a vizinha da direita usam: SORTEADOS,
 * NS pares por hipotese (NS=0 percorre os 126x126, caro). O resto dos
 * contextos vem saturado da entrada, como no ancora2.c.
 *
 * Serve para AUDITAR correcao de cauda: se a cauda sem a correcao encaixa com
 * distancia 0 numa sintaxe variante, a correcao nao esta provada. Para no
 * primeiro encaixe a distancia 0 (PARA=0 continua e conta todos). C1/C3 fixam
 * os dois estados de cipr (validacao com os estados que o JM grava); R, K1 e
 * K20 fixam as outras tres. Com NS=0 e C1/C3 livres, os 126x126 de cipr sao
 * percorridos -- barato so com R/K1/K20 fixos (refinamento por coordenadas).
 *
 * Entrada (stdin): n_mbs col0 / 29 contextos (estado*2+MPS, ordem JM_MBINFO)
 *                  / col i16 cm / nbits bits
 * Saida: "dmin D  zeros Z  exemplo range R k1 K1 k20 K20 cipr1 C1 cipr3 C3"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabelas_cabac.h"

typedef struct { int st, mps; } Ctx;
typedef struct { unsigned low, range; int outst; int n; unsigned char bits[8192]; } Enc;

static void putbit(Enc *e, int b) { e->bits[e->n++] = b; while (e->outst > 0) { e->bits[e->n++] = 1 - b; e->outst--; } }
static void renorm(Enc *e) {
    while (e->range < 256) {
        if (e->low < 256) putbit(e, 0);
        else if (e->low >= 512) { e->low -= 512; putbit(e, 1); }
        else { e->low -= 256; e->outst++; }
        e->range <<= 1; e->low <<= 1;
    }
}
static void eb(Enc *e, Ctx *c, int bin) {
    unsigned rlps = rLPS_table_64x4[c->st][(e->range >> 6) & 3];
    e->range -= rlps;
    if (bin != c->mps) { e->low += e->range; e->range = rlps; if (c->st == 0) c->mps = 1 - c->mps; c->st = AC_next_state_LPS_64[c->st]; }
    else c->st = AC_next_state_MPS_64[c->st];
    renorm(e);
}
static void et(Enc *e, int bin) {
    e->range -= 2;
    if (bin) { e->low += e->range; e->range = 2; renorm(e); putbit(e, (e->low >> 9) & 1); e->bits[e->n++] = (e->low >> 8) & 1; e->bits[e->n++] = 1; }
    else renorm(e);
}

static int nmb, col0, cv[29], vcol, vi16, vcm;

/* sintaxe da tarja: (modo i16, modo croma) de cada coluna -- a variante em todas as fileiras */
#define I16(c) ((c) == vcol ? vi16 : 2)
#define CM(c)  ((c) == vcol ? vcm : 0)

static void codifica(Enc *e, int range, int k1, int k20, int c1, int c3) {
    Ctx x[29];
    for (int i = 0; i < 29; i++) { x[i].st = cv[i] >> 1; x[i].mps = cv[i] & 1; }
    x[1].st = k1 >> 1; x[1].mps = k1 & 1; x[20].st = k20 >> 1; x[20].mps = k20 & 1;
    x[16].st = c1 >> 1; x[16].mps = c1 & 1; x[18].st = c3 >> 1; x[18].mps = c3 & 1;
    e->low = 0; e->range = range; e->outst = 0; e->n = 0;
    for (int m = 0; m < nmb && e->n < 8000; m++) {
        int c = (col0 + m) % 120, i16 = I16(c), cm = CM(c);
        eb(e, &x[c == 0 ? 1 : 2], 1); et(e, 0);
        eb(e, &x[4], 0); eb(e, &x[5], 0); eb(e, &x[7], i16 >> 1); eb(e, &x[8], i16 & 1);
        int inc = (c > 0 && CM(c - 1) != 0) + (CM(c) != 0);    /* esquerda + cima (mesma coluna, fileira de cima) */
        eb(e, &x[15 + inc], cm != 0);
        if (cm) eb(e, &x[18], cm > 1);
        if (cm > 1) eb(e, &x[18], cm > 2);
        eb(e, &x[11], 0);
        eb(e, &x[c == 0 ? 20 : 19], 0);
        et(e, m == nmb - 1);
    }
}

int main(void) {
    int nobs; static char obs[1 << 16];
    if (scanf("%d %d", &nmb, &col0) != 2) return 1;
    for (int i = 0; i < 29; i++) scanf("%d", &cv[i]);
    scanf("%d %d %d", &vcol, &vi16, &vcm);
    scanf("%d %s", &nobs, obs);
    int ns = getenv("NS") ? atoi(getenv("NS")) : 8;
    int para = getenv("PARA") ? atoi(getenv("PARA")) : 1;
    int fc1 = getenv("C1") ? atoi(getenv("C1")) : -1, fc3 = getenv("C3") ? atoi(getenv("C3")) : -1;
    int npar = vcm && fc1 < 0 ? (ns > 0 ? ns : 126 * 126) : 1;
    /* R, K1, K20 fixam as incognitas exaustivas (refinamento por coordenadas:
     * fixa a melhor hipotese e varre as outras) */
    int r0 = getenv("R") ? atoi(getenv("R")) : 256, r1 = getenv("R") ? r0 : 510;
    int a0 = getenv("K1") ? atoi(getenv("K1")) : 0, a1 = getenv("K1") ? a0 : 125;
    int b0 = getenv("K20") ? atoi(getenv("K20")) : 0, b1 = getenv("K20") ? b0 : 125;
    int dmin = 1 << 30; long zeros = 0; int ex[6] = {0}; volatile int achou = 0;
    #pragma omp parallel
    {
        Enc *e = malloc(sizeof(Enc)); int ld = 1 << 30; long lz = 0; int lex[6] = {0};
        #pragma omp for schedule(dynamic)
        for (int range = r0; range <= r1; range++) {
            unsigned sd = 2463534242u ^ (unsigned)range * 2654435761u;
            for (int k1 = a0; k1 <= a1 && !(para && achou); k1++)
            for (int k20 = b0; k20 <= b1; k20++)
            for (int p = 0; p < npar; p++) {
                int c1 = 0, c3 = 0;
                if (vcm && fc1 >= 0) { c1 = fc1; c3 = fc3; }
                else if (vcm) {
                    if (ns > 0) { sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; c1 = sd % 126; c3 = (sd / 126) % 126; }
                    else { c1 = p / 126; c3 = p % 126; }
                }
                codifica(e, range, k1, k20, c1, c3);
                if (e->n > nobs) continue;
                int d = 0; const char *ob = obs + nobs - e->n;
                for (int i = 12; i < e->n && d <= ld; i++) d += (ob[i] - '0') != e->bits[i];
                if (d < ld) { ld = d; lex[0] = range; lex[1] = k1; lex[2] = k20; lex[3] = c1; lex[4] = c3; lex[5] = e->n; }
                if (d == 0) { lz++; if (para) achou = 1; }
            }
        }
        #pragma omp critical
        { zeros += lz; if (ld < dmin) { dmin = ld; memcpy(ex, lex, sizeof ex); } }
        free(e);
    }
    printf("dmin %d  zeros %ld  exemplo range %d k1 %d k20 %d cipr1 %d cipr3 %d bits %d\n",
           dmin, zeros, ex[0], ex[1], ex[2], ex[3], ex[4], ex[5]);
    return 0;
}
