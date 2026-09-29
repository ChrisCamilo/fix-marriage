# cabac_p.py -- CABAC de slice P que espelha o decodificador do JM, nos dois
# sentidos: codifica uma sintaxe dada ou decodifica um fluxo.
#
# Plano 5 (docs/PLANO_RECODIFICA_F11.md), passos 2 e 3. A sintaxe de cada MB
# tem o formato que o jm_trace.py le do trace do JM. Cada elemento usa o
# contexto que o JM usa ao le-lo (source/app/ldecod/cabac.c do JM 19.1,
# funcoes read_*): a mesma escolha de contexto pelos vizinhos, os mesmos
# arrays, a mesma inicializacao (ctx_init.json, extraido do ctx_tables.h do
# JM). Cada elemento e escrito UMA vez, em processa_mb, contra um objeto de E/S:
# Enc escreve o bin pedido; Dec ignora o pedido e devolve o bin lido. Assim
# codificar e decodificar nao podem divergir.
#
# Cobre o que um quadro P de fluxo progressivo 4:2:0 sem 8x8 usa: mb_skip_flag,
# mb_type de P (inter 16x16, 16x8 e 8x16, e o prefixo intra com I16x16),
# ref_idx (por particao; omitido quando o slice tem uma referencia ativa so),
# mvd (por particao; contexto pelos blocos 4x4 vizinhos), coded_block_pattern,
# mb_qp_delta, intra_chroma_pred_mode e o residuo (DC luma de I16x16, blocos
# 4x4 de luma, DC e AC de croma), mais o end_of_slice_flag. Nao cobre P8x8,
# I4x4, AC de luma em I16x16 nem I_PCM (erro explicito).
#
# Slices I e B, so o que a tarja usa (censo da cabeca, tools/anchor/cabeca.py):
# em I, o mb_type do slice I (I16x16; I4x4 e I_PCM sao erro explicito) e o
# mesmo caminho intra do P; em B, o MB pulado e o B_Direct_16x16 (sem
# referencia nem vetor no fluxo; CBP, mb_qp_delta e residuo como no inter do
# P). Qualquer outro tipo de B e erro.
#
# Validado: codifica os quadros P 3, 5, 7 e 9 do GOP 0 a partir do trace do JM
# com 0 diferencas, slice inteiro com o stop bit (o 3 e o 5 com o campo em
# skip, o 5 com um MB 16x8; o 7 e o 9 com o campo em I16x16); decodifica os
# quatro e devolve a sintaxe do trace. O 1 (tem MB I4x4) fica fora do alcance.
#
#   codifica(sintaxe, qp, modelo, n_mb=None, posicoes=None, reinicio=None, tipo='P')
#     -> lista de bits (0/1) do slice data, do primeiro bit depois do
#        alinhamento ate o stop bit, inclusive
#     sintaxe  {mb: [(elemento, (valores...)), ...]} para os MBs 0..N-1 em ordem
#     qp       QP do slice (26 + pic_init_qp_minus26 + slice_qp_delta)
#     modelo   cabac_init_idc
#     n_mb     quantos MBs codificar (padrao: todos)
#     posicoes lista que recebe, por MB, o indice do primeiro bit escrito nele
#              (os bits de um simbolo saem com atraso: e aproximado)
#     reinicio (mb, range): ao chegar nesse MB, zera o low e os bits pendentes
#              e poe o range dado, mantendo os contextos -- para encaixar um
#              trecho cujo estado aritmetico de entrada e desconhecido (os
#              ~12 primeiros bits dali dependem do low e nao valem)
#     tipo     'P', 'I' ou 'B' (tipo do slice)
#
#   decodifica(bits, qp, modelo, n_mb=8160, nref=3, posicoes=None, tipo='P')
#     -> {mb: sintaxe} dos MBs lidos ate o fim do slice, o n_mb ou o primeiro
#        elemento fora do alcance (esse MB fica de fora)
#     bits     lista de 0/1 do slice data (do primeiro bit depois do alinhamento)
#     nref     referencias ativas da lista 0 (ref_idx so e lido se > 1)
#     posicoes lista que recebe, por MB, quantos bits o decodificador ja tinha
#              lido ao comecar o MB (inclui os 9 da inicializacao)
#     tipo     como no codifica
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
NOME_TIPO = {v: k for k, v in NOME_BLOCO.items()}


class ForaDoAlcance(Exception):
    """Elemento que o modulo nao cobre (ou que so o lixo produz)."""


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
    """Codificador aritmetico binario da norma (9.3.4), com o firstBitFlag.
    Cada metodo escreve o valor pedido e o devolve."""
    codifica = True
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
        return b
    def bypass(self, b):
        self.low <<= 1
        if b: self.low += self.rng
        if self.low >= 1024: self._put(1); self.low -= 1024
        elif self.low < 512: self._put(0)
        else: self.low -= 512; self.outst += 1
        return b
    def term(self, b):
        self.rng -= 2
        if b:
            self.low += self.rng; self.rng = 2; self._ren(); self._put((self.low >> 9) & 1)
            self.bits.append((self.low >> 8) & 1); self.bits.append(1)
        else: self._ren()
        return b
    def eg(self, v, k):
        """Exp-Golomb de ordem k em bypass (exp_golomb_decode_eq_prob do JM)."""
        v0 = v
        while v >= (1 << k):
            self.bypass(1); v -= 1 << k; k += 1
        self.bypass(0)
        while k:
            k -= 1; self.bypass((v >> k) & 1)
        return v0


class Dec:
    """Decodificador aritmetico binario da norma (9.3.3). Cada metodo ignora o
    valor pedido (None) e devolve o bin lido. Alem do fim dos bits, le zeros."""
    codifica = False
    def __init__(self, bits):
        self.b = bits; self.p = 0; self.rng = 510; self.off = 0
        for _ in range(9): self.off = (self.off << 1) | self._rd()
    def _rd(self):
        x = self.b[self.p] if self.p < len(self.b) else 0
        self.p += 1; return x
    def bin(self, c, b=None):
        r = RL[c[0]][(self.rng >> 6) & 3]; self.rng -= r
        if self.off >= self.rng:
            v = 1 - c[1]; self.off -= self.rng; self.rng = r
            if c[0] == 0: c[1] = 1 - c[1]
            c[0] = LPS[c[0]]
        else:
            v = c[1]; c[0] = MPS[c[0]]
        while self.rng < 256:
            self.rng <<= 1; self.off = (self.off << 1) | self._rd()
        return v
    def bypass(self, b=None):
        self.off = (self.off << 1) | self._rd()
        if self.off >= self.rng: self.off -= self.rng; return 1
        return 0
    def term(self, b=None):
        self.rng -= 2
        if self.off >= self.rng: return 1
        while self.rng < 256:
            self.rng <<= 1; self.off = (self.off << 1) | self._rd()
        return 0
    def eg(self, v, k):
        s = 0
        while self.bypass():
            s += 1 << k; k += 1
        b = 0
        while k:
            k -= 1
            if self.bypass(): b |= 1 << k
        return s + b


class MB:
    """O que um MB ja processado deixa para os vizinhos."""
    def __init__(self):
        self.skip = False; self.intra = False; self.cbp = 0; self.cipr = 0
        self.sbits = 0; self.ref = -1; self.mvd = [[0, 0]] * 16; self.ref4 = [-1] * 16
        self.bmt = 0                      # mb_type de B (0 = direto ou pulado)


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


def _pares(tipo, coef):
    """Coeficientes -> elementos do trace: (nivel, zeros antes), e o (0, 0) no fim."""
    out = []; z = 0
    for x in coef:
        if x: out.append((NOME_TIPO[tipo], (x, z))); z = 0
        else: z += 1
    return out + [(NOME_TIPO[tipo], (0, 0))]


def _bloco(io, ctx, tipo, up, lf, coef):
    """Um bloco de residuo: coded_block_flag, mapa de significancia e niveis.

      io      Enc ou Dec
      ctx     contextos do slice
      tipo    tipo de bloco do JM (LUMA_16DC, LUMA_4x4, CHROMA_DC, CHROMA_AC)
      up, lf  bit de CBF do bloco de cima e do da esquerda (contexto 2*up + lf)
      coef    coeficientes em ordem de varredura (Enc) ou None (Dec)

    Devolve: a lista de coeficientes (lida ou a dada), com o comprimento do
    tipo de bloco; toda zero se o coded_block_flag for 0."""
    enc = io.codifica
    i0 = 0 if C1ISDC[tipo] else 1; i1 = MAXPOS[tipo] + (0 if C1ISDC[tipo] else 1); n = i1 - i0 + 1
    if enc: coef = list(coef) + [0] * (n - len(coef))
    tem = io.bin(ctx['bcbp'][T2BCBP[tipo]][2 * up + lf], (1 if any(coef) else 0) if enc else None)
    if not tem: return [0] * n
    ult = max(i for i, x in enumerate(coef) if x) if enc else None
    c = [0] * n; fechou = False
    mp = ctx['map'][T2MAP[tipo]]; lp = ctx['last'][T2MAP[tipo]]
    for i in range(i0, i1):
        s = io.bin(mp[POS2CTX_MAP[tipo][i]], (1 if coef[i - i0] else 0) if enc else None)
        if s:
            c[i - i0] = 1
            if io.bin(lp[POS2CTX_LAST[tipo][i]], (1 if i - i0 == ult else 0) if enc else None):
                fechou = True; break
    if not fechou: c[n - 1] = 1                          # o ultimo e significativo por construcao
    one = ctx['one'][T2ONE[tipo]]; ab = ctx['abs'][T2ONE[tipo]]
    c1, c2 = 1, 0
    for j in reversed(range(n)):
        if not c[j]: continue
        a_ = abs(coef[j]) if enc else None
        val = 1
        if io.bin(one[c1], (1 if a_ > 1 else 0) if enc else None):
            k = a_ - 2 if enc else None; kk = 0
            if io.bin(ab[c2], (1 if k else 0) if enc else None):
                it = 0; l = 1
                while l and it < 12:
                    l = io.bin(ab[c2], (1 if it + 1 < k else 0) if enc else None); it += 1
                kk = it
                if l: kk = 12 + io.eg((k - 13) if enc else None, 0) + 1
            val = 2 + kk
            c2 = min(c2 + 1, MAXC2[tipo]); c1 = 0
        elif c1: c1 = min(c1 + 1, 4)
        sg = io.bypass((1 if coef[j] < 0 else 0) if enc else None)
        c[j] = -val if sg else val
    return c


def processa_mb(io, ctx, mbs, m, se, st, fim, marca=None):
    """Um MB de slice P (ou I, ou B pulado/direto), codificado (se dado) ou decodificado (se None).

      io   Enc ou Dec
      ctx  contextos do slice (mudam)
      mbs  {mb: MB} dos ja processados (recebe o atual)
      m    endereco do MB
      se   sintaxe do MB no formato do trace (Enc) ou None (Dec)
      st   estado do slice: 'dq' (ultimo mb_qp_delta), 'nref' e 'tipo'
           ('P', 'I' ou 'B'; ausente = 'P')
      fim  end_of_slice_flag a codificar (Enc; ignorado no Dec)
      marca  funcao chamada sem argumentos ao fim do cabecalho do MB (depois do
           mb_qp_delta, ou do CBP quando ele e 0) e ao fim de cada bloco de
           residuo -- para quem precisa saber quantos bits ja sairam em cada
           ponto (busca elemento a elemento)

    Devolve: (sintaxe no formato do trace, end_of_slice_flag). Levanta
    ForaDoAlcance em elemento nao coberto."""
    enc = io.codifica
    r, c = divmod(m, W)
    esq = mbs.get(m - 1) if c > 0 else None
    cima = mbs.get(m - W) if r > 0 else None
    cur = MB(); mbs[m] = cur; out = []
    d = {}; fila = []
    if enc:
        for k, v in se:
            if k not in NOME_BLOCO: d.setdefault(k, v[0] if v else None)
        fila = _blocos([(k, v) for k, v in se if k in NOME_BLOCO])
    E = lambda x: x if enc else None
    tipo = st.get('tipo', 'P'); direto = False
    if tipo == 'I':
        # --- mb_type de I (readMB_typeInfo_CABAC_i_slice): vizinho conta se existe
        # e nao e I4x4 -- e I4x4 aqui nunca e processado
        mt = ctx['mb_type'][0]; t = d.get('mb_type')
        a = 1 if esq else 0; b = 1 if cima else 0
        if not io.bin(mt[a + b], E(1)): raise ForaDoAlcance('I4x4 em I')
        if io.term(E(0)): raise ForaDoAlcance('I_PCM')
        v = t - 1 if enc else 0
        ac = io.bin(mt[4], E(v // 12))
        croma = 0
        if io.bin(mt[5], E(1 if (v % 12) // 4 else 0)):
            croma = 2 if io.bin(mt[6], E(1 if (v % 12) // 4 == 2 else 0)) else 1
        pred = 2 * io.bin(mt[7], E((v % 4) >> 1)) + io.bin(mt[8], E((v % 4) & 1))
        t = 1 + pred + 4 * croma + 12 * ac
        cur.intra = True; cur.cbp = croma * 16 + (15 if ac else 0)
    else:
        # --- mb_skip_flag (no trace: 1 = NAO pulado); em B, contextos 7-9 da linha 2
        a = 1 if esq and not esq.skip else 0; b = 1 if cima and not cima.skip else 0
        cs = ctx['mb_type'][2][7 + a + b] if tipo == 'B' else ctx['mb_type'][1][a + b]
        pul = io.bin(cs, E(1 if enc and d['mb_skip_flag'] == 0 else 0))
        out.append(('mb_skip_flag', (0 if pul else 1,)))
        if pul:
            cur.skip = True; cur.ref = 0; cur.ref4 = [0] * 16; st['dq'] = 0
            eos = io.term(E(fim)); out.append(('end_of_slice_flag', (eos,))); return out, eos
        if tipo == 'B':
            # --- mb_type de B (readMB_typeInfo_CABAC_b_slice): vizinho conta se
            # existe e tem mb_type != 0 (nem direto nem pulado); so o direto e coberto
            t = d.get('mb_type')
            a = 1 if esq and esq.bmt else 0; b = 1 if cima and cima.bmt else 0
            if io.bin(ctx['mb_type'][2][a + b], E(1 if enc and t else 0)): raise ForaDoAlcance('B nao direto')
            t = 0; direto = True
        # --- mb_type de P
        mt = ctx['mb_type'][1]
        if not direto: t = d.get('mb_type')
        if direto: pass
        elif io.bin(mt[4], E(1 if enc and t >= 6 else 0)):
            if not io.bin(mt[7], E(1 if enc and t >= 7 else 0)): raise ForaDoAlcance('I4x4 em P')
            if io.term(E(0)): raise ForaDoAlcance('I_PCM')
            v = t - 7 if enc else 0
            ac = io.bin(mt[8], E(v // 12))
            croma = 0
            if io.bin(mt[9], E(1 if (v % 12) // 4 else 0)):
                croma = 2 if io.bin(mt[9], E(1 if (v % 12) // 4 == 2 else 0)) else 1
            pred = 2 * io.bin(mt[10], E((v % 4) >> 1)) + io.bin(mt[10], E((v % 4) & 1))
            t = 7 + pred + 4 * croma + 12 * ac
            cur.intra = True; cur.cbp = croma * 16 + (15 if ac else 0)
        else:
            if io.bin(mt[5], E(1 if enc and t in (2, 3) else 0)):
                t = 2 if io.bin(mt[7], E(1 if enc and t == 2 else 0)) else 3
            else:
                t = 4 if io.bin(mt[6], E(1 if enc and t == 4 else 0)) else 1
            if t == 4: raise ForaDoAlcance('particao 8x8')
    out.append(('mb_type', (t,)))
    if cur.intra:
        # --- intra_chroma_pred_mode
        cm = d.get('intra_chroma_pred_mode')
        a = 1 if esq and esq.cipr != 0 else 0; b = 1 if cima and cima.cipr != 0 else 0
        v = 0
        if io.bin(ctx['cipr'][a + b], E(1 if enc and cm else 0)):
            v = 1
            if io.bin(ctx['cipr'][3], E(1 if enc and cm > 1 else 0)):
                v = 3 if io.bin(ctx['cipr'][3], E(1 if enc and cm > 2 else 0)) else 2
        cur.cipr = v; out.append(('intra_chroma_pred_mode', (v,)))
    else:
        if not direto:
            # --- particoes: 16x16 (1), 16x8 (2: cima, baixo), 8x16 (3: esquerda, direita).
            # Cada particao e dada pelo bloco 4x4 do canto (bx, by) e pelos blocos que cobre.
            if t == 1: parts = [((0, 0), range(16))]
            elif t == 2: parts = [((0, 0), range(8)), ((0, 2), range(8, 16))]
            else: parts = [((0, 0), [i for i in range(16) if i % 4 < 2]), ((2, 0), [i for i in range(16) if i % 4 >= 2])]
            def viz_bloco(bx, by):
                """(MB, indice do bloco) do vizinho esquerdo e de cima de um bloco 4x4 do MB atual."""
                a = (cur, by * 4 + bx - 1) if bx > 0 else ((esq, by * 4 + 3) if esq else (None, None))
                b = (cur, (by - 1) * 4 + bx) if by > 0 else ((cima, 12 + bx) if cima else (None, None))
                return a, b
            # --- ref_idx de todas as particoes, depois os mvd (ordem do JM)
            codificado = ('ref_idx_l0' in d) if enc else st['nref'] > 1
            refs_dados = [v[0] for k, v in se if k == 'ref_idx_l0'] if enc else []
            for pi, ((bx, by), blocos) in enumerate(parts):
                ref = 0
                if codificado:
                    rv = refs_dados[pi] if enc else None
                    (ma, ia), (mb_, ib) = viz_bloco(bx, by)
                    a = 1 if ma is not None and ma.ref4[ia] > 0 else 0
                    b = 2 if mb_ is not None and mb_.ref4[ib] > 0 else 0
                    rn = ctx['ref_no'][0]
                    if io.bin(rn[a + b], E(1 if enc and rv else 0)):
                        ref = 1
                        if io.bin(rn[4], E(1 if enc and rv > 1 else 0)):
                            ref = 2
                            while io.bin(rn[5], E(1 if enc and ref < rv else 0)): ref += 1
                    out.append(('ref_idx_l0', (ref,)))
                for i in blocos: cur.ref4[i] = ref
            cur.ref = cur.ref4[0]
            # --- mvd (x e y) de cada particao
            if t == 1: mvds_dados = [d.get('mvd0_l0'), d.get('mvd1_l0')] if enc else []
            else: mvds_dados = [v[0] for k, v in se if k == 'mvd_l0'] if enc else []
            cur.mvd = [[0, 0] for _ in range(16)]
            for pi, ((bx, by), blocos) in enumerate(parts):
                mv = []
                (ma, ia), (mb_, ib) = viz_bloco(bx, by)
                for k in range(2):
                    s = (abs(ma.mvd[ia][k]) if ma is not None else 0) + (abs(mb_.mvd[ib][k]) if mb_ is not None else 0)
                    ci = 5 * k if s < 3 else (5 * k + 3 if s > 32 else 5 * k + 2)
                    v = mvds_dados[2 * pi + k] if enc else None; val = 0
                    if io.bin(ctx['mv_res'][0][ci], E(1 if enc and v else 0)):
                        u = abs(v) - 1 if enc else None
                        base = ctx['mv_res'][1]; b0 = 5 * k; uu = 0
                        if io.bin(base[b0], E(1 if enc and u > 0 else 0)):
                            seq = [b0 + 1, b0 + 2] + [b0 + 3] * 10
                            it = 0; l = 1
                            while l and it < 7:
                                l = io.bin(base[seq[it]], E(1 if enc and it + 1 < u else 0)); it += 1
                            uu = it
                            if l: uu = 7 + io.eg((u - 8) if enc else None, 3) + 1
                        mag = uu + 1
                        val = -mag if io.bypass(E(1 if enc and v < 0 else 0)) else mag
                    mv.append(val); out.append((('mvd%d_l0' % k) if t == 1 else 'mvd_l0', (val,)))
                for i in blocos: cur.mvd[i] = mv
        # --- coded_block_pattern
        cbp_v = d.get('coded_block_pattern'); cc = ctx['cbp']; acum = 0
        for my in (0, 2):
            for mx in (0, 2):
                if my == 0: bb = (2 if (cima.cbp & (1 << (2 + (mx >> 1)))) == 0 else 0) if cima else 0
                else: bb = 2 if (acum & (1 << (mx // 2))) == 0 else 0
                if mx == 0: aa = (1 if (esq.cbp & (1 << (2 * (my // 2) + 1))) == 0 else 0) if esq else 0
                else: aa = 1 if (acum & (1 << my)) == 0 else 0
                sh = my + (mx >> 1)
                acum |= io.bin(cc[0][aa + bb], E((cbp_v >> sh) & 1 if enc else 0)) << sh
        bb = 2 if cima and cima.cbp > 15 else 0; aa = 1 if esq and esq.cbp > 15 else 0
        if io.bin(cc[1][aa + bb], E(1 if enc and cbp_v > 15 else 0)):
            bb = 2 if cima and (cima.cbp >> 4) == 2 else 0; aa = 1 if esq and (esq.cbp >> 4) == 2 else 0
            acum += 32 if io.bin(cc[2][aa + bb], E(1 if enc and (cbp_v >> 4) == 2 else 0)) else 16
        cur.cbp = acum; out.append(('coded_block_pattern', (acum,)))
        if acum == 0:
            st['dq'] = 0
            if marca: marca()
    # --- mb_qp_delta (sempre em I16x16; em inter so com cbp)
    if cur.intra or cur.cbp:
        dq = d.get('mb_qp_delta'); dc = ctx['delta_qp']; val = 0
        if io.bin(dc[1 if st['dq'] else 0], E(1 if enc and dq else 0)):
            u = ((2 * dq - 1 if dq > 0 else -2 * dq) - 1) if enc else None; cnt = 0
            if io.bin(dc[2], E(1 if enc and u else 0)):
                cnt = 1
                while io.bin(dc[3], E(1 if enc and cnt < u else 0)): cnt += 1
            act = cnt + 1
            val = (act + 1) >> 1
            if act % 2 == 0: val = -val
        st['dq'] = val; out.append(('mb_qp_delta', (val,)))
        if marca: marca()
    # --- residuo, na ordem de leitura do JM
    def bloco(tipo, up, lf, bit_seta):
        if enc:
            t_, coef = fila.pop(0)
            assert t_ == tipo, 'bloco %d no trace, esperado %d (MB %d)' % (t_, tipo, m)
        else: coef = None
        cf = _bloco(io, ctx, tipo, up, lf, coef)
        if any(cf): cur.sbits |= 1 << bit_seta
        out.extend(_pares(tipo, cf) if any(cf) else [(NOME_TIPO[tipo], (0, 0))])
        if marca: marca()
    def bit_viz(mbv, bit, padrao):
        return padrao if mbv is None else (mbv.sbits >> bit) & 1
    dflt = 1 if cur.intra else 0
    if cur.intra:                                     # DC de luma do I16x16
        bloco(LUMA_16DC, bit_viz(cima, 0, 1), bit_viz(esq, 0, 1), 0)
        if cur.cbp & 15: raise ForaDoAlcance('AC de luma em I16x16')
    else:
        for b8 in range(4):
            if not (cur.cbp >> b8) & 1: continue
            for j in range(2):
                for i in range(2):
                    x = 2 * (b8 & 1) + i; y = 2 * (b8 >> 1) + j
                    lf = ((cur.sbits >> (1 + 4 * y + x - 1)) & 1) if x > 0 else bit_viz(esq, 1 + 4 * y + 3, dflt)
                    up = ((cur.sbits >> (1 + 4 * (y - 1) + x)) & 1) if y > 0 else bit_viz(cima, 1 + 12 + x, dflt)
                    bloco(LUMA_4x4, up, lf, 1 + 4 * y + x)
    if cur.cbp > 15:
        for bit in (17, 18):
            bloco(CHROMA_DC, bit_viz(cima, bit, dflt), bit_viz(esq, bit, dflt), bit)
    if (cur.cbp >> 4) == 2:
        for base in (19, 35):
            for y in range(2):
                for x in range(2):
                    lf = ((cur.sbits >> (base + 4 * y + x - 1)) & 1) if x > 0 else bit_viz(esq, base + 4 * y + 1, dflt)
                    up = ((cur.sbits >> (base + 4 * (y - 1) + x)) & 1) if y > 0 else bit_viz(cima, base + 4 + x, dflt)
                    bloco(CHROMA_AC, up, lf, base + 4 * y + x)
    if enc: assert not fila, 'sobrou residuo no trace (MB %d)' % m
    eos = io.term(E(fim)); out.append(('end_of_slice_flag', (eos,)))
    return out, eos


def codifica(sintaxe, qp, modelo, n_mb=None, posicoes=None, reinicio=None, tipo='P'):
    ctx = contextos(qp, modelo, 'I' if tipo == 'I' else 'P'); e = Enc()
    n = n_mb if n_mb is not None else len(sintaxe)
    mbs = {}; st = {'dq': 0, 'nref': 3, 'tipo': tipo}
    for m in range(n):
        if reinicio is not None and m == reinicio[0]:
            e.low, e.rng, e.outst, e.primeiro = 0, reinicio[1], 0, False
        if posicoes is not None: posicoes.append(len(e.bits))
        se = sintaxe[m]
        # o trace do JM nao registra o end_of_slice_flag = 1 do ultimo MB do slice
        fim = 1 if m == len(sintaxe) - 1 else dict((k, v[0]) for k, v in se if v).get('end_of_slice_flag', 0)
        processa_mb(e, ctx, mbs, m, se, st, fim)
    return e.bits


def decodifica(bits, qp, modelo, n_mb=8160, nref=3, posicoes=None, tipo='P'):
    ctx = contextos(qp, modelo, 'I' if tipo == 'I' else 'P'); dcd = Dec(bits)
    mbs = {}; st = {'dq': 0, 'nref': nref, 'tipo': tipo}; out = {}
    for m in range(n_mb):
        if posicoes is not None: posicoes.append(dcd.p)
        try: se, eos = processa_mb(dcd, ctx, mbs, m, None, st, None)
        except ForaDoAlcance: break
        out[m] = se
        if eos: break
    return out


if __name__ == '__main__':
    # Validacao: recodifica quadros a partir do trace do JM e compara com o arquivo
    # (buffer ja remendado pelo patches.txt); depois decodifica o arquivo e
    # compara a sintaxe lida com a do trace. QP, modelo e byte de inicio vem do
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
        tipo = {0: 'P', 1: 'B', 2: 'I'}[q[poc]['tipo']]
        try: bits = codifica(q[poc]['mbs'], qp, mod, tipo=tipo)
        except ForaDoAlcance as x: print('quadro %d (POC %d, %s): fora do alcance: %s' % (t, poc, tipo, x)); continue
        n = min(len(bits), len(obs)); dif = [i for i in range(n) if bits[i] != obs[i]]
        lido = decodifica(obs, qp, mod, nref=cab[poc]['campos'].get('num_ref_idx_l0_active_minus1', 2) + 1, tipo=tipo)
        norm = lambda se: [(k, v) for k, v in se if k != 'end_of_slice_flag']
        iguais = sum(1 for mm in q[poc]['mbs'] if mm in lido and norm(lido[mm]) == norm(q[poc]['mbs'][mm]))
        print('quadro %d (POC %d, %s, QP %d, modelo %d): %d bits codificados, %d observados; %d diferencas%s; '
              'decodificado: %d MBs, %d com a sintaxe do trace' % (
            t, poc, tipo, qp, mod, len(bits), len(obs), len(dif), (', primeira em %d' % dif[0]) if dif else '',
            len(lido), iguais))
