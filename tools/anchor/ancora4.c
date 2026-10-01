/* ancora4.c -- a tarja de IDR com UMA coluna de modo variante, varrendo TODOS
 * os padroes: qual coluna, com que modo I16x16 e de croma, explica a cauda?
 *
 * O ancora3.c recodifica a tarja com uma coluna variante dada (validado com
 * distancia 0 no 3348, 2333 e 3319 usando os estados reais). Aqui o padrao e
 * incognita tambem: a tarja pura e cada (coluna 0-119, i16 0-3, croma 0-3)
 * diferente de (2, 0), 1.921 padroes. Para cada um, a busca de estado do
 * ancora3.c: codIRange 256-510, k1 e k20 (os dois contextos da coluna 0)
 * exaustivos, e com croma != 0 os estados de cipr[1] e cipr[3] sorteados
 * (NS pares, 8 por padrao). Mesmo codificador, mesma selecao de contexto.
 *
 * Uso no 1773 (2026-10-01): da fileira 66 para cima a tarja pura deixa 3, 9,
 * 22, 26 diferencas, concentradas na coluna 0 -- dano denso ou sintaxe
 * variante (armadilha 61)? Se um padrao fecha a cauda com distancia pequena,
 * as diferencas que sobram sao bits trocados; se nenhum fecha, e dano.
 *
 * Entrada (stdin): n_mbs col0 / 29 contextos (estado*2+MPS, ordem JM_MBINFO)
 *                  / nbits bits (RBSP terminando no stop bit)
 * Variaveis: NS (pares de cipr sorteados, 8), COLS=a:b (faixa de colunas
 *            variantes, 0:119), SO_PURA=1 (so a tarja pura, para conferir).
 * Saida: uma linha por padrao,
 *   "col C i16 I cm M dmin D zeros Z range R k1 K1 k20 K20 cipr1 C1 cipr3 C3 bits N"
 *   (col -1 = tarja pura). O chamador ordena.
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

static int nmb, col0, cv[29];

/* Recodifica a tarja com a coluna vcol em (vi16, vcm) em todas as fileiras --
 * exatamente o codifica() do ancora3.c, com o padrao como argumento. */
static void codifica(Enc *e, int vcol, int vi16, int vcm, int range, int k1, int k20, int c1, int c3) {
    Ctx x[29];
    for (int i = 0; i < 29; i++) { x[i].st = cv[i] >> 1; x[i].mps = cv[i] & 1; }
    x[1].st = k1 >> 1; x[1].mps = k1 & 1; x[20].st = k20 >> 1; x[20].mps = k20 & 1;
    x[16].st = c1 >> 1; x[16].mps = c1 & 1; x[18].st = c3 >> 1; x[18].mps = c3 & 1;
    e->low = 0; e->range = range; e->outst = 0; e->n = 0;
    for (int m = 0; m < nmb && e->n < 8000; m++) {
        int c = (col0 + m) % 120;
        int i16 = c == vcol ? vi16 : 2, cm = c == vcol ? vcm : 0;
        int cme = (c > 0 && c - 1 == vcol) ? vcm : 0;                 /* croma do vizinho da esquerda */
        eb(e, &x[c == 0 ? 1 : 2], 1); et(e, 0);
        eb(e, &x[4], 0); eb(e, &x[5], 0); eb(e, &x[7], i16 >> 1); eb(e, &x[8], i16 & 1);
        int inc = (c > 0 && cme != 0) + (cm != 0);                     /* esquerda + cima (mesma coluna) */
        eb(e, &x[15 + inc], cm != 0);
        if (cm) eb(e, &x[18], cm > 1);
        if (cm > 1) eb(e, &x[18], cm > 2);
        eb(e, &x[11], 0);
        eb(e, &x[c == 0 ? 20 : 19], 0);
        et(e, m == nmb - 1);
    }
}

typedef struct { int col, i16, cm, d; long zeros; int ex[6]; } Res;

int main(void) {
    int nobs; static char obs[1 << 16];
    if (scanf("%d %d", &nmb, &col0) != 2) return 1;
    for (int i = 0; i < 29; i++) scanf("%d", &cv[i]);
    if (scanf("%d %65535s", &nobs, obs) != 2) return 1;
    int ns = getenv("NS") ? atoi(getenv("NS")) : 8;
    int ca = 0, cb = 119;
    if (getenv("COLS")) sscanf(getenv("COLS"), "%d:%d", &ca, &cb);
    int so_pura = getenv("SO_PURA") ? atoi(getenv("SO_PURA")) : 0;
    /* a lista de padroes: a pura primeiro */
    int np = 0; Res *pad = malloc(sizeof(Res) * (1 + 120 * 16));
    pad[np++] = (Res){ -1, 2, 0, 1 << 30, 0, {0} };
    if (!so_pura)
        for (int c = ca; c <= cb; c++) for (int i = 0; i < 4; i++) for (int m = 0; m < 4; m++)
            if (!(i == 2 && m == 0)) pad[np++] = (Res){ c, i, m, 1 << 30, 0, {0} };
    /* paralelo em (padrao, range): 255 tarefas por padrao */
    #pragma omp parallel
    {
        Enc *e = malloc(sizeof(Enc));
        #pragma omp for schedule(dynamic)
        for (long t = 0; t < (long)np * 255; t++) {
            Res *p = &pad[t / 255]; int range = 256 + (int)(t % 255);
            int npar = p->cm ? ns : 1;
            unsigned sd = 2463534242u ^ (unsigned)range * 2654435761u ^ (unsigned)(t / 255) * 40503u;
            int ld = 1 << 30; long lz = 0; int lex[6] = {0};
            for (int k1 = 0; k1 < 126; k1++)
            for (int k20 = 0; k20 < 126; k20++)
            for (int q = 0; q < npar; q++) {
                int c1 = 0, c3 = 0;
                if (p->cm) { sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; c1 = sd % 126; c3 = (sd / 126) % 126; }
                codifica(e, p->col, p->i16, p->cm, range, k1, k20, c1, c3);
                if (e->n > nobs) continue;
                /* os primeiros 12 bits dependem do low da entrada: fora da conta */
                int d = 0; const char *ob = obs + nobs - e->n;
                for (int i = 12; i < e->n && d <= ld; i++) d += (ob[i] - '0') != e->bits[i];
                if (d < ld) { ld = d; lex[0] = range; lex[1] = k1; lex[2] = k20; lex[3] = c1; lex[4] = c3; lex[5] = e->n; }
                if (d == 0) lz++;
            }
            #pragma omp critical
            { p->zeros += lz; if (ld < p->d) { p->d = ld; memcpy(p->ex, lex, sizeof lex); } }
        }
        free(e);
    }
    for (int i = 0; i < np; i++)
        printf("col %d i16 %d cm %d dmin %d zeros %ld range %d k1 %d k20 %d cipr1 %d cipr3 %d bits %d\n",
               pad[i].col, pad[i].i16, pad[i].cm, pad[i].d, pad[i].zeros,
               pad[i].ex[0], pad[i].ex[1], pad[i].ex[2], pad[i].ex[3], pad[i].ex[4], pad[i].ex[5]);
    return 0;
}
