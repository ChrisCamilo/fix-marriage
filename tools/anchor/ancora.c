/* ancora.c -- plano 2 (docs/PLANO_ANCORA_CAUDA.md), versao minima.
 *
 * Recodifica com CABAC a sintaxe conhecida da tarja (MB I16x16, predicao DC,
 * croma DC, cbp 0, mb_qp_delta 0, DC sem coeficiente) do MB de entrada ate o
 * fim do slice, para CADA estado possivel do codificador aritmetico no inicio
 * do MB de entrada (codILow, codIRange, bitsOutstanding), e compara o que sai
 * com os bits observados, alinhados pelo fim (o ultimo bit 1 e o
 * rbsp_stop_one_bit). Imprime os estados de menor distancia de Hamming.
 *
 * Entrada (stdin, uma linha cada):
 *   n_mbs  col0            MBs a codificar (o ultimo leva end_of_slice=1) e a
 *                          coluna do primeiro (so importa se algum cair na 0)
 *   ctx: 8 valores estado*2+MPS, na ordem do JM_MBINFO:
 *        mb_type[2] mb_type[4] mb_type[5] mb_type[7] mb_type[8] dqp[0] cipr[0] bcbp[0]
 *   ctxcol0: 2 valores (mb_type[1], bcbp[1]) -- usados so por MB na coluna 0
 *   nbits e a string de bits observados (RBSP, terminando no stop bit)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabelas_cabac.h"

typedef struct { int st, mps; } Ctx;
typedef struct { unsigned low, range; int outst; int n; unsigned char bits[4096]; } Enc;

static void putbit(Enc *e, int b) {
    e->bits[e->n++] = b;
    while (e->outst > 0) { e->bits[e->n++] = 1 - b; e->outst--; }
}
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
    if (bin != c->mps) {
        e->low += e->range; e->range = rlps;
        if (c->st == 0) c->mps = 1 - c->mps;
        c->st = AC_next_state_LPS_64[c->st];
    } else c->st = AC_next_state_MPS_64[c->st];
    renorm(e);
}
static void enc_term(Enc *e, int bin) {
    e->range -= 2;
    if (bin) {
        e->low += e->range;
        e->range = 2; renorm(e);                 /* EncodeFlush */
        putbit(e, (e->low >> 9) & 1);
        e->bits[e->n++] = (e->low >> 8) & 1;     /* WriteBits(((low>>7)&3)|1, 2) */
        e->bits[e->n++] = 1;                     /* ... o bit 1 e o rbsp_stop_one_bit */
    } else renorm(e);
}

int main(void) {
    int nmb, col0; int cv[8], cc[2]; int nobs; static char obs[1 << 16];
    if (scanf("%d %d", &nmb, &col0) != 2) return 1;
    for (int i = 0; i < 8; i++) scanf("%d", &cv[i]);
    for (int i = 0; i < 2; i++) scanf("%d", &cc[i]);
    scanf("%d %s", &nobs, obs);
    int omax = getenv("OMAX") ? atoi(getenv("OMAX")) : 8;
    int top = getenv("TOP") ? atoi(getenv("TOP")) : 10;
    typedef struct { int d, len; unsigned low, range; int o; } Res;
    Res *best = calloc(top, sizeof(Res)); for (int i = 0; i < top; i++) best[i].d = 1 << 30;
    Enc e;
    for (unsigned range = 256; range <= 510; range++)
    for (unsigned low = 0; low < 1024; low++)
    for (int o = 0; o <= omax; o++) {
        Ctx c[8], k1, k20;
        for (int i = 0; i < 8; i++) { c[i].st = cv[i] >> 1; c[i].mps = cv[i] & 1; }
        k1.st = cc[0] >> 1; k1.mps = cc[0] & 1; k20.st = cc[1] >> 1; k20.mps = cc[1] & 1;
        e.low = low; e.range = range; e.outst = o; e.n = 0;
        for (int m = 0; m < nmb; m++) {
            int col = (col0 + m) % 120;
            enc_bin(&e, col == 0 ? &k1 : &c[0], 1);          /* mb_type b0: nao e I_NxN */
            enc_term(&e, 0);                                 /* nao e I_PCM */
            enc_bin(&e, &c[1], 0);                           /* sem AC */
            enc_bin(&e, &c[2], 0);                           /* cbp croma 0 */
            enc_bin(&e, &c[3], 1);                           /* modo 16x16 = 2 (DC): bits 1,0 */
            enc_bin(&e, &c[4], 0);
            enc_bin(&e, &c[6], 0);                           /* croma DC */
            enc_bin(&e, &c[5], 0);                           /* mb_qp_delta = 0 */
            enc_bin(&e, col == 0 ? &k20 : &c[7], 0);         /* coded_block_flag do DC = 0 */
            enc_term(&e, m == nmb - 1);                      /* end_of_slice_flag */
            if (e.n > 4000) break;
        }
        if (e.n > nobs) continue;
        int d = 0; const char *ob = obs + nobs - e.n;
        for (int i = 0; i < e.n && d < best[top - 1].d; i++) d += (ob[i] - '0') != e.bits[i];
        if (d < best[top - 1].d) {
            int j = top - 1;
            while (j > 0 && best[j - 1].d > d) { best[j] = best[j - 1]; j--; }
            best[j].d = d; best[j].len = e.n; best[j].low = low; best[j].range = range; best[j].o = o;
        }
    }
    for (int i = 0; i < top; i++)
        printf("dist %d  bits %d  low %u range %u outst %d\n", best[i].d, best[i].len, best[i].low, best[i].range, best[i].o);
    return 0;
}
