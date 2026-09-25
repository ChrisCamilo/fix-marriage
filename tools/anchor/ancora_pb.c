/* ancora_pb.c -- plano 2 para quadros P e B: a tarja inferior deles e so skip.
 *
 * Medido no GOP 3319 (28 quadros P/B): as fileiras 61-67 sao 840 de 840 MBs
 * skip, e o contexto do flag de skip ja esta saturado (estado 62, MPS 1) na
 * fileira 62. Cada MB da tarja e entao: mb_skip_flag = 1 (o simbolo mais
 * provavel) e end_of_slice_flag = 0 -- so o ultimo leva 1. Nao ha incognita de
 * coluna 0 como no IDR: o contexto do skip depende de o vizinho NAO ser skip,
 * e na tarja todos sao.
 *
 * Com so simbolos mais provaveis, o codificador nunca soma ao codILow: a saida
 * e o low da entrada saindo pelos deslocamentos, zeros, e a terminacao. A
 * hipotese e o codIRange da entrada e o ESTADO do contexto do skip (low = 0,
 * primeiros 12 bits fora da conta, como no ancora2.c). O estado entra como
 * incognita porque o contexto "os dois vizinhos sao skip" so e usado de
 * verdade a partir da fileira 62, e num quadro com movimento pode chegar ali
 * sem saturar: isso muda quantos zeros saem e deslocaria a terminacao --
 * pareceria dano de 1-3 bits sem ser. Com ctx_skip >= 0 fixa o estado; com -1
 * percorre os 126. Agrupa as saidas distintas na distancia minima.
 *
 * Entrada (stdin): n_mbs ctx_skip(estado*2+MPS, ou -1) nbits bits (RBSP ate o stop bit)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabelas_cabac.h"

typedef struct { int st, mps; } Ctx;
typedef struct { unsigned low, range; int outst; int n; unsigned char bits[4096]; } Enc;

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

static int codifica(Enc *e, int range, int cs, int nmb) {
    Ctx c = { cs >> 1, cs & 1 };
    e->low = 0; e->range = range; e->outst = 0; e->n = 0;
    for (int m = 0; m < nmb; m++) { enc_bin(e, &c, 1); enc_term(e, m == nmb - 1); }
    return e->n;
}

int main(void) {
    int nmb, cs0, nobs; static char obs[1 << 16];
    if (scanf("%d %d %d %s", &nmb, &cs0, &nobs, obs) != 4) return 1;
    int c_ini = cs0 >= 0 ? cs0 : 0, c_fim = cs0 >= 0 ? cs0 : 125;
    static Enc e;
    int dmin = 1 << 30;
    for (int cs = c_ini; cs <= c_fim; cs++)
    for (int range = 256; range <= 510; range++) {
        int n = codifica(&e, range, cs, nmb);
        if (n > nobs) continue;
        int d = 0; const char *ob = obs + nobs - n;
        for (int i = 12; i < n; i++) d += (ob[i] - '0') != e.bits[i];
        if (d < dmin) dmin = d;
    }
    /* saidas distintas na distancia minima */
    enum { MAXDIST = 256 };
    unsigned long long hsh[MAXDIST]; int hr[MAXDIST], hc[MAXDIST], hn[MAXDIST], hb[MAXDIST], nd = 0, ntot = 0;
    for (int cs = c_ini; cs <= c_fim; cs++)
    for (int range = 256; range <= 510; range++) {
        int n = codifica(&e, range, cs, nmb);
        if (n > nobs) continue;
        int d = 0; const char *ob = obs + nobs - n;
        for (int i = 12; i < n; i++) d += (ob[i] - '0') != e.bits[i];
        if (d != dmin) continue;
        unsigned long long h = 1469598103934665603ULL ^ (unsigned long long)n;
        for (int i = 12; i < n; i++) h = (h ^ e.bits[i]) * 1099511628211ULL;
        ntot++;
        int j; for (j = 0; j < nd; j++) if (hsh[j] == h) { hn[j]++; break; }
        if (j == nd && nd < MAXDIST) { hsh[nd] = h; hr[nd] = range; hc[nd] = cs; hn[nd] = 1; hb[nd] = n; nd++; }
    }
    printf("dist %d  empatadas %d  saidas_distintas %d\n", dmin, ntot, nd);
    for (int j = 0; j < nd; j++) printf("distinta %d  range %d  bits %d  ctx %d\n", hn[j], hr[j], hb[j], hc[j]);
    return 0;
}
