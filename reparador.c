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
#include <math.h>
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
static _Thread_local int log_mbx = -1, log_mby = -1;

static void meu_log(void *avcl, int nivel, const char *fmt, va_list vl) {
    if (nivel > AV_LOG_ERROR) return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    long bs; int mb1, mb2, oc;
    if (sscanf(buf, "error while decoding MB %d %d, bytestream %ld",
               &mb1, &mb2, &bs) == 3) {
        log_bytestream = bs;
        /* O decoder informa a COORDENADA do macrobloco onde falhou, em unidades
         * de 16 pixels. E medida na fonte, nao inferida do pixel de saida --
         * imune ao lixo colorido que burlou a contagem de linhas propagadas tres
         * vezes. Endereco linear = mb_y * 120 + mb_x, de 0 a 8159. */
        log_mbx = mb1; log_mby = mb2;
    }
    if (sscanf(buf, "concealing %d DC", &oc) == 1) log_ocultados = oc;
    if (getenv("LOG")) fputs(buf, stderr);   /* ver o que o decoder diz, cru */
    log_erros++;
}
static void log_zerar(void) { log_erros = 0; log_bytestream = -1; log_ocultados = -1;
                              log_mbx = -1; log_mby = -1; }

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
    /* Fica em 0 de proposito. Testei AV_EF_EXPLODE|AV_EF_BITSTREAM achando que
     * pegaria a slice que termina cedo: nao pega, e ainda piora -- o ffmpeg
     * aborta e devolve quadro de ocultacao no lugar da imagem parcialmente
     * decodificada, que e justamente o que queremos ver. Quem detecta slice
     * truncada e o criterio visual, nao o err_recognition. */
    ctx->err_recognition = getenv("EF_RECOG") ? atoi(getenv("EF_RECOG")) : 0;
    avcodec_open2(ctx, c, NULL);
}

/* ---- captura da imagem decodificada ----
 * Quando `cap_buf` esta setado, o plano Y do ultimo quadro recebido e copiado
 * para la. Serve ao criterio visual: a sintaxe nao distingue o bit certo, mas
 * a imagem sim. */
static _Thread_local uint8_t *cap_buf = NULL;
/* Croma, so para o modo dumpyuv: o resto do programa decide por luma. */
static _Thread_local uint8_t *cap_u = NULL, *cap_v = NULL;
static int guardar_croma = 0;
static _Thread_local int cap_w = 0, cap_h = 0;

/* Criterio de 2 partes ligado por padrao. VISUAL=0 volta ao criterio so
 * sintatico, que aceita listra vertical -- serve para reproduzir medidas
 * antigas, nao para decidir reparo. */
static int exigir_imagem = 1;

static _Thread_local uint64_t cap_hash = 0;

/* Indice do frame que se quer capturar. O `decodifica` marca cada pacote com
 * pts = indice, e so o quadro que volta com esse pts e copiado.
 *
 * Guardar "o ultimo quadro recebido" estava ERRADO: com reordenacao de B-frames
 * a ordem de saida nao e a de decodificacao, entao o ultimo a sair podia ser
 * outro frame. O sintoma era diferenca 0,00 entre frames consecutivos bons --
 * eu estava comparando a mesma imagem com ela mesma. */
static _Thread_local int cap_alvo = -1;
/* So para inspecao: aceita qualquer quadro emitido, nao so o do alvo.
 * Nunca ligar durante busca -- e o casamento por pts que impede a
 * ocultacao (copia do frame anterior) de passar por reparo. */
static _Thread_local int cap_qualquer = 0;

/* Panorama: em vez de guardar um quadro, MEDE cada quadro emitido e imprime.
 * Nasceu do GOP 0, onde 10 frames produzem a rampa de fade-in correta com
 * tarja 16,000 e desvio 0,000, e so 2 passam no criterio rigoroso. O criterio
 * e mais estrito que 'produz a imagem certa', e isso nunca foi medido no filme
 * inteiro -- pode haver muito mais assistivel do que a contagem diz. */
static int panorama = 0;

static void mede_e_imprime(const AVFrame *fr) {
    int w = fr->width, h = fr->height, ls = fr->linesize[0];
    const uint8_t *Y = fr->data[0];
    if (w <= 0 || h < 1080) return;
    double si = 0, si2 = 0; long ni = 0;
    for (int y = 130; y < 950; y += 3)
        for (int x = 0; x < w; x += 11) {
            double v = Y[(size_t)y * ls + x]; si += v; si2 += v * v; ni++;
        }
    double mi = si / ni, di = sqrt(si2 / ni - mi * mi);
    double st = 0, st2 = 0; long nt = 0;
    for (int y = 962; y < 1080; y++)
        for (int x = 0; x < w; x += 8) {
            double v = Y[(size_t)y * ls + x]; st += v; st2 += v * v; nt++;
        }
    double mt = st / nt, dt = sqrt(st2 / nt - mt * mt);
    int listra = 0;
    for (int y = 131; y < 950; y++) {
        long d = 0;
        for (int x = 0; x < w; x += 4)
            d += labs((long)Y[(size_t)y * ls + x] - (long)Y[(size_t)(y - 1) * ls + x]);
        if (d == 0) listra++;
    }
    double gv = 0, gh = 0; long ng = 0;
    for (int y = 140; y < 940; y += 3)
        for (int x = 4; x < w - 4; x += 13) {
            gv += labs((long)Y[(size_t)y * ls + x] - (long)Y[(size_t)(y-1) * ls + x]);
            gh += labs((long)Y[(size_t)y * ls + x] - (long)Y[(size_t)y * ls + x - 1]);
            ng++;
        }
    printf("%d %.3f %.3f %.4f %.4f %.1f %.4f\n", (int)fr->pts, mi, di, mt, dt,
           listra * 100.0 / 819.0, gh > 0 ? gv / gh : -1.0);
}

static void captura_frame(AVFrame *fr) {
    if (panorama) { mede_e_imprime(fr); return; }
    if (!cap_buf || fr->width <= 0) return;
    if (cap_alvo >= 0 && fr->pts != cap_alvo) return;
    cap_w = fr->width; cap_h = fr->height;
    uint64_t hh = 1469598103934665603ULL;
    for (int y = 0; y < cap_h; y++) {
        const uint8_t *src = fr->data[0] + (size_t)y * fr->linesize[0];
        memcpy(cap_buf + (size_t)y * cap_w, src, cap_w);
        for (int x = 0; x < cap_w; x += 8) { hh ^= src[x]; hh *= 1099511628211ULL; }
    }
    cap_hash = hh;
    if (guardar_croma && fr->data[1] && fr->data[2]) {
        int cw = cap_w / 2, ch = cap_h / 2;
        if (!cap_u) { cap_u = malloc((size_t)cw * ch); cap_v = malloc((size_t)cw * ch); }
        for (int y = 0; y < ch; y++) {
            memcpy(cap_u + (size_t)y * cw, fr->data[1] + (size_t)y * fr->linesize[1], cw);
            memcpy(cap_v + (size_t)y * cw, fr->data[2] + (size_t)y * fr->linesize[2], cw);
        }
    }
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

/* Quantas linhas de imagem real o frame tem antes de virar propagacao.
 * Pula a tarja preta do letterbox (linhas uniformes do topo) e devolve a
 * primeira linha a partir da qual 20 seguidas sao copia da anterior. E o proxy
 * util de "quao corrompido": mede o que sobrou de imagem, nao quanto byte o
 * decoder leu. Devolve -1 para quadro de ocultacao (poucos tons distintos),
 * que nao e imagem nenhuma -- armadilha 1 do ARMADILHAS.md. */
static int linhas_reais(const uint8_t *Y, int w, int h) {
    int hist[256] = {0}, distintos = 0;
    for (size_t i = 0; i < (size_t)w*h; i += 97) hist[Y[i]] = 1;
    for (int i = 0; i < 256; i++) distintos += hist[i];
    if (distintos < 16) return -1;                    /* quadro de ocultacao */

    /* Uma linha e RUIM de dois jeitos, e os dois precisam contar:
     *   - repetida: copia da anterior -> propagacao (slice terminou cedo);
     *   - blocada:  degraus fortes na grade 16x16 -> lixo de macrobloco.
     * Contar so a repeticao foi um erro caro: a busca incremental "melhorava" o
     * frame trocando propagacao limpa por ruido embaralhado, que nao repete e
     * por isso pontuava alto. Armadilha 2 do ARMADILHAS.md, na pratica.
     * A diferenca linha-a-linha e calculada UMA vez; o laco aninhado ingenuo
     * custava ~10 ms por frame e dominava a busca. */
    static _Thread_local unsigned char rep[2048];
    static _Thread_local float dl[2048], d30[2048];
    for (int y = 1; y < h; y++) {
        long s = 0, borda = 0, dentro = 0; int nb = 0, ni = 0, nv = 0;
        for (int x = 4; x < w; x += 4) {
            s += abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)(y-1)*w+x]); nv++;
            int dh = abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)y*w+x-1]);
            if (x % 16 == 0) { borda += dh; nb++; } else { dentro += dh; ni++; }
        }
        dl[y] = (float)s / nv;
        /* Borrao vertical e continuo entre linhas VIZINHAS -- comparar so com a
         * linha de cima nao o detecta, e foi assim que a busca burlou a metrica
         * pela terceira vez. O que o denuncia e a distancia longa: em imagem
         * real a linha y difere bastante da y-30; num borrao, quase nao. */
        long s30 = 0;
        if (y >= 30) {
            for (int x = 4; x < w; x += 4)
                s30 += abs((int)Y[(size_t)y*w+x] - (int)Y[(size_t)(y-30)*w+x]);
            d30[y] = (float)s30 / nv;
        } else d30[y] = 1e9f;
        int repetida = (dl[y] < 1) || (d30[y] < 2);
        /* degrau medio na borda do macrobloco > 2x o do interior = lixo */
        int blocada = (dentro > 0 && borda * ni > 2 * dentro * nb);
        rep[y] = repetida || blocada;
    }
    int y0 = 0;                                       /* fim do letterbox */
    for (int y = 1; y < h; y++) if (!rep[y]) { y0 = y; break; }

    /* Cresce a regiao boa de cima para baixo, medindo cada linha nova contra a
     * estatistica das que ja foram aceitas. E a ancora que faltava: limiar fixo
     * inventado por mim a busca aprende a burlar; a media do proprio conteudo
     * real do frame, nao. Lixo de macrobloco tem diferenca linha-a-linha muito
     * fora da distribuicao natural da imagem acima dele. */
    /* O que separa imagem real de borrao NAO e o nivel da diferenca, e a
     * REGULARIDADE dela. Medido em 60 linhas consecutivas:
     *   real:   8 9 8 7 9 7 7 6 7 6 6 6 ...   (suave)
     *   borrao: 15 4 3 3 10 3 3 3 8 3 3 3 ... (periodo 4: degrau + copias)
     * Saltos bruscos em 60 linhas: 0 no real, 8 no borrao. Limiar de nivel nao
     * separa -- as linhas borradas ficam em 1,3-2,2 e passavam raspando pelo
     * corte em 1. Contar salto brusco separa. */
    int jan = 0, saltos = 0;
    for (int y = y0 + 2; y < h; y++) {
        float a = dl[y-1], b = dl[y];
        int salto = (b > 3.0f * a + 1.0f) || (3.0f * b + 1.0f < a);
        saltos += salto; jan++;
        if (jan >= 40) {
            if (saltos >= 4) return (y - 40) - y0;    /* aqui virou borrao */
            saltos = 0; jan = 0;                      /* janela seguinte */
        }
        if (rep[y]) {                                 /* propagacao pura */
            int seq = 1;
            while (y + seq < h && rep[y + seq] && seq < 20) seq++;
            if (seq >= 20) return y - y0;
        }
    }
    return h - y0;
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
    if (exigir_imagem && !cap_buf) cap_buf = malloc((size_t)1920 * 1088);
    /* Zerar antes de decodificar. Se nenhum quadro sair, estes tem que denunciar
     * a ausencia em vez de manter o valor da decodificacao anterior -- foi o que
     * fez a busca binaria de acha_consumo ler "nao mudou" e convergir para 5. */
    cap_hash = 0; cap_w = 0; cap_h = 0; cap_alvo = cap_qualquer ? -1 : alvo;
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
        pkt->pts = i;                  /* casa o quadro de saida com o frame pedido */
        if (avcodec_send_packet(ctx, pkt) == 0) enviados++;
        av_packet_unref(pkt);
        while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
    }
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
    av_frame_free(&fr); av_packet_free(&pkt);
    if (quadros_out) *quadros_out = quadros;
    int esperado = alvo - ancora + 1;
    int ok = (log_erros == 0 && quadros == esperado);
    /* Segunda parte do criterio: a imagem tem que existir. Sem isto passa
     * slice que termina cedo e vira listra -- armadilha 7 do ARMADILHAS.md. */
    if (ok && exigir_imagem)
        ok = (cap_w > 0 && propagacao(cap_buf, cap_w, cap_h) < 0.995);
    return ok ? 0 : 1;
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

/* Ate onde o decoder de fato consome a amostra.
 *
 * Para frame que termina cedo SEM erro, `acha_corte` nao serve: ele localiza
 * onde o decoder reclamou, e esse nao reclama. O ponto util e outro -- a partir
 * de onde corromper nao muda mais a imagem, o dado nao esta sendo lido. O
 * primeiro bit corrompido esta perto dessa fronteira, porque foi ele que fez o
 * decoder parar ali. Busca binaria: O(log n) decodificacoes. */
static int acha_consumo(int alvo) {
    int len = ix[alvo].size;
    uint8_t *base = arq + ix[alvo].off;
    uint8_t *copia = malloc(len);
    memcpy(copia, base, len);
    /* O buffer tem que existir ANTES de desligar exigir_imagem: e ele que
     * habilita a captura, e sem captura o hash nunca muda e a busca binaria
     * colapsa no limite inferior. */
    if (!cap_buf) cap_buf = malloc((size_t)1920 * 1088);
    int salvo = exigir_imagem; exigir_imagem = 0;   /* aqui so importa a imagem */
    decodifica(alvo, alvo, NULL, 0, NULL);
    uint64_t h_ref = cap_hash;

    int lo = 5, hi = len;               /* menor p tal que corromper [p,len) nao muda nada */
    while (lo < hi) {
        int md = (lo + hi) / 2;
        memcpy(copia, base, len);
        for (int k = md; k < len; k++) copia[k] ^= 0xFF;
        decodifica(alvo, alvo, copia, len, NULL);
        if (cap_hash == h_ref) hi = md; else lo = md + 1;
    }
    exigir_imagem = salvo;
    free(copia);
    return lo;
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

/* ---- modo varre2: pares de bits ----
 * So faz sentido depois que o de 1 bit devolve zero: ai o dano e multiplo e a
 * busca linear nao alcanca. O custo e quadratico, entao vale apenas para NAL
 * pequeno -- o frame 3443 tem 261 bytes, que sao 2 milhoes de pares; um IDR de
 * 220 mil bytes seriam 10^12, que e o "programa de meses" a evitar.
 *
 * Determinismo: cada par recebe um indice unico k, distribuido por contador
 * atomico, e a saida sai ordenada por esse indice. Mesma regra da varredura de
 * 1 bit. */
static int alvo2;
static int n_bits2;
static _Atomic long prox_par;
static long n_pares;
static _Atomic int achados2;
typedef struct { int a, b; } Par;
static Par pares[4096];
static _Atomic int n_pares_achados;

static void *worker_par2(void *p) {
    (void)p;
    int alvo = alvo2, len = ix[alvo].size, anc = ancora_de(alvo);
    uint8_t *copia = malloc(len);
    cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        long k = atomic_fetch_add(&prox_par, 1);
        if (k >= n_pares) break;
        /* k -> (i, j) com i < j, sem tabela: percorre triangular */
        long i = 0, resto = k;
        while (resto >= n_bits2 - i - 1) { resto -= n_bits2 - i - 1; i++; }
        long j = i + 1 + resto;
        memcpy(copia, arq + ix[alvo].off, len);
        copia[5 + i / 8] ^= (1 << (i % 8));
        copia[5 + j / 8] ^= (1 << (j % 8));
        if (decodifica(anc, alvo, copia, len, NULL) == 0) {
            int n = atomic_fetch_add(&n_pares_achados, 1);
            if (n < 4096) { pares[n].a = (int)i; pares[n].b = (int)j; }
        }
    }
    free(copia); free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* Varredura exaustiva de k bits numa faixa. Nasceu dos 17 IDRs que nao produzem
 * imagem: todos morrem entre o byte 10 e o 12, ou seja o dano cabe em poucas
 * dezenas de bits. Varrer o NAL inteiro atras de 1 bit, como ja se fez tres
 * vezes nesse grupo, cobre 2 milhoes de posicoes irrelevantes e nenhuma
 * combinacao. Aqui a faixa e minuscula e a profundidade e que cresce.
 *
 * As combinacoes sao pre-geradas em ordem lexicografica num vetor, e os workers
 * puxam indice do contador atomico. A ordem do vetor E a ordem sequencial, entao
 * o resultado nao depende de escalonamento. */
static int   alvok, prof_k, ini_k, nbits_k;
/* Aceite alternativo: em vez de exigir decodificacao limpa do NAL inteiro,
 * aceita a combinacao que faz o quadro PRODUZIR IMAGEM. Nos 17 IDRs que
 * morrem no byte ~10, exigir NAL perfeito confunde duas perguntas: o
 * cabecalho e consertavel, e o corpo esta sao. Esta flag separa as duas. */
static int   aceita_imagem = 0;

/* cap_w > 0 NAO significa que existe imagem: o decoder emite quadro de
 * ocultacao cinza, campo constante 128, e isso e a armadilha 1. O aceite por
 * imagem exige campo nao constante -- amostra esparsa basta, porque o quadro
 * de ocultacao e uniforme byte a byte. */
static int linhas_identicas(const uint8_t *Y, int w, int h);   /* definida adiante */
static int sem_imagem(const uint8_t *Y, int w, int h) {
    /* Amostra esparsa nao serve: o quadro de ocultacao com meia duzia de
     * macroblocos decodificados passa por ela e ainda e cinza. Usa o detector
     * de listra do projeto, que e o que separa imagem de propagacao. */
    return linhas_identicas(Y, w, h) > 400;   /* metade das 813 linhas */
}
static int  *combos_k;
static long  n_combos;
static _Atomic long prox_combo;
static int   achk[4096 * 4];
static long  achk_c[4096];   /* indice da combinacao, para ordenar a saida */
static _Atomic int n_achk;

static void *worker_varrek(void *p) {
    (void)p;
    int alvo = alvok, len = ix[alvo].size, anc = ancora_de(alvo);
    uint8_t *copia = malloc(len);
    cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        long c = atomic_fetch_add(&prox_combo, 1);
        if (c >= n_combos) break;
        const int *comb = combos_k + c * prof_k;
        memcpy(copia, arq + ix[alvo].off, len);
        for (int q = 0; q < prof_k; q++)
            copia[ini_k + comb[q] / 8] ^= (1 << (comb[q] % 8));
        int r = decodifica(anc, alvo, copia, len, NULL);
        int bom = aceita_imagem ? (cap_w > 0 && !sem_imagem(cap_buf, cap_w, cap_h))
                                : (r == 0);
        if (bom) {
            int n = atomic_fetch_add(&n_achk, 1);

            if (n < 4096) {
                achk_c[n] = c;
                for (int q = 0; q < prof_k; q++) achk[n * 4 + q] = comb[q];
            }
        }
    }
    free(copia); free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* Gera C(nbits_k, prof_k) combinacoes em ordem lexicografica. */
static void gera_combos(void) {
    long total = 1;
    for (int q = 0; q < prof_k; q++) total = total * (nbits_k - q) / (q + 1);
    combos_k = malloc((size_t)total * prof_k * sizeof(int));
    int idx[8];
    for (int q = 0; q < prof_k; q++) idx[q] = q;
    long w = 0;
    for (;;) {
        for (int q = 0; q < prof_k; q++) combos_k[w * prof_k + q] = idx[q];
        w++;
        int q = prof_k - 1;
        while (q >= 0 && idx[q] == nbits_k - prof_k + q) q--;
        if (q < 0) break;
        idx[q]++;
        for (int r = q + 1; r < prof_k; r++) idx[r] = idx[r - 1] + 1;
    }
    n_combos = w;
}


/* ---- linhas identicas: o detector de propagacao vertical ----
 * O `propagacao` conta linhas PARECIDAS com a de cima, e cena desfocada com
 * grandes areas uniformes acerta valores altos legitimamente -- o GOP 3368 tem
 * quadros perfeitos com propagacao 0,98. Linha EXATAMENTE identica separa de
 * forma binaria: medido em 137 quadros bons, todos dao ZERO; o IDR 1683,
 * listrado, da 34,3%. Ver armadilha 16. */
static int linhas_identicas(const uint8_t *Y, int w, int h) {
    int n = 0;
    for (int y = 137; y < 950 && y < h; y++) {
        long d = 0;
        for (int x = 0; x < w; x += 4)
            d += abs((int)Y[(size_t)y * w + x] - (int)Y[(size_t)(y - 1) * w + x]);
        if (d == 0) n++;
    }
    return n;
}

/* ---- modo cresce: objetivo CONTINUO, para frame que passa no criterio ----
 * Existe frame que decodifica sem erro e mesmo assim so mostra metade da
 * imagem: a slice para no meio e o ffmpeg preenche repetindo a ultima linha,
 * sem reclamar. O IDR 1683 e assim -- consome 49% do NAL e decodifica 49% da
 * altura. Para ele o criterio binario e inutil, porque ja esta satisfeito: a
 * varredura do frame 1684, que depende dele, devolveu 75.909 "solucoes".
 *
 * Aqui o alvo e outro: MINIMIZAR as linhas identicas. E nota continua, nao
 * binaria, entao cada candidato diz quantas fileiras de macrobloco a mais
 * foram decodificadas. Determinismo pela regra de sempre: vence a menor nota,
 * empate pelo menor indice. */
/* O criterio nao pode ser SO "menos linhas identicas": medido no IDR 1683, o
 * melhor candidato por essa nota trocava propagacao limpa por lixo colorido --
 * ruido nao tem linha identica, entao a nota cai e a imagem piora. E a armadilha
 * 8 outra vez. Por isso a nota so conta se o quadro passar em duas guardas que
 * o lixo nao consegue satisfazer junto:
 *   - croma perto de 128, porque o filme e desaturado (medido U 101-135 nos
 *     quadros bons; o lixo vai a 56-171);
 *   - blocagem dentro da faixa dos genuinos. */
/* Estatistica de croma numa faixa de linhas. Min/max NAO serve de guarda --
 * listra pastel cabe dentro da faixa e passa; foi assim que o segundo candidato
 * do IDR 1683 escapou. O DESVIO separa: cena real fica em 4 a 8, e o lixo pastel
 * deu 10,1. */
static void croma_stat(int y0, int y1, double *um, double *ud, double *vm, double *vd) {
    int cw = cap_w / 2, ch = cap_h / 2;
    if (y1 / 2 > ch) y1 = ch * 2;
    double su = 0, sv = 0, qu = 0, qv = 0; long n = 0;
    for (int y = y0 / 2; y < y1 / 2; y++)
        for (int x = 0; x < cw; x += 2) {
            int u = cap_u[(size_t)y * cw + x], v = cap_v[(size_t)y * cw + x];
            su += u; qu += (double)u * u; sv += v; qv += (double)v * v; n++;
        }
    *um = n ? su / n : 0; *vm = n ? sv / n : 0;
    *ud = n ? sqrt(qu / n - *um * *um) : 0;
    *vd = n ? sqrt(qv / n - *vm * *vm) : 0;
}

/* Guarda calibrada pela PROPRIA metade integra do quadro: a regiao danificada e
 * a mesma cena continuando, entao o croma dela tem que ter media e desvio
 * parecidos com os de cima. Referencia medida no 1683 integro: U media 125,8
 * desvio 6,7; V media 136,3 desvio 7,8. */
static double gu_med, gu_des, gv_med, gv_des;
static int gy0 = 581;

static int croma_sao(void) {
    if (!cap_u || !cap_v || cap_w <= 0) return 0;
    double um, ud, vm, vd;
    croma_stat(gy0, 950, &um, &ud, &vm, &vd);
    return fabs(um - gu_med) <= 4.0 && fabs(vm - gv_med) <= 4.0
        && ud <= gu_des * 1.15 && vd <= gv_des * 1.15;
}

/* Despejo de TODOS os candidatos, para filtrar depois sem refazer a corrida.
 * Motivo concreto: o IDR 1683 custou tres corridas de 12 min so porque eu mudei
 * o limiar da guarda entre elas, e as tres mediram a mesma coisa. Com o despejo
 * e uma corrida e tres filtragens de segundos.
 *
 * Cada worker escreve so no proprio indice k, entao nao ha trava nem corrida --
 * e a saida sai ordenada por construcao. Sao 8 bytes por candidato: 5 MB para os
 * 654 mil do 1683, no scratchpad, descartavel quando o frame fechar. */
typedef struct {
    int nota;              /* linhas identicas; <0 = nao produziu imagem */
    short prim;            /* primeira linha propagada */
    unsigned char um, ud, vm, vd;   /* croma da regiao nova: media e desvio x10 */
    unsigned char bloc;    /* blocagem x50, saturando em 255 */
} Medida;
static Medida *medidas = NULL;

typedef struct { int alvo, ancora, ini; int nota, off, bit, idx; } WorkerC;

static void *worker_cresce(void *p) {
    WorkerC *w = p;
    int len = ix[w->alvo].size;
    uint8_t *copia = malloc(len);
    memcpy(copia, arq + ix[w->alvo].off, len);
    if (!cap_buf) cap_buf = malloc((size_t)1920 * 1088);
    guardar_croma = 1;
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_cand) break;
        int off = w->ini + k / 8, bit = k % 8;
        copia[off] ^= (1 << bit);
        decodifica(w->ancora, w->alvo, copia, len, NULL);
        int nota = 1 << 20;
        int cru = cap_w > 0 ? linhas_identicas(cap_buf, cap_w, cap_h) : -1;
        double b = cap_w > 0 ? blocagem(cap_buf, cap_w, cap_h) : 0;
        if (cap_w > 0 && croma_sao() && b > 0.4 && b < 2.0) nota = cru;
        if (medidas) {                     /* despejo, ANTES de qualquer guarda */
            Medida *m = &medidas[k];
            m->nota = cru; m->prim = -1;
            if (cap_w > 0) {
                double um, ud, vm, vd;
                croma_stat(gy0, 950, &um, &ud, &vm, &vd);
                m->um = (unsigned char)(um > 255 ? 255 : um);
                m->vm = (unsigned char)(vm > 255 ? 255 : vm);
                m->ud = (unsigned char)(ud * 10 > 255 ? 255 : ud * 10);
                m->vd = (unsigned char)(vd * 10 > 255 ? 255 : vd * 10);
                m->bloc = (unsigned char)(b * 50 > 255 ? 255 : b * 50);
                short pr = 950;
                for (int y = 137; y < 950 && y < cap_h; y++) {
                    long dd = 0;
                    for (int x = 0; x < cap_w; x += 8)
                        dd += abs((int)cap_buf[(size_t)y*cap_w+x]
                                - (int)cap_buf[(size_t)(y-1)*cap_w+x]);
                    if (dd == 0) { pr = (short)y; break; }
                }
                m->prim = pr;
            }
        }
        copia[off] ^= (1 << bit);
        if (nota < w->nota || (nota == w->nota && k < w->idx)) {
            w->nota = nota; w->off = off; w->bit = bit; w->idx = k;
        }
    }
    free(copia);
    if (cap_buf) { free(cap_buf); cap_buf = NULL; }
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* ---- modo campo: mede o quadro resultante de cada candidato ----
 * Nasceu do trecho final do filme, que e um fade: dali para a frente o frame
 * certo e um campo UNIFORME de valor previsivel, e o valor sai da media dos
 * dois vizinhos na ordem de EXIBICAO (poc), nao na de decodificacao. Isso da um
 * gabarito aritmetico, nao um proxy visual.
 *
 * Alem do valor, dois detalhes separam o frame verdadeiro da ocultacao:
 *   - a borda da tarja e SECA (o preto comeca de uma linha para a outra);
 *     a ocultacao deixa uma rampa esfumada, que e o borrao vertical.
 *   - o fundo da tarja e uniforme 16; a ocultacao poe ruido la.
 * Imprime as medidas e deixa a decisao para quem le. */
typedef struct { long off; int bit; int pmin, pmax, l949, l952, l960, bmin, bmax;
                 double bmed, bdes, tmed, dref;
                 double tmed2, tdes2; int tmaxdev; double tfora;
                 double imed, ides, ibloc; } Campo;
static Campo *campos = NULL;
static int n_campos = 0;
static int campo_alvo = 0;
/* Referencia opcional: o quadro que o candidato DEVERIA produzir. Numa cena
 * parada que so escurece, o frame certo e quase a interpolacao dos dois
 * vizinhos de exibicao -- gabarito que vale onde o campo nao e uniforme. */
static uint8_t *alvo_img = NULL;
static int alvo_w = 0, alvo_h = 0;

static void *worker_campo(void *p) {
    (void)p;
    cap_buf = malloc((size_t)1920 * 1088);
    int len = ix[campo_alvo].size, anc = ancora_de(campo_alvo);
    uint8_t *copia = malloc(len);
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_campos) break;
        memcpy(copia, arq + ix[campo_alvo].off, len);
        copia[campos[k].off - ix[campo_alvo].off] ^= (1 << campos[k].bit);
        campos[k].pmin = -1;
        if (decodifica(anc, campo_alvo, copia, len, NULL) != 0 || cap_w <= 0) continue;
        int W = cap_w, H = cap_h;
        int pmin = 255, pmax = 0;
        for (int y = 136; y < 944 && y < H; y++)
            for (int x = 0; x < W; x += 4) {
                int v = cap_buf[(size_t)y * W + x];
                if (v < pmin) pmin = v;
                if (v > pmax) pmax = v;
            }
        /* A tarja nao e chapada: nos frames bons ela varia de 9 a 24, com media
         * EXATAMENTE 16,00 em todo o filme -- e grao que o encoder preservou.
         * Entao o gabarito e a media e o desvio, nao o intervalo. Filtrar por
         * "tarja uniforme 16" rejeita frame realista e premia frame liso
         * demais; foi erro cometido antes de medir o filme inteiro.
         *
         * E ela tem DUAS REGIOES com tolerancias muito diferentes, porque a
         * fronteira imagem/tarja cai DENTRO da fileira de macroblocos 59 (linhas
         * 944-959). Medido em 144 quadros genuinos:
         *
         *   transicao 950-956: desvio ate 1,36, pixel desviando ate 14 de 16,
         *                      ate 0,238% dos pixels fora de +-10
         *   fundo    957-1079: desvio ate 0,40, pixel ate 7, ZERO fora de +-10
         *
         * Julgar a faixa inteira com o limiar do fundo condena quadro genuino --
         * erro ja cometido. Por isso as duas regioes saem separadas. */
        /* transicao: 950-956 */
        double tsoma2 = 0, tq2 = 0; long tn2 = 0; int tmaxdev = 0; long tfora = 0;
        for (int y = 950; y < 957 && y < H; y++)
            for (int x = 0; x < W; x += 4) {
                int v = cap_buf[(size_t)y * W + x];
                int dv = v > 16 ? v - 16 : 16 - v;
                if (dv > tmaxdev) tmaxdev = dv;
                if (dv > 10) tfora++;
                tsoma2 += v; tq2 += (double)v * v; tn2++;
            }
        double tmed2 = tn2 ? tsoma2 / tn2 : 0;
        double tdes2 = tn2 ? sqrt(tq2 / tn2 - tmed2 * tmed2) : 0;
        /* fundo: 957 ate o fim */
        int bmin = 255, bmax = 0;
        double bsoma = 0, bq = 0; long bn = 0;
        for (int y = 957; y < H; y++)
            for (int x = 0; x < W; x += 4) {
                int v = cap_buf[(size_t)y * W + x];
                if (v < bmin) bmin = v;
                if (v > bmax) bmax = v;
                bsoma += v; bq += (double)v * v; bn++;
            }
        double bmed = bn ? bsoma / bn : 0;
        double bdes = bn ? sqrt(bq / bn - bmed * bmed) : 0;
        double tsoma = 0; long tn = 0;
        for (int y = 0; y < 130 && y < H; y++)
            for (int x = 0; x < W; x += 4) { tsoma += cap_buf[(size_t)y * W + x]; tn++; }
        double tmed = tn ? tsoma / tn : 0;
        /* JUIZ DE IMAGEM: so a area que se assiste, linhas 136-949. E o juiz que
         * MANDA -- ver CRITERIOS.md. Blocagem baixa nao e defeito; dano de
         * macrobloco a EMPURRA PARA CIMA. */
        double ib = 0, ii = 0; long inb = 0, ini_ = 0;
        double isoma = 0, iq = 0; long inn = 0;
        for (int y = 144; y < 944 && y < H; y++)
            for (int x = 1; x < W; x++) {
                int v = cap_buf[(size_t)y * W + x];
                int dd = abs(v - (int)cap_buf[(size_t)y * W + x - 1]);
                if (x % 16 == 0) { ib += dd; inb++; } else { ii += dd; ini_++; }
                if ((x & 1) == 0) { isoma += v; iq += (double)v * v; inn++; }
            }
        double imed = inn ? isoma / inn : 0;
        double ides = inn ? sqrt(iq / inn - imed * imed) : 0;
        double ibloc = (inb && ini_ && ii > 0) ? (ib / inb) / (ii / ini_) : 0;
        double dref = -1;
        if (alvo_img && W == alvo_w) {
            double sd = 0; long nd = 0;
            for (int y = 136; y < 950 && y < H; y++)
                for (int x = 0; x < W; x += 2) {
                    sd += abs((int)cap_buf[(size_t)y * W + x]
                              - (int)alvo_img[(size_t)y * W + x]);
                    nd++;
                }
            dref = nd ? sd / nd : -1;
        }
        long s9 = 0, s2 = 0, s6 = 0;
        for (int x = 0; x < W; x++) {
            s9 += cap_buf[(size_t)949 * W + x];
            s2 += cap_buf[(size_t)952 * W + x];
            s6 += cap_buf[(size_t)960 * W + x];
        }
        campos[k].pmin = pmin; campos[k].pmax = pmax;
        campos[k].l949 = (int)(s9 / W); campos[k].l952 = (int)(s2 / W);
        campos[k].l960 = (int)(s6 / W);
        campos[k].bmin = bmin; campos[k].bmax = bmax;
        campos[k].bmed = bmed; campos[k].bdes = bdes; campos[k].tmed = tmed;
        campos[k].dref = dref;
        campos[k].tmed2 = tmed2; campos[k].tdes2 = tdes2;
        campos[k].tmaxdev = tmaxdev;
        campos[k].tfora = tn2 ? 100.0 * tfora / tn2 : 0;
        campos[k].imed = imed; campos[k].ides = ides; campos[k].ibloc = ibloc;
    }
    free(copia); free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* ---- modo testa: prova candidatos de cabecalho no decoder ----
 * Recebe uma lista de flips propostos (um por linha, com o frame alvo) e diz
 * quais fazem o frame passar no criterio rigoroso. E o unico juiz que vale: o
 * cabecalho consistente prova que o bit estava errado, nao que era o UNICO
 * errado. Paralelo porque sao centenas de candidatos independentes. */
typedef struct { int alvo; long off; int bit; int ok; } Cand;
static Cand *cands = NULL;
static int n_cands = 0;

static void *worker_testa(void *p) {
    (void)p;
    cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_cands) break;
        int t = cands[k].alvo, len = ix[t].size;
        long rel = cands[k].off - ix[t].off;
        if (rel < 0 || rel >= len) { cands[k].ok = -1; continue; }
        uint8_t *copia = malloc(len);
        memcpy(copia, arq + ix[t].off, len);
        copia[rel] ^= (1 << cands[k].bit);
        cands[k].ok = decodifica(ancora_de(t), t, copia, len, NULL) == 0;
        free(copia);
    }
    free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* O "contexto aquecido" -- reaproveitar um mesmo contexto entre candidatos,
 * porque decodificar frame nao-referencia nao suja o buffer de referencias --
 * foi implementado aqui, medido e DESCARTADO em 2026-09-14. Dava 31x de ganho e
 * resultado nao reproduzivel: candidato quebrado deixa estado para tras, entao a
 * nota de um candidato passa a depender de quais candidatos vieram antes na
 * mesma thread. Postmortem no item 6 do MELHORIAS.md. Nao reimplementar.
 */

/* ---- criterio de referencia temporal ----
 * Todas as metricas anteriores eram proxies inventados ("isto parece imagem?"),
 * e a busca aprendeu a burlar cada uma. Aqui existe gabarito de verdade: a 30
 * quadros por segundo o frame vizinho e quase identico ao alvo. Comparar o
 * candidato com um vizinho INTEGRO nao e heuristica -- nenhum bit errado
 * reproduz a cena certa por acaso. Vale so onde ha vizinho bom, que e a borda
 * dos trechos que sobreviveram. */
static uint8_t *ref_img = NULL;          /* so-leitura nos workers */
static int ref_w = 0, ref_h = 0;

static double mad_ref(const uint8_t *Y, int w, int h) {
    if (!ref_img || w != ref_w || h != ref_h) return 1e9;
    double s = 0; size_t n = 0;
    for (size_t i = 0; i < (size_t)w * h; i += 3) { s += abs((int)Y[i] - (int)ref_img[i]); n++; }
    return n ? s / n : 1e9;
}

typedef struct {
    int alvo, ancora, ini;
    double mad; int off, bit, idx;
} WorkerR;

static void *worker_ref(void *p) {
    WorkerR *w = p;
    int len = ix[w->alvo].size;
    uint8_t *copia = malloc(len);
    memcpy(copia, arq + ix[w->alvo].off, len);
    if (!cap_buf) cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_cand) break;
        int off = w->ini + k / 8, bit = k % 8;
        copia[off] ^= (1 << bit);
        decodifica(w->ancora, w->alvo, copia, len, NULL);
        double m = cap_w > 0 ? mad_ref(cap_buf, cap_w, cap_h) : 1e9;
        copia[off] ^= (1 << bit);
        if (m < w->mad || (m == w->mad && k < w->idx)) {
            w->mad = m; w->off = off; w->bit = bit; w->idx = k;
        }
    }
    free(copia);
    if (cap_buf) { free(cap_buf); cap_buf = NULL; }
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

static double melhor_ref(int ancora, int alvo, int ini, int fim, int nthr,
                         int *off_out, int *bit_out) {
    if (fim <= ini) return 1e9;
    n_cand = (fim - ini) * 8;
    atomic_store(&prox_cand, 0);
    WorkerR *w = calloc(nthr, sizeof *w);
    pthread_t *th = calloc(nthr, sizeof *th);
    for (int i = 0; i < nthr; i++) {
        w[i].alvo = alvo; w[i].ancora = ancora; w[i].ini = ini;
        w[i].mad = 1e9; w[i].idx = n_cand + 1;
        pthread_create(&th[i], NULL, worker_ref, &w[i]);
    }
    double mad = 1e9; int off = -1, bit = -1, idx = n_cand + 1;
    for (int i = 0; i < nthr; i++) {
        pthread_join(th[i], NULL);
        if (w[i].mad < mad || (w[i].mad == mad && w[i].idx < idx)) {
            mad = w[i].mad; off = w[i].off; bit = w[i].bit; idx = w[i].idx;
        }
    }
    free(w); free(th);
    *off_out = off; *bit_out = bit;
    return mad;
}

/* ---- recuperacao incremental ----
 * Exigir frame perfeito nao funciona quando ha varios bits corrompidos por
 * frame: nenhum flip sozinho conserta tudo, e a busca devolve zero. Aqui o
 * criterio e PROGRESSO -- aceita o flip que faz a imagem crescer, e repete.
 * Determinismo: vence o maior numero de linhas; empate, o menor indice. */
typedef struct {
    int alvo, ini;
    int lin, off, bit, idx;
} WorkerL;

static void *worker_linhas(void *p) {
    WorkerL *w = p;
    int len = ix[w->alvo].size;
    uint8_t *copia = malloc(len);
    memcpy(copia, arq + ix[w->alvo].off, len);
    if (!cap_buf) cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        int k = atomic_fetch_add(&prox_cand, 1);
        if (k >= n_cand) break;
        int off = w->ini + k / 8, bit = k % 8;
        copia[off] ^= (1 << bit);
        decodifica(w->alvo, w->alvo, copia, len, NULL);
        int lr = cap_w > 0 ? linhas_reais(cap_buf, cap_w, cap_h) : -1;
        copia[off] ^= (1 << bit);
        if (lr > w->lin || (lr == w->lin && k < w->idx)) {
            w->lin = lr; w->off = off; w->bit = bit; w->idx = k;
        }
    }
    free(copia);
    if (cap_buf) { free(cap_buf); cap_buf = NULL; }
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

static int melhor_flip(int alvo, int ini, int fim, int nthr,
                       int *off_out, int *bit_out) {
    if (fim <= ini) return -1;
    n_cand = (fim - ini) * 8;
    atomic_store(&prox_cand, 0);
    Worker *dummy; (void)dummy;
    WorkerL *w = calloc(nthr, sizeof *w);
    pthread_t *th = calloc(nthr, sizeof *th);
    for (int i = 0; i < nthr; i++) {
        w[i].alvo = alvo; w[i].ini = ini;
        w[i].lin = -1; w[i].idx = n_cand + 1;
        pthread_create(&th[i], NULL, worker_linhas, &w[i]);
    }
    int lin = -1, off = -1, bit = -1, idx = n_cand + 1;
    for (int i = 0; i < nthr; i++) {
        pthread_join(th[i], NULL);
        if (w[i].lin > lin || (w[i].lin == lin && w[i].idx < idx)) {
            lin = w[i].lin; off = w[i].off; bit = w[i].bit; idx = w[i].idx;
        }
    }
    free(w); free(th);
    *off_out = off; *bit_out = bit;
    return lin;
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

/* A tarja inferior vale exatamente 16 com desvio zero em todo quadro genuino
 * (ver CRITERIOS.md). E gabarito conhecido a priori: quadro borrado ou listrado
 * nao produz isso por acaso -- os candidatos reprovados deram de 138 a 221.
 * Serve para aceitar na remontagem quadros cuja IMAGEM esta certa mas que nao
 * passam no criterio rigoroso, que e o caso dos dois fades do filme. */
static int tarja_perfeita(const uint8_t *Y, int w, int h) {
    if (w < 1920 || h < 1080) return 0;
    for (int y = 962; y < 1080; y++)
        for (int x = 0; x < w; x += 8)
            if (Y[(size_t)y * w + x] != 16) return 0;
    return 1;
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
    /* Sem buffer: corridas duram dezenas de minutos e a saida vai para arquivo,
     * onde stdout bufferiza em blocos. Sem isto o progresso so aparece no fim e
     * nao da para acompanhar nem estimar quanto falta. */
    setvbuf(stdout, NULL, _IONBF, 0);

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
    if (getenv("VISUAL")) exigir_imagem = atoi(getenv("VISUAL"));
    fprintf(stderr, "[+] criterio: sintatico%s\n",
            exigir_imagem ? " + imagem (propagacao)" : " apenas (VISUAL=0)");
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
        /* testa cada patch isoladamente: sem ele o frame quebra? com ele fecha?
         *
         * Pula os DETERMINISTICOS, listados em deterministicos.txt. Sao patches
         * provados por invariante do encoder, nao por busca: o cabecalho de um
         * IDR so pode comecar com 65 88 80, porque e a unica codificacao de
         * first_mb=0, slice_type=7, pps_id=0 e frame_num=0. Eles nao fazem o
         * frame decodificar sozinhos, entao apareceriam como falsos e afogariam
         * o sinal -- que e justamente "0 falsos" querer dizer "nenhum patch
         * ruim". Foram 31 de uma vez, contra 4 falsos de verdade. */
        int bons = 0, falsos = 0, pulados = 0;
        int base_n = getenv("BASE_N") ? atoi(getenv("BASE_N")) : 0;
        static long det_off[4096]; static int det_bit[4096]; int n_det = 0;
        {
            FILE *fd = fopen(getenv("DET") ? getenv("DET") : "deterministicos.txt", "r");
            if (fd) {
                long o; int b;
                while (n_det < 4096 && fscanf(fd, "%ld %d", &o, &b) == 2) {
                    det_off[n_det] = o; det_bit[n_det] = b; n_det++;
                }
                fclose(fd);
                fprintf(stderr, "[+] %d patches deterministicos serao pulados\n", n_det);
            }
        }
        for (int k = base_n; k < n_patch; k++) {
            long o = patches[k].off;
            int det = 0;
            for (int j = 0; j < n_det; j++)
                if (det_off[j] == o && det_bit[j] == patches[k].bit) { det = 1; break; }
            if (det) { pulados++; continue; }
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
        printf("\n[+] patches validos: %d | falsos: %d | deterministicos pulados: %d\n", bons, falsos, pulados);
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

            int len = ix[t].size;
            /* Centro da busca: a fronteira de consumo, nao o corte. Ver
             * acha_consumo(). O corte so vale quando o decoder reclamou. */
            int corte = exigir_imagem ? acha_consumo(t) : acha_corte(t, t);
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
    /* Recupera um frame usando um vizinho INTEGRO como gabarito. */
    else if (!strcmp(modo, "vizinho")) {
        int alvo = atoi(argv[5]);
        int ref  = atoi(argv[6]);
        int jan  = argc > 7 ? atoi(argv[7]) : 0;      /* 0 = NAL inteiro */
        int passos = argc > 8 ? atoi(argv[8]) : 4;
        int nthr = quantas_threads();
        exigir_imagem = 0;
        cap_buf = malloc((size_t)1920 * 1088);

        if (decodifica(ancora_de(ref), ref, NULL, 0, NULL) != 0 || cap_w <= 0) {
            fprintf(stderr, "referencia %d nao decodifica limpo -- escolha outro\n", ref);
            return 1;
        }
        ref_w = cap_w; ref_h = cap_h;
        ref_img = malloc((size_t)ref_w * ref_h);
        memcpy(ref_img, cap_buf, (size_t)ref_w * ref_h);

        int anc = ancora_de(alvo), len = ix[alvo].size;
        decodifica(anc, alvo, NULL, 0, NULL);
        double m0 = cap_w > 0 ? mad_ref(cap_buf, cap_w, cap_h) : 1e9;
        printf("frame %d (%d bytes, ancora %d) vs referencia %d: "
               "diferenca inicial %.2f\n", alvo, len, anc, ref, m0);
        printf("  (quanto menor a diferenca, mais parecido com o vizinho bom)\n");
        uint8_t *base = arq + ix[alvo].off;

        for (int p = 0; p < passos; p++) {
            int ini, fim;
            if (jan == 0) { ini = 5; fim = len; }
            else { int c = acha_consumo(alvo);
                   ini = c - jan; if (ini < 0) ini = 0;
                   fim = c + jan; if (fim > len) fim = len; }
            time_t t0 = time(NULL);
            int off, bit;
            double m = melhor_ref(anc, alvo, ini, fim, nthr, &off, &bit);
            if (m >= m0) {
                printf("  passo %d: sem ganho (%.2f vs %.2f) [%.0fs]\n",
                       p+1, m, m0, difftime(time(NULL), t0));
                break;
            }
            base[off] ^= (1 << bit);
            printf("  passo %d: flip off %d bit %d | diferenca %.2f -> %.2f "
                   "(-%.2f) [%.0fs]\n", p+1, off, bit, m0, m, m0 - m,
                   difftime(time(NULL), t0));
            m0 = m;
            fflush(stdout);
        }
        printf("[+] frame %d terminou com diferenca %.2f para o vizinho %d\n",
               alvo, m0, ref);
        free(ref_img); ref_img = NULL; free(cap_buf); cap_buf = NULL;
    }
    /* Recuperacao incremental de um frame: aceita o flip que faz a imagem
     * crescer e repete. NAO grava patches -- imprime o que achou. */
    else if (!strcmp(modo, "recupera")) {
        int alvo = atoi(argv[5]);
        int jan   = argc > 6 ? atoi(argv[6]) : 2048;
        int passos= argc > 7 ? atoi(argv[7]) : 8;
        int nthr = quantas_threads();
        exigir_imagem = 0;                 /* aqui queremos imagem parcial */
        cap_buf = malloc((size_t)1920 * 1088);
        decodifica(alvo, alvo, NULL, 0, NULL);
        int L = cap_w > 0 ? linhas_reais(cap_buf, cap_w, cap_h) : -1;
        printf("frame %d (%d bytes): comeca com %d linhas reais\n", alvo, ix[alvo].size, L);
        uint8_t *base = arq + ix[alvo].off;

        for (int p = 0; p < passos; p++) {
            int ini, fim, c = 0;
            if (jan == 0) {                 /* forca bruta: o NAL inteiro */
                ini = 5; fim = ix[alvo].size;
            } else {
                c = acha_consumo(alvo);
                ini = c - jan; if (ini < 0) ini = 0;
                fim = c + jan; if (fim > ix[alvo].size) fim = ix[alvo].size;
            }
            time_t t0 = time(NULL);
            int off, bit;
            int lin = melhor_flip(alvo, ini, fim, nthr, &off, &bit);
            if (lin <= L) {
                printf("  passo %d: consumo %d, janela [%d,%d) -- sem ganho "
                       "(melhor %d vs %d) [%.0fs]\n",
                       p+1, c, ini, fim, lin, L, difftime(time(NULL), t0));
                break;
            }
            /* Parada: depois de acertar o frame a busca continua "melhorando" e
             * comeca a estraga-lo. No oraculo o conserto verdadeiro deu +596 e o
             * falso seguinte +45, ja com o frame completo. Ganho pequeno sobre
             * imagem quase inteira e sinal de que passou do ponto. */
            if (L > 700 && lin - L < L / 10) {
                printf("  passo %d: ganho de apenas +%d sobre %d linhas -- "
                       "provavelmente o frame ja esta correto, parando [%.0fs]\n",
                       p+1, lin - L, L, difftime(time(NULL), t0));
                break;
            }
            base[off] ^= (1 << bit);
            printf("  passo %d: consumo %d | flip off %d bit %d | linhas %d -> %d "
                   "(+%d) [%.0fs]\n",
                   p+1, c, off, bit, L, lin, lin - L, difftime(time(NULL), t0));
            L = lin;
            fflush(stdout);
        }
        printf("[+] frame %d terminou com %d linhas reais de ~850 visiveis\n", alvo, L);
        free(cap_buf); cap_buf = NULL;
    }
    /* Rankeia os IDRs por quanto de imagem real sobrou. Saida: <idr> <linhas>
     * (-1 = quadro de ocultacao, nao decodificou nada). */
    else if (!strcmp(modo, "ranking")) {
        int salvo = exigir_imagem; exigir_imagem = 0;   /* queremos ver todos */
        cap_buf = malloc((size_t)1920 * 1088);
        for (int t = 0; t < n_ix; t++) {
            if (!ix[t].idr) continue;
            decodifica(t, t, NULL, 0, NULL);
            int lr = cap_w > 0 ? linhas_reais(cap_buf, cap_w, cap_h) : -1;
            printf("%d %d %d\n", t, lr, ix[t].size);
            fflush(stdout);
        }
        exigir_imagem = salvo;
        free(cap_buf); cap_buf = NULL;
    }
    /* Despeja o plano Y de um frame como PGM, para inspecao visual. */
    else if (!strcmp(modo, "varre2")) {
        alvo2 = atoi(argv[5]);
        int len = ix[alvo2].size;
        n_bits2 = (len - 5) * 8;
        n_pares = (long)n_bits2 * (n_bits2 - 1) / 2;
        int nthr = quantas_threads();
        printf("frame %d: %d bytes, %d bits, %ld pares, %d threads\n",
               alvo2, len, n_bits2, n_pares, nthr);
        fflush(stdout);
        atomic_store(&prox_par, 0);
        atomic_store(&n_pares_achados, 0);
        time_t t0 = time(NULL);
        pthread_t *th = calloc(nthr, sizeof *th);
        for (int i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_par2, NULL);
        for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
        free(th);
        int n = atomic_load(&n_pares_achados);
        printf("[+] frame %d: %d pares resolvem [%.0fs]\n",
               alvo2, n, difftime(time(NULL), t0));
        FILE *g = argc > 6 ? fopen(argv[6], "w") : NULL;
        for (int k = 0; k < n && k < 4096; k++) {
            long o1 = ix[alvo2].off + 5 + pares[k].a / 8;
            long o2 = ix[alvo2].off + 5 + pares[k].b / 8;
            if (g) fprintf(g, "%ld %d %ld %d\n", o1, pares[k].a % 8, o2, pares[k].b % 8);
            else if (k < 30)
                printf("    off %ld bit %d  +  off %ld bit %d\n",
                       o1, pares[k].a % 8, o2, pares[k].b % 8);
        }
        if (g) { fclose(g); printf("    gravados em %s\n", argv[6]); }
    }
    else if (!strcmp(modo, "varrek")) {
        alvok  = atoi(argv[5]);
        prof_k = atoi(argv[6]);
        int len = ix[alvok].size;
        ini_k   = argc > 7 ? atoi(argv[7]) : 5;
        int fim = argc > 8 ? atoi(argv[8]) : len;
        if (ini_k < 5) ini_k = 5;
        if (fim > len) fim = len;
        if (prof_k < 1) prof_k = 1;
        if (prof_k > 4) prof_k = 4;
        nbits_k = (fim - ini_k) * 8;
        aceita_imagem = getenv("IMAGEM") ? atoi(getenv("IMAGEM")) : 0;
        gera_combos();
        int nthr = quantas_threads();
        printf("frame %d: %d bytes, faixa [%d,%d) = %d bits, %d a %d, %ld combinacoes, %d threads, aceite %s\n",
               alvok, len, ini_k, fim, nbits_k, prof_k, prof_k, n_combos, nthr, aceita_imagem ? "produz imagem" : "decodifica limpo");
        fflush(stdout);
        atomic_store(&prox_combo, 0);
        atomic_store(&n_achk, 0);
        time_t t0 = time(NULL);
        pthread_t *th = calloc(nthr, sizeof *th);
        for (int i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_varrek, NULL);
        for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
        free(th);
        int n = atomic_load(&n_achk);
        printf("[+] frame %d: %d combinacoes de %d bits resolvem [%.0fs]\n",
               alvok, n, prof_k, difftime(time(NULL), t0));
        /* Os workers gravam na ordem em que acham, que depende do escalonamento.
         * A saida tem que sair na ordem da combinacao, senao 1 thread e 12 dao
         * arquivos diferentes -- foi o que aconteceu na primeira versao. */
        int lim = n < 4096 ? n : 4096;
        for (int a = 1; a < lim; a++) {
            long ca = achk_c[a]; int tmp[4];
            for (int q = 0; q < prof_k; q++) tmp[q] = achk[a * 4 + q];
            int b = a - 1;
            while (b >= 0 && achk_c[b] > ca) {
                achk_c[b + 1] = achk_c[b];
                for (int q = 0; q < prof_k; q++) achk[(b + 1) * 4 + q] = achk[b * 4 + q];
                b--;
            }
            achk_c[b + 1] = ca;
            for (int q = 0; q < prof_k; q++) achk[(b + 1) * 4 + q] = tmp[q];
        }
        FILE *g = argc > 9 ? fopen(argv[9], "w") : NULL;
        for (int k = 0; k < n && k < 4096; k++) {
            for (int q = 0; q < prof_k; q++) {
                long o = ix[alvok].off + ini_k + achk[k * 4 + q] / 8;
                int  b = achk[k * 4 + q] % 8;
                if (g) fprintf(g, "%ld %d%s", o, b, q + 1 < prof_k ? " " : "\n");
                else if (k < 30) printf("    off %ld bit %d%s", o, b,
                                        q + 1 < prof_k ? "  +" : "\n");
            }
        }
        if (g) { fclose(g); printf("    gravadas em %s\n", argv[9]); }
        free(combos_k);
    }
    else if (!strcmp(modo, "cresce")) {
        int alvo = atoi(argv[5]);
        int len = ix[alvo].size, anc = ancora_de(alvo);
        int ini = argc > 6 ? atoi(argv[6]) : 5;
        int fim = argc > 7 ? atoi(argv[7]) : len;
        if (ini < 5) ini = 5;
        if (fim > len) fim = len;
        int nthr = quantas_threads();
        cap_buf = malloc((size_t)1920 * 1088);
        guardar_croma = 1;
        decodifica(anc, alvo, NULL, 0, NULL);
        int base = cap_w > 0 ? linhas_identicas(cap_buf, cap_w, cap_h) : -1;
        /* calibra a guarda pela parte integra: ate a primeira linha propagada */
        gy0 = 950;
        for (int y = 137; y < 950 && y < cap_h; y++) {
            long dd = 0;
            for (int x = 0; x < cap_w; x += 4)
                dd += abs((int)cap_buf[(size_t)y*cap_w+x] - (int)cap_buf[(size_t)(y-1)*cap_w+x]);
            if (dd == 0) { gy0 = y; break; }
        }
        croma_stat(136, gy0, &gu_med, &gu_des, &gv_med, &gv_des);
        printf("  parte integra ate a linha %d | croma U %.1f+-%.1f  V %.1f+-%.1f\n",
               gy0, gu_med, gu_des, gv_med, gv_des);
        printf("  guarda: media a menos de 4,0 e desvio ate 1,15x disso, na faixa [%d,950)\n", gy0);
        printf("frame %d: %d bytes, faixa [%d,%d), %d candidatos, %d threads\n",
               alvo, len, ini, fim, (fim - ini) * 8, nthr);
        printf("  linhas identicas hoje: %d de 813  (quadro bom = 0)\n", base);
        fflush(stdout);
        free(cap_buf); cap_buf = NULL;
        n_cand = (fim - ini) * 8;
        if (argc > 8) {
            medidas = calloc((size_t)n_cand, sizeof *medidas);
            if (medidas)
                printf("  despejando %d medidas em %s (%.1f MB)\n",
                       n_cand, argv[8], n_cand * sizeof(Medida) / 1e6);
        }
        atomic_store(&prox_cand, 0);
        WorkerC *w = calloc(nthr, sizeof *w);
        pthread_t *th = calloc(nthr, sizeof *th);
        time_t t0 = time(NULL);
        for (int i = 0; i < nthr; i++) {
            w[i].alvo = alvo; w[i].ancora = anc; w[i].ini = ini;
            w[i].nota = 1 << 20; w[i].idx = n_cand + 1;
            pthread_create(&th[i], NULL, worker_cresce, &w[i]);
        }
        int nota = 1 << 20, off = -1, bit = -1, idx = n_cand + 1;
        for (int i = 0; i < nthr; i++) {
            pthread_join(th[i], NULL);
            if (w[i].nota < nota || (w[i].nota == nota && w[i].idx < idx)) {
                nota = w[i].nota; off = w[i].off; bit = w[i].bit; idx = w[i].idx;
            }
        }
        free(w); free(th);
        printf("[+] melhor: off %ld bit %d -> %d linhas identicas "
               "(era %d) [%.0fs]\n",
               ix[alvo].off + off, bit, nota, base, difftime(time(NULL), t0));
        if (medidas) {
            FILE *g = fopen(argv[8], "w");
            /* CARIMBO. As medidas valem so enquanto a CADEIA do alvo nao mudar:
             * patch no proprio frame ou em qualquer um entre a ancora e ele
             * invalida tudo. Patch em outra parte do filme nao. Sem isto, usar
             * um despejo velho seria erro silencioso -- os numeros pareceriam
             * bons medindo um estado que nao existe mais. */
            long ini_b = ix[anc].off, fim_b = ix[alvo].off + ix[alvo].size;
            uint64_t h = 1469598103934665603ULL; int n_cad = 0;
            for (int k2 = 0; k2 < n_patch; k2++) {
                if (patches[k2].off < ini_b || patches[k2].off >= fim_b) continue;
                h ^= (uint64_t)patches[k2].off; h *= 1099511628211ULL;
                h ^= (uint64_t)patches[k2].bit; h *= 1099511628211ULL;
                n_cad++;
            }
            fprintf(g, "# alvo %d ancora %d faixa [%d,%d) cadeia_patches %d cadeia_hash %016llx\n",
                    alvo, anc, ini, fim, n_cad, (unsigned long long)h);
            fprintf(g, "# vale enquanto a cadeia nao mudar: refazer o hash dos patches em"
                       " [%ld,%ld) e comparar\n", ini_b, fim_b);
            fprintf(g, "# off bit nota primeira_propagada U_med U_des V_med V_des blocagem\n");
            for (int k = 0; k < n_cand; k++) {
                Medida *m = &medidas[k];
                if (m->nota < 0) continue;          /* sem imagem, nao interessa */
                fprintf(g, "%ld %d %d %d %d %.1f %d %.1f %.2f\n",
                        ix[alvo].off + ini + k / 8, k % 8, m->nota, m->prim,
                        m->um, m->ud / 10.0, m->vm, m->vd / 10.0, m->bloc / 50.0);
            }
            fclose(g);
            printf("  despejo gravado em %s\n", argv[8]);
            free(medidas); medidas = NULL;
        }
    }
    else if (!strcmp(modo, "campo")) {
        campo_alvo = atoi(argv[5]);
        FILE *f = fopen(argv[6], "r");
        if (!f) { fprintf(stderr, "nao abriu %s%s", argv[6], "\n"); return 1; }
        campos = calloc(200000, sizeof *campos);
        long o; int b;
        while (fscanf(f, "%ld %d", &o, &b) == 2) {
            campos[n_campos].off = o; campos[n_campos].bit = b; n_campos++;
        }
        fclose(f);
        if (argc > 7) {
            FILE *r = fopen(argv[7], "rb");
            if (r) {
                char m[3]; int mx;
                fscanf(r, "%2s %d %d %d", m, &alvo_w, &alvo_h, &mx);
                fgetc(r);
                alvo_img = malloc((size_t)alvo_w * alvo_h);
                fread(alvo_img, 1, (size_t)alvo_w * alvo_h, r);
                fclose(r);
                fprintf(stderr, "[+] referencia %dx%d carregada de %s\n",
                        alvo_w, alvo_h, argv[7]);
            }
        }
        int nthr = quantas_threads();
        printf("frame %d: medindo %d candidatos com %d threads%s",
               campo_alvo, n_campos, nthr, "\n");
        fflush(stdout);
        atomic_store(&prox_cand, 0);
        pthread_t *th = calloc(nthr, sizeof *th);
        for (int i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_campo, NULL);
        for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
        free(th);
        printf("off bit campo_min campo_max L949 L952 L960 tarja_min tarja_max tarja_media tarja_desvio topo_media dif_referencia trans_media trans_desvio trans_maxdev trans_fora% img_media img_desvio img_blocagem\n");
        for (int k = 0; k < n_campos; k++) {
            if (campos[k].pmin < 0) continue;
            printf("%ld %d %d %d %d %d %d %d %d %.3f %.3f %.3f %.4f %.2f %.2f %d %.3f %.2f %.2f %.4f\n",
                   campos[k].off, campos[k].bit, campos[k].pmin, campos[k].pmax,
                   campos[k].l949, campos[k].l952, campos[k].l960,
                   campos[k].bmin, campos[k].bmax,
                   campos[k].bmed, campos[k].bdes, campos[k].tmed, campos[k].dref,
                   campos[k].tmed2, campos[k].tdes2, campos[k].tmaxdev, campos[k].tfora,
                   campos[k].imed, campos[k].ides, campos[k].ibloc);
        }
        free(campos);
    }
    else if (!strcmp(modo, "panorama")) {
        /* Decodifica cada GOP numa passada e mede TODO quadro emitido. Uma
         * decodificacao por frame, contra as ~13 que medir um a um custaria. */
        panorama = 1; cap_qualquer = 1;
        cap_buf = malloc((size_t)1920 * 1088);
        printf("frame campo_med campo_des tarja_med tarja_des listra vh\n");
        fflush(stdout);
        for (int t = 0; t < n_ix; t++) {
            if (!ix[t].idr) continue;
            int fim = t + 1;
            while (fim < n_ix && !ix[fim].idr) fim++;
            decodifica(t, fim - 1, NULL, 0, NULL);
            fflush(stdout);
        }
        panorama = 0; cap_qualquer = 0;
        free(cap_buf); cap_buf = NULL;
    }
    else if (!strcmp(modo, "cortes")) {
        /* Lista o ponto de corte de todo IDR que nao decodifica. E o que define
         * a faixa a varrer: a armadilha 9 mede o bit ~2600 bytes ANTES do corte,
         * entao a janela util e [5, corte+folga] -- e nao a de +-1024 em volta do
         * corte das varreduras antigas, que erra o alvo quando o corte e cedo. */
        int folga = argc > 5 ? atoi(argv[5]) : 1024;
        cap_buf = malloc((size_t)1920 * 1088);
        long total = 0; int n = 0;
        printf("idr tamanho corte fim_faixa candidatos listra pct\n");
        for (int t = 0; t < n_ix; t++) {
            if (!ix[t].idr) continue;
            if (decodifica(t, t, NULL, 0, NULL) == 0) continue;   /* ja decodifica */
            /* Quanto do quadro e listra, medido ANTES do acha_consumo, que
             * redecodifica e sobrescreve o cap_buf. A blocagem nao separa
             * listra de cena (armadilha 18) e foi por isso que o IDR 1712
             * entrou na lista dos reparaveis sendo 71% propagacao. */
            int listra = cap_w > 0 ? linhas_identicas(cap_buf, cap_w, cap_h) : -1;
            int c = acha_consumo(t);
            int fim = c + folga; if (fim > ix[t].size) fim = ix[t].size;
            long cand = (long)(fim - 5) * 8;
            printf("%d %d %d %d %ld %d %.1f\n", t, ix[t].size, c, fim, cand,
                   listra, listra < 0 ? -1.0 : listra * 100.0 / 813.0);
            fflush(stdout);
            total += cand; n++;
        }
        fprintf(stderr, "[+] %d IDRs quebrados, %ld candidatos na soma das faixas\n",
                n, total);
        free(cap_buf); cap_buf = NULL;
    }
    else if (!strcmp(modo, "corte")) {
        int alvo = atoi(argv[5]);
        cap_buf = malloc((size_t)1920 * 1088);
        printf("frame %d (%d bytes): o decoder para de consumir por volta do byte %d\n",
               alvo, ix[alvo].size, acha_consumo(alvo));
        free(cap_buf); cap_buf = NULL;
    }
    else if (!strcmp(modo, "varre1")) {
        /* Varredura exaustiva de 1 bit sobre o NAL INTEIRO de um frame comum.
         * O modo `unico` so olha IDR; este serve para os frames quebrados que
         * estao cercados de frames bons, que sao os melhores alvos de reparo. */
        int alvo = atoi(argv[5]);
        int len = ix[alvo].size, anc = ancora_de(alvo);
        /* Janela opcional. Sem ela varre o NAL inteiro; com ela repete o recorte
         * do INVESTIGACOES.md (em volta do corte e no inicio do NAL), que e
         * ordens de grandeza mais barato num IDR de 150 KB. */
        int jini = argc > 7 ? atoi(argv[7]) : 5;
        int jfim = argc > 8 ? atoi(argv[8]) : len;
        if (jini < 5) jini = 5;
        if (jfim > len) jfim = len;
        int nthr = quantas_threads();
        int cap = 4096;
        Sol *sol = malloc((size_t)cap * sizeof(Sol));
        /* Sem teto: o 4096 fixo truncava pelo indice, que e a ordem do offset,
         * e guardava justamente o comeco do NAL. No IDR 1773 as 26.865 solucoes
         * viraram as 4.096 de rel 10537 a 21067, e o corte esta em 33489 -- a
         * regiao onde o bit provavelmente esta ficou de fora do arquivo. */
        int maxsol = (jfim - jini) * 8;
        int *offs = malloc((size_t)maxsol * sizeof(int));
        int *bits = malloc((size_t)maxsol * sizeof(int));
        int guardados = 0;
        printf("frame %d: %d bytes, ancora %d, faixa [%d,%d), %d candidatos, %d threads%s",
               alvo, len, anc, jini, jfim, (jfim - jini) * 8, nthr, "\n");
        fflush(stdout);
        time_t t0 = time(NULL);
        int total = conta_solucoes_par(anc, alvo, jini, jfim, offs, bits,
                                       maxsol, &guardados, nthr);
        printf("[+] frame %d: %d solucoes de 1 bit [%.0fs]%s",
               alvo, total, difftime(time(NULL), t0), "\n");
        if (argc > 6) {
            FILE *g = fopen(argv[6], "w");
            for (int i = 0; i < guardados; i++)
                fprintf(g, "%ld %d\n", ix[alvo].off + offs[i], bits[i]);
            fclose(g);
            printf("    %d solucoes gravadas em %s\n", guardados, argv[6]);
        } else {
            for (int i = 0; i < guardados && i < 40; i++)
                printf("    off %ld bit %d  (rel %d)\n",
                       ix[alvo].off + offs[i], bits[i], offs[i]);
        }
        free(sol); free(offs); free(bits);
    }
    else if (!strcmp(modo, "testa")) {
        FILE *f = fopen(argv[5], "r");
        if (!f) { fprintf(stderr, "nao abriu %s%s", argv[5], "\n"); return 1; }
        cands = calloc(100000, sizeof *cands);
        int t, gop, pos; char campo[32]; int visto, esp; long off; int bit; char est[32];
        while (fscanf(f, "%d %d %d %31s %d %d %ld %d %31s",
                      &t, &gop, &pos, campo, &visto, &esp, &off, &bit, est) == 9) {
            cands[n_cands].alvo = t; cands[n_cands].off = off;
            cands[n_cands].bit = bit; cands[n_cands].ok = 0; n_cands++;
        }
        fclose(f);
        int nthr = quantas_threads();
        printf("testando %d candidatos com %d threads%s", n_cands, nthr, "\n");
        atomic_store(&prox_cand, 0);
        pthread_t *th = calloc(nthr, sizeof *th);
        for (int i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_testa, NULL);
        for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
        free(th);
        int bons = 0;
        for (int k = 0; k < n_cands; k++) {
            if (cands[k].ok == 1) {
                printf("PASSA frame %d off %ld bit %d%s",
                       cands[k].alvo, cands[k].off, cands[k].bit, "\n");
                bons++;
            }
        }
        printf("%s[+] %d de %d candidatos fazem o frame decodificar limpo%s",
               "\n", bons, n_cands, "\n");
        free(cands);
    }
    else if (!strcmp(modo, "serie")) {
        /* Extrai uma faixa de frames em I420 cru, numa passada so, mais um mapa
         * de quais decodificaram. E a materia-prima da REMONTAGEM -- que e
         * ocultacao, nao reparo: os frames quebrados sao preenchidos depois pela
         * media dos vizinhos de exibicao, o que da video assistivel sem afirmar
         * nada sobre os bits. Nunca confundir com o patches.txt. */
        int ini = atoi(argv[5]), fim = atoi(argv[6]);
        int aceita_tarja = getenv("TARJA") ? atoi(getenv("TARJA")) : 0;
        guardar_croma = 1;
        cap_buf = malloc((size_t)1920 * 1088);
        FILE *g = fopen(argv[7], "wb");
        FILE *m = fopen(argv[8], "w");
        size_t ny = (size_t)1920 * 1080, nc = (size_t)960 * 540;
        uint8_t *zero = calloc(1, ny > nc ? ny : nc);
        int bons = 0;
        for (int t = ini; t <= fim && t < n_ix; t++) {
            int r = decodifica(ancora_de(t), t, NULL, 0, NULL);
            int tem = (cap_w == 1920 && cap_h == 1080);
            /* TARJA=1 aceita tambem quadro de imagem certa que o criterio
             * rigoroso rejeita -- 14 frames dos dois fades estao assim. */
            int ok = tem && (r == 0 ||
                     (aceita_tarja && tarja_perfeita(cap_buf, cap_w, cap_h)));
            if (ok) {
                fwrite(cap_buf, 1, ny, g);
                fwrite(cap_u ? cap_u : zero, 1, nc, g);
                fwrite(cap_v ? cap_v : zero, 1, nc, g);
                bons++;
            } else {
                fwrite(zero, 1, ny, g);
                fwrite(zero, 1, nc, g); fwrite(zero, 1, nc, g);
            }
            fprintf(m, "%d %s\n", t, ok ? "ok" : "quebrado");
        }
        fclose(g); fclose(m); free(zero);
        printf("faixa %d-%d: %d de %d decodificaram -> %s\n",
               ini, fim, bons, fim - ini + 1, argv[7]);
        free(cap_buf); cap_buf = NULL;
    }
    else if (!strcmp(modo, "dumpyuv")) {
        /* Grava o quadro inteiro em I420 cru, para remontagem externa. O resto
         * do programa decide por luma; aqui o croma importa porque a saida vai
         * virar video para assistir. */
        int alvo = atoi(argv[5]);
        guardar_croma = 1;
        cap_buf = malloc((size_t)1920 * 1088);
        int r = decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
        if (cap_w <= 0) { fprintf(stderr, "frame %d nao produz imagem\n", alvo); return 1; }
        FILE *g = fopen(argv[6], "wb");
        fwrite(cap_buf, 1, (size_t)cap_w * cap_h, g);
        int cw = cap_w / 2, ch = cap_h / 2;
        if (cap_u) { fwrite(cap_u, 1, (size_t)cw * ch, g); fwrite(cap_v, 1, (size_t)cw * ch, g); }
        fclose(g);
        printf("frame %d -> %s  %dx%d I420 (decode %s)\n",
               alvo, argv[6], cap_w, cap_h, r ? "COM ERRO" : "limpo");
        free(cap_buf); cap_buf = NULL;
    }
    else if (!strcmp(modo, "dump")) {
        int alvo = atoi(argv[5]);
        const char *saida = argv[6];
        cap_buf = malloc((size_t)1920 * 1088);
        int r = decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
        /* Frame que nao decodifica nao produz quadro com o pts dele -- a captura
         * casada por pts devolve vazio, que e a resposta correta. Mas para
         * INSPECAO interessa ver o que o decoder poe na tela no lugar dele: o
         * quadro de ocultacao. Por isso a segunda tentativa, sem casar o pts, e
         * rotulada como ocultacao para ninguem confundir com o frame. */
        int ocultacao = 0;
        if (cap_w <= 0) {
            cap_qualquer = 1;
            r = decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
            cap_qualquer = 0;
            ocultacao = 1;
        }
        if (cap_w <= 0) { fprintf(stderr, "sem imagem nenhuma\n"); return 1; }
        FILE *g = fopen(saida, "wb");
        fprintf(g, "P5\n%d %d\n255\n", cap_w, cap_h);
        fwrite(cap_buf, 1, (size_t)cap_w * cap_h, g);
        fclose(g);
        printf("frame %d (ancora %d, decode %s)%s -> %s  %dx%d | "
               "propagacao %.1f%% | blocagem %.3f\n",
               alvo, ancora_de(alvo), r ? "COM ERRO" : "limpo",
               ocultacao ? "  [!] OCULTACAO, nao e o frame" : "",
               saida, cap_w, cap_h,
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
