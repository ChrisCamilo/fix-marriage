# cabac_p.py -- codificador CABAC de slice P que espelha o decodificador do JM.
#
# Plano 5 (docs/PLANO_RECODIFICA_F11.md), passo 2. Recebe a sintaxe de cada MB
# -- no formato que o jm_trace.py le do trace do JM -- e devolve os bits que um
# codificador conforme a norma escreve para ela. Cada elemento usa o contexto
# que o JM usa ao le-lo (source/app/ldecod/cabac.c do JM 19.1, funcoes read_*):
# a mesma escolha de contexto pelos vizinhos, os mesmos arrays, a mesma
# inicializacao (ctx_init.json, extraido do ctx_tables.h do JM).
#
# Cobre o que um quadro P de fluxo progressivo 4:2:0 sem 8x8 usa: mb_skip_flag,
# mb_type de P (inter 16x16 e o prefixo intra com I16x16), ref_idx (omitido
# quando o trace nao o traz: uma referencia ativa so),
# mvd, coded_block_pattern, mb_qp_delta, intra_chroma_pred_mode e o residuo
# (DC luma de I16x16, blocos 4x4 de luma, DC e AC de croma), mais o
# end_of_slice_flag. Nao cobre 16x8, 8x16, P8x8 nem I4x4 (erro explicito).
#
# Validado bit a bit, slice inteiro com o stop bit, nos quadros P 3, 7 e 9 do
# GOP 0 (o 3 com o campo em skip; o 7 e o 9 com o campo em I16x16). O 1 (tem
# MB I4x4) e o 5 (tem 16x8) ficam fora do alcance.
#
#   codifica(sintaxe, qp, modelo, n_mb=None, posicoes=None)
#     -> lista de bits (0/1) do slice data, do primeiro bit depois do
#        alinhamento ate o stop bit, inclusive
#     sintaxe  {mb: [(elemento, (valores...)), ...]} para os MBs 0..N-1 em ordem
#     qp       QP do slice (26 + pic_init_qp_minus26 + slice_qp_delta)
#     modelo   cabac_init_idc
#     n_mb     quantos MBs codificar (padrao: todos)
#     posicoes lista que recebe, por MB, o indice do primeiro bit escrito nele
#              (os bits de um simbolo saem com atraso: e aproximado)
#
# Validado reproduzindo quadros do arquivo bit a bit (ver o __main__).
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cabac_enc import RL, MPS, LPS

TAB = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ctx_init.json')))
W = 120                                   # MBs por fileira (1920/16)

# tabelas de bloco de residuo do JM (cabac.c), indexadas pelo tipo de bloco
LUMA_16DC, LUMA_16AC, LUMA_4x4, CHROMA_DC, CHROMA_AC = 0, 1, 5, 6, 7
MAXPOS = [15, 14, 63, 31, 31, 15, 3, 14, 7, 15, 15, 14, 63, 31, 31, 15, 15, 14, 63, 31, 31, 15]
C1ISDC = [1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1]
T2BCBP = [0, 1, 2, 3, 3, 4, 5, 6, 5, 5, 10, 11, 12, 13, 13, 14, 16, 17, 18, 19, 19, 20]
T2MAP = [0, 1, 2, 3, 4, 5, 6, 7, 6, 6, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21]
T2ONE = T2BCBP
MAXC2 = [4, 4, 4, 4, 4, 4, 3, 4, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4]
P2C_4x4 = list(range(15)) + [14]
P2L_4x4 = list(range(16))
# CHROMA_DC (6) usa o mapa 4x4 comum no JM (pos2ctx_map[6]); o 4x4c e do tipo 9 (4:4:4)
POS2CTX_MAP = {0: P2C_4x4, 1: P2C_4x4, 5: P2C_4x4, 6: P2C_4x4, 7: P2C_4x4}
POS2CTX_LAST = {0: P2L_4x4, 1: P2L_4x4, 5: P2L_4x4, 6: P2L_4x4, 7: P2L_4x4}
NOME_BLOCO = {'DC luma 16x16': LUMA_16DC, 'AC luma 16x16': LUMA_16AC, 'Luma sng': LUMA_4x4,
              '2x2 DC Chroma': CHROMA_DC, 'AC Chroma': CHROMA_AC}


def contextos(qp, modelo, tipo='P'):
    """Todos os contextos do slice, inicializados como no init_contexts do JM.

      qp      QP do slice
      modelo  cabac_init_idc (0-2 em P; ignorado em I)
      tipo    'P' ou 'I'

    Devolve: dict nome -> listas aninhadas de [estado, MPS], com a forma dos
    arrays do JM (mb_type[3][11], ref_no[2][6], bcbp[22][4], ...)."""
    m = modelo if tipo == 'P' else 0
    def ini(mn):
        pre = ((mn[0] * qp) >> 4) + mn[1]
        if pre >= 64: return [min(126, pre) - 64, 1]
        return [63 - max(1, pre), 0]
    def arr(nome, dim1=False):
        t = TAB['INIT_%s_%s' % (nome, tipo)][m]
        c = [[ini(x) for x in linha] for linha in t]
        return c[0] if dim1 else c
    return dict(mb_type=arr('MB_TYPE'), b8_type=arr('B8_TYPE'), mv_res=arr('MV_RES'), ref_no=arr('REF_NO'),
                delta_qp=arr('DELTA_QP', True), cipr=arr('CIPR', True), cbp=arr('CBP'), bcbp=arr('BCBP'),
                map=arr('MAP'), last=arr('LAST'), one=arr('ONE'), abs=arr('ABS'))


class Enc:
    """Codificador aritmetico binario da norma (9.3.4), com o firstBitFlag."""
    def __init__(self):
        self.low, self.rng, self.outst, self.primeiro, self.bits = 0, 510, 0, True, []
    def _put(self, b):
        if self.primeiro: self.primeiro = False
        else: self.bits.append(b)
        while self.outst > 0:
            self.bits.append(1 - b); self.outst -= 1
    def _ren(self):
        while self.rng < 256:
            if self.low < 256: self._put(0)
            elif self.low >= 512: self.low -= 512; self._put(1)
            else: self.low -= 256; self.outst += 1
            self.rng <<= 1; self.low <<= 1
    def bin(self, c, b):
        r = RL[c[0]][(self.rng >> 6) & 3]; self.rng -= r
        if b != c[1]:
            self.low += self.rng; self.rng = r
            if c[0] == 0: c[1] = 1 - c[1]
            c[0] = LPS[c[0]]
        else: c[0] = MPS[c[0]]
        self._ren()
    def bypass(self, b):
        self.low <<= 1
        if b: self.low += self.rng
        if self.low >= 1024: self._put(1); self.low -= 1024
        elif self.low < 512: self._put(0)
        else: self.low -= 512; self.outst += 1
    def term(self, b):
        self.rng -= 2
        if b:
            self.low += self.rng; self.rng = 2; self._ren(); self._put((self.low >> 9) & 1)
            self.bits.append((self.low >> 8) & 1); self.bits.append(1)
        else: self._ren()
    def eg(self, v, k):
        """Exp-Golomb de ordem k em bypass (exp_golomb_decode_eq_prob do JM)."""
        while v >= (1 << k):
            self.bypass(1); v -= 1 << k; k += 1
        self.bypass(0)
        while k:
            k -= 1; self.bypass((v >> k) & 1)


class MB:
    """O que um MB ja codificado deixa para os vizinhos."""
    def __init__(self):
        self.skip = False; self.intra = False; self.cbp = 0; self.cipr = 0
        self.sbits = 0; self.ref = -1; self.mvd = [[0, 0]] * 16


def _blocos(elems):
    """Separa os coeficientes do trace em blocos: [(tipo, [coeficientes por posicao de varredura])]."""
    out = []; cur = None
    for nome, v in elems:
        t = NOME_BLOCO[nome]
        if cur is None: cur = (t, [])
        lev, run = v
        if lev == 0 and run == 0:
            out.append(cur); cur = None; continue
        cur[1].extend([0] * run); cur[1].append(lev)
    assert cur is None, 'bloco sem terminador no trace'
    return out


def codifica(sintaxe, qp, modelo, n_mb=None, posicoes=None):
    ctx = contextos(qp, modelo); e = Enc()
    n = n_mb if n_mb is not None else len(sintaxe)
    mbs = {}
    last_dq = [0]
    # o trace do JM nao registra o end_of_slice_flag = 1 do ultimo MB do slice
    fim_de_slice = lambda m, d: 1 if m == len(sintaxe) - 1 else d.get('end_of_slice_flag', 0)
    for m in range(n):
        if posicoes is not None: posicoes.append(len(e.bits))
        se = sintaxe[m]; r, c = divmod(m, W)
        esq = mbs.get(m - 1) if c > 0 else None
        cima = mbs.get(m - W) if r > 0 else None
        cur = MB(); mbs[m] = cur
        cab = [(k, v) for k, v in se if k not in NOME_BLOCO]
        res = _blocos([(k, v) for k, v in se if k in NOME_BLOCO])
        d = {}
        for k, v in cab: d.setdefault(k, v[0] if v else None)
        # --- mb_skip_flag: 1 no trace = NAO pulado
        a = 1 if esq and not esq.skip else 0; b = 1 if cima and not cima.skip else 0
        pulado = d['mb_skip_flag'] == 0
        e.bin(ctx['mb_type'][1][a + b], 1 if pulado else 0)
        if pulado:
            cur.skip = True; cur.ref = 0; last_dq[0] = 0
            e.term(fim_de_slice(m, d)); continue
        # --- mb_type de P
        t = d['mb_type']; mt = ctx['mb_type'][1]
        if t >= 6:
            e.bin(mt[4], 1)
            if t == 6: raise NotImplementedError('I4x4 em P')
            e.bin(mt[7], 1); e.term(0)                     # nao e I_PCM
            v = t - 7; ac, croma, pred = v // 12, (v % 12) // 4, v % 4
            e.bin(mt[8], ac)
            e.bin(mt[9], 1 if croma else 0)
            if croma: e.bin(mt[9], 1 if croma == 2 else 0)
            e.bin(mt[10], pred >> 1); e.bin(mt[10], pred & 1)
            cur.intra = True; cur.cbp = croma * 16 + (15 if ac else 0)
        else:
            if t == 1: e.bin(mt[4], 0); e.bin(mt[5], 0); e.bin(mt[6], 0)
            elif t == 2: e.bin(mt[4], 0); e.bin(mt[5], 1); e.bin(mt[7], 1)
            elif t == 3: e.bin(mt[4], 0); e.bin(mt[5], 1); e.bin(mt[7], 0)
            else: raise NotImplementedError('mb_type P %d' % t)
            if t != 1: raise NotImplementedError('particao %d' % t)
        if cur.intra:
            # --- intra_chroma_pred_mode
            cm = d['intra_chroma_pred_mode']
            a = 1 if esq and esq.cipr != 0 else 0; b = 1 if cima and cima.cipr != 0 else 0
            e.bin(ctx['cipr'][a + b], 1 if cm else 0)
            if cm: e.bin(ctx['cipr'][3], 1 if cm > 1 else 0)
            if cm > 1: e.bin(ctx['cipr'][3], 1 if cm > 2 else 0)
            cur.cipr = cm
        else:
            # --- ref_idx (P16x16: bloco 0,0; vizinhos 4x4 esquerdo e de cima)
            # ausente no trace = uma referencia ativa so: ref_idx nao e codificado
            ref = d.get('ref_idx_l0', 0)
            a = 1 if esq and esq.ref > 0 else 0; b = 2 if cima and cima.ref > 0 else 0
            rn = ctx['ref_no'][0]
            if 'ref_idx_l0' in d: e.bin(rn[a + b], 1 if ref else 0)
            if ref:
                e.bin(rn[4], 1 if ref > 1 else 0)
                for _ in range(ref - 2): e.bin(rn[5], 1)
                if ref > 1: e.bin(rn[5], 0)
            cur.ref = ref
            # --- mvd (x e y)
            mv = [d['mvd0_l0'], d['mvd1_l0']]
            for k in range(2):
                s = (abs(esq.mvd[3][k]) if esq else 0) + (abs(cima.mvd[12][k]) if cima else 0)
                ci = 5 * k if s < 3 else (5 * k + 3 if s > 32 else 5 * k + 2)
                v = mv[k]
                e.bin(ctx['mv_res'][0][ci], 1 if v else 0)
                if v:
                    u = abs(v) - 1; base = ctx['mv_res'][1]; b0 = 5 * k
                    e.bin(base[b0], 1 if u > 0 else 0)
                    if u > 0:
                        seq = [b0 + 1, b0 + 2] + [b0 + 3] * 10
                        if u < 8:
                            for i in range(u - 1): e.bin(base[seq[i]], 1)
                            e.bin(base[seq[u - 1]], 0)
                        else:
                            for i in range(7): e.bin(base[seq[i]], 1)
                            e.eg(u - 8, 3)
                    e.bypass(1 if v < 0 else 0)
            cur.mvd = [mv] * 16
            # --- coded_block_pattern
            cbp = d['coded_block_pattern']; cc = ctx['cbp']; acum = 0
            for my in (0, 2):
                for mx in (0, 2):
                    if my == 0: bb = (2 if (cima.cbp & (1 << (2 + (mx >> 1)))) == 0 else 0) if cima else 0
                    else: bb = 2 if (acum & (1 << (mx // 2))) == 0 else 0
                    if mx == 0: aa = (1 if (esq.cbp & (1 << (2 * (my // 2) + 1))) == 0 else 0) if esq else 0
                    else: aa = 1 if (acum & (1 << my)) == 0 else 0
                    bit = (cbp >> (my + (mx >> 1))) & 1
                    e.bin(cc[0][aa + bb], bit); acum |= bit << (my + (mx >> 1))
            bb = 2 if cima and cima.cbp > 15 else 0; aa = 1 if esq and esq.cbp > 15 else 0
            e.bin(cc[1][aa + bb], 1 if cbp > 15 else 0)
            if cbp > 15:
                bb = 2 if cima and (cima.cbp >> 4) == 2 else 0; aa = 1 if esq and (esq.cbp >> 4) == 2 else 0
                e.bin(cc[2][aa + bb], 1 if (cbp >> 4) == 2 else 0)
            cur.cbp = cbp
            if cbp == 0: last_dq[0] = 0
        # --- mb_qp_delta (sempre em I16x16; em inter so com cbp)
        if cur.intra or cur.cbp:
            dq = d['mb_qp_delta']; dc = ctx['delta_qp']
            e.bin(dc[1 if last_dq[0] else 0], 1 if dq else 0)
            if dq:
                u = (2 * dq - 1 if dq > 0 else -2 * dq) - 1
                e.bin(dc[2], 1 if u else 0)
                if u:
                    for _ in range(u - 1): e.bin(dc[3], 1)
                    e.bin(dc[3], 0)
            last_dq[0] = dq
        # --- residuo, na ordem de leitura do JM
        fila = list(res)
        def bloco(tipo, cbf_ctx_bits, bit_seta):
            t_, coef = fila.pop(0)
            assert t_ == tipo, 'bloco %d no trace, esperado %d (MB %d)' % (t_, tipo, m)
            up, lf = cbf_ctx_bits
            tem = any(coef)
            e.bin(ctx['bcbp'][T2BCBP[tipo]][2 * up + lf], 1 if tem else 0)
            if not tem: return
            cur.sbits |= 1 << bit_seta
            i0 = 0 if C1ISDC[tipo] else 1; i1 = MAXPOS[tipo] + (0 if C1ISDC[tipo] else 1)
            coef = coef + [0] * (i1 - i0 + 1 - len(coef))
            ult = max(i for i, x in enumerate(coef) if x)
            mp = ctx['map'][T2MAP[tipo]]; lp = ctx['last'][T2MAP[tipo]]
            for i in range(i0, i1):
                sig = coef[i - i0] != 0
                e.bin(mp[POS2CTX_MAP[tipo][i]], 1 if sig else 0)
                if sig:
                    fim = (i - i0) == ult
                    e.bin(lp[POS2CTX_LAST[tipo][i]], 1 if fim else 0)
                    if fim: break
            one = ctx['one'][T2ONE[tipo]]; ab = ctx['abs'][T2ONE[tipo]]
            c1, c2 = 1, 0
            for x in reversed(coef[:ult + 1]):
                if x == 0: continue
                a_ = abs(x)
                e.bin(one[c1], 1 if a_ > 1 else 0)
                if a_ > 1:
                    k = a_ - 2
                    e.bin(ab[c2], 1 if k else 0)
                    if k:
                        if k < 13:
                            for _ in range(k - 1): e.bin(ab[c2], 1)
                            e.bin(ab[c2], 0)
                        else:
                            for _ in range(12): e.bin(ab[c2], 1)
                            e.eg(k - 13, 0)
                    c2 = min(c2 + 1, MAXC2[tipo]); c1 = 0
                elif c1: c1 = min(c1 + 1, 4)
                e.bypass(1 if x < 0 else 0)
        def bit_viz(mbv, bit, padrao):
            return padrao if mbv is None else (mbv.sbits >> bit) & 1
        dflt = 1 if cur.intra else 0
        if cur.intra:                                     # DC de luma do I16x16
            bloco(LUMA_16DC, (bit_viz(cima, 0, 1), bit_viz(esq, 0, 1)), 0)
            if cur.cbp & 15: raise NotImplementedError('AC de luma em I16x16')
        else:
            for b8 in range(4):
                if not (cur.cbp >> b8) & 1: continue
                for j in range(2):
                    for i in range(2):
                        x = 2 * (b8 & 1) + i; y = 2 * (b8 >> 1) + j
                        lf = ((cur.sbits >> (1 + 4 * y + x - 1)) & 1) if x > 0 else bit_viz(esq, 1 + 4 * y + 3, dflt)
                        up = ((cur.sbits >> (1 + 4 * (y - 1) + x)) & 1) if y > 0 else bit_viz(cima, 1 + 12 + x, dflt)
                        bloco(LUMA_4x4, (up, lf), 1 + 4 * y + x)
        if cur.cbp > 15:
            for comp, bit in ((0, 17), (1, 18)):
                bloco(CHROMA_DC, (bit_viz(cima, bit, dflt), bit_viz(esq, bit, dflt)), bit)
        if (cur.cbp >> 4) == 2:
            for base in (19, 35):
                for y in range(2):
                    for x in range(2):
                        lf = ((cur.sbits >> (base + 4 * y + x - 1)) & 1) if x > 0 else bit_viz(esq, base + 4 * y + 1, dflt)
                        up = ((cur.sbits >> (base + 4 * (y - 1) + x)) & 1) if y > 0 else bit_viz(cima, base + 4 + x, dflt)
                        bloco(CHROMA_AC, (up, lf), base + 4 * y + x)
        assert not fila, 'sobrou residuo no trace (MB %d)' % m
        e.term(fim_de_slice(m, d))
    return e.bits


if __name__ == '__main__':
    # Validacao: recodifica quadros a partir do trace do JM e compara com o arquivo
    # (buffer ja remendado pelo patches.txt). QP, modelo e byte de inicio vem do
    # cabecalho de slice que o proprio trace registra.
    # uso (da raiz): SCRATCH=<dir com juiz_encoder.py> python tools/anchor/cabac_p.py <trace_dec.txt> <quadro:poc> ...
    import jm_trace
    tr = sys.argv[1]; pares = [tuple(map(int, x.split(':'))) for x in sys.argv[2:]]
    q = jm_trace.le(tr); cab = jm_trace.cabecalhos(tr)
    sys.argv = [sys.argv[0], os.environ.get('SCRATCH', '.'), '0']
    import calib_enc as CE, cauda_lote as L
    for t, poc in pares:
        f = cab[poc]['campos']; qp = 26 + f['slice_qp_delta']; mod = f.get('cabac_init_idc', 0)
        o, s_ = CE.IX[t][1], CE.IX[t][2]; nal = bytes(CE.d[o+4:o+s_])
        rb, _ = L.rbsp(nal); obs = [int(x) for x in rb[8 * cab[poc]['byte_ini']:]]
        bits = codifica(q[poc]['mbs'], qp, mod)
        n = min(len(bits), len(obs)); dif = [i for i in range(n) if bits[i] != obs[i]]
        print('quadro %d (POC %d, QP %d, modelo %d): %d bits codificados, %d observados; %d diferencas%s' % (
            t, poc, qp, mod, len(bits), len(obs), len(dif), (', primeira em %d' % dif[0]) if dif else ''))
