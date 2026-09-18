"""cabecalho_slice.py -- acha bits trocados no cabecalho do slice pelo que ele DEVERIA dizer.

O cabecalho do slice deste filme e quase todo previsivel:

  slice_type, frame_num, pic_order_cnt_lsb   saem do ctts (ordem de exibicao),
                                             que o ESTADO.md da como integro
  first_mb, pps_id, cabac_init, deblocking   sao constantes no filme inteiro
  reordenacao, marcacao, num_ref_idx         seguem poucos padroes fixos

Isso da um juiz que nao depende do decodificador: um candidato so passa se o
cabecalho que ele produz e VALIDO pela norma e bate com o esperado. Foi assim
que os 2 bits do cabecalho do frame 11 apareceram (dados/janela_f11.txt).

Pega os dois casos:
  - cabecalho INVALIDO (o ffmpeg rejeita o quadro ou decodifica lixo);
  - cabecalho valido mas ERRADO (POC 168 onde deveria ser 40: um bit, e nenhum
    validador acusa).

    python ferramentas/cabecalho_slice.py [saida.txt]

Le o MP4 ORIGINAL com o patches.txt aplicado em memoria. NAO escreve no
patches.txt: a saida e uma lista de candidatos para o usuario decidir.
"""
import struct
import sys
from collections import Counter
from itertools import combinations

MP4 = 'Caio & Lizandra - Making- Caio-Balu.mp4'
V_N = 3445

# ---- parametros do SPS/PPS corrigidos (ESTADO.md, secao 1) ----
LOG2_MAX_FN = 8          # log2_max_frame_num_minus4 = 4
LOG2_MAX_POC = 8         # log2_max_pic_order_cnt_lsb_minus4 = 4
NREF_L0_PADRAO = 3       # num_ref_idx_l0_default_active_minus1 = 2
NREF_L1_PADRAO = 3
PIC_INIT_QP = 26


class Invalido(Exception):
    pass


class Bits:
    """Leitor de bits sobre o RBSP.

      dados  bytes do RBSP (sem os bytes de prevencao de emulacao)
    """
    def __init__(self, dados):
        self.b = dados
        self.p = 0
        self.n = 8 * len(dados)

    def u(self, k):
        if self.p + k > self.n:
            raise Invalido('fim dos dados')
        v = 0
        for _ in range(k):
            v = (v << 1) | ((self.b[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            if z > 31:
                raise Invalido('ue com zeros demais')
        return (1 << z) - 1 + (self.u(z) if z else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)


def faixa(nome, v, lo, hi):
    if not lo <= v <= hi:
        raise Invalido('%s=%d fora de [%d,%d]' % (nome, v, lo, hi))
    return v


def rbsp(nal):
    """Tira os bytes de prevencao de emulacao.

      nal  bytes do NAL a partir do byte de cabecalho

    Devolve: os bytes do RBSP.
    """
    out = bytearray()
    z = 0
    for x in nal:
        if z >= 2 and x == 3:
            z = 0
            continue
        out.append(x)
        z = z + 1 if x == 0 else 0
    return bytes(out)


def le_cabecalho(nal):
    """Le o cabecalho do slice, com as faixas que a norma exige.

      nal  bytes do NAL a partir do byte de cabecalho (nal_unit_type 1 ou 5)

    Devolve: dict com os campos. Levanta Invalido em qualquer valor fora da
    faixa, em codigo Exp-Golomb impossivel ou em bit de alinhamento CABAC em 0
    -- as mesmas recusas do `-bsf:v trace_headers` do ffmpeg.
    """
    r = Bits(rbsp(nal[:64]))
    h = {}
    r.u(1)
    h['nal_ref_idc'] = r.u(2)
    tipo_nal = r.u(5)
    idr = tipo_nal == 5
    h['first_mb'] = faixa('first_mb', r.ue(), 0, 8159)
    h['slice_type'] = faixa('slice_type', r.ue(), 0, 9)
    st = h['slice_type'] % 5
    if st not in (0, 1, 2):
        raise Invalido('slice_type SP/SI')
    h['pps_id'] = faixa('pps_id', r.ue(), 0, 0)
    h['frame_num'] = r.u(LOG2_MAX_FN)
    if idr:
        h['idr_pic_id'] = faixa('idr_pic_id', r.ue(), 0, 65535)
    h['poc'] = r.u(LOG2_MAX_POC)
    n0, n1 = NREF_L0_PADRAO, NREF_L1_PADRAO
    if st == 1:
        h['direct_spatial'] = r.u(1)
    if st in (0, 1):
        h['override'] = r.u(1)
        if h['override']:
            n0 = faixa('num_ref_idx_l0', r.ue(), 0, 31) + 1
            if st == 1:
                n1 = faixa('num_ref_idx_l1', r.ue(), 0, 31) + 1
        h['n0'], h['n1'] = n0, (n1 if st == 1 else 0)
        for lista in ((0, 1) if st == 1 else (0,)):
            f = r.u(1)
            h['mod_l%d' % lista] = f
            while f:
                idc = faixa('modification_of_pic_nums_idc', r.ue(), 0, 3)
                if idc == 3:
                    break
                if idc in (0, 1):
                    faixa('abs_diff_pic_num_minus1', r.ue(), 0, (1 << LOG2_MAX_FN) - 1)
                else:
                    faixa('long_term_pic_num', r.ue(), 0, (1 << LOG2_MAX_FN) - 1)
    if st == 0:                                   # weighted_pred_flag = 1, so P
        h['luma_denom'] = faixa('luma_log2_weight_denom', r.ue(), 0, 7)
        h['chroma_denom'] = faixa('chroma_log2_weight_denom', r.ue(), 0, 7)
        pesos = []
        for i in range(n0):
            lw = (r.u(1) and (faixa('luma_weight', r.se(), -128, 127),
                              faixa('luma_offset', r.se(), -128, 127))) or None
            cw = (r.u(1) and tuple(faixa('chroma', r.se(), -128, 127)
                                   for _ in range(4))) or None
            pesos.append((lw, cw))
        h['pesos'] = pesos
    if h['nal_ref_idc']:
        if idr:
            h['no_output'] = r.u(1)
            h['long_term'] = r.u(1)
        else:
            h['amrpm'] = r.u(1)
            if h['amrpm']:
                while True:
                    op = faixa('mmco', r.ue(), 0, 6)
                    if op == 0:
                        break
                    if op in (1, 3):
                        r.ue()
                    if op in (2, 3, 6, 4):
                        r.ue()
    if st != 2:
        h['cabac_init'] = faixa('cabac_init_idc', r.ue(), 0, 2)
    h['qp_delta'] = r.se()
    faixa('qp', PIC_INIT_QP + h['qp_delta'], 0, 51)
    h['deblock'] = faixa('disable_deblocking_filter_idc', r.ue(), 0, 2)
    if h['deblock'] != 1:
        h['alpha'] = faixa('alpha', r.se(), -6, 6)
        h['beta'] = faixa('beta', r.se(), -6, 6)
    while r.p & 7:
        if r.u(1) != 1:
            raise Invalido('cabac_alignment_one_bit = 0')
    h['bits'] = r.p
    return h


# ---- o que cada campo DEVERIA valer ----

def esperado_por_ctts(d, pos):
    """Tipo, frame_num e POC esperados de cada quadro, pela ordem de exibicao.

      d    o arquivo inteiro, com os patches aplicados
      pos  offset de cada amostra de video

    Devolve: lista de (slice_type, frame_num, poc) por quadro. O tipo sai da
    exibicao: quadro exibido depois de tudo o que veio antes no GOP e
    referencia (P); o que e exibido antes e B. frame_num conta as referencias
    desde o IDR; POC e 2x a posicao de exibicao dentro do GOP.
    """
    i = d.find(b'ctts', 24, 24 + 61164)          # o primeiro ctts do moov e o do video
    n = struct.unpack_from('>I', d, i + 8)[0]
    comp = []
    for k in range(n):
        c, o = struct.unpack_from('>II', d, i + 12 + 8 * k)
        comp += [o] * c
    assert len(comp) == V_N, 'ctts nao cobre os %d quadros' % V_N
    disp = [(t * 1001 + comp[t]) // 1001 for t in range(V_N)]

    def gop(g, fim):
        """Tipo, frame_num e POC de cada quadro de [g, fim), com IDR em g."""
        e, mx, refs = {}, -1, 0
        for t in range(g, fim):
            k = disp[t] - disp[g]
            ref = t == g or k > mx
            e[t] = (7 if t == g else (5 if ref else 6), refs % 256, (2 * k) % 256)
            refs += ref
            mx = max(mx, k)
        return e

    def lido(t):
        try:
            h = le_cabecalho(bytes(d[pos[t] + 4:pos[t] + 68]))
            return h['slice_type'], h['frame_num'], h['poc']
        except Invalido:
            return None

    # O byte NAL diz quem e IDR, mas ele tambem sofre bit-rot. O 1595 tem 0x65
    # (IDR) e NAO e IDR: os 9 cabecalhos legiveis de 1596-1610 continuam a
    # sequencia do IDR 1582, nenhum reinicia. Contar a partir dele "consertou"
    # frame_num/POC certos em 6 quadros (dados/patches_cabecalho.txt). Entao um
    # candidato so vira IDR se os quadros seguintes REINICIAM a partir dele.
    cand = [t for t in range(V_N) if eh_idr(d, pos[t])]
    idr = [cand[0]]
    for j, g in enumerate(cand[1:], 1):
        fim = cand[j + 1] if j + 1 < len(cand) else V_N
        reinicia = gop(g, fim)
        continua = gop(idr[-1], fim)
        viz = [(t, lido(t)) for t in range(g + 1, min(fim, g + 17))]
        a = sum(1 for t, v in viz if v and v == reinicia[t])
        b = sum(1 for t, v in viz if v and v == continua[t])
        if b > a:
            print('[!] quadro %d: byte NAL diz IDR, mas %d vizinhos continuam o GOP '
                  'do %d contra %d que reiniciam -- nao e IDR' % (g, b, idr[-1], a))
        else:
            idr.append(g)
    esp = [None] * V_N
    for j, g in enumerate(idr):
        for t, v in gop(g, idr[j + 1] if j + 1 < len(idr) else V_N).items():
            esp[t] = v
    return esp


def eh_idr(d, off):
    return d[off + 4] & 0x1f == 5


def desvios(h, esp):
    """Quantos campos fogem do padrao do filme.

      h    o cabecalho lido
      esp  (slice_type, frame_num, poc) esperados

    Devolve: (duros, moles). DURO e o que o ctts e o SPS/PPS fixam: tipo,
    frame_num, POC, first_mb. MOLE e o que o filme usa em ~97% dos quadros
    validos (medido): cabac_init 0, deblocking 0/-1/-1, sem reordenacao, sem
    marcacao adaptativa, direct espacial nos B, denominadores de peso 0 ou 5.
    """
    tipo, fn, poc = esp
    duro = (h['slice_type'] != tipo) + (h['frame_num'] != fn) + (h['poc'] != poc) \
        + (h['first_mb'] != 0)
    st = h['slice_type'] % 5
    mole = 0
    mole += h.get('cabac_init', 0) != 0
    mole += h['deblock'] != 0
    mole += h.get('alpha', -1) != -1
    mole += h.get('beta', -1) != -1
    mole += h.get('mod_l0', 0) + h.get('mod_l1', 0)
    mole += h.get('amrpm', 0)
    # O QP varia de verdade por quadro: so da para limitar a faixa. A primeira
    # versao usava faixas estreitas (B -12..2, P -16..0) e acusou 36 quadros
    # BONS das ilhas -- a cena final usa QP mais alto. Agora e o minimo e o
    # maximo medidos nos cabecalhos coerentes do filme, com folga de 2.
    if st == 1:
        mole += h.get('direct_spatial', 1) != 1
        mole += (h['n0'], h['n1']) not in ((2, 3), (1, 2))
        mole += not -15 <= h['qp_delta'] <= 10
    elif st == 0:
        mole += h['luma_denom'] not in (0, 5)
        mole += h['chroma_denom'] != 0
        mole += h['n0'] not in (1, 2, 3)
        mole += not -18 <= h['qp_delta'] <= 12
    else:
        mole += not -22 <= h['qp_delta'] <= 2
    return duro, mole


def main():
    saida = sys.argv[1] if len(sys.argv) > 1 else 'dados/cabecalho_slice.txt'
    d = bytearray(open(MP4, 'rb').read())
    for l in open('patches.txt'):
        p = l.split()
        if len(p) >= 2 and p[0].isdigit():
            d[int(p[0])] ^= 1 << int(p[1])
    pos = [int(l.split()[1]) for l in open('index.txt') if l.strip()]
    tam = [int(l.split()[2]) for l in open('index.txt') if l.strip()]
    esp = esperado_por_ctts(d, pos)

    # Validacao do proprio juiz: nos cabecalhos que ja estao bons, o padrao tem
    # que bater. Se nao bater em quase todos, o juiz esta errado, nao o filme.
    bons = Counter()
    for t in range(V_N):
        try:
            h = le_cabecalho(bytes(d[pos[t] + 4:pos[t] + 4 + 64]))
            bons[desvios(h, esp[t])] += 1
        except Invalido:
            bons['invalido'] += 1
    print('estado atual: %s' % dict(bons.most_common(8)))

    linhas = []
    resumo = Counter()
    for t in range(V_N):
        nal = bytearray(d[pos[t] + 4:pos[t] + 4 + min(64, tam[t] - 4)])
        try:
            h0 = le_cabecalho(bytes(nal))
            if desvios(h0, esp[t]) == (0, 0):
                continue                                  # ja esta bom
            fim_bits = h0['bits']
            motivo0 = 'valido, desvia %d/%d' % desvios(h0, esp[t])
        except Invalido as e:
            fim_bits = 8 * min(40, len(nal))
            motivo0 = 'invalido: %s' % e
        janela = range(8, min(8 * len(nal), fim_bits + 24))   # nunca o byte do NAL
        achados = []
        for k in (1, 2):
            for combo in combinations(janela, k):
                c = bytearray(nal)
                for b in combo:
                    c[b >> 3] ^= 0x80 >> (b & 7)
                try:
                    h = le_cabecalho(bytes(c))
                except Invalido:
                    continue
                if desvios(h, esp[t]) == (0, 0):
                    achados.append(combo)
            if achados:
                break
        resumo[(len(achados[0]) if achados else 0, min(len(achados), 2))] += 1
        fmt = lambda combo: ' + '.join('%d %d' % (pos[t] + 4 + (b >> 3), 7 - (b & 7))
                                       for b in combo)
        linhas.append('%d | %s | %d solucao(oes): %s' % (
            t, motivo0, len(achados), ' ; '.join(fmt(c) for c in achados[:6])))
        print(linhas[-1][:150], flush=True)
    with open(saida, 'w', newline='\n') as f:
        f.write('# quadro | estado do cabecalho hoje | solucoes (offset bit [+ offset bit])\n')
        f.write('\n'.join(linhas) + '\n')
    print('\nresumo (bits, solucoes: 1 = unica, 2 = ambigua): %s' % dict(resumo))


if __name__ == '__main__':
    main()
