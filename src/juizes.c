/* juizes.c -- a camada de MEDIDAS.
 *
 * Toda funcao aqui depende exclusivamente dos argumentos: recebe um plano de
 * pixels e devolve um numero ou um sim/nao. Nao le estado do decodificador, nao
 * sabe o que e um candidato, nao conhece piso nenhum.
 *
 * O motivo de existir nao e arrumacao: MEDIDA PURA SE TESTA SEM DECODIFICADOR.
 * Um quadro sintetico com estrutura conhecida -- borrao de N linhas a partir da
 * linha L, faixa com salto de valor V -- responde em milissegundos o que hoje
 * exige decodificar quadro real e olhar. Foi essa friccao que deixou passar as
 * armadilhas 40 a 43, todas da forma "a medida mede outra coisa".
 *
 * A POLITICA -- os piso_*, o estado da base, o trinca_ok, a linha de pontuacao
 * -- fica no reparador.c. Esta camada nao chama aquela.
 */
#include "juizes.h"
#include <stdlib.h>
#include <math.h>

/* Tolerancia da comparacao entre linhas vizinhas. Fica DENTRO do modulo, com
 * ajustador, porque o valor vem de variavel de ambiente lida no main. */
static int tol_copia = 1;
void juizes_tolerancia(int t) { tol_copia = t; }

/* Blocagem do QUADRO INTEIRO: descontinuidade na grade 16x16 do macrobloco
 * contra a do interior. Num decode correto o deblocking deixa a razao perto de
 * 1; residuo corrompido cria degraus nas bordas e a razao sobe. Nao precisa de
 * imagem de referencia.
 *
 *   Y     plano de luma, empacotado com passo w
 *   w, h  largura e altura em pixels
 *
 * Devolve: razao borda/interior, tipicamente entre 0,8 e 1,5. **ZERO quando nao
 *          da para medir** -- e o valor de "tudo bem" nesta escala, entao um 0
 *          lido sem contexto passa por quadro perfeito.
 *
 * NAO CONFUNDIR com `blocagem_faixa`, que mede outra coisa, amostra diferente e
 * devolve 99,0 no caso de falha -- o veredicto OPOSTO. As duas convivem de
 * proposito: cada uma tem calibracao propria e unifica-las invalidaria as duas.
 * Aqui a calibracao e do quadro inteiro, com os valores 0,80 / 1,04 / 1,429
 * registrados no docs/CRITERIOS.md. Ver docs/REFATORACAO.md, secao 2.
 *
 * Amostragem: TODO pixel, nos dois eixos. Borda e x%16==0 e y%16==0; interior e
 * so x%16==8 e y%16==8. */
double blocagem(const uint8_t *Y, int w, int h) {
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
double propagacao(const uint8_t *Y, int w, int h) {
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
int linhas_reais(const uint8_t *Y, int w, int h) {
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

int sem_imagem(const uint8_t *Y, int w, int h) {
    /* Amostra esparsa nao serve: o quadro de ocultacao com meia duzia de
     * macroblocos decodificados passa por ela e ainda e cinza. Usa o detector
     * de listra do projeto, que e o que separa imagem de propagacao. */
    return linhas_identicas(Y, w, h) > 400;   /* metade das 813 linhas */
}

int linha_copia(const uint8_t *Y, int w, int y) {
    for (int x = 0; x < w; x += 16) {
        int d = (int)Y[(size_t)y * w + x] - (int)Y[(size_t)(y - 1) * w + x];
        if (d < -tol_copia || d > tol_copia) return 0;
    }
    return 1;
}

/* Primeira linha a partir de y0 que inicia uma sequencia de 8+ copias exatas. */
int fronteira_borrao(const uint8_t *Y, int w, int h, int y0) {
    for (int y = y0 + 1; y < h - 8; ) {
        if (!linha_copia(Y, w, y)) { y++; continue; }
        int n = 0;
        while (y + n < h && linha_copia(Y, w, y + n)) n++;
        if (n >= 8) return y - 1;
        y += n + 1;
    }
    return h;
}

/* Estrutura do borrao abaixo de y: quantos trechos de 3+ e o maior deles. */
/* Estrutura do borrao abaixo de y0. Alem da contagem de trechos devolve quantas
 * LINHAS eles cobrem, porque contagem nao e comparavel entre candidatos: o
 * numero de trechos e proporcional a area que ainda esta borrada, e essa area
 * encolhe exatamente quando o conserto avanca. Medido no IDR 3047, a densidade
 * de trechos por linha e praticamente constante -- 0,2354 na base contra 0,2361
 * de media em 119 candidatos -- entao quem compara e a densidade e a cobertura,
 * nao os totais. Exigir "80% dos trechos da base" em valor absoluto reprovaria
 * um candidato por liberar imagem demais. */
void estrutura_borrao(const uint8_t *Y, int w, int h, int y0,
                             int *ntrechos, int *maior, int *linhas, int *longos) {
    int nt = 0, mx = 0, tot = 0, lg = 0;
    for (int y = y0 + 1; y < h; ) {
        if (!linha_copia(Y, w, y)) { y++; continue; }
        int n = 0;
        while (y + n < h && linha_copia(Y, w, y + n)) n++;
        if (n >= 3) { nt++; tot += n; if (n >= 8) lg += n; if (n > mx) mx = n; }
        y += n + 1;
    }
    *ntrechos = nt; *maior = mx;
    if (linhas) *linhas = tot;
    if (longos) *longos = lg;
}

/* Blocagem de uma FAIXA, nao do quadro. Mesma ideia da `blocagem` -- degrau na
 * grade 16x16 contra o interior -- com amostragem e convencao proprias.
 *
 *   Y       plano de luma, empacotado com passo w
 *   w       largura em pixels
 *   y0, y1  a faixa, [y0, y1), em linhas do quadro
 *
 * Devolve: razao borda/interior na faixa. **99,0 quando nao da para medir** --
 *          valor ALTO, que reprova em qualquer piso. A `blocagem` devolve ZERO
 *          no mesmo caso, que e o veredicto oposto.
 *
 * Calibracao propria, e por isso as duas nao se unificam: o limiar do juiz da
 * trinca e 1,45 NESTA faixa e NESTA amostragem. Medido no IDR 3047, na faixa
 * liberada: 0,97 em quadro bom, 1,34 no candidato aprovado pelo olho, 2,08 no
 * reprovado. No quadro inteiro os mesmos tres dao 1,07 / 1,17 / 1,19 e nao
 * separam nada -- era a medida certa no lugar errado (armadilha 41).
 *
 * Amostragem: 1 pixel em 4 no eixo X, linhas alternadas, so o eixo horizontal.
 * Interior e tudo que nao e x%16==0, ao contrario da `blocagem`. */
double blocagem_faixa(const uint8_t *Y, int w, int y0, int y1) {
    double bo = 0, bi = 0; long no = 0, ni = 0;
    for (int y = y0 + 1; y < y1; y += 2)
        for (int x = 16; x < w - 1; x += 4) {
            double d = fabs((double)Y[(size_t)y * w + x] - Y[(size_t)y * w + x - 1]);
            if (x % 16 == 0) { bo += d; no++; } else { bi += d; ni++; }
        }
    if (!no || !ni || bi == 0) return 99.0;
    return (bo / no) / (bi / ni);
}

/* Variante do piso da tarja de baixo que NAO fixa o valor em 16.
 * Existe para responder uma duvida de satisfazibilidade: o frame 13 decodifica
 * a tarja de cima em 15,000 com desvio zero, e todo quadro verificado do filme
 * tem 16. Se a tarja verdadeira dele for 15 -- herdada do frame 11, cuja tarja
 * so foi repintada depois da decodificacao e continua 15 no DPB -- entao exigir
 * 16 reprovaria ate o conserto certo, e o zero da varredura nao valeria nada.
 * Com o valor livre, sobra a uniformidade, que e o que se sabe a priori. */
/* Desvio da tarja de baixo. Em alvo que TEM tarja, ela e o melhor guia de
 * encadeamento que existe: o estado de chegada e desvio zero, e o caminho ate
 * la e monotono de um jeito que a fronteira do borrao nao e.
 *
 * Medido no frame 13: os candidatos que levam a fronteira a 1080 -- "sem borrao
 * nenhum" -- tem desvio de tarja de 13 a 26, contra ~2,5 dos que avancam pouco.
 * Ali "liberar tudo" quer dizer transformar a tarja em ruido. Seguir a fronteira
 * neste alvo e subir no ramo errado. */
double tarja_baixo_desvio(const uint8_t *Y, int w, int h) {
    if (w < 1920 || h < 1080) return -1;
    double s = 0, s2 = 0; long n = 0;
    for (int y = 962; y < 1080; y++)
        for (int x = 0; x < w; x++) { double c = Y[(size_t)y * w + x]; s += c; s2 += c * c; n++; }
    double m = s / n, va = s2 / n - m * m;
    return va > 0 ? sqrt(va) : 0;
}

int tarja_baixo_uniforme(const uint8_t *Y, int w, int h) {
    if (w < 1920 || h < 1080) return 0;
    int v = Y[(size_t)962 * w];
    for (int y = 962; y < 1080; y++)
        for (int x = 0; x < w; x += 8)
            if (Y[(size_t)y * w + x] != v) return 0;
    return 1;
}

/* A tarja de baixo (linhas 962-1079) e a fileira de macrobloco 60 em diante --
 * DEPOIS do ponto onde o frame 13 falha, na fileira 55. Exigi-la como piso de
 * uma metrica de PROGRESSO e contradicao: reprova todo candidato que ainda nao
 * terminou o quadro, e foi o que zerou os 292.216 do frame 13.
 *
 * A tarja de CIMA e a fileira 0 a 7, decodificada antes de qualquer defeito
 * tardio. Serve de piso sem estragar a medida. Nao fixa o valor em 16 de
 * proposito: o frame 13 sai com a de cima em 15,000 e desvio zero, herdando o
 * frame 11, e uniformidade e o que se sabe a priori -- o valor, nao. */
int tarja_topo_uniforme(const uint8_t *Y, int w, int h, int valor_exigido) {
    if (w < 1920 || h < 1080) return 0;
    int v = Y[0];
    /* PISO_TOPO=2 exige o VALOR, nao so a uniformidade. Medido nos quadros
     * verificados 0, 5, 9, 12, 2340, 2350, 2360 e 3443: as DUAS tarjas dao
     * 16,000 com desvio 0,000, sem excecao. O frame 13 da 15,000 na de cima --
     * uniforme, entao nao e dessincronizacao, e deslocamento global de -1 num
     * trecho que decodifica MUITO antes do travamento da fileira 54. Julgar por
     * esse valor isola esse defeito dos outros.
     *
     *   valor_exigido  o valor que a tarja tem que ter; NEGATIVO aceita
     *                  qualquer um, exigindo so a uniformidade. O chamador
     *                  passa 16 quando PISO_TOPO=2. A politica mora la, nao
     *                  aqui -- esta funcao nao le variavel de ambiente. */
    if (valor_exigido >= 0 && v != valor_exigido) return 0;
    for (int y = 0; y < 124; y++)
        for (int x = 0; x < w; x += 8)
            if (Y[(size_t)y * w + x] != v) return 0;
    return 1;
}

/* ---- linhas identicas: o detector de propagacao vertical ----
 * O `propagacao` conta linhas PARECIDAS com a de cima, e cena desfocada com
 * grandes areas uniformes acerta valores altos legitimamente -- o GOP 3368 tem
 * quadros perfeitos com propagacao 0,98. Linha EXATAMENTE identica separa de
 * forma binaria: medido em 137 quadros bons, todos dao ZERO; o IDR 1683,
 * listrado, da 34,3%. Ver armadilha 16. */
int linhas_identicas(const uint8_t *Y, int w, int h) {
    int n = 0;
    for (int y = 137; y < 950 && y < h; y++) {
        long d = 0;
        for (int x = 0; x < w; x += 4)
            d += abs((int)Y[(size_t)y * w + x] - (int)Y[(size_t)(y - 1) * w + x]);
        if (d == 0) n++;
    }
    return n;
}

int tarja_baixo_e_16(const uint8_t *Y, int w, int h) {
    if (w < 1920 || h < 1080) return 0;
    for (int y = 962; y < 1080; y++)
        for (int x = 0; x < w; x += 8)
            if (Y[(size_t)y * w + x] != 16) return 0;
    return 1;
}
