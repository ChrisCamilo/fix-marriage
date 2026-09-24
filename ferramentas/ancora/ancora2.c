/* ancora2.c -- plano 2, extensao: varias fileiras de tarja, com os dois
 * contextos da coluna 0 como incognitas.
 *
 * A partir da fileira 63 todo MB fora da coluna 0 usa contextos saturados e
 * iguais nos IDRs integros; o MB da coluna 0 usa mb_type[1] (1o bit, vizinho
 * esquerdo ausente) e bcbp[0][1] (flag do DC, idem), que nunca saturam.
 * Hipotese = (codIRange na entrada, estado de mb_type[1], estado de bcbp[0][1]),
 * com codILow = 0 e 0 bits pendentes; o low da entrada so afeta os primeiros
 * bits e se refina depois com o ancora.c. Compara com o RBSP observado
 * alinhado pelo fim e guarda as menores distancias.
 *
 * Entrada (stdin): n_mbs col0 / 8 contextos saturados / nbits bits
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
static void enc_bin(Enc *e, Ctx *c, int bin) {
    unsigned rlps = rLPS_table_64x4[c->st][(e->range >> 6) & 3];
    e->range -= rlps;
    if (bin != c->mps) { e->low += e->range; e->range = rlps; if (c->st == 0) c->mps = 1 - c->mps; c->st = AC_next_state_LPS_64[c->st]; }
    else c->st = AC_next_state_MPS_64[c->st];
    renorm(e);
}
static void enc_term(Enc *e, int bin) {
    e->range -= 2;
    if (bin) { e->low += e->range; e->range = 2; renorm(e); putbit(e, (e->low >> 9) & 1); e->bits[e->n++] = (e->low >> 8) & 1; e->bits[e->n++] = 1; }
    else renorm(e);
}
typedef struct { int d, len; unsigned range; int k1, k20; } Res;

int main(void) {
    int nmb, col0, cv[8], nobs; static char obs[1 << 16];
    if (scanf("%d %d", &nmb, &col0) != 2) return 1;
    for (int i = 0; i < 8; i++) scanf("%d", &cv[i]);
    scanf("%d %s", &nobs, obs);
    int top = getenv("TOP") ? atoi(getenv("TOP")) : 20;
    Res *best = malloc(sizeof(Res) * top); for (int i = 0; i < top; i++) best[i].d = 1 << 30;
    #pragma omp parallel
    {
        Enc *e = malloc(sizeof(Enc));
        Res *lb = malloc(sizeof(Res) * top); for (int i = 0; i < top; i++) lb[i].d = 1 << 30;
        #pragma omp for schedule(dynamic)
        for (int range = 256; range <= 510; range++)
        for (int k1 = 0; k1 < 126; k1++)
        for (int k20 = 0; k20 < 126; k20++) {
            Ctx c[8], a, b;
            for (int i = 0; i < 8; i++) { c[i].st = cv[i] >> 1; c[i].mps = cv[i] & 1; }
            a.st = k1 >> 1; a.mps = k1 & 1; b.st = k20 >> 1; b.mps = k20 & 1;
            e->low = 0; e->range = range; e->outst = 0; e->n = 0;
            for (int m = 0; m < nmb && e->n < 8000; m++) {
                int col = (col0 + m) % 120;
                enc_bin(e, col == 0 ? &a : &c[0], 1); enc_term(e, 0);
                enc_bin(e, &c[1], 0); enc_bin(e, &c[2], 0); enc_bin(e, &c[3], 1); enc_bin(e, &c[4], 0);
                enc_bin(e, &c[6], 0); enc_bin(e, &c[5], 0);
                enc_bin(e, col == 0 ? &b : &c[7], 0);
                enc_term(e, m == nmb - 1);
            }
            if (e->n > nobs) continue;
            /* os primeiros 12 bits dependem do low da entrada: ficam fora da conta */
            int d = 0, ini = 12; const char *ob = obs + nobs - e->n;
            for (int i = ini; i < e->n && d < lb[top - 1].d; i++) d += (ob[i] - '0') != e->bits[i];
            if (d < lb[top - 1].d) {
                int j = top - 1; while (j > 0 && lb[j - 1].d > d) { lb[j] = lb[j - 1]; j--; }
                lb[j].d = d; lb[j].len = e->n; lb[j].range = range; lb[j].k1 = k1; lb[j].k20 = k20;
            }
        }
        #pragma omp critical
        for (int i = 0; i < top; i++) {
            int d = lb[i].d; if (d >= best[top - 1].d) break;
            int j = top - 1; while (j > 0 && best[j - 1].d > d) { best[j] = best[j - 1]; j--; }
            best[j] = lb[i];
        }
        free(e); free(lb);
    }
    for (int i = 0; i < top; i++)
        printf("dist %d  bits %d  range %u  k1 %d  k20 %d\n", best[i].d, best[i].len, best[i].range, best[i].k1, best[i].k20);
    return 0;
}
