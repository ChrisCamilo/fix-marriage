/* reparador.c - conserta bits corrompidos em H.264 dentro de um MP4
 *
 * Principio de projeto: o MP4 ORIGINAL nunca e modificado. Todo conserto vira
 * uma linha num arquivo de patches (offset absoluto + bit). O MP4 reparado e
 * um artefato derivado, sempre reconstruivel.
 *
 * Compilar:
 *   gcc -O2 -o reparador reparador.c $(pkg-config --cflags --libs libavcodec libavutil)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

#define MAXS 8192

typedef struct { long off; int size; int idr; } Amostra;
static Amostra ix[MAXS];
static int n_ix = 0;

static uint8_t *arq = NULL;      /* copia do MP4 em memoria, com patches */
static long arq_len = 0;

/* ---- captura do log do decoder ----
 * Por thread: o callback do av_log e global, mas cada worker decodifica no
 * proprio contexto e o libavcodec (com thread_count=1) chama o callback na
 * mesma thread que decodifica. */
static _Thread_local int log_erros = 0;
static _Thread_local long log_bytestream = -1;
static _Thread_local int log_ocultados = -1;

static void meu_log(void *avcl, int nivel, const char *fmt, va_list vl) {
    if (nivel > AV_LOG_ERROR) return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    long bs; int mb1, mb2, oc;
    if (sscanf(buf, "error while decoding MB %d %d, bytestream %ld",
               &mb1, &mb2, &bs) == 3) log_bytestream = bs;
    if (sscanf(buf, "concealing %d DC", &oc) == 1) log_ocultados = oc;
    log_erros++;
}
static void log_zerar(void) { log_erros = 0; log_bytestream = -1; log_ocultados = -1; }

/* ---- decodificador ----
 * `ctx` e por thread; `extradata` e compartilhado e so-leitura apos monta_avcc. */
static _Thread_local AVCodecContext *ctx = NULL;
static uint8_t extradata[256];
static int extradata_len = 0;

static void monta_avcc(const char *sps_hex, const char *pps_hex) {
    uint8_t sps[64], pps[64]; int ls = 0, lp = 0;
    for (const char *p = sps_hex; p[0] && p[1]; p += 2)
        sscanf(p, "%2hhx", &sps[ls++]);
    for (const char *p = pps_hex; p[0] && p[1]; p += 2)
        sscanf(p, "%2hhx", &pps[lp++]);
    uint8_t *e = extradata; int k = 0;
    e[k++] = 1; e[k++] = sps[1]; e[k++] = sps[2]; e[k++] = sps[3];
    e[k++] = 0xFF; e[k++] = 0xE1;
    e[k++] = ls >> 8; e[k++] = ls & 0xFF;
    memcpy(e + k, sps, ls); k += ls;
    e[k++] = 1; e[k++] = lp >> 8; e[k++] = lp & 0xFF;
    memcpy(e + k, pps, lp); k += lp;
    extradata_len = k;
}

static void abre_decoder(void) {
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
    ctx = avcodec_alloc_context3(c);
    ctx->extradata = av_mallocz(extradata_len + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(ctx->extradata, extradata, extradata_len);
    ctx->extradata_size = extradata_len;
    ctx->thread_count = 1;                 /* determinismo acima de velocidade */
    /* Detectar slice que termina cedo. Com err_recognition=0 o ffmpeg aceitava
     * em silencio uma slice que decodificava so as primeiras fileiras de
     * macrobloco e propagava o resto -- e o criterio dava esse frame como
     * perfeito. EF_RECOG=0 volta ao comportamento antigo, para comparacao. */
    ctx->err_recognition = getenv("EF_RECOG") ? atoi(getenv("EF_RECOG"))
                                              : (AV_EF_EXPLODE | AV_EF_BITSTREAM);
    avcodec_open2(ctx, c, NULL);
}

/* ---- captura da imagem decodificada ----
 * Quando `cap_buf` esta setado, o plano Y do ultimo quadro recebido e copiado
 * para la. Serve ao criterio visual: a sintaxe nao distingue o bit certo, mas
 * a imagem sim. */
static _Thread_local uint8_t *cap_buf = NULL;
static _Thread_local int cap_w = 0, cap_h = 0;

static void captura_frame(AVFrame *fr) {
    if (!cap_buf || fr->width <= 0) return;
    cap_w = fr->width; cap_h = fr->height;
    for (int y = 0; y < cap_h; y++)
        memcpy(cap_buf + (size_t)y * cap_w, fr->data[0] + (size_t)y * fr->linesize[0], cap_w);
}

/* Blocagem: descontinuidade na grade 16x16 do macrobloco contra a do interior.
 * Num decode correto o deblocking deixa a razao perto de 1; residuo corrompido
 * cria degraus nas bordas e a razao sobe. Nao precisa de imagem de referencia. */
static double blocagem(const uint8_t *Y, int w, int h) {
    double borda = 0, interior = 0; long nb = 0, ni = 0;
    for (int y = 0; y < h; y++)
        for (int x = 16; x < w; x++) {
            int d = abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)y*w+x-1]);
            if (x % 16 == 0)      { borda += d; nb++; }
            else if (x % 16 == 8) { interior += d; ni++; }
        }
    for (int y = 16; y < h; y++)
        for (int x = 0; x < w; x++) {
            int d = abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)(y-1)*w+x]);
            if (y % 16 == 0)      { borda += d; nb++; }
            else if (y % 16 == 8) { interior += d; ni++; }
        }
    if (!nb || !ni || interior <= 0) return 0;
    return (borda / nb) / (interior / ni);
}

/* Fracao de linhas da metade de baixo que sao praticamente copia da linha
 * acima. Quando a slice termina cedo, o decoder propaga verticalmente o ultimo
 * macrobloco decodificado e a imagem vira listras: quase toda linha repete a
 * anterior. Numa imagem real isso e raro. E o sinal que o criterio sintatico
 * nao da -- ele aceita a slice truncada em silencio. */
static double propagacao(const uint8_t *Y, int w, int h) {
    int y0 = h / 2, repet = 0, total = 0;
    for (int y = y0; y < h; y++) {
        long s = 0;
        for (int x = 0; x < w; x++)
            s += abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)(y-1)*w+x]);
        if ((double)s / w < 1.0) repet++;
        total++;
    }
    return total ? (double)repet / total : 0;
}

/* Diferenca entre duas imagens Y: quanto o flip de fato estragou. */
static void difere(const uint8_t *A, const uint8_t *B, int w, int h,
                   double *media, int *maxd, double *frac) {
    double s = 0; int mx = 0; long mud = 0;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        int d = abs((int)A[i] - (int)B[i]);
        s += d; if (d > mx) mx = d; if (d) mud++;
    }
    *media = s / n; *maxd = mx; *frac = (double)mud / n;
}

/* Decodifica a cadeia [ancora..alvo]. Opcionalmente troca o payload do alvo.
 * Devolve 0 se TUDO saiu perfeito: nenhum log de erro e o numero de quadros
 * emitidos (apos flush) igual ao numero de pacotes enviados.
 * O flush e essencial: sem ele a reordenacao de B-frames mascara falhas. */
static int decodifica(int ancora, int alvo, const uint8_t *alt, int alt_len,
                      int *quadros_out) {
    abre_decoder();
    log_zerar();
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    int enviados = 0, quadros = 0;
    for (int i = ancora; i <= alvo; i++) {
        const uint8_t *src; int len;
        if (i == alvo && alt) { src = alt; len = alt_len; }
        else { src = arq + ix[i].off; len = ix[i].size; }
        av_new_packet(pkt, len);
        memcpy(pkt->data, src, len);
        if (avcodec_send_packet(ctx, pkt) == 0) enviados++;
        av_packet_unref(pkt);
        while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
    }
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
    av_frame_free(&fr); av_packet_free(&pkt);
    if (quadros_out) *quadros_out = quadros;
    int esperado = alvo - ancora + 1;
    return (log_erros == 0 && quadros == esperado) ? 0 : 1;
}

static int ancora_de(int alvo) {
    for (int i = alvo; i >= 0; i--) if (ix[i].idr) return i;
    return 0;
}

/* ---- busca do byte culpado ---- */
static int acha_corte(int ancora, int alvo) {
    /* 1) o proprio decoder costuma informar quantos bytes sobraram */
    decodifica(ancora, alvo, NULL, 0, NULL);
    int len = ix[alvo].size;
    if (log_bytestream >= 0 && log_bytestream < len)
        return len - (int)log_bytestream;
    /* 2) senao, busca binaria truncando o NAL do alvo */
    uint8_t *tmp = malloc(len);
    memcpy(tmp, arq + ix[alvo].off, len);
    decodifica(ancora, alvo, NULL, 0, NULL);
    int ref_oc = log_ocultados, ref_er = log_erros;
    int lo = 5, hi = len;
    while (lo < hi) {
        int md = (lo + hi) / 2;
        tmp[0] = (md - 4) >> 24; tmp[1] = (md - 4) >> 16;
        tmp[2] = (md - 4) >> 8;  tmp[3] = (md - 4);
        decodifica(ancora, alvo, tmp, md, NULL);
        if (log_ocultados == ref_oc && log_erros == ref_er) hi = md; else lo = md + 1;
    }
    free(tmp);
    return lo;
}

/* Procura um flip de 1 bit que faca a cadeia inteira ficar perfeita.
 * Devolve o offset no payload (>=0) e escreve o bit em *bit_out. */
static int repara(int alvo, int janela, int *bit_out) {
    int ancora = ancora_de(alvo);
    if (decodifica(ancora, alvo, NULL, 0, NULL) == 0) return -2;  /* ja esta bom */
    int corte = acha_corte(ancora, alvo);
    int len = ix[alvo].size;
    uint8_t *base = arq + ix[alvo].off;
    int ini = corte - janela; if (ini < 0) ini = 0;
    int fim = corte + 3;      if (fim > len) fim = len;
    for (int off = fim - 1; off >= ini; off--) {      /* do corte para tras */
        uint8_t o = base[off];
        for (int b = 0; b < 8; b++) {
            base[off] = o ^ (1 << b);
            if (decodifica(ancora, alvo, NULL, 0, NULL) == 0) {
                base[off] = o;
                *bit_out = b;
                return off;
            }
            base[off] = o;
        }
    }
    return -1;
}

/* Busca de 1 bit restrita a [ini,fim). Nao grava nada. */
static int busca1(int ancora, int alvo, int ini, int fim, int *off_out, int *bit_out) {
    uint8_t *base = arq + ix[alvo].off;
    for (int off = fim - 1; off >= ini; off--) {
        uint8_t o = base[off];
        for (int b = 0; b < 8; b++) {
            base[off] = o ^ (1 << b);
            int r = decodifica(ancora, alvo, NULL, 0, NULL);
            base[off] = o;
            if (r == 0) { *off_out = off; *bit_out = b; return 1; }
        }
    }
    return 0;
}

/* Busca de 2 bits em [ini,fim). Custo C(n,2) decodificacoes: manter estreita. */
static int busca2(int ancora, int alvo, int ini, int fim,
                  int *o1, int *b1, int *o2, int *b2) {
    uint8_t *base = arq + ix[alvo].off;
    int nb = (fim - ini) * 8;
    for (int p = 0; p < nb; p++) {
        int f1 = ini + p / 8, t1 = p % 8;
        base[f1] ^= (1 << t1);
        for (int q = p + 1; q < nb; q++) {
            int f2 = ini + q / 8, t2 = q % 8;
            base[f2] ^= (1 << t2);
            int r = decodifica(ancora, alvo, NULL, 0, NULL);
            base[f2] ^= (1 << t2);
            if (r == 0) {
                base[f1] ^= (1 << t1);
                *o1 = f1; *b1 = t1; *o2 = f2; *b2 = t2;
                return 1;
            }
        }
        base[f1] ^= (1 << t1);
    }
    return 0;
}

/* Varre [ini,fim) SEM parar na primeira solucao e acumula todas.
 * Serve para responder se o conserto de 1 bit e unico: se houver mais de uma
 * solucao, o criterio de decodificacao perfeita nao distingue a correta. */
static int conta_solucoes(int ancora, int alvo, int ini, int fim,
                          int *offs, int *bits, int max, int *guardados) {
    uint8_t *base = arq + ix[alvo].off;
    int n = 0;
    for (int off = ini; off < fim; off++) {
        uint8_t o = base[off];
        for (int b = 0; b < 8; b++) {
            base[off] = o ^ (1 << b);
            int r = decodifica(ancora, alvo, NULL, 0, NULL);
            base[off] = o;
            if (r == 0) {
                if (*guardados < max) {
                    offs[*guardados] = off; bits[*guardados] = b; (*guardados)++;
                }
                n++;              /* conta todas, mesmo alem do que cabe guardar */
            }
        }
    }
    return n;
}

/* ---- enumeracao exaustiva paralela ----
 * Cada worker tem decoder proprio (_Thread_local) e sua copia mutavel do NAL,
 * passada via o parametro `alt` que ja existia: nada compartilhado e escrito,
 * `arq` fica so-leitura. Os candidatos saem de um contador atomico, e nao de
 * blocos fixos, porque o custo varia muito com a posicao do bit -- um flip no
 * inicio faz o decoder abortar cedo, no fim faz percorrer o frame inteiro.
 *
 * Determinismo: no fim as solucoes sao ordenadas pelo indice do candidato, que
 * e a mesma ordem da varredura sequencial. O resultado e identico para qualquer
 * numero de threads, por construcao e nao por sorte de escalonamento. */

typedef struct { int idx, off, bit; double m; } Sol;

typedef struct {
    int ancora, alvo, ini;
    int quebra, primeiro, metrica;
    Sol *sol; int n, cap;
    int total;
} Worker;

static _Atomic int prox_cand;
static _Atomic int menor_aceito;
static int n_cand;

static void *worker_varre(void *p) {
    Worker *w = p;
    int len = ix[w->alvo].size;
    uint8_t *copia = malloc(len);
    memcpy(copia, arq + ix[w->alvo].off, len);
    if (w->metrica) cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_cand) break;
        /* Regra do menor indice: com parada antecipada, so interessa candidato
         * anterior ao melhor ja achado. Garante o mesmo resultado da varredura
         * sequencial, independente do escalonamento. */
        if (w->primeiro && k >= atomic_load(&menor_aceito)) break;
        int off = w->ini + k / 8, bit = k % 8;
        copia[off] ^= (1 << bit);
        int r = decodifica(w->ancora, w->alvo, copia, len, NULL);
        int aceita = w->quebra ? (r != 0) : (r == 0);
        double m = (aceita && w->metrica && r == 0) ? blocagem(cap_buf, cap_w, cap_h) : 0;
        copia[off] ^= (1 << bit);
        if (aceita) {
            w->total++;
            if (w->n < w->cap) {
                w->sol[w->n].idx = k; w->sol[w->n].off = off;
                w->sol[w->n].bit = bit; w->sol[w->n].m = m; w->n++;
            }
            if (w->primeiro) {
                int cur = atomic_load(&menor_aceito);
                while (k < cur && !atomic_compare_exchange_weak(&menor_aceito, &cur, k)) { }
            }
        }
    }
    free(copia);
    if (cap_buf) { free(cap_buf); cap_buf = NULL; }
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

static int cmp_sol(const void *a, const void *b) {
    int x = ((const Sol *)a)->idx, y = ((const Sol *)b)->idx;
    return (x > y) - (x < y);
}

/* Varre [ini,fim) em paralelo. `quebra`: aceita quem QUEBRA o decode em vez de
 * quem fecha. `primeiro`: para no menor indice aceito. `metrica`: calcula a
 * blocagem da imagem de cada aceito. Devolve o total aceito e preenche `saida`
 * ordenada por indice, que e a ordem da varredura sequencial. */
static int varre_par(int ancora, int alvo, int ini, int fim,
                     int quebra, int primeiro, int metrica,
                     Sol *saida, int max_saida, int nthr) {
    if (fim <= ini) return 0;
    n_cand = (fim - ini) * 8;
    atomic_store(&prox_cand, 0);
    atomic_store(&menor_aceito, n_cand);

    int cap = primeiro ? 64 : (max_saida < 4096 ? 4096 : max_saida);
    Worker *w = calloc(nthr, sizeof *w);
    pthread_t *th = calloc(nthr, sizeof *th);
    for (int i = 0; i < nthr; i++) {
        w[i].ancora = ancora; w[i].alvo = alvo; w[i].ini = ini;
        w[i].quebra = quebra; w[i].primeiro = primeiro; w[i].metrica = metrica;
        w[i].cap = cap; w[i].sol = malloc((size_t)cap * sizeof(Sol));
        pthread_create(&th[i], NULL, worker_varre, &w[i]);
    }
    int total = 0, nsol = 0;
    for (int i = 0; i < nthr; i++) {
        pthread_join(th[i], NULL);
        total += w[i].total; nsol += w[i].n;
    }
    Sol *todas = malloc((size_t)(nsol ? nsol : 1) * sizeof(Sol));
    int k = 0;
    for (int i = 0; i < nthr; i++)
        for (int j = 0; j < w[i].n; j++) todas[k++] = w[i].sol[j];
    qsort(todas, nsol, sizeof(Sol), cmp_sol);
    for (int i = 0; i < nsol && i < max_saida; i++) saida[i] = todas[i];
    if (primeiro && nsol > 1) total = 1;      /* so o menor indice conta */
    free(todas);
    for (int i = 0; i < nthr; i++) free(w[i].sol);
    free(w); free(th);
    return total;
}

static int conta_solucoes_par(int ancora, int alvo, int ini, int fim,
                              int *offs, int *bits, int max, int *guardados,
                              int nthr) {
    if (fim <= ini) return 0;
    int cap = (fim - ini) * 8;
    Sol *s = malloc((size_t)cap * sizeof(Sol));
    int total = varre_par(ancora, alvo, ini, fim, 0, 0, 0, s, cap, nthr);
    int n = total < cap ? total : cap;
    for (int i = 0; i < n; i++)
        if (*guardados < max) {
            offs[*guardados] = s[i].off; bits[*guardados] = s[i].bit; (*guardados)++;
        }
    free(s);
    return total;
}

/* Despacha entre sequencial e paralelo. THREADS=1 cai no codigo sequencial
 * original, o que torna a comparacao A/B honesta: mesma maquina, mesmos dados. */
static int conta(int ancora, int alvo, int ini, int fim,
                 int *offs, int *bits, int max, int *g, int nthr) {
    return nthr <= 1
        ? conta_solucoes(ancora, alvo, ini, fim, offs, bits, max, g)
        : conta_solucoes_par(ancora, alvo, ini, fim, offs, bits, max, g, nthr);
}

/* Default 12, teto em nucleos-2 para nao saturar a maquina em corridas longas.
 * THREADS=1 usa o caminho sequencial original, para a comparacao A/B. */
static int quantas_threads(void) {
    const char *s = getenv("NUMBER_OF_PROCESSORS");
    int nc = s ? atoi(s) : 4; if (nc < 1) nc = 4;
    int n = getenv("THREADS") ? atoi(getenv("THREADS")) : 12;
    if (n > nc - 2) n = nc - 2;
    if (n < 1) n = 1;
    return n;
}

/* ---- patches ---- */
typedef struct { long off; int bit; } Patch;
static Patch patches[200000];
static int n_patch = 0;

static void carrega_patches(const char *fn) {
    FILE *f = fopen(fn, "r");
    if (!f) return;
    long o; int b;
    while (fscanf(f, "%ld %d", &o, &b) == 2) {
        patches[n_patch].off = o; patches[n_patch].bit = b; n_patch++;
        arq[o] ^= (1 << b);
    }
    fclose(f);
    fprintf(stderr, "[+] %d patches carregados de %s\n", n_patch, fn);
}
static void grava_patch(const char *fn, long off, int bit) {
    FILE *f = fopen(fn, "ab");   /* binario: mantem LF tambem no Windows */
    fprintf(f, "%ld %d\n", off, bit);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
          "uso: %s <mp4> <index.txt> <patches.txt> [modo] [args]\n"
          "  modos:\n"
          "    repair <ini> <fim> [janela]   conserta os frames do intervalo\n"
          "    verify                        revalida cada patch, um a um\n"
          "    report                        estado de cada frame\n"
          "    idr [j1] [j2] [max]           diagnostica IDRs quebrados (nao grava):\n"
          "                                  1 bit no corte, 1 bit no inicio, 2 bits\n", argv[0]);
        return 1;
    }
    const char *f_mp4 = argv[1], *f_ix = argv[2], *f_pt = argv[3];
    const char *modo = argc > 4 ? argv[4] : "report";

    FILE *f = fopen(f_mp4, "rb");
    fseek(f, 0, SEEK_END); arq_len = ftell(f); fseek(f, 0, SEEK_SET);
    arq = malloc(arq_len);
    if (fread(arq, 1, arq_len, f) != (size_t)arq_len) { perror("read"); return 1; }
    fclose(f);

    f = fopen(f_ix, "r");
    int i; long off; int size, idr;
    while (fscanf(f, "%d %ld %d %d", &i, &off, &size, &idr) == 4) {
        ix[n_ix].off = off; ix[n_ix].size = size; ix[n_ix].idr = idr; n_ix++;
    }
    fclose(f);
    fprintf(stderr, "[+] %d amostras no indice, %ld bytes de arquivo\n", n_ix, arq_len);

    monta_avcc(getenv("SPS") ? getenv("SPS")
               : "674d4029965200f0044fcb29010101400000fa40003a9821",
               getenv("PPS") ? getenv("PPS") : "68eb7352");
    av_log_set_callback(meu_log);
    carrega_patches(f_pt);

    if (!strcmp(modo, "repair")) {
        int a = atoi(argv[5]), b = atoi(argv[6]);
        int janela = argc > 7 ? atoi(argv[7]) : 4096;
        int ok = 0, duro = 0, jaok = 0;
        for (int t = a; t <= b && t < n_ix; t++) {
            int bit, r = repara(t, janela, &bit);
            if (r == -2) { jaok++; continue; }
            if (r >= 0) {
                long abs = ix[t].off + r;
                arq[abs] ^= (1 << bit);
                grava_patch(f_pt, abs, bit);
                ok++;
                printf("frame %5d: REPARADO offset %ld bit %d (byte %d de %d)\n",
                       t, abs, bit, r, ix[t].size);
            } else {
                duro++;
                printf("frame %5d: sem solucao de 1 bit na janela\n", t);
            }
            fflush(stdout);
        }
        printf("\n[+] ja estavam bons: %d | reparados: %d | duros: %d\n",
               jaok, ok, duro);
    }
    else if (!strcmp(modo, "verify")) {
        /* testa cada patch isoladamente: sem ele o frame quebra? com ele fecha? */
        int bons = 0, falsos = 0;
        int base_n = getenv("BASE_N") ? atoi(getenv("BASE_N")) : 0;
        for (int k = base_n; k < n_patch; k++) {
            long o = patches[k].off;
            int alvo = -1;
            for (int t = 0; t < n_ix; t++)
                if (o >= ix[t].off && o < ix[t].off + ix[t].size) { alvo = t; break; }
            if (alvo < 0) continue;
            int anc = ancora_de(alvo);
            int com = decodifica(anc, alvo, NULL, 0, NULL);
            arq[o] ^= (1 << patches[k].bit);
            int sem = decodifica(anc, alvo, NULL, 0, NULL);
            arq[o] ^= (1 << patches[k].bit);
            if (com == 0 && sem != 0) { bons++; }
            else {
                falsos++;
                printf("FALSO patch %ld bit %d (frame %d): com=%d sem=%d\n",
                       o, patches[k].bit, alvo, com, sem);
            }
        }
        printf("\n[+] patches validos: %d | falsos: %d\n", bons, falsos);
    }
    /* Diagnostico dos IDRs quebrados. Um IDR e sua propria ancora, entao cada
     * teste custa 1 decodificacao. Nao grava patches: so mede. */
    else if (!strcmp(modo, "idr")) {
        int j1  = argc > 5 ? atoi(argv[5]) : 4096;   /* janela da busca de 1 bit */
        int j2  = argc > 6 ? atoi(argv[6]) : 24;     /* janela da busca de 2 bits */
        int max = argc > 7 ? atoi(argv[7]) : 0;      /* 0 = todos */
        int n_um = 0, n_ini = 0, n_dois = 0, n_sem = 0, n_ok = 0, feitos = 0;

        for (int t = 0; t < n_ix; t++) {
            if (!ix[t].idr) continue;
            if (decodifica(t, t, NULL, 0, NULL) == 0) { n_ok++; continue; }
            if (max && feitos >= max) break;
            feitos++;

            int len = ix[t].size, corte = acha_corte(t, t);
            clock_t t0 = clock();
            int off, bit, o1, b1, o2, b2;
            const char *via = NULL; char det[128];

            int ini = corte - j1; if (ini < 0) ini = 0;
            int fim = corte + 3;  if (fim > len) fim = len;
            if (busca1(t, t, ini, fim, &off, &bit)) {
                via = "1bit@corte"; snprintf(det, sizeof det, "off %d bit %d", off, bit);
            } else {
                /* Melhoria 1: o inicio do NAL. Quando o corte cai antes do byte
                 * ~64 a janela acima colapsa, entao esta busca e a unica real. */
                int f0 = len < 256 ? len : 256;
                int coberto = (ini == 0 && fim >= f0);
                if (!coberto && busca1(t, t, 0, f0, &off, &bit)) {
                    via = "1bit@inicio"; snprintf(det, sizeof det, "off %d bit %d", off, bit);
                } else {
                    int i2 = corte - j2; if (i2 < 0) i2 = 0;
                    int f2 = corte + 3;  if (f2 > len) f2 = len;
                    if (busca2(t, t, i2, f2, &o1, &b1, &o2, &b2)) {
                        via = "2bit@corte";
                        snprintf(det, sizeof det, "off %d bit %d + off %d bit %d", o1, b1, o2, b2);
                    }
                }
            }
            double seg = (double)(clock() - t0) / CLOCKS_PER_SEC;
            if (!via)                          { n_sem++;  via = "SEM SOLUCAO"; det[0] = 0; }
            else if (!strcmp(via, "1bit@corte"))  n_um++;
            else if (!strcmp(via, "1bit@inicio")) n_ini++;
            else                                  n_dois++;
            printf("IDR %5d (%7dB, corte %6d): %-11s %-34s [%.1fs]\n",
                   t, len, corte, via, det, seg);
            fflush(stdout);
        }
        printf("\n[+] IDRs ja bons: %d | testados: %d\n"
               "    1 bit no corte: %d | 1 bit no inicio: %d | 2 bits: %d | sem solucao: %d\n",
               n_ok, feitos, n_um, n_ini, n_dois, n_sem);
    }
    /* Conta TODAS as solucoes de 1 bit por IDR quebrado. Nao grava. */
    else if (!strcmp(modo, "unico")) {
        int jc = argc > 5 ? atoi(argv[5]) : 1024;   /* janela em volta do corte */
        int ji = argc > 6 ? atoi(argv[6]) : 256;    /* janela no inicio do NAL  */
        int offs[64], bits[64];
        int n_unico = 0, n_multi = 0, n_zero = 0;
        int nthr = quantas_threads();
        fprintf(stderr, "[+] modo unico com %d thread(s)%s\n", nthr,
                nthr <= 1 ? " (caminho sequencial)" : "");

        for (int t = 0; t < n_ix; t++) {
            if (!ix[t].idr) continue;
            if (decodifica(t, t, NULL, 0, NULL) == 0) continue;

            int len = ix[t].size, corte = acha_corte(t, t);
            clock_t t0 = clock();
            int ini = corte - jc; if (ini < 0) ini = 0;
            int fim = corte + 3;  if (fim > len) fim = len;
            int f0  = len < ji ? len : ji;

            int g = 0;
            int n = conta(t, t, ini, fim, offs, bits, 64, &g, nthr);
            /* Faixa do inicio do NAL ainda nao coberta por [ini,fim). Quando o
             * corte e pequeno, ini==0 e o que falta fica DEPOIS de fim. */
            if (ini == 0) {
                if (f0 > fim) n += conta(t, t, fim, f0, offs, bits, 64, &g, nthr);
            } else {
                int lim = ini < f0 ? ini : f0;
                if (lim > 0)  n += conta(t, t, 0, lim, offs, bits, 64, &g, nthr);
            }
            double seg = (double)(clock() - t0) / CLOCKS_PER_SEC;

            if (n == 0) n_zero++; else if (n == 1) n_unico++; else n_multi++;
            printf("IDR %5d (corte %6d): %d solucao(oes)", t, corte, n);
            for (int k = 0; k < n && k < 8; k++) printf("  %d/%d", offs[k], bits[k]);
            printf("   [%.1fs]\n", seg);
            fflush(stdout);
        }
        printf("\n[+] unica: %d | multipla: %d | nenhuma: %d\n",
               n_unico, n_multi, n_zero);
    }
    /* Duas perguntas de uma vez, em IDRs que ja decodificam limpos:
     *
     * 1. Quanto dano visual um bit corrompido causa de fato? Medido contra a
     *    imagem limpa (a verdade conhecida), em pixels alterados e diferenca.
     * 2. A blocagem detecta esse dano? Se nao subir acima do valor limpo, o
     *    criterio visual nao serve e nao ha por que segui-lo.
     *
     * A versao anterior injetava um flip que QUEBRASSE o decode. Nao existe na
     * pratica: 6.300 flips em quatro keyframes e nenhum quebrou. Este desenho
     * usa essa tolerancia a favor -- injeta flips que nao quebram, que e o caso
     * real, e pergunta se a imagem denuncia o que a sintaxe deixa passar. */
    else if (!strcmp(modo, "oraculo")) {
        int n_am = argc > 5 ? atoi(argv[5]) : 5;
        int n_fl = argc > 6 ? atoi(argv[6]) : 24;
        size_t PX = 1920 * 1088;
        uint8_t *R = malloc(PX);
        cap_buf = malloc(PX);
        int feitos = 0, testes = 0, acima = 0;

        for (int t = 0; t < n_ix && feitos < n_am; t++) {
            if (!ix[t].idr) continue;
            if (decodifica(t, t, NULL, 0, NULL) != 0) continue;   /* precisa estar limpo */
            int w = cap_w, h = cap_h, len = ix[t].size;
            memcpy(R, cap_buf, (size_t)w * h);
            double m0 = blocagem(R, w, h);
            uint8_t *base = arq + ix[t].off;

            int passo = len / (n_fl + 2); if (passo < 1) passo = 1;
            int n = 0, n_acima = 0, quebrou = 0, pior_max = 0;
            double som_m = 0, som_dif = 0, som_frac = 0, pior_dif = 0, pior_m = 0;

            for (int off = len / 4; off < len && n < n_fl; off += passo) {
                int b = off % 8;                       /* bit deterministico */
                base[off] ^= (1 << b);
                int r = decodifica(t, t, NULL, 0, NULL);
                if (r != 0) { quebrou++; }
                else {
                    double m = blocagem(cap_buf, cap_w, cap_h), media, frac; int mx;
                    difere(R, cap_buf, w, h, &media, &mx, &frac);
                    n++; som_m += m; som_dif += media; som_frac += frac;
                    if (media > pior_dif) pior_dif = media;
                    if (m > pior_m) pior_m = m;
                    if (mx > pior_max) pior_max = mx;
                    if (m > m0) n_acima++;
                }
                base[off] ^= (1 << b);
            }
            if (!n) continue;
            feitos++; testes += n; acima += n_acima;
            printf("IDR %5d: %2d flips (%d quebraram) | pixels alterados %5.1f%% | "
                   "dif media %5.2f pior %5.2f maxpix %3d | blocagem limpo %.4f "
                   "media %.4f pior %.4f | %d/%d acima\n",
                   t, n, quebrou, 100 * som_frac / n, som_dif / n, pior_dif, pior_max,
                   m0, som_m / n, pior_m, n_acima, n);
            fflush(stdout);
        }
        printf("\n[+] IDRs: %d | flips: %d | com blocagem acima do limpo: %d (%.0f%%)\n",
               feitos, testes, acima, testes ? 100.0 * acima / testes : 0.0);
        free(R); free(cap_buf); cap_buf = NULL;
    }
    /* Despeja o plano Y de um frame como PGM, para inspecao visual. */
    else if (!strcmp(modo, "dump")) {
        int alvo = atoi(argv[5]);
        const char *saida = argv[6];
        cap_buf = malloc((size_t)1920 * 1088);
        int r = decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
        if (cap_w <= 0) { fprintf(stderr, "sem imagem capturada\n"); return 1; }
        FILE *g = fopen(saida, "wb");
        fprintf(g, "P5\n%d %d\n255\n", cap_w, cap_h);
        fwrite(cap_buf, 1, (size_t)cap_w * cap_h, g);
        fclose(g);
        printf("frame %d (ancora %d, decode %s) -> %s  %dx%d | "
               "propagacao %.1f%% | blocagem %.3f\n",
               alvo, ancora_de(alvo), r ? "COM ERRO" : "limpo", saida, cap_w, cap_h,
               100 * propagacao(cap_buf, cap_w, cap_h), blocagem(cap_buf, cap_w, cap_h));
    }
    else {
        /* Saida por frame: <t> <estado> <propagacao> <blocagem>
         *   quebrado  - falhou no criterio sintatico
         *   propagado - decodificou "limpo" mas a slice terminou cedo e a
         *               imagem e listra vertical (>=99,5% das linhas repetidas)
         *   uniforme  - imagem chapada (preto/fade); pode ser legitima
         *   real      - decodificou limpo e tem conteudo de imagem */
        int quebrado = 0, propagado = 0, uniforme = 0, real = 0;
        cap_buf = malloc((size_t)1920 * 1088);
        for (int t = 0; t < n_ix; t++) {
            int q, r = decodifica(ancora_de(t), t, NULL, 0, &q);
            const char *est; double pr = 0, bl = 0;
            if (r) { est = "quebrado"; quebrado++; }
            else {
                pr = propagacao(cap_buf, cap_w, cap_h);
                bl = blocagem(cap_buf, cap_w, cap_h);
                if (bl == 0)         { est = "uniforme";  uniforme++;  }
                else if (pr >= 0.995){ est = "propagado"; propagado++; }
                else                 { est = "real";      real++;      }
            }
            printf("%d %s %.3f %.3f\n", t, est, pr, bl);
        }
        printf("\n[+] de %d frames: real %d | propagado %d | uniforme %d | quebrado %d\n",
               n_ix, real, propagado, uniforme, quebrado);
        free(cap_buf); cap_buf = NULL;
    }
    return 0;
}
