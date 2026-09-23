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
#include "juizes.h"

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
static int traco = 0;           /* TRACO=1: mapa de tipos de macrobloco */
static _Thread_local int log_erros = 0;
static int erros_base = 0;      /* erros que o caminho ja tem sem nenhum flip */
static _Thread_local long log_bytestream = -1;
static _Thread_local int log_ocultados = -1;
static _Thread_local int log_mbx = -1, log_mby = -1;
/* Macroblocos que o ffmpeg OCULTOU no quadro cujo pacote acabou de ser enviado.
 * A mensagem "concealing N DC" sai no nivel INFO, e o filtro abaixo descartava
 * tudo acima de ERROR: o reparador nunca viu ocultacao -- nem o `log_ocultados`,
 * que so era preenchido depois do filtro e por isso sempre valeu -1. Slice que
 * acaba cedo (end_of_slice antes do ultimo MB) nao gera erro nenhum; so esta
 * mensagem o denuncia (armadilha 59). Sem CHUNKS, o h264 fecha cada quadro no
 * fim do proprio pacote, entao a mensagem chega durante o envio dele. */
static _Thread_local int log_ocultados_quadro = -1;

/* Callback de log do libavcodec. E daqui que sai TODA a informacao de
 * diagnostico: o decodificador escreve o macrobloco onde falhou e quantos bytes
 * sobraram, e nao ha outra forma de saber isso de fora.
 *
 *   avcl   contexto, ignorado
 *   nivel  severidade do libavcodec; acima de ERROR so passa com TRACO=1
 *   fmt,vl a mensagem
 *
 * Nao devolve nada; preenche log_mbx, log_mby, log_bytestream, log_ocultados e
 * conta log_erros. Todos sao _Thread_local, senao workers se atropelariam.
 *
 * `bytestream` e o gradiente barato: consumo = tamanho - bytestream. */
static void meu_log(void *avcl, int nivel, const char *fmt, va_list vl) {
    /* TRACO=1 deixa passar o nivel de depuracao, que e onde o h264 do ffmpeg
     * imprime o mapa de tipos de macrobloco -- uma linha por fileira, um
     * caractere por macrobloco. E o unico jeito de ver ONDE o CABAC saiu do
     * lugar em vez de so onde o erro apareceu. */
    if (nivel > (traco ? AV_LOG_DEBUG : AV_LOG_INFO)) return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    long bs; int mb1, mb2, oc;
    if (sscanf(buf, "concealing %d DC", &oc) == 1) log_ocultados_quadro = oc;
    if (nivel > AV_LOG_ERROR) {           /* INFO e DEBUG: nao contam como erro */
        if (traco) fputs(buf, stderr);
        return;
    }
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

/* Monta o extradata avcC a partir do SPS e do PPS em hexadecimal.
 *
 *   sps_hex, pps_hex  os parametros JA CORRIGIDOS, em hexa
 *
 * Nao devolve nada; preenche o buffer global que `abre_decoder` entrega ao
 * libavcodec. Usa os corrigidos e nao os do arquivo de proposito: o SPS do MP4
 * tem 4 bits errados e o PPS 2, e sem isso nada decodifica. Ver ESTADO.md. */
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

/* Cria o AVCodecContext da thread, ou reaproveita o que ja existe.
 *
 * O contexto e _Thread_local: cada worker tem o seu, e por isso a varredura
 * paralela nao compartilha estado de decodificacao.
 *
 * Nao recebe nem devolve nada. Duas escolhas que nao se mexem:
 *   thread_count = 1  determinismo acima de velocidade. Ver PARALELIZACAO.md.
 *   extradata         o avcC montado do SPS/PPS corrigidos, nao o do arquivo. */
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
    if (traco) ctx->debug = FF_DEBUG_MB_TYPE;
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

/* Lookahead: quantos quadros ALEM do alvo alimentar o decoder.
 *
 * O modelo de cadeia -- alimentar de ancora ate alvo e exigir quadros ==
 * pacotes -- supoe que todo quadro so depende do que veio antes dele em ordem
 * de decodificacao. No GOP 0 isso e falso: o frame 2 morre com "co located POCs
 * unavailable" na cadeia 0..2 e decodifica sem problema quando o GOP inteiro
 * passa. Varredura em qualquer frame daquele GOP depois do 1 devolvia zero por
 * construcao.
 *
 * Fica em 0 por padrao, que e o comportamento historico: todo resultado do
 * projeto foi medido assim, e mudar o padrao invalidaria a comparacao. FOLGA=n
 * estende o alcance, limitado ao fim do GOP -- estender alem dele traria os
 * erros de outros frames quebrados para dentro do log e reprovaria tudo. */
static int folga_lookahead = 0;
static int aceita_oculto = 0;          /* ACEITA_OCULTO=1: criterio sem a parte da ocultacao */
static _Thread_local int ocultados_alvo = -1;  /* MBs ocultados no alvo da ultima decodifica() */

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

/* Mede um quadro e imprime a linha do `panorama`, sem guardar nada.
 *
 *   fr  o AVFrame recem-decodificado
 *
 * Nao devolve nada. Existe para o panorama percorrer o filme inteiro numa
 * passada: medir e imprimir na hora custa menos que capturar 3.445 quadros. */
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

/* Guarda o quadro decodificado nos buffers da thread, se for o quadro pedido.
 *
 *   fr  o AVFrame que o decodificador acabou de entregar
 *
 * Nao devolve nada; preenche cap_buf, cap_w, cap_h, cap_hash e, quando
 * `guardar_croma`, tambem cap_u e cap_v.
 *
 * Casa por pts: quadro cujo pts nao e o alvo e IGNORADO, senao a medida sairia
 * do quadro errado numa cadeia de doze. `cap_qualquer` desliga esse casamento,
 * e ai o que se captura e ocultacao -- util para inspecao, nunca para julgar. */
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
    /* Nao passa do fim do GOP: o proximo IDR reinicia as referencias, entao
     * alimentar alem dele nao ajuda o alvo e so traz erro alheio. */
    int ate = alvo + folga_lookahead;
    if (ate >= n_ix) ate = n_ix - 1;
    for (int i = alvo + 1; i <= ate; i++)
        if (ix[i].idr) { ate = i - 1; break; }
    for (int i = ancora; i <= ate; i++) {
        const uint8_t *src; int len;
        if (i == alvo && alt) { src = alt; len = alt_len; }
        else { src = arq + ix[i].off; len = ix[i].size; }
        /* O log guarda o ULTIMO erro visto. Zerando aqui, o que sobrar depois
         * de mandar o alvo e o erro DO ALVO -- e -1 quer dizer que ele nao
         * errou, mesmo que quadros anteriores da cadeia tenham errado. */
        if (i == alvo) { log_mbx = -1; log_mby = -1; log_bytestream = -1; }
        log_ocultados_quadro = -1;
        av_new_packet(pkt, len);
        memcpy(pkt->data, src, len);
        pkt->pts = i;                  /* casa o quadro de saida com o frame pedido */
        if (avcodec_send_packet(ctx, pkt) == 0) enviados++;
        av_packet_unref(pkt);
        while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
        if (i == alvo) ocultados_alvo = log_ocultados_quadro;
    }
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, fr) == 0) { quadros++; captura_frame(fr); av_frame_unref(fr); }
    av_frame_free(&fr); av_packet_free(&pkt);
    if (quadros_out) *quadros_out = quadros;
    int esperado = ate - ancora + 1;
    /* Erros que o proprio caminho ja tem, antes de qualquer flip, nao podem
     * reprovar o candidato -- senao a varredura fica insatisfazivel e o zero
     * nao quer dizer nada. O frame 11 emite duas linhas de erro em TODA
     * decodificacao do GOP 0, e com ERROS_BASE=0 a varredura do frame 12
     * devolveu 0 solucoes sem ter testado nada de verdade.
     * Medir a base antes de varrer, e passar aqui. */
    int ok = (log_erros <= erros_base && quadros == esperado);
    /* Terceira parte: o alvo nao pode ter macrobloco OCULTADO. Slice que acaba
     * cedo passa nas duas partes acima -- sem erro, quadro emitido -- e a imagem
     * vira ocultacao de vizinhos bons, que parece certa. Foi assim que o reparo
     * antigo do 2361 "passou em tudo" e que o `repair` achou outro atalho igual
     * (armadilha 59). ACEITA_OCULTO=1 volta ao criterio antigo, para comparar. */
    if (ok && !aceita_oculto) ok = (ocultados_alvo <= 0);
    /* Segunda parte do criterio: a imagem tem que existir. Sem isto passa
     * slice que termina cedo e vira listra -- armadilha 7 do ARMADILHAS.md. */
    if (ok && exigir_imagem)
        ok = (cap_w > 0 && propagacao(cap_buf, cap_w, cap_h) < 0.995);
    return ok ? 0 : 1;
}

/* Quantos macroblocos do alvo da ultima decodifica() foram decodificados DE FATO.
 *
 * Sem argumentos: le log_mbx/log_mby e ocultados_alvo da thread.
 *
 * Devolve: o endereco do MB do erro quando houve erro; 8160 - ocultados quando
 * o slice acabou cedo SEM erro; 8160 quando o quadro saiu inteiro. Antes o
 * segundo caso valia 8160 -- nota maxima para o atalho do end_of_slice que o
 * `repair` e o reparo antigo do 2361 acharam (armadilha 59). */
static int mb_alcancado(void) {
    if (log_mbx >= 0) return log_mby * 120 + log_mbx;
    if (ocultados_alvo > 0) return 8160 - ocultados_alvo;
    return 8160;
}

/* Acha o IDR que ancora um quadro: o ultimo quadro-chave em `alvo` ou antes.
 *
 *   alvo  indice do quadro no index.txt
 *
 * Devolve: o indice do IDR, ou 0 se nao houver nenhum antes. Toda decodificacao
 * comeca aqui, porque quadro comum nao decodifica sozinho -- e por isso GOP com
 * IDR quebrado esta bloqueado por dependencia, nao por dano proprio. */
static int ancora_de(int alvo) {
    for (int i = alvo; i >= 0; i--) if (ix[i].idr) return i;
    return 0;
}

/* ---- busca do byte culpado ----
 *
 *   ancora, alvo  indices no index.txt; a cadeia vai de um ao outro
 *
 * Devolve: o byte ate onde o decodificador consumiu antes de falhar. Prefere a
 * conta do proprio log (tamanho - bytestream) e so cai na busca binaria por
 * truncamento quando o log nao informa.
 */
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
 * Devolve o offset no payload (>=0) e escreve o bit em *bit_out.
 *
 *   alvo     indice do quadro a consertar
 *   janela   quantos bytes em volta do corte varrer
 *   bit_out  recebe o bit achado, quando houver
 *
 * Devolve: o deslocamento do byte consertado, ou -1 se nao achou. GRAVA no
 * patches.txt quando acha -- e o unico caminho que escreve em fonte de verdade.
 */
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
 * decoder parar ali. Busca binaria: O(log n) decodificacoes.
 *
 *   alvo  indice do quadro
 *
 * Devolve: o byte a partir do qual truncar nao muda mais o resultado, ou seja,
 * ate onde o decodificador realmente le. Busca binaria por truncamento.
 */
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

/* Busca de 1 bit restrita a [ini,fim). Nao grava nada.
 *
 *   ancora, alvo      indices no index.txt
 *   ini, fim          a faixa de bytes a varrer, [ini, fim)
 *   off_out, bit_out  recebem o primeiro acerto
 *
 * Devolve: 1 se achou, 0 se nao. Para no PRIMEIRO acerto, entao serve para
 * decidir "existe solucao?" e nao para contar quantas.
 */
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

/* ============================ OS WORKERS ============================
 *
 * Os nove seguem o mesmo esqueleto, e a reescrita do item 7 vai unifica-lo:
 *
 *   1. aloca buffers PROPRIOS -- copia do NAL e cap_buf. Nada compartilhado.
 *   2. puxa o proximo indice de um contador atomico, ate acabar.
 *   3. aplica o flip na copia, decodifica a cadeia desde a ancora, pontua,
 *      e devolve a copia ao estado anterior.
 *
 * Todos recebem `void *p` e devolvem NULL: a assinatura e imposta pelo
 * pthread_create, nao pelo problema. Alguns leem a tarefa de uma struct em `p`,
 * outros de variaveis globais preenchidas antes do disparo -- a diferenca esta
 * anotada em cada um.
 *
 * O determinismo NAO vem de sorte de escalonamento: os candidatos sao numerados
 * na ordem sequencial, distribuidos por contador atomico e a saida e ordenada
 * pelo indice no fim. Ver a secao 7 do AGENTS.md, e nao mexer nisso sem provar
 * com THREADS=1.
 * ==================================================================== */

/* Varre 1 bit por candidato numa faixa, para o modo `unico` e o `varre_par`.
 *
 *   p  ignorado; a tarefa vem de `prox_cand` e das globais da varredura
 *
 * Pontua: aceita ou rejeita segundo o criterio de `decodifica`, e opcionalmente
 * mede a blocagem. Suporta parada antecipada pela regra do menor indice --
 * achar em k para de puxar indices >= k, mas termina os menores que ja estao em
 * voo. Nunca "a primeira que chegar": ha quadros com milhares de solucoes e a
 * escolha por chegada pegaria bit errado.
 *
 *   p  ignorado; a tarefa vem do contador atomico e das globais da varredura
 *
 * Devolve: NULL sempre. A assinatura e imposta pelo pthread_create.
 */
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

/* Comparador do qsort: ordena solucoes pelo indice do candidato.
 *
 *   a, b  ponteiros para Sol
 *
 * Devolve: -1, 0 ou 1. A ordem do INDICE, e nao a de chegada, e o que torna a
 * saida paralela identica a sequencial -- ver a regra do menor indice na secao
 * 7 do AGENTS.md. */
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

/* Varre PARES de bits, para o modo `varre2`.
 *
 *   p  ignorado; a tarefa vem de `prox_par` e de `pares`
 *
 * Pontua: aceita ou rejeita. Os pares vem pre-gerados num vetor global de teto
 * FIXO (4096) -- faixa maior que isso trunca em silencio.
 *
 *   p  ignorado; a tarefa vem de `prox_par` e do vetor `pares`
 *
 * Devolve: NULL sempre.
 */
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
static int  *combos_k;
static long  n_combos;
static _Atomic long prox_combo;
/* Sem teto fixo: o varre1 ja tinha esse bug e foi corrigido; aqui ficou. No
 * frame 11 a janela [5,200) achou 22.243 solucoes e o arquivo saiu com 4.096,
 * as primeiras por indice de combinacao -- ou seja, enviesadas para o comeco da
 * janela, que e exatamente onde menos se quer truncar. */
static int  *achk = NULL;
static long *achk_c = NULL;
static long  achk_cap = 0;
static _Atomic int n_achk;

static int *placar = NULL;      /* pontuacao por combinacao, modo avanco */
static int piso_tarja = 0;      /* PISO_TARJA=1: sem tarja 16 nao ha pontuacao */
static int piso_topo  = 0;      /* PISO_TOPO=1: tarja de CIMA uniforme */
static int piso_base  = 0;      /* PISO_BASE=1: tarja de BAIXO uniforme, valor livre */
static int consumo_t  = 0;      /* CONSUMO=n: quadro que fecha truncado em n bytes e atalho */
static int iguais     = 0;      /* IGUAIS=1: grava tambem quem empata com a base */
static int piso_croma = 0;      /* PISO_CROMA=1: desvio de croma dentro da faixa real */
static int piso_trinca = 0;     /* PISO_TRINCA=1: libera + limpa + borrao intacto */
static int base_fronteira = -1; /* fronteira do borrao sem nenhum flip */
static int base_ntrechos = -1, base_maior = -1, base_linhas = -1, base_longos = -1;

/* O juiz que faltava, e ele veio do olho do usuario antes de vir da medida.
 *
 * A listra premia ruido: linha diferente da de cima satisfaz o criterio, e lixo
 * decodificado satisfaz trivialmente. Encadeando no IDR 3047 eu levei a listra
 * de 69,4% para 40,0% trocando borrao limpo por faixa cheia de artefato
 * colorido -- "melhorou" na metrica e piorou na imagem.
 *
 * O croma separa os tres estados, e a faixa e estreita. Medido em 12 quadros
 * verificados de dois trechos diferentes do filme, na regiao 160-639:
 *
 *     imagem real .... desvio U de 5,99 a 8,52   V de 3,40 a 7,42
 *     borrao ......... U 2,78  V 2,25   -- repetir linha achata a cor
 *     lixo ........... U 21,43 V 15,49  -- 2,5x acima do real
 *
 * Por isso e FAIXA e nao limiar: baixo demais e borrao, alto demais e lixo, e a
 * imagem verdadeira esta no meio. Ver armadilha 39. */
/* ---- o juiz de tres partes ----
 *
 * Veio da leitura do usuario sobre o IDR 3047, e cada parte corrige um jeito
 * meu de medir errado:
 *
 *  1. LIBERA  a fronteira do borrao tem que DESCER. No original o primeiro
 *             trecho grande de copia comeca na linha 292; com o candidato bom
 *             comeca na 328.
 *  2. LIMPA   a faixa liberada nao pode ter artefato. Blocagem restrita a essa
 *             faixa separa forte -- 0,97 em quadro bom, 1,34 no candidato bom,
 *             2,08 no ruim -- e eu tinha descartado a blocagem por medi-la no
 *             quadro inteiro, onde ela nao discrimina.
 *  3. INTACTA o borrao que SOBRA tem que continuar limpo. Medido: o candidato
 *             bom mantem 167 trechos com maior de 19 linhas, igual ao original;
 *             o ruim cai para 158 trechos com maior de 11. Borrao fragmentado
 *             nao e borrao, e ruido invadindo a regiao borrada.
 *
 * A terceira e a que inverte o que eu vinha fazendo: menos listra so e melhor
 * ACIMA da fronteira. Abaixo dela, a listra tem que ficar como estava. */
/* Tolerancia da comparacao entre linhas vizinhas. Era igualdade exata, e
 * igualdade exata NAO mede o borrao: mede uma coincidencia fragil.
 *
 * Medido no IDR 3047, nas linhas 400 a 1070: em 100% delas a diferenca para a
 * linha de cima e no maximo 1, mas so ~75% sao exatamente iguais, espalhadas.
 * Exigir 8 linhas exatas seguidas e um evento de sorte -- um bit de dither
 * quebra a corrida e a fronteira salta centenas de linhas. Dois quadros com o
 * MESMO borrao mediam 328 e 1080.
 *
 * Foi assim que a etapa 2 devolveu 591 candidatos "sem borrao nenhum" que, na
 * tela, sao o mesmo quadro listrado da base. TOL_COPIA=0 volta ao antigo. */



/* Blocagem: salto medio nas bordas de macrobloco contra o do interior. Num
 * quadro bom fica perto de 1,0; lixo decodificado passa de 2. */
/* Respingo de croma na faixa liberada: p99 da diferenca entre pixels VIZINHOS
 * de U e de V, o maior dos dois planos. Em histograma de 256 baldes, exato para
 * inteiros e sem ordenacao.
 *
 * Existe porque a blocagem nao pega o artefato que o olho pega. Os candidatos
 * com respingo colorido na linha da fronteira tinham blocagem MELHOR que o
 * aprovado -- 1,194 e 1,254 contra 1,375 -- porque o respingo e pequeno em area
 * e a media o dilui. O p99 do vizinho de croma, medido so na faixa liberada,
 * separa na mesma ordem do olho:
 *
 *   original (borrao liso) .... 2 / 1
 *   o candidato do olho ....... 3 / 7
 *   os dois da blocagem ....... 7 / 9  e  16 / 19
 *   o descartado pelo olho .... 14 / 14
 *
 * O teto de 8 e o dobro do pior quadro INTACTO medido (p99U ate 6, p99V ate 3,
 * em 20 faixas de 5 quadros). Isso quer dizer que nem o melhor candidato chega
 * a ter faixa liberada com cara de imagem: ele e o menos ruim, nao um acerto.
 *
 *   y0, y1  a faixa a medir, [y0, y1), em linhas do QUADRO (nao do plano)
 *
 * Devolve: o maior p99 entre os planos U e V, ou -1 quando nao da para medir --
 * sem captura, faixa curta demais ou menos de mil amostras.
 */
static int croma_salto_p99(int y0, int y1) {
    if (!cap_u || !cap_v || cap_w <= 0 || y1 > cap_h || y1 - y0 < 8) return -1;
    int cw = cap_w / 2, h0 = y0 / 2, h1 = y1 / 2, pior = -1;
    for (int p = 0; p < 2; p++) {
        const uint8_t *C = p ? cap_v : cap_u;
        int hist[256] = {0}; long n = 0;
        for (int y = h0; y < h1; y++)
            for (int x = 1; x < cw; x++) {
                int d = (int)C[(size_t)y * cw + x] - (int)C[(size_t)y * cw + x - 1];
                hist[d < 0 ? -d : d]++; n++;
            }
        if (n < 1000) return -1;
        long alvo = n - n / 100, acc = 0; int p99 = 255;
        for (int d = 0; d < 256; d++) { acc += hist[d]; if (acc >= alvo) { p99 = d; break; } }
        if (p99 > pior) pior = p99;
    }
    return pior;
}
/* Desvio padrao dos planos U e V numa faixa. E a medida calibrada da armadilha
 * 39, em 12 quadros verificados de dois trechos do filme:
 *
 *   imagem real .... U 5,99 a 8,52   V 3,40 a 7,42
 *   borrao ......... U 2,78          V 2,25
 *   lixo ........... U 21,43         V 15,49
 *
 * Repetir linha ACHATA a cor, lixo a ESTOURA, e o alvo esta no meio -- por isso
 * e faixa e nao limiar. Estava calibrada desde a armadilha 39 e o juiz nao a
 * consultava: o `croma_mb_na_faixa` mede outra coisa (variacao DENTRO do macrobloco e
 * salto entre medias de macroblocos vizinhos), nao o desvio do plano.
 *
 *   y0, y1  a faixa, [y0, y1), em linhas do quadro
 *   du, dv  recebem o desvio padrao de U e de V
 *
 * Nao devolve valor: ambos saem -1 quando nao da para medir.
 */
static void croma_desvio_plano(int y0, int y1, double *du, double *dv) {
    *du = *dv = -1;
    if (!cap_u || !cap_v || cap_w <= 0 || y1 > cap_h || y1 - y0 < 8) return;
    int cw = cap_w / 2, h0 = y0 / 2, h1 = y1 / 2;
    for (int p = 0; p < 2; p++) {
        const uint8_t *C = p ? cap_v : cap_u;
        double s = 0, s2 = 0; long n = 0;
        for (int y = h0; y < h1; y++)
            for (int x = 0; x < cw; x++) { double c = C[(size_t)y * cw + x]; s += c; s2 += c * c; n++; }
        if (n < 100) return;
        double m = s / n, var = s2 / n - m * m;
        *(p ? dv : du) = var > 0 ? sqrt(var) : 0;
    }
}

static int croma_mb_na_faixa(int y0, int y1);
static int croma_salto_p99(int y0, int y1);
static void croma_desvio_plano(int y0, int y1, double *du, double *dv);
static _Thread_local int tri_front = -1, tri_nt = -1, tri_mx = -1, tri_ln = -1, tri_lg = -1, tri_resp = -1;
static int teto_respingo = 8;   /* TETO_RESPINGO */
static int croma_faixa  = 0;   /* CROMA_FAIXA */
static int intacto_ate  = 0;   /* INTACTO_ATE: linhas que tem que ficar identicas */
static int sem_croma    = 0;   /* SEM_CROMA=1: quadro sem croma utilizavel */
static int tarja_desce  = 0;   /* TARJA_DESCE=1: guiar pela tarja, nao pela fronteira */
static int pontua_consumo = 0; /* PONTUA_CONSUMO=1: nota = bytes consumidos */
static double base_tarja_desvio = -1;
static uint8_t *img_base = NULL;  /* luma da base, so leitura nos workers */
static int janela_resp  = 64;  /* JANELA_RESP: linhas medidas a partir da base */
static _Thread_local double tri_bloc = -1, tri_tarja = -1;
/* Os numeros do croma, nao so o veredito. Sem eles o modo `trinca` so diz
 * "passou/reprovou" e nao da para ver QUAL criterio derrubou o candidato --
 * que e exatamente a informacao de que a etapa seguinte precisa. */
static _Thread_local double cr_dentro = -1, cr_p99 = -1;

/* Veredito dos criterios. Precisa da linha de base ja medida.
 *
 * Sem argumentos: le a captura da thread e o estado da base.
 *
 * Devolve: 1 quando o candidato passa em TODOS os criterios; 0 em qualquer
 * reprovacao, inclusive sem captura ou sem base medida.
 */
static int trinca_ok(void) {
    if (!cap_buf || cap_w <= 0 || base_fronteira < 0) return 0;
    /* 0. NAO ESTRAGUE O QUE JA ESTA BOM.
     *
     * Num slice CABAC tudo antes do primeiro erro esta correto, entao um flip
     * num byte que so e lido depois da linha N nao pode mexer em nada acima de
     * N. Candidato que mexe ou deslocou o estado do CABAC mais cedo do que a
     * janela diz, ou o decodificador reconstruiu outra coisa -- em qualquer dos
     * dois casos ele esta apagando imagem boa para "avancar".
     *
     * E exato: memcmp, sem calibracao e sem limiar. So existe em alvo que TEM
     * imagem boa antes do travamento, que e o caso do frame 13 (linhas 0 a 879
     * sao imagem de verdade) e nao e o caso do IDR 3047, borrado quase desde o
     * topo. INTACTO_ATE=<linha> liga. */
    if (intacto_ate > 0 && img_base &&
        memcmp(cap_buf, img_base, (size_t)cap_w * intacto_ate) != 0) return 0;
    /* Guia alternativo: a TARJA em vez da fronteira. Em alvo com tarja o estado
     * de chegada e desvio zero, e o caminho ate la e monotono -- a fronteira
     * nao e (ver o comentario do tarja_baixo_desvio). Com TARJA_DESCE=1 o criterio 1
     * passa a ser "a tarja ficou mais uniforme", e a fronteira sai do juiz. */
    if (tarja_desce) {
        double td = tarja_baixo_desvio(cap_buf, cap_w, cap_h);
        tri_tarja = td;
        if (td < 0 || td >= base_tarja_desvio) return 0;
        if (intacto_ate > 0 && img_base) return 1;  /* a invariante exata ja julgou */
    }
    int f = fronteira_borrao(cap_buf, cap_w, cap_h, 160);
    tri_front = f;
    if (!tarja_desce && f <= base_fronteira) return 0;   /* 1. tem que DESCER */
    tri_bloc = blocagem_faixa(cap_buf, cap_w, base_fronteira, f);
    if (tri_bloc > 1.45) return 0;                  /* 2a. faixa liberada LIMPA na luma */
    /* 2b. e na COR. Janela FIXA de 64 linhas a partir da fronteira da base, nao
     * a faixa que cada candidato liberou: faixa estreita concentra o respingo e
     * faixa larga o dilui, entao medir "cada um na sua" nao compara. Com a
     * janela fixa a ordem bate com a do olho; com a propria, dois candidatos
     * trocam de lugar. E a mesma licao da blocagem, que eu tinha medido no
     * quadro inteiro e so discrimina na regiao onde os candidatos atuam. */
    /* Os criterios de croma so valem em quadro que TEM croma. O frame 13 mede
     * p99 = 1 e desvio 0,4 a 0,8 do topo a base -- na imagem boa e no borrao
     * igualmente -- porque e quadro escuro de saida de fade, quase
     * monocromatico. Ali o croma nao separa nada, e a faixa calibrada em
     * quadros claros (0,70 a 1,60) reprova ate a parte BOA do proprio quadro,
     * que mede 0,10. Criterio que reprova o alvo certo nao filtra: zera.
     * TETO_RESPINGO=0 e SEM_CROMA=1 desligam os dois. */
    int jr = base_fronteira + janela_resp;
    if (jr > cap_h) jr = cap_h;
    if (teto_respingo > 0) {
        tri_resp = croma_salto_p99(base_fronteira, jr);
        if (tri_resp < 0 || tri_resp > teto_respingo) return 0;
    }
    /* 2c. desvio dos planos de croma na MESMA janela, dentro da faixa dos
     * quadros intactos (armadilha 39). Desligado por padrao, e o motivo importa:
     * e uma porta de CHEGADA, nao de passagem. Na etapa 1 do IDR 3047 a base e
     * borrao puro -- dpV 2,21 -- e TODOS os candidatos dao dpV 9 a 10; com a
     * porta ligada nenhum passaria e a etapa 1 nao teria saida. Na etapa 2,
     * partindo do candidato escolhido, 30 dos 734 entram na faixa nos dois
     * planos. Serve para escolher entre sobreviventes e para saber quando
     * chegou, nao para filtrar cedo. CROMA_FAIXA=1 liga. */
    if (croma_faixa) {
        double du, dv; croma_desvio_plano(base_fronteira, jr, &du, &dv);
        if (du < 5.99 || du > 8.52 || dv < 3.40 || dv > 7.42) return 0;
    }
    int nt, mx, ln, lg; estrutura_borrao(cap_buf, cap_w, cap_h, f, &nt, &mx, &ln, &lg);
    tri_nt = nt; tri_mx = mx; tri_ln = ln; tri_lg = lg;
    /* 3. o borrao que SOBRA tem que continuar sendo borrao: COBERTURA, so.
     *
     * A versao anterior comparava densidade de trechos, comprimento medio e
     * maior trecho. Com a tolerancia certa nada disso existe -- o borrao e UM
     * trecho unico cobrindo 99% da area abaixo da fronteira, nos tres quadros
     * de calibracao. Os 181 trechos de "maior 19" que eu media, e a
     * "fragmentacao" que reprovava o candidato descartado pelo olho (92
     * trechos, maior 11), eram artefato da igualdade exata, nao propriedade da
     * imagem. Quem reprovou aquele candidato foi a blocagem, sozinha.
     *
     * Entao o que sobra aqui e um piso barato contra o caso em que a regiao de
     * baixo deixa de ser borrao e vira outra coisa. Nao e o criterio que
     * seleciona -- esse e o 2. */
    int area = cap_h - f;
    if (area > 64) {
        double cob = (double)ln / area;
        if (cob < 0.90) return 0;
    }
    /* area <= 64 e o desfecho: nao sobrou borrao. Nao ha o que exigir de
     * "intacto" -- quem julga ai sao a blocagem e o croma, ja medidos. */
    if (sem_croma) return 1;
    return croma_mb_na_faixa(160, 640);                    /* e o croma na faixa */
}

/* Comparador do qsort para double, usado no percentil do croma.
 *
 *   a, b  ponteiros para double
 *
 * Devolve: -1, 0 ou 1. */
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Versao por MACROBLOCO. A anterior media o desvio global da faixa, que mistura
 * "a cena tem muitas cores" com "a cor esta coerente" -- ela varia 33% entre
 * quadros bons (6,29 a 8,41) e por isso a faixa saia larga demais.
 *
 * O croma e definido POR MACROBLOCO: uma predicao por MB mais um CBP que diz se
 * ha residuo DC apenas ou DC+AC. Medido em 94.080 macroblocos de 16 quadros
 * intactos, duas estatisticas separam bem e as duas sao de DUAS PONTAS:
 *
 *                        dentro do MB      p99 entre vizinhos
 *   imagem real ....... 0,91 a 1,33        8,48 a 14,31
 *   borrao ............ 0,33               3,69
 *   lixo .............. 1,78 a 2,51        27,12 a 33,59
 *
 * Borrao fica abaixo porque repetir linha achata a cor; lixo fica acima porque
 * estoura. Ver a armadilha 39 e o CRITERIOS.md.
 *
 * CROMA_DEBUG=1 imprime os valores medidos, para conferir contra a medicao
 * feita por fora -- foi a falta disso que me deixou cego na primeira versao.
 *
 *   y0, y1  a faixa a julgar, [y0, y1), em linhas do quadro
 *
 * Devolve: 1 quando a variacao dentro do macrobloco e o salto entre macroblocos
 * vizinhos ficam na faixa calibrada; 0 fora dela E tambem quando nao da para
 * medir. Os dois casos dao 0, entao um zero nao distingue "reprovou" de "nao
 * mediu" -- quem precisa da diferenca tem que olhar cap_w antes.
 */
static int croma_mb_na_faixa(int y0, int y1) {
    if (!cap_u || !cap_v || cap_w <= 0 || cap_h < y1) return 0;
    int cw = cap_w / 2, nmx = cap_w / 16;
    int nmy = (y1 - y0) / 16;
    if (nmy < 4 || nmx < 4) return 0;
    double soma_dentro = 0; int nblocos = 0;
    double *dif = malloc((size_t)nmy * nmx * sizeof(double));
    int ndif = 0;
    for (int my = 0; my < nmy; my++) {
        double ant = -1;
        for (int mx = 0; mx < nmx; mx++) {
            double s = 0, s2 = 0;
            int by = (y0 / 16 + my) * 8, bx = mx * 8;
            for (int j = 0; j < 8; j++)
                for (int i = 0; i < 8; i++) {
                    double u = cap_u[(size_t)(by + j) * cw + bx + i];
                    s += u; s2 += u * u;
                }
            double m = s / 64.0, var = s2 / 64.0 - m * m;
            soma_dentro += var > 0 ? sqrt(var) : 0; nblocos++;
            if (ant >= 0) dif[ndif++] = fabs(m - ant);
            ant = m;
        }
    }
    if (nblocos < 100 || ndif < 100) { free(dif); return 0; }
    double dentro = soma_dentro / nblocos;
    qsort(dif, ndif, sizeof(double), cmp_double);
    double p99 = dif[(int)(ndif * 0.99)];
    free(dif);
    cr_dentro = dentro; cr_p99 = p99;
    if (getenv("CROMA_DEBUG"))
        fprintf(stderr, "[croma] dentro_MB=%.2f  p99_vizinho=%.2f  (%d blocos)\n",
                dentro, p99, nblocos);
    return dentro >= 0.70 && dentro <= 1.60 && p99 >= 7.0 && p99 <= 18.0;
}



/* Varre combinacoes de k bits e pontua por PROGRESSO, para o modo `avanco`.
 *
 *   p  ignorado; a tarefa vem de `prox_combo` e de `combos_k`
 *
 * Pontua, em `placar[indice]`: por padrao o macrobloco alcancado, 8160 quando
 * nao ha erro, -1 quando reprova num piso. Com PONTUA_CONSUMO=1 a nota vira
 * bytes consumidos antes do erro.
 *
 * E aqui que moram os pisos -- e SO aqui, o que nao esta obvio: passar
 * PISO_TARJA para o `varrek` ou o `cresce` nao da erro, da silencio. O item 6
 * do REFATORACAO.md tira essa linha daqui para valer em todo modo.
 *
 *   p  ignorado; a tarefa vem de `prox_combo` e de `combos_k`
 *
 * Devolve: NULL sempre. O resultado sai em `placar[indice]`.
 */
static void *worker_avanco(void *p) {
    (void)p;
    int alvo = alvok, len = ix[alvo].size, anc = ancora_de(alvo);
    uint8_t *copia = malloc(len);
    uint8_t *corte = malloc(len);
    cap_buf = malloc((size_t)1920 * 1088);
    for (;;) {
        long c = atomic_fetch_add(&prox_combo, 1);
        if (c >= n_combos) break;
        const int *comb = combos_k + c * prof_k;
        memcpy(copia, arq + ix[alvo].off, len);
        for (int q = 0; q < prof_k; q++)
            copia[ini_k + comb[q] / 8] ^= (1 << (comb[q] % 8));
        decodifica(anc, alvo, copia, len, NULL);
        /* Sem quadro na saida nao ha pontuacao possivel. Se o decoder rejeita o
         * pacote antes de decodificar macrobloco nenhum, nao sai linha "error
         * while decoding MB", o log_mbx fica em -1 e o candidato tirava 8160 --
         * a nota maxima para um quadro que nem existe. Medido: os tres flips no
         * slice_type do frame 12 dao "0 de 1" no serie e tiravam 8160 aqui.
         * Ver armadilha 32.
         *
         * E "quadro existe" nao basta. No frame 13, 51,6% dos flips de 1 bit
         * tiravam 8160 porque o decodificador desiste em silencio e devolve
         * ocultacao -- quadro liso, sem uma linha de erro. A base, que erra no
         * macrobloco 6600, tirava MENOS que o desastre completo. O piso que
         * separa e a tarja, que e conhecida a priori em todo quadro do filme.
         * Ver armadilha 33. */
        int mb = (cap_w <= 0) ? -1
               : (piso_tarja && !tarja_baixo_e_16(cap_buf, cap_w, cap_h)) ? -1
               : (piso_topo  && !tarja_topo_uniforme(cap_buf, cap_w, cap_h, piso_topo >= 2 ? 16 : -1)) ? -1
               : (piso_base  && !tarja_baixo_uniforme(cap_buf, cap_w, cap_h)) ? -1
               : (piso_croma && !croma_mb_na_faixa(160, 640)) ? -1
               : (piso_trinca && !trinca_ok()) ? -1
               : mb_alcancado();

        /* PONTUA_CONSUMO=1: a nota passa a ser BYTES CONSUMIDOS antes do erro,
         * nao o macrobloco alcancado.
         *
         * Existe porque o macrobloco nao tem gradiente em alvo como o frame 11.
         * Ali o ffmpeg descarta o quadro inteiro ao errar em qualquer
         * macrobloco -- os MB 0 a 14, decodificados de verdade, saem iguais ao
         * resto ocultado -- entao a imagem nao informa progresso, e o
         * macrobloco informa pouco: a base para no 15 e os candidatos que
         * "avancam" param no 21.
         *
         * O consumo informa muito mais, e vem de graca: o decodificador escreve
         * o bytestream restante na propria mensagem de erro. Medido no frame
         * 11, mesma escala, mesmo erro `Reference 3 >= 3`:
         *
         *     base ....... macrobloco 15, consome  54 de 2.832
         *     byte 8 b5 .. macrobloco 21, consome 348      <- 6,4x
         *
         * Candidato SEM linha de erro nao tem bytestream para ler. Recebe `len`,
         * a nota maxima, mas isso NAO quer dizer que consumiu: os 14 bits que
         * "completam" o frame 11 leem ~216 bytes e declaram os 8.160
         * macroblocos prontos. Sao poucos e conferidos um a um com o `corta`. */
        if (pontua_consumo && mb >= 0)
            mb = (log_bytestream < 0 || log_bytestream > len)
               ? len : (int)(len - log_bytestream);

        /* Juiz do consumo -- REFUTADO PELO PROPRIO CONTROLE. Nao usar.
         *
         * A ideia era: um slice sem cabac_zero_word tem que gastar quase todo o
         * payload, entao quem fecha o quadro muito antes atravessou a cauda como
         * skip. O frame 13 com o candidato 184311 bit 6 completava no byte
         * 31.900 de 36.528, deixando 12,7% sem ler, e isso parecia prova.
         *
         * O controle obrigatorio derrubou: o frame 12, que e reparo VERIFICADO,
         * completa truncado em **100 bytes** dos 1.135 de dados reais -- 91% sem
         * ler. O ffmpeg termina a slice com muito menos dado do que ela tem,
         * preenchendo o resto. "Completa cedo" nao diz nada sobre estar certo, e
         * as proporcoes ate coincidem: 11,9% no quadro correto contra 12,7% no
         * candidato que eu queria reprovar.
         *
         * O codigo fica, desligado por padrao, com a refutacao escrita junto
         * para ninguem reinventa-lo. Ver armadilha 36. */
        if (consumo_t > 0 && mb >= 8160) {
            memcpy(corte, copia, consumo_t);
            int n = consumo_t - 4;
            corte[0] = n >> 24; corte[1] = n >> 16; corte[2] = n >> 8; corte[3] = n;
            decodifica(anc, alvo, corte, consumo_t, NULL);
            int mb2 = (cap_w <= 0) ? -1 : mb_alcancado();
            if (mb2 >= 8160) mb = -1;        /* fechou sem o pedaco: atalho */
        }
        placar[c] = mb;
    }
    free(copia); free(corte); free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* Varre combinacoes de k bits pontuando por aceita/rejeita, para o `varrek`.
 *
 *   p  ignorado; a tarefa vem de `prox_combo` e de `combos_k`
 *
 * Pontua: binario, sem os pisos do `avanco`. A diferenca entre os dois e
 * justamente essa -- aqui nao ha nota de progresso nem piso.
 *
 *   p  ignorado; a tarefa vem de `prox_combo` e de `combos_k`
 *
 * Devolve: NULL sempre.
 */
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

            /* Nao truncar em constante: o teto de 4096 do varre1 guardou as
             * solucoes pela ordem em que as threads acharam, e as que sobraram
             * nao eram as do lado certo do NAL. Aqui a alocacao e n_combos. */
            if (n < achk_cap) {
                achk_c[n] = c;
                for (int q = 0; q < prof_k; q++) achk[n * 4 + q] = comb[q];
            }
        }
    }
    free(copia); free(cap_buf); cap_buf = NULL;
    if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
    return NULL;
}

/* Gera C(nbits_k, prof_k) combinacoes em ordem lexicografica.
 *
 * Sem argumentos: le `nbits_k` e `prof_k`.
 *
 * Nao devolve valor; aloca `combos_k` e preenche `n_combos`. A tabela e
 * materializada inteira -- com profundidade 4 numa janela larga isso passa de
 * um gigabyte, e a decisao de nao trocar por aritmetica combinatoria esta no
 * docs/REFATORACAO.md, secao 4.
 */
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

/* Gera os pares (i, j) com o 1o bit na faixa principal e o 2o numa faixa
 * propria, sempre j > i. Existe porque o primeiro bit errado de um quadro
 * costuma ser localizavel -- curva de truncamento, imagem, avanco de 1 bit --
 * e o segundo nao: ele fica em qualquer lugar DEPOIS do primeiro. No IDR 1773
 * o primeiro cabe em 160 bytes e o segundo pode estar ate o fim do NAL; com
 * uma faixa so para os dois, cobrir isso custava ~13 M pares a mais, todos com
 * o primeiro bit onde ele sabidamente nao esta.
 *
 *   j0, j1  faixa do 2o bit, em bits relativos a `ini_k` (j0 >= 0)
 *
 * Le `nbits_k` (tamanho da faixa do 1o bit). Aloca `combos_k` e preenche
 * `n_combos`; a ordem e lexicografica, como no `gera_combos`.
 */
static void gera_combos_janelas(int j0, int j1) {
    long total = 0;
    for (int i = 0; i < nbits_k; i++) {
        int a = j0 > i + 1 ? j0 : i + 1;
        if (a < j1) total += j1 - a;
    }
    combos_k = malloc((size_t)(total ? total : 1) * 2 * sizeof(int));
    long w = 0;
    for (int i = 0; i < nbits_k; i++) {
        int a = j0 > i + 1 ? j0 : i + 1;
        for (int j = a; j < j1; j++) {
            combos_k[w * 2] = i;
            combos_k[w * 2 + 1] = j;
            w++;
        }
    }
    n_combos = w;
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
 * deu 10,1.
 *
 *   y0, y1          a faixa, [y0, y1), em linhas do quadro
 *   um, ud, vm, vd  recebem media e desvio de U e de V
 *
 * Nao devolve valor; com faixa vazia todos saem zero. Amostra 1 coluna em 2.
 */
static void croma_media_desvio(int y0, int y1, double *um, double *ud, double *vm, double *vd) {
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

/* Diz se o croma do quadro bate com a referencia medida do proprio trecho.
 *
 * Sem argumentos: le a captura da thread e as globais gu_med/gv_med/gu_des/
 * gv_des, preenchidas antes da varredura a partir de quadros integros vizinhos.
 *
 * Devolve: 1 quando as medias ficam a 4 niveis da referencia e os desvios
 * dentro de 15% dela; 0 caso contrario, inclusive sem captura.
 *
 * A referencia e do TRECHO, nao do filme: faixa calibrada em quadro claro
 * reprova quadro escuro de fade, e foi assim que uma varredura do frame 13 deu
 * zero sem testar nada (armadilha 45). */
static int croma_bate_referencia(void) {
    if (!cap_u || !cap_v || cap_w <= 0) return 0;
    double um, ud, vm, vd;
    croma_media_desvio(gy0, 950, &um, &ud, &vm, &vd);
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

/* Cresce uma solucao bit a bit a partir de uma base, para o modo `cresce`.
 *
 *   p  ponteiro para WorkerC, com o alvo e a faixa da tarefa
 *
 * Pontua: mede a imagem resultante e guarda as que melhoram, com guardas de
 * media e desvio calibradas pela metade integra do proprio quadro -- sem elas a
 * busca sobe em lixo colorido saturado.
 *
 *   p  ponteiro para WorkerC, com o alvo e a faixa
 *
 * Devolve: NULL sempre.
 */
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
        if (cap_w > 0 && croma_bate_referencia() && b > 0.4 && b < 2.0) nota = cru;
        if (medidas) {                     /* despejo, ANTES de qualquer guarda */
            Medida *m = &medidas[k];
            m->nota = cru; m->prim = -1;
            if (cap_w > 0) {
                double um, ud, vm, vd;
                croma_media_desvio(gy0, 950, &um, &ud, &vm, &vd);
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
typedef struct { long off; int bit; long off2; int bit2;   /* off2 < 0 = candidato de 1 bit */
                 int pmin, pmax, l949, l952, l960, bmin, bmax;
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

/* Mede o CAMPO e as tarjas de uma lista de candidatos, para o modo `campo`.
 *
 *   p  ignorado; a tarefa vem de `prox_cand` e de `campos`
 *
 * Pontua: nao aceita nem rejeita -- preenche, para cada candidato, vinte
 * medidas do quadro resultante. E modo de MEDICAO, nao de busca: quem decide e
 * quem le a tabela.
 *
 *   p  ignorado; a tarefa vem de `prox_cand` e de `campos`
 *
 * Devolve: NULL sempre. O resultado sai nos campos de `campos[k]`.
 */
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
        /* Par: o varrek devolve combinacoes de 2 bits, e medir so o primeiro
         * daria leitura de um candidato que nao existe. */
        if (campos[k].off2 >= 0)
            copia[campos[k].off2 - ix[campo_alvo].off] ^= (1 << campos[k].bit2);
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

/* Reconfere candidatos ja achados, para o modo `testa`.
 *
 *   p  ignorado; a tarefa vem de `prox_cand` e de `cands`
 *
 * Pontua: marca `ok` em cada candidato que faz o quadro decodificar limpo.
 * Serve para revalidar uma lista vinda de outra corrida, sem refazer a busca.
 *
 *   p  ignorado; a tarefa vem de `prox_cand` e de `cands`
 *
 * Devolve: NULL sempre. Marca `ok` em cada candidato aprovado.
 */
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

/* Diferenca media absoluta entre o quadro e a imagem de referencia carregada.
 *
 *   Y     plano de luma do candidato
 *   w, h  dimensoes, que precisam bater com as da referencia
 *
 * Devolve: a media das diferencas por pixel, ou -1 se nao houver referencia
 * carregada ou as dimensoes nao baterem. Serve para comparar contra um quadro
 * bom conhecido; sem referencia nao ha o que medir. */
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

/* Varre comparando contra uma imagem de REFERENCIA, para o modo `vizinho`.
 *
 *   p  ponteiro para WorkerR, com o alvo e a faixa da tarefa
 *
 * Pontua: pela diferenca media absoluta contra a referencia (`mad_ref`), o que
 * so vale quando existe um quadro bom conhecido para comparar.
 *
 *   p  ponteiro para WorkerR, com o alvo e a faixa
 *
 * Devolve: NULL sempre.
 */
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

/* Varre pontuando por LINHAS REAIS de imagem, para o modo `recupera`.
 *
 *   p  ponteiro para WorkerL, com o alvo e a faixa da tarefa
 *
 * Pontua: quantas linhas do quadro tem conteudo de verdade, em vez de listra
 * propagada. Criterio de imagem, nao sintatico.
 *
 *   p  ponteiro para WorkerL, com o alvo e a faixa
 *
 * Devolve: NULL sempre.
 */
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
 * THREADS=1 usa o caminho sequencial original, para a comparacao A/B.
 *
 * Sem argumentos: le THREADS e NUMBER_OF_PROCESSORS.
 *
 * Devolve: o numero de threads, nunca menor que 1 nem maior que nucleos-2.
 * O padrao e 12 por decisao medida; nao "otimizar" para 26.
 */
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
/* Nome do arquivo de patches. Existe porque o modo `repair` grava nele e os
 * blocos de modo viraram funcoes proprias, sem acesso ao argv do main. */
static const char *arq_patches = NULL;
static int n_patch = 0;

/* Le o patches.txt e aplica cada linha ao buffer do MP4 em memoria, por XOR.
 * O arquivo em disco nunca e tocado.
 *
 *   fn  caminho do patches.txt
 *
 * Nao devolve nada; preenche o vetor global `patches` e `n_patch`. Arquivo
 * ausente e silencioso, o que e proposital: varredura em MP4 cru tem que rodar.
 *
 * ATENCAO: toda medida sobre bytes tem que sair DESTE buffer, nunca do arquivo
 * lido direto. Script que abre o MP4 por fora enxerga dano ja consertado -- ver
 * armadilha 49, que custou uma sessao. */
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
/* Acrescenta uma linha ao patches.txt. E a UNICA funcao do programa que
 * escreve numa fonte de verdade, e por isso abre em modo "ab": nunca reescreve
 * o arquivo, so acrescenta.
 *
 *   fn    caminho do patches.txt, vindo do argv via `arq_patches`
 *   off   deslocamento absoluto no MP4, em bytes
 *   bit   qual bit do byte inverter, de 0 a 7
 *
 * Nao devolve nada e nao confere nada: quem chama ja decidiu que o patch passa
 * no criterio. Linha repetida se CANCELA -- XOR duas vezes e identidade -- e
 * isso e usado de proposito para desfazer reparo aceito por engano. */
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
/* Repintura da tarja, por LISTA EXPLICITA de frames.
 *
 * O frame 11 produz a imagem certa -- campo 43, o valor que a rampa do fade
 * preve -- e tem as duas tarjas em 15,000 com desvio zero. A causa esta na
 * predicao ponderada: os macroblocos da tarja decodificam como inter em vez de
 * intra, e 16 * 40/32 - 5 da exatamente 15. Nao ha conserto de bitstream (sete
 * vias fechadas), mas o valor certo da tarja e conhecido a priori.
 *
 * A lista e explicita de proposito. Um criterio generico do tipo "tarja
 * uniforme mas fora de 16" pegaria 761 frames, dos quais 736 tem tarja 128 --
 * o cinza de ocultacao. Repintar aqueles seria maquiar lixo. So entra quadro
 * cuja IMAGEM foi verificada contra gabarito.
 *
 *   t  indice do quadro
 *
 * Devolve: 1 se o quadro esta na lista de REPINTA, 0 caso contrario. Repintar e
 * OCULTACAO, nao reparo: melhora o que se ve e nao afirma nada sobre os bits.
 */
static int repinta_tarja(int t) {
    const char *l = getenv("REPINTA");
    if (!l) return 0;
    char buf[256]; snprintf(buf, sizeof buf, ",%s,", l);
    char alvo[16]; snprintf(alvo, sizeof alvo, ",%d,", t);
    return strstr(buf, alvo) != NULL;
}


/* =========================== OS MODOS ==============================
 *
 * Uma funcao por modo, todas com a mesma assinatura (argc, argv) e devolvendo
 * 0 no sucesso ou 1 em erro. O CONTRATO de cada uma -- nome, argumentos
 * obrigatorios e texto de uso -- nao esta repetido aqui: mora na tabela MODOS,
 * logo abaixo, que e o unico lugar onde a validacao acontece.
 *
 * Deixar o contrato em um lugar so foi deliberado. Antes ele estava em dois --
 * uma string de uso no comeco do main e a leitura de argv espalhada em cada
 * bloco -- e os dois dessincronizaram: a string documentava cinco modos de
 * vinte e cinco, e citava um `report` que nao existia como entrada propria.
 *
 * Os corpos vieram da cadeia de else-if do main sem uma linha alterada. Ver
 * docs/REFATORACAO.md, secao 3, e a prova de 27 modos identicos no arnes.
 * ==================================================================== */

static int modo_repair(int argc, char **argv) {
    (void)argc; (void)argv;
        int a = atoi(argv[5]), b = atoi(argv[6]);
        int janela = argc > 7 ? atoi(argv[7]) : 4096;
        int ok = 0, duro = 0, jaok = 0;
        for (int t = a; t <= b && t < n_ix; t++) {
            int bit, r = repara(t, janela, &bit);
            if (r == -2) { jaok++; continue; }
            if (r >= 0) {
                long abs = ix[t].off + r;
                arq[abs] ^= (1 << bit);
                grava_patch(arq_patches, abs, bit);
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
    return 0;
}

static int modo_verify(int argc, char **argv) {
    (void)argc; (void)argv;
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
        /* Os cabecalhos de slice consertados por coerencia (patches_cabecalho,
         * ferramentas/cabecalho_slice.py) sao do mesmo tipo: provam o
         * cabecalho, nao fazem o quadro fechar -- o dano segue no corpo. Sem
         * pula-los, 791 linhas viram "falsos" e afogam o sinal. O arquivo tem
         * comentarios e texto depois do bit, entao a leitura e por linha. */
        const char *listas[2] = {
            getenv("DET") ? getenv("DET") : "dados/deterministicos.txt",
            "dados/patches_cabecalho.txt" };
        for (int f = 0; f < 2; f++) {
            FILE *fd = fopen(listas[f], "r");
            if (!fd) continue;
            char linha[512]; long o; int b, n0 = n_det;
            while (n_det < 4096 && fgets(linha, sizeof linha, fd))
                if (linha[0] != '#' && sscanf(linha, "%ld %d", &o, &b) == 2) {
                    det_off[n_det] = o; det_bit[n_det] = b; n_det++;
                }
            fclose(fd);
            fprintf(stderr, "[+] %d patches de %s serao pulados\n", n_det - n0, listas[f]);
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
    /* Diagnostico dos IDRs quebrados. Um IDR e sua propria ancora, entao cada
     * teste custa 1 decodificacao. Nao grava patches: so mede. */
    return 0;
}

static int modo_idr(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* Conta TODAS as solucoes de 1 bit por IDR quebrado. Nao grava. */
    return 0;
}

static int modo_unico(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_oraculo(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* Recupera um frame usando um vizinho INTEGRO como gabarito. */
    return 0;
}

static int modo_vizinho(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* Recuperacao incremental de um frame: aceita o flip que faz a imagem
     * crescer e repete. NAO grava patches -- imprime o que achou. */
    return 0;
}

static int modo_recupera(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* Rankeia os IDRs por quanto de imagem real sobrou. Saida: <idr> <linhas>
     * (-1 = quadro de ocultacao, nao decodificou nada). */
    return 0;
}

static int modo_ranking(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* Despeja o plano Y de um frame como PGM, para inspecao visual. */
    return 0;
}

static int modo_varre2(int argc, char **argv) {
    (void)argc; (void)argv;
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
    /* ---- modo corta ----
     * Onde comeca cada fileira de macrobloco, medido em vez de estimado.
     * Trunca o NAL em k bytes e le no log ate onde o decodificador chegou: isso
     * e exatamente "quantos macroblocos cabem em k bytes".
     *
     * Tem que truncar POR DENTRO, ajustando o prefixo AVCC e o tamanho do
     * pacote juntos. Patchar so o prefixo nao serve -- o ffmpeg confere o
     * tamanho contra o buffer e descarta o NAL inteiro, e ai o unico erro que
     * sobra e de outro quadro da cadeia, o que parece resposta e nao e. */
    return 0;
}

static int modo_corta(int argc, char **argv) {
    (void)argc; (void)argv;
        int alvo = atoi(argv[5]);
        int ini  = argc > 6 ? atoi(argv[6]) : 100;
        int fim  = argc > 7 ? atoi(argv[7]) : ix[alvo].size;
        int passo = argc > 8 ? atoi(argv[8]) : 500;
        int len = ix[alvo].size, anc = ancora_de(alvo);
        uint8_t *copia = malloc(len);
        cap_buf = malloc((size_t)1920 * 1088);
        printf("frame %d: %d bytes, cortando de %d a %d de %d em %d\n",
               alvo, len, ini, fim, len, passo);
        for (int k = ini; k <= fim; k += passo) {
            if (k < 8) continue;
            memcpy(copia, arq + ix[alvo].off, len);
            int n = k - 4;                      /* payload que sobra */
            copia[0] = n >> 24; copia[1] = n >> 16; copia[2] = n >> 8; copia[3] = n;
            decodifica(anc, alvo, copia, k, NULL);
            /* A medida aqui e do PARSE, nao da saida. O frame 19 nao emite
             * quadro nenhum, e exigir cap_w > 0 devolvia -1 em todo corte --
             * o modo ficava cego justamente no caso em que mais interessa.
             * O macrobloco do log diz ate onde a analise sintatica chegou,
             * exista quadro ou nao; a coluna "quadro" registra se saiu. */
            int mb = mb_alcancado();
            printf("  corte %6d -> macrobloco %5d   fileira %3d   quadro %s\n",
                   k, mb, mb >= 8160 ? -1 : mb / 120, cap_w > 0 ? "sim" : "nao");
        }
        free(copia); free(cap_buf); cap_buf = NULL;
    return 0;
}

/* Modo `avanco`: varre TODAS as combinacoes de k bits (1 a 4) de uma faixa do
 * NAL do alvo e da a cada uma uma NOTA DE PROGRESSO -- ate onde o decodificador
 * chegou -- em vez do sim/nao do `varrek`. Serve para localizar dano e para
 * encadear etapas (`encadeia.py`), nao para aprovar reparo: 8160 aqui quer
 * dizer "o decodificador nao reclamou e nao ocultou nada", e so a imagem e a
 * tarja dizem se esta certo (CRITERIOS.md).
 *
 * Com k = 2 e JANELA2, o 2o bit sai de uma faixa propria (ver abaixo): o
 * primeiro bit errado costuma ser localizavel e o segundo nao, e uma faixa so
 * para os dois gasta quase tudo com o 1o bit onde ele sabidamente nao esta.
 *
 *   argv[5]  alvo     indice do quadro no index.txt
 *   argv[6]  k        bits por combinacao, 1 a 4 (fora disso e grampeado)
 *   argv[7]  ini      1o byte da faixa, no NAL (com o prefixo de 4 bytes);
 *                     padrao e minimo 5, o 1o byte depois do cabecalho NAL
 *   argv[8]  fim      fim da faixa, exclusivo; padrao o tamanho do NAL
 *   argv[9]  saida    arquivo das combinacoes que passam; sem ele imprime 25
 *
 * Variaveis de ambiente que mudam o que o modo faz:
 *   JANELA2=ini:fim  so com k = 2: 1o bit em [ini,fim) da faixa principal, 2o
 *                    em [ini2,fim2) e sempre depois do 1o; exige
 *                    ini_principal <= ini2 < fim2. Em bytes do NAL.
 *   SEM_CLAMP=1      nao corta o enchimento (00 00 03 / 00 finais) da faixa;
 *                    o corte vale para as duas janelas
 *   PORTA=n          so conta e grava nota >= n
 *   BASE=n           substitui a nota medida sem flip (a linha de corte)
 *   IGUAIS=1         grava tambem as que EMPATAM com a base
 *   TETO=n           linhas do arquivo, 40.000 por padrao; 0 = sem limite
 *   PONTUA_CONSUMO=1 nota em bytes consumidos antes do erro, nao em MB
 *   PISO_TARJA, PISO_TOPO, PISO_BASE, PISO_CROMA, PISO_TRINCA, INTACTO_ATE,
 *   TARJA_DESCE      pisos de imagem: reprovado vira nota -1. So valem AQUI
 *                    (worker_avanco), nao no `varrek` nem no `cresce`
 *   CONSUMO=n        juiz do consumo -- REFUTADO, nao usar (armadilha 36)
 *
 * Nota de cada combinacao (worker_avanco + mb_alcancado): o endereco linear do
 * MB onde o alvo parou, mb_y * 120 + mb_x, de 0 a 8159; 8160 - ocultados
 * quando o slice acabou cedo sem erro; 8160 so quando saiu inteiro; -1 quando
 * nao sai quadro ou reprova num piso.
 *
 * Saida no stdout: a linha da faixa, o histograma das notas em 7 faixas de
 * 1.166 MB, a nota da base sem flip, a melhor nota e quantas passam da base,
 * e as 5 MELHORES combinacoes (empate pela ordem da combinacao), no formato do
 * arquivo. Quando o teto corta, uma linha diz a nota de corte.
 * No arquivo: uma linha por combinacao que passa da base (ou empata, com
 * IGUAIS) e da PORTA, "off bit [off bit ...]   mb N", em ordem de combinacao
 * e NAO de nota, ate TETO linhas. Passando do teto ficam as de MAIOR nota
 * (empate no corte pela ordem da combinacao). Ate 2026-09-23 ficavam as
 * primeiras 40.000 na ordem da combinacao, e no 1773 o par da melhor nota
 * (7.652, de 305.175 que passaram) ficou de fora do arquivo.
 *
 * Devolve: 0; 1 se JANELA2 vier malformada ou com k != 2.
 */
/* Por que: o criterio binario "decodifica limpo" desperdica a informacao mais
 * util que o decoder da: ATE ONDE ele chegou antes de falhar. Num slice CABAC
 * nao existe ponto de ressincronizacao, entao tudo que vem ANTES do
 * primeiro erro esta correto -- e empurrar o primeiro erro para a frente e
 * progresso medivel, mesmo quando o quadro ainda nao fecha.
 *
 * Ressalva que o texto acima nao fazia: "primeiro erro" e o bit errado, nao o
 * MB onde o decodificador RECLAMA. Entre os dois ele segue lendo lixo valido,
 * as vezes por milhares de bytes (armadilha 9), e empurrar a reclamacao para a
 * frente pode ser so disfarcar melhor o lixo -- e assim que o encadeamento
 * guloso sobe em ramo falso (frame 12, IDRs 1683 e 3047). Onde o bit errado
 * esta de fato se ve cruzando a curva do `corta` com a imagem (IDR 1773,
 * RASTREIO.md). */
static int modo_avanco(int argc, char **argv) {
    (void)argc; (void)argv;
        alvok  = atoi(argv[5]);
        prof_k = atoi(argv[6]);
        int len = ix[alvok].size;
        ini_k   = argc > 7 ? atoi(argv[7]) : 5;
        int fim = argc > 8 ? atoi(argv[8]) : len;
        if (ini_k < 5) ini_k = 5;
        if (fim > len) fim = len;
        /* JANELA2=ini:fim -- faixa propria para o 2o bit (so com k = 2), em
         * bytes do NAL como a faixa principal. Ver gera_combos_janelas. */
        const char *j2 = getenv("JANELA2");
        int ini2 = 0, fim2 = 0;
        if (j2) {
            if (sscanf(j2, "%d:%d", &ini2, &fim2) != 2 || prof_k != 2
                || ini2 < ini_k || fim2 <= ini2) {
                fprintf(stderr, "JANELA2=ini:fim exige k = 2 e ini_principal <= ini < fim\n");
                return 1;
            }
            if (fim2 > len) fim2 = len;
        }
        /* Enchimento nao e dado. Depois que a slice acaba vem cabac_zero_word,
         * que pela norma e 00 00 -- aqui aparece como 00 00 03 repetido por
         * causa do byte de prevencao de emulacao. Trocar bit dali para o
         * decodificador andar mais nao conserta nada, fabrica dado: foi
         * exatamente assim que o encadeamento de 8 etapas do frame 12 subiu num
         * ramo falso, com dois dos oito bits nos bytes 1136 e 1140 de um payload
         * que acaba no 1135. Medido e cortado sozinho, para valer em qualquer
         * quadro; SEM_CLAMP=1 desliga. */
        if (!getenv("SEM_CLAMP")) {
            const uint8_t *p = arq + ix[alvok].off;
            int f = len;
            while (f >= 3 && p[f-3] == 0 && p[f-2] == 0 && p[f-1] == 3) f -= 3;
            while (f > 0 && p[f-1] == 0) f--;
            if (f < fim) {
                printf("[+] enchimento cortado: dados acabam no byte %d de %d, "
                       "faixa vai ate ai\n", f, len);
                fim = f;
            }
            if (j2 && f < fim2) {
                printf("[+] enchimento cortado: faixa do 2o bit vai ate o byte %d\n", f);
                fim2 = f;
            }
        }
        if (prof_k < 1) prof_k = 1;
        if (prof_k > 4) prof_k = 4;
        nbits_k = (fim - ini_k) * 8;
        if (j2) gera_combos_janelas((ini2 - ini_k) * 8, (fim2 - ini_k) * 8);
        else    gera_combos();
        placar = malloc((size_t)n_combos * sizeof(int));
        int nthr = quantas_threads();
        if (j2)
            printf("frame %d: 1o bit em [%d,%d) = %d bits, 2o bit em [%d,%d) e depois do 1o, "
                   "%ld combinacoes, %d threads\n",
                   alvok, ini_k, fim, nbits_k, ini2, fim2, n_combos, nthr);
        else
            printf("frame %d: faixa [%d,%d) = %d bits, %d a %d, %ld combinacoes, %d threads\n",
                   alvok, ini_k, fim, nbits_k, prof_k, prof_k, n_combos, nthr);
        fflush(stdout);
        atomic_store(&prox_combo, 0);
        time_t t0 = time(NULL);
        /* A linha de base tem que ser medida ANTES dos workers: o criterio da
         * trinca compara cada candidato contra ela. */
        if (piso_trinca) {
            cap_buf = malloc((size_t)1920 * 1088);
            decodifica(ancora_de(alvok), alvok, NULL, 0, NULL);
            if (cap_w > 0) {
                base_fronteira = fronteira_borrao(cap_buf, cap_w, cap_h, 160);
                estrutura_borrao(cap_buf, cap_w, cap_h, base_fronteira,
                                 &base_ntrechos, &base_maior, &base_linhas, &base_longos);
                if (intacto_ate > 0) {
                    if (intacto_ate > cap_h) intacto_ate = cap_h;
                    img_base = malloc((size_t)cap_w * intacto_ate);
                    memcpy(img_base, cap_buf, (size_t)cap_w * intacto_ate);
                    printf("[+] guardadas %d linhas da base que nao podem mudar\n", intacto_ate);
                }
                base_tarja_desvio = tarja_baixo_desvio(cap_buf, cap_w, cap_h);
                if (tarja_desce)
                    printf("[+] tarja da base: desvio %.4f (alvo: zero)\n", base_tarja_desvio);
            }
            printf("[+] base da trinca: fronteira na linha %d, %d trechos cobrindo %d linhas "
                   "(densidade %.4f, media %.2f), maior de %d linhas\n",
                   base_fronteira, base_ntrechos, base_linhas,
                   base_fronteira >= 0 ? (double)base_ntrechos / (1080 - base_fronteira) : 0.0,
                   base_ntrechos ? (double)base_linhas / base_ntrechos : 0.0, base_maior);
            free(cap_buf); cap_buf = NULL;
        }
        pthread_t *th = calloc(nthr, sizeof *th);
        for (int i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_avanco, NULL);
        for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
        free(th);
        /* linha de base: sem flip nenhum */
        cap_buf = malloc((size_t)1920 * 1088);
        decodifica(ancora_de(alvok), alvok, NULL, 0, NULL);
        int base = (cap_w <= 0) ? -1
                 : mb_alcancado();
        /* A base tem que ser lida na MESMA escala dos candidatos. Sem isto a
         * linha imprime macrobloco enquanto o placar guarda consumo, e comparar
         * os dois numeros leva a conclusao errada. */
        if (pontua_consumo && base >= 0)
            base = (log_bytestream < 0 || log_bytestream > ix[alvok].size)
                 ? ix[alvok].size : (int)(ix[alvok].size - log_bytestream);
        /* BASE=n substitui a base medida. Existe porque a base nao passa pelos
         * pisos: no IDR 29 ela vale 8160 -- o quadro "completa" disparando --
         * e com isso nada abaixo dela e gravado, mesmo os 361.434 candidatos
         * que PARAM de disparar e decodificam de verdade ate o macrobloco
         * 1.700. Sem isto a varredura mede certo e nao guarda nada. */
        if (getenv("BASE")) base = atoi(getenv("BASE"));
        /* Histograma das notas. Sem ele a corrida do frame 13 devolveu "150.794
         * passam da base" e eu so descobri que eram todas 8160 -- e todas lixo
         * -- depois de abrir os candidatos um a um. Ver armadilha 33. */
        {
            long fx[9] = {0};
            for (long c = 0; c < n_combos; c++) {
                int v = placar[c];
                int k;
                if (v < 0)          k = 0;
                else if (v >= 8160) k = 8;
                else { k = 1 + v / 1166; if (k > 7) k = 7; }
                fx[k]++;
            }
            printf("[+] histograma: reprovadas %ld", fx[0]);
            for (int k = 1; k <= 7; k++) printf(" | mb %d-%d: %ld", (k-1)*1166, k*1166-1, fx[k]);
            printf(" | sem erro: %ld\n", fx[8]);
        }
        printf("[+] base sem flip: macrobloco %d [%.0fs]\n", base, difftime(time(NULL), t0));
        /* Medir o croma da BASE e obrigatorio antes de confiar no piso: e o
         * unico jeito de saber se a faixa calibrada cabe neste quadro. Sem isto
         * eu media candidatos quebrados, achava 0,00 e concluia que o juiz
         * estava com defeito -- quando o defeito era o meu teste. */
        if (piso_croma) {
            if (getenv("CROMA_DEBUG")) {
                int cw0 = cap_w / 2;
                fprintf(stderr, "[base] cap_w=%d cap_h=%d hash=%llx  u[200][500]=%d u[201][500]=%d u[200][501]=%d\n",
                        cap_w, cap_h, (unsigned long long)cap_hash,
                        cap_u ? cap_u[(size_t)200 * cw0 + 500] : -1,
                        cap_u ? cap_u[(size_t)201 * cw0 + 500] : -1,
                        cap_u ? cap_u[(size_t)200 * cw0 + 501] : -1);
            }
            int ok = croma_mb_na_faixa(160, 640);
            printf("[+] croma da base: %s a faixa calibrada\n",
                   ok ? "DENTRO de" : "FORA da");
        }
        int melhor = -1;
        for (long c = 0; c < n_combos; c++) if (placar[c] > melhor) melhor = placar[c];
        long quantos = 0;
        for (long c = 0; c < n_combos; c++) if (placar[c] > base) quantos++;
        /* Porta da tarja. A imagem comeca na linha 130, entao as fileiras de
         * macrobloco 0 a 7 sao SO tarja: 960 macroblocos chapados e iguais a
         * referencia, que num slice P ou B tem que sair como skip com residuo
         * zero. Candidato que para antes do 960 errou dentro da tarja, e tarja
         * nao tem o que errar -- e descarte, nao e progresso. */
        int porta = getenv("PORTA") ? atoi(getenv("PORTA")) : 0;
        long passam = 0;
        for (long c = 0; c < n_combos; c++) if (placar[c] > base && placar[c] >= porta) passam++;
        printf("[+] melhor macrobloco alcancado: %d   (%ld passam da base, %ld tambem da porta %d)\n",
               melhor, quantos, passam, porta);
        /* Quem vai para o arquivo: nota >= porta e acima da base. Quando a base
         * ja esta no maximo -- caso do encadeamento, em que o bit anterior
         * fecha o quadro -- exigir "> base" nao deixa passar nada e o arquivo
         * sai vazio mesmo havendo milhares de candidatos validos. IGUAIS=1
         * tambem grava os que empatam com a base, e ai quem separa e o piso,
         * nao o macrobloco. */
        int piso = iguais ? base : base + 1;
        if (piso < porta) piso = porta;
        long elegiveis = 0;
        for (long c = 0; c < n_combos; c++) if (placar[c] >= piso) elegiveis++;
        /* As 5 melhores notas, empate pela ordem da combinacao. Sem isto a
         * melhor so aparecia se coubesse no arquivo -- ver o teto abaixo. */
        {
            long top[5]; int ntop = 0;
            for (long c = 0; c < n_combos; c++) {
                if (placar[c] < piso) continue;
                if (ntop == 5 && placar[c] <= placar[top[4]]) continue;
                int p = ntop < 5 ? ntop++ : 4;
                while (p > 0 && placar[c] > placar[top[p - 1]]) { top[p] = top[p - 1]; p--; }
                top[p] = c;
            }
            if (ntop) printf("[+] as %d melhores:\n", ntop);
            for (int t = 0; t < ntop; t++) {
                const int *comb = combos_k + top[t] * prof_k;
                printf("   ");
                for (int q = 0; q < prof_k; q++)
                    printf(" %ld %d", ix[alvok].off + ini_k + comb[q] / 8, comb[q] % 8);
                printf("   mb %d\n", placar[top[t]]);
            }
        }
        /* TETO=n: quantas linhas o arquivo guarda, 40.000 por padrao. Passando
         * disso ficam as de MAIOR nota -- empate no corte pela ordem da
         * combinacao -- e o arquivo continua em ordem de combinacao. Antes
         * ficavam as PRIMEIRAS 40.000 na ordem da combinacao: no IDR 1773
         * (2026-09-22) passaram 305.175 pares e o de melhor nota, 7.652, nao
         * estava no arquivo; ordenar o arquivo nao o recuperava. */
        long teto = getenv("TETO") ? atol(getenv("TETO")) : 40000;
        int nota_corte = piso;
        long vagas_corte = -1;              /* -1: tudo que passa cabe */
        if (elegiveis > teto && teto > 0) {
            long *hist = calloc((size_t)(melhor - piso + 1), sizeof *hist);
            for (long c = 0; c < n_combos; c++) if (placar[c] >= piso) hist[placar[c] - piso]++;
            long acima = 0;
            int v = melhor;
            while (v > piso && acima + hist[v - piso] < teto) acima += hist[v-- - piso];
            nota_corte = v;
            vagas_corte = teto - acima;
            free(hist);
            printf("[+] teto de %ld linhas: %ld passam; ficam as de nota > %d e as %ld primeiras de nota %d\n",
                   teto, elegiveis, v, vagas_corte, v);
        }
        FILE *g = argc > 9 ? fopen(argv[9], "w") : NULL;
        int mostradas = 0;
        for (long c = 0; c < n_combos; c++) {
            if (placar[c] < piso) continue;
            if (vagas_corte >= 0) {
                if (placar[c] < nota_corte) continue;
                if (placar[c] == nota_corte) { if (vagas_corte == 0) continue; vagas_corte--; }
            }
            const int *comb = combos_k + c * prof_k;
            for (int q = 0; q < prof_k; q++) {
                long o = ix[alvok].off + ini_k + comb[q] / 8;
                int  b = comb[q] % 8;
                if (g) fprintf(g, "%ld %d%s", o, b, q + 1 < prof_k ? " " : "");
                else if (mostradas < 25) printf("    off %ld bit %d%s", o, b,
                                                q + 1 < prof_k ? "  +" : "");
            }
            if (g) fprintf(g, "   mb %d\n", placar[c]);
            else if (mostradas < 25) printf("   -> mb %d\n", placar[c]);
            mostradas++;
        }
        if (g) { fclose(g); printf("    gravadas em %s\n", argv[9]); }
        free(combos_k); free(placar);
    return 0;
}

static int modo_varrek(int argc, char **argv) {
    (void)argc; (void)argv;
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
        achk_cap = n_combos;
        achk = malloc((size_t)achk_cap * 4 * sizeof(int));
        achk_c = malloc((size_t)achk_cap * sizeof(long));
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
        int lim = n < achk_cap ? n : (int)achk_cap;
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
        for (int k = 0; k < lim; k++) {
            for (int q = 0; q < prof_k; q++) {
                long o = ix[alvok].off + ini_k + achk[k * 4 + q] / 8;
                int  b = achk[k * 4 + q] % 8;
                if (g) fprintf(g, "%ld %d%s", o, b, q + 1 < prof_k ? " " : "\n");
                else if (k < 30) printf("    off %ld bit %d%s", o, b,
                                        q + 1 < prof_k ? "  +" : "\n");
            }
        }
        if (g) { fclose(g); printf("    gravadas em %s\n", argv[9]); }
        free(combos_k); free(achk); free(achk_c); achk = NULL; achk_c = NULL;
    return 0;
}

static int modo_cresce(int argc, char **argv) {
    (void)argc; (void)argv;
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
        croma_media_desvio(136, gy0, &gu_med, &gu_des, &gv_med, &gv_des);
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
    return 0;
}

static int modo_campo(int argc, char **argv) {
    (void)argc; (void)argv;
        campo_alvo = atoi(argv[5]);
        FILE *f = fopen(argv[6], "r");
        if (!f) { fprintf(stderr, "nao abriu %s%s", argv[6], "\n"); return 1; }
        campos = calloc(4000000, sizeof *campos);   /* sem teto: o de 200 mil truncaria como o do varrek */
        char linha[256];
        while (fgets(linha, sizeof linha, f)) {
            long o, o2; int b, b2;
            int n = sscanf(linha, "%ld %d %ld %d", &o, &b, &o2, &b2);
            if (n != 2 && n != 4) continue;
            campos[n_campos].off = o; campos[n_campos].bit = b;
            campos[n_campos].off2 = (n == 4) ? o2 : -1;
            campos[n_campos].bit2 = (n == 4) ? b2 : 0;
            n_campos++;
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
    /* ---- modo mapa ----
     * Onde cada quadro do filme para, num arquivo so. Uma decodificacao por
     * GOP, como o panorama: manda pacote por pacote, zera o log antes de cada
     * um e le depois, entao o macrobloco que sobra e o DAQUELE quadro.
     *
     * Serve para escolher alvo por medida em vez de por ordem: quadro que para
     * cedo tem dano de cabecalho e janela pequena; quadro que para tarde tem
     * dessincronizacao e janela grande; e quadro que nao para esta bom. */
    return 0;
}

static int modo_mapa(int argc, char **argv) {
    (void)argc; (void)argv;
        cap_qualquer = 1;
        cap_buf = malloc((size_t)1920 * 1088);
        printf("frame bytes mb_parada fileira pct_decodificado\n");
        fflush(stdout);
        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        /* Silencio de macrobloco nao e quadro inteiro: quando o ffmpeg rejeita o
         * cabecalho do slice ele nao emite imagem nenhuma e tambem nao imprime
         * erro de MB (armadilha 55). Cada pacote leva pts = i, entao o quadro
         * que sai diz de qual pacote veio, e pacote sem imagem sai com
         * mb_parada -1, a mesma convencao do avanco.
         *
         * UM decodificador para o filme inteiro, como o ffprobe. Reabrir a cada
         * GOP -- como era antes -- faz o h264 reaprender a profundidade de
         * reordenacao dos B em todo GOP e descartar quadros no caminho: 2.769
         * imagens em vez de 3.107, e 338 quadros bons contados como sem imagem.
         * Medido: com um so decodificador o conjunto sem imagem e EXATAMENTE o
         * do ffprobe. A saida vem reordenada, entao imprime tudo no fim. */
        int *mbs = malloc((size_t)n_ix * sizeof *mbs);
        char *emitiu = calloc((size_t)n_ix, 1);
        abre_decoder();
        for (int i = 0; i < n_ix; i++) {
            av_new_packet(pkt, ix[i].size);
            memcpy(pkt->data, arq + ix[i].off, ix[i].size);
            pkt->pts = i;
            log_zerar();
            log_ocultados_quadro = -1;
            if (avcodec_send_packet(ctx, pkt) == 0) { }
            av_packet_unref(pkt);
            while (avcodec_receive_frame(ctx, fr) == 0) {
                if (fr->pts >= 0 && fr->pts < n_ix) emitiu[fr->pts] = 1;
                av_frame_unref(fr);
            }
            /* Sem erro de MB mas com ocultacao: o slice acabou cedo e o resto
             * foi preenchido. O que se decodificou de fato e 8160 - ocultados
             * (armadilha 59); antes isto saia 8160. */
            mbs[i] = (log_mbx >= 0) ? log_mby * 120 + log_mbx
                   : (log_ocultados_quadro > 0) ? 8160 - log_ocultados_quadro : 8160;
        }
        avcodec_send_packet(ctx, NULL);
        while (avcodec_receive_frame(ctx, fr) == 0) {
            if (fr->pts >= 0 && fr->pts < n_ix) emitiu[fr->pts] = 1;
            av_frame_unref(fr);
        }
        for (int i = 0; i < n_ix; i++) {
            int mb = emitiu[i] ? mbs[i] : -1;
            printf("%d %d %d %d %.1f\n", i, ix[i].size, mb,
                   (mb < 0 || mb >= 8160) ? -1 : mb / 120,
                   mb < 0 ? 0.0 : 100.0 * mb / 8160.0);
        }
        free(mbs); free(emitiu);
        av_frame_free(&fr); av_packet_free(&pkt);
        if (ctx) { avcodec_free_context(&ctx); ctx = NULL; }
        cap_qualquer = 0; free(cap_buf); cap_buf = NULL;
    return 0;
}

static int modo_panorama(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_cortes(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_corte(int argc, char **argv) {
    (void)argc; (void)argv;
        int alvo = atoi(argv[5]);
        cap_buf = malloc((size_t)1920 * 1088);
        printf("frame %d (%d bytes): o decoder para de consumir por volta do byte %d\n",
               alvo, ix[alvo].size, acha_consumo(alvo));
        free(cap_buf); cap_buf = NULL;
    return 0;
}

static int modo_varre1(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_testa(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_serie(int argc, char **argv) {
    (void)argc; (void)argv;
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
        /* Quadro decodificado sem erro sobre referencia borrada HERDA o borrao
         * -- e o teste de propagacao nao o pega, porque o movimento desenha por
         * cima das listras (armadilha 56: 42 dos 209 "bons" eram isso). Entao
         * um quadro so passa se toda referencia decodificada antes dele no GOP
         * (IDR e quadros com nal_ref_idc > 0) tambem passou. A cadeia comeca no
         * IDR mesmo quando `ini` cai no meio do GOP: os quadros antes de `ini`
         * sao julgados so para isso, sem gravar nada. */
        int ref_quebrada = 0;
        for (int t = ancora_de(ini); t <= fim && t < n_ix; t++) {
            if (ix[t].idr) ref_quebrada = 0;
            int r = decodifica(ancora_de(t), t, NULL, 0, NULL);
            int tem = (cap_w == 1920 && cap_h == 1080);
            /* TARJA=1 aceita tambem quadro de imagem certa que o criterio
             * rigoroso rejeita -- 14 frames dos dois fades estao assim. */
            int pinta = tem && repinta_tarja(t);
            if (pinta) {
                for (int y = 0; y < 130; y++)
                    memset(cap_buf + (size_t)y * cap_w, 16, cap_w);
                for (int y = 950; y < cap_h; y++)
                    memset(cap_buf + (size_t)y * cap_w, 16, cap_w);
                if (cap_u && cap_v) {
                    int cw = cap_w / 2;
                    for (int y = 0; y < 65; y++) {
                        memset(cap_u + (size_t)y * cw, 128, cw);
                        memset(cap_v + (size_t)y * cw, 128, cw);
                    }
                    for (int y = 475; y < cap_h / 2; y++) {
                        memset(cap_u + (size_t)y * cw, 128, cw);
                        memset(cap_v + (size_t)y * cw, 128, cw);
                    }
                }
                fprintf(stderr, "[!] frame %d: tarja REPINTADA (ocultacao)\n", t);
            }
            /* As excecoes (tarja 16, repintura) sao para quadro de IMAGEM CERTA
             * que o teste de propagacao rejeita -- os fades. Quadro com MB
             * ocultado nao tem a imagem decodificada, entao nao se beneficia
             * delas: 2359, 2360, 2361 e 3442 entravam por aqui (armadilha 59). */
            int inteiro = aceita_oculto || ocultados_alvo <= 0;
            int ok = tem && !ref_quebrada && (r == 0 || (inteiro && (pinta ||
                     (aceita_tarja && tarja_baixo_e_16(cap_buf, cap_w, cap_h)))));
            if (!ok && ((arq[ix[t].off + 4] >> 5) & 3)) ref_quebrada = 1;
            if (t < ini) continue;               /* so alimentou a cadeia */
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
    /* ---- modo trinca ----
     * Abre a caixa-preta do juiz de tres partes: para cada candidato de uma
     * lista imprime as CINCO medidas em vez do veredito.
     *
     * Existe porque a etapa 2 do IDR 3047 devolveu 82 candidatos com
     * "macrobloco 8160" -- sem erro nenhum -- e 8160 e nota maxima no `avanco`.
     * Se forem dessincronizacao silenciosa eles vencem o feixe e a cadeia sobe
     * num ramo falso, que foi exatamente o que aconteceu no frame 12. A unica
     * maneira de separar os dois casos e olhar a fronteira do borrao de cada
     * um: quadro consertado nao tem borrao, quadro dessincronizado tem. */
    return 0;
}

static int modo_trinca(int argc, char **argv) {
    (void)argc; (void)argv;
        int alvo = atoi(argv[5]);
        guardar_croma = 1;
        cap_buf = malloc((size_t)1920 * 1088);
        decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
        if (cap_w <= 0) { fprintf(stderr, "base do frame %d nao da imagem\n", alvo); return 1; }
        base_fronteira = fronteira_borrao(cap_buf, cap_w, cap_h, 160);
        estrutura_borrao(cap_buf, cap_w, cap_h, base_fronteira, &base_ntrechos, &base_maior, &base_linhas, &base_longos);
        if (intacto_ate > 0) {
            if (intacto_ate > cap_h) intacto_ate = cap_h;
            img_base = malloc((size_t)cap_w * intacto_ate);
            memcpy(img_base, cap_buf, (size_t)cap_w * intacto_ate);
        }
        int base_mb = mb_alcancado();
        croma_mb_na_faixa(160, 640);
        printf("# base: mb %d fronteira %d trechos %d maior %d linhas %d "
               "densidade %.4f media %.2f croma %.2f/%.2f\n",
               base_mb, base_fronteira, base_ntrechos, base_maior, base_linhas,
               (double)base_ntrechos / (cap_h - base_fronteira),
               base_ntrechos ? (double)base_linhas / base_ntrechos : 0.0,
               cr_dentro, cr_p99);
        printf("# base: longos %d = %.4f da area\n", base_longos,
               (double)base_longos / (cap_h - base_fronteira));
        printf("off bit mb fronteira blocagem respingo cobertura dpU dpV tarja_med tarja_des intacta veredito\n");
        FILE *f = fopen(argv[6], "r");
        if (!f) { fprintf(stderr, "nao abriu a lista\n"); return 1; }
        char linha[256];
        while (fgets(linha, sizeof linha, f)) {
            long o; int b;
            if (sscanf(linha, "%ld %d", &o, &b) != 2) continue;
            arq[o] ^= (1 << b);
            cr_dentro = cr_p99 = -1;
            decodifica(ancora_de(alvo), alvo, NULL, 0, NULL);
            int mb = (cap_w <= 0) ? -1 : mb_alcancado();
            int ok = trinca_ok();
            int fr = -1, nt = -1, mx = -1, ln = -1, lg = -1; double bl = -1;
            if (cap_w > 0) {
                fr = fronteira_borrao(cap_buf, cap_w, cap_h, 160);
                if (fr > base_fronteira)
                    bl = blocagem_faixa(cap_buf, cap_w, base_fronteira, fr);
                estrutura_borrao(cap_buf, cap_w, cap_h, fr, &nt, &mx, &ln, &lg);
                croma_mb_na_faixa(160, 640);
            }
            int jr2 = base_fronteira + janela_resp; if (jr2 > cap_h) jr2 = cap_h;
            int rp = (cap_w > 0 && fr > base_fronteira) ? croma_salto_p99(base_fronteira, jr2) : -1;
            double du = -1, dv = -1;
            if (cap_w > 0) croma_desvio_plano(base_fronteira, jr2, &du, &dv);
            /* Tarja de baixo: media e desvio, e o VALOR sai junto. Pode ser 16
             * ou 15 -- o frame 11 deixa 15 no DPB e a tarja de cima do 13 sai
             * 15 -- entao o criterio de chegada e "uniforme", com o valor
             * livre, e nao "igual a 16". */
            double tm = -1, td = -1;
            if (cap_w >= 1920 && cap_h >= 1080) {
                double s = 0, s2 = 0; long n = 0;
                for (int y = 962; y < 1080; y++)
                    for (int x = 0; x < cap_w; x++) {
                        double c = cap_buf[(size_t)y * cap_w + x]; s += c; s2 += c * c; n++;
                    }
                tm = s / n; double va = s2 / n - tm * tm; td = va > 0 ? sqrt(va) : 0;
            }
            int intacta = (intacto_ate > 0 && img_base && cap_w > 0)
                        ? (memcmp(cap_buf, img_base, (size_t)cap_w * intacto_ate) == 0) : -1;
            printf("%ld %d %d %d %.3f %d %.4f %.2f %.2f %.3f %.3f %d %s\n",
                   o, b, mb, fr, bl, rp,
                   (fr >= 0 && cap_h > fr) ? (double)ln / (cap_h - fr) : -1.0,
                   du, dv, tm, td, intacta, ok ? "passa" : "nao");
            arq[o] ^= (1 << b);
        }
        fclose(f);
        free(cap_buf); cap_buf = NULL;
    return 0;
}

static int modo_dumpyuv(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_dump(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

static int modo_report(int argc, char **argv) {
    (void)argc; (void)argv;
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
    return 0;
}

/* Le o MP4 e o indice, aplica os patches em memoria, interpreta as variaveis de
 * ambiente e despacha para o modo pedido.
 *
 *   argc, argv  <mp4> <index.txt> <patches.txt> <modo> [args do modo]
 *
 * Devolve: 0 no sucesso, 1 em erro de uso ou de abertura. O modo em si e uma
 * entrada da tabela MODOS, que valida o argc ANTES de chamar -- nome
 * desconhecido lista os modos em vez de rodar um relatorio em silencio.
 *
 * O MP4 NUNCA e reaberto para escrita: os patches sao aplicados por XOR sobre a
 * copia em memoria, e so o `repair` escreve, no patches.txt. */
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
    arq_patches = f_pt;   /* o modo `repair` grava nele, e vira funcao propria */
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
    if (getenv("ERROS_BASE")) erros_base = atoi(getenv("ERROS_BASE"));
    if (getenv("PISO_TARJA")) piso_tarja = atoi(getenv("PISO_TARJA"));
    if (getenv("PISO_TOPO")) piso_topo = atoi(getenv("PISO_TOPO"));
    if (getenv("PISO_BASE")) piso_base = atoi(getenv("PISO_BASE"));
    if (getenv("CONSUMO")) consumo_t = atoi(getenv("CONSUMO"));
    if (getenv("IGUAIS")) iguais = atoi(getenv("IGUAIS"));
    if (getenv("PISO_CROMA")) { piso_croma = atoi(getenv("PISO_CROMA")); if (piso_croma) guardar_croma = 1; }
    if (getenv("TOL_COPIA")) juizes_tolerancia(atoi(getenv("TOL_COPIA")));
    if (getenv("TETO_RESPINGO")) teto_respingo = atoi(getenv("TETO_RESPINGO"));
    if (getenv("JANELA_RESP")) janela_resp = atoi(getenv("JANELA_RESP"));
    if (getenv("CROMA_FAIXA")) croma_faixa = atoi(getenv("CROMA_FAIXA"));
    if (getenv("INTACTO_ATE")) intacto_ate = atoi(getenv("INTACTO_ATE"));
    if (getenv("SEM_CROMA")) sem_croma = atoi(getenv("SEM_CROMA"));
    if (getenv("TARJA_DESCE")) tarja_desce = atoi(getenv("TARJA_DESCE"));
    if (getenv("PONTUA_CONSUMO")) pontua_consumo = atoi(getenv("PONTUA_CONSUMO"));
    if (getenv("PISO_TRINCA")) { piso_trinca = atoi(getenv("PISO_TRINCA")); if (piso_trinca) guardar_croma = 1; }
    if (getenv("TRACO")) { traco = atoi(getenv("TRACO")); if (traco) av_log_set_level(AV_LOG_DEBUG); }
    if (getenv("FOLGA")) folga_lookahead = atoi(getenv("FOLGA"));
    if (getenv("ACEITA_OCULTO")) aceita_oculto = atoi(getenv("ACEITA_OCULTO"));
    fprintf(stderr, "[+] criterio: sintatico%s\n",
            exigir_imagem ? " + imagem (propagacao)" : " apenas (VISUAL=0)");
    carrega_patches(f_pt);


/* Tabela de modos. Substitui a cadeia de 24 else-if, e traz duas coisas
 * que a cadeia nao tinha: o argc minimo de cada modo, validado ANTES de
 * despachar, e o texto de uso ao lado da implementacao.
 *
 * A cadeia antiga terminava num else SEM comparacao, que era o modo
 * `report`. Erro de digitacao no nome rodava um relatorio do filme inteiro
 * em silencio -- 3.445 decodificacoes -- em vez de dizer "modo
 * desconhecido". Ver docs/REFATORACAO.md, secao 3. */
typedef struct { const char *nome; int min_args; const char *uso;
                 int (*executa)(int, char **); } Modo;
static const Modo MODOS[] = {
    { "repair", 7, "<ini> <fim> [janela]", modo_repair },
    { "verify", 5, "", modo_verify },
    { "idr", 5, "[j1] [j2] [max]", modo_idr },
    { "unico", 5, "[jc] [ji] [max]", modo_unico },
    { "oraculo", 5, "[n_amostras] [n_flips]", modo_oraculo },
    { "vizinho", 7, "<alvo> <ref> [janela] [passos]", modo_vizinho },
    { "recupera", 6, "<alvo> [janela] [passos]", modo_recupera },
    { "ranking", 5, "", modo_ranking },
    { "varre2", 6, "<alvo> [saida]", modo_varre2 },
    { "corta", 6, "<alvo> [ini] [fim] [passo]", modo_corta },
    { "avanco", 7, "<alvo> <k> [ini] [fim] [saida]", modo_avanco },
    { "varrek", 7, "<alvo> <k> [ini] [fim] [saida]", modo_varrek },
    { "cresce", 6, "<alvo> [ini] [fim] [saida]", modo_cresce },
    { "campo", 7, "<alvo> <lista> [referencia.pgm]", modo_campo },
    { "mapa", 5, "", modo_mapa },
    { "panorama", 5, "", modo_panorama },
    { "cortes", 5, "[folga]", modo_cortes },
    { "corte", 6, "<alvo>", modo_corte },
    { "varre1", 6, "<alvo> [saida] [jini] [jfim]", modo_varre1 },
    { "testa", 6, "<lista>", modo_testa },
    { "serie", 9, "<ini> <fim> <saida.yuv> <mapa.txt>", modo_serie },
    { "trinca", 7, "<alvo> <lista>", modo_trinca },
    { "dumpyuv", 7, "<alvo> <saida.yuv>", modo_dumpyuv },
    { "dump", 7, "<alvo> <saida.pgm>", modo_dump },
    { "report", 5, "", modo_report },
};
static const int N_MODOS = sizeof MODOS / sizeof MODOS[0];

    const Modo *m = NULL;
    for (int k = 0; k < N_MODOS; k++)
        if (!strcmp(modo, MODOS[k].nome)) { m = &MODOS[k]; break; }
    if (!m) {
        fprintf(stderr, "modo desconhecido: %s\n\nmodos:\n", modo);
        for (int k = 0; k < N_MODOS; k++)
            fprintf(stderr, "  %-10s %s\n", MODOS[k].nome, MODOS[k].uso);
        return 1;
    }
    if (argc < m->min_args) {
        fprintf(stderr, "uso: %s <mp4> <index.txt> <patches.txt> %s %s\n",
                argv[0], m->nome, m->uso);
        return 1;
    }
    return m->executa(argc, argv);
}
