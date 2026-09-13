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
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

#define MAXS 8192

typedef struct { long off; int size; int idr; } Amostra;
static Amostra ix[MAXS];
static int n_ix = 0;

static uint8_t *arq = NULL;      /* copia do MP4 em memoria, com patches */
static long arq_len = 0;

/* ---- captura do log do decoder ---- */
static int log_erros = 0;
static long log_bytestream = -1;
static int log_ocultados = -1;

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

/* ---- decodificador ---- */
static AVCodecContext *ctx = NULL;
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
    ctx->err_recognition = 0;
    avcodec_open2(ctx, c, NULL);
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
        while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; av_frame_unref(fr); }
    }
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; av_frame_unref(fr); }
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
          "    report                        estado de cada frame\n", argv[0]);
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
    else {
        int bons = 0;
        for (int t = 0; t < n_ix; t++) {
            int q, r = decodifica(ancora_de(t), t, NULL, 0, &q);
            if (!r) bons++;
            else printf("frame %5d: quebrado (ancora %d)\n", t, ancora_de(t));
        }
        printf("\n[+] frames perfeitos: %d/%d\n", bons, n_ix);
    }
    return 0;
}
