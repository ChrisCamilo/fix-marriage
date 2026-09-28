# f11.py -- ferramentas do plano 5 (docs/PLANO_RECODIFICA_F11.md) para o frame 11.
#
# Tudo roda da raiz do projeto e le o MP4 so para leitura, com o patches.txt
# aplicado (buffer remendado, armadilha 49). A sintaxe de referencia dos
# quadros P do fade vem de data/f11/sintaxe_pocNN.json.gz, extraida do trace do
# JM 19.1 (jm_trace.py) -- o JM mora fora do repositorio; as hipoteses em
# andamento sobre o 11 ficam em data/f11/hipoteses.json (provisorias: nada ali
# esta provado nem vai para o patches.txt).
#
# Subcomandos (python tools/anchor/f11.py <sub> ...):
#   encaixe K:K2 [N]   encaixa o molde do 9 a partir do MB K no frame 11 com o
#                      range de entrada livre (no regime do padrao o estado
#                      converge); mostra onde o trecho comeca no 11 e a
#                      discordancia nos N bits (padrao 1500)
#   perfil A B PASSO N janelas curtas de N bits a cada PASSO MBs entre A e B:
#                      deslocamento e discordancia -- acha onde o molde vale
#   le K T1,T2,.. ATE  decodifica o 11 a partir do estado exato no MB K (prefixo
#                      recodificado pelas hipoteses + arquivo), com as trocas
#                      dadas (bits absolutos do slice data), ate o MB ATE
#   feixe K KF LARG JAN MAXF [T1,..]
#                      decodificacao em feixe da regiao [K, KF): cada MB lido
#                      com 0..MAXF trocas novas numa janela de JAN bits,
#                      filtro de fisica da borda, fecho pelo molde depois.
#                      Ponto de retomada a cada MB; PROCS e JAN0 no ambiente.
#   sintaxe K KF KT LARG
#                      feixe sobre VALORES de sintaxe na regiao [K, KF), elemento
#                      a elemento, pontuado pelos bits emitidos (metrica de
#                      Fano, EPS = 4%) mais um custo de complexidade; o molde de
#                      KF a KT e o juiz final
#
# F11_VALIDA=9 no ambiente troca o 11 pelo 9 (sem dano, sintaxe conhecida):
# para validar as buscas num caso de resposta sabida.
#
# Constantes do 11 (medidas no passo 3): QP 10, cabac_init_idc 0, slice data a
# partir do byte 21 do NAL, 3 referencias ativas.
import sys, os, gzip, json, copy, pickle, itertools
from concurrent.futures import ProcessPoolExecutor
AQUI = os.path.dirname(os.path.abspath(__file__)); RAIZ = os.path.dirname(os.path.dirname(AQUI))
sys.path.insert(0, AQUI)
import cabac_p as C

QP, MODELO, BYTE_INI, NREF = 10, 0, 21, 3
MP4 = os.path.join(RAIZ, 'Caio & Lizandra - Making- Caio-Balu.mp4')


def buffer():
    """O MP4 com o patches.txt aplicado e o indice.

    Devolve: (bytearray do arquivo remendado, lista de (i, offset, tamanho, idr))."""
    ix = [tuple(map(int, l.split()[:4])) for l in open(os.path.join(RAIZ, 'index.txt')) if l.strip()]
    d = bytearray(open(MP4, 'rb').read())
    for l in open(os.path.join(RAIZ, 'patches.txt')):
        p = l.split()
        if len(p) >= 2 and p[0].isdigit(): d[int(p[0])] ^= 1 << int(p[1])
    return d, ix


def rbsp(nal):
    """RBSP (sem os 03 de escape) de um NAL sem prefixo, como string de bits.

    Devolve: (bits, idx) com idx[k] = byte do NAL de onde veio o byte k do RBSP."""
    out = bytearray(); idx = []; z = 0
    for i, b in enumerate(nal):
        if z >= 2 and b == 3: z = 0; continue
        out.append(b); idx.append(i); z = z + 1 if b == 0 else 0
    return ''.join(format(b, '08b') for b in out), idx


def obs11():
    """Bits do slice data do frame 11 como estao no arquivo (remendado). Com
    F11_VALIDA=9 no ambiente, os do frame 9 (sem dano, resposta conhecida).

    Devolve: lista de 0/1 a partir do byte 21 do NAL (inclui o enchimento do fim)."""
    d, ix = buffer(); t = 9 if os.environ.get('F11_VALIDA') == '9' else 11; o, s = ix[t][1], ix[t][2]
    bits, _ = rbsp(bytes(d[o + 4:o + s]))
    return [int(c) for c in bits[8 * BYTE_INI:]]


def sintaxe(poc):
    """Sintaxe de um quadro P do fade lida do trace do JM (data/f11).

      poc  4, 8, 12, 16 ou 20 (quadros 1, 3, 5, 7 e 9)

    Devolve: {mb: [(elemento, (valores...)), ...]}."""
    with gzip.open(os.path.join(RAIZ, 'data', 'f11', 'sintaxe_poc%02d.json.gz' % poc), 'rt', encoding='utf-8') as f:
        d = json.load(f)
    return {int(m): [(k, tuple(v)) for k, v in se] for m, se in d.items()}


def hipoteses():
    """Molde do 9 com as hipoteses do 11 por cima (data/f11/hipoteses.json).

    Devolve: {mb: sintaxe} para os 8.160 MBs."""
    S = sintaxe(20)
    if os.environ.get('F11_VALIDA') == '9': return S
    h = json.load(open(os.path.join(RAIZ, 'data', 'f11', 'hipoteses.json')))
    for m, se in h['mb'].items(): S[int(m)] = [(k, tuple(v)) for k, v in se]
    return S


# ---------------------------------------------------------------- encaixe / perfil
def _encaixa(args):
    import numpy as np
    S, obs, k, k2, R, N, p0, p1 = args
    pos = []; bits = C.codifica(S, QP, MODELO, n_mb=k2, posicoes=pos, reinicio=(k, R))
    seg = np.array(bits[pos[k]:pos[k] + N], dtype=np.int8); L = len(seg); ob = np.array(obs, dtype=np.int8)
    best = (10 ** 9, None)
    for p in range(max(0, p0), min(p1, len(ob) - L)):
        dd = int(np.count_nonzero(seg[12:] != ob[p + 12:p + L]))
        if dd < best[0]: best = (dd, p)
    return best[0], best[1], R, L - 12


def encaixe(k, k2, N=1500, ranges=range(256, 511, 8), procs=8):
    """Encaixa o trecho [k, k2) do molde do 9 no frame 11 com o range livre.

      k, k2   MBs do trecho
      N       bits do trecho usados na comparacao
      ranges  ranges de entrada testados

    Devolve: lista ordenada de (discordancias, bit de inicio no 11, range, bits comparados)."""
    S = sintaxe(20); obs = obs11()
    pos9 = []; C.codifica(S, QP, MODELO, n_mb=k + 1, posicoes=pos9)
    args = [(S, obs, k, k2, R, N, pos9[k] - 400, pos9[k] + 1600) for R in ranges]
    with ProcessPoolExecutor(procs) as ex: return sorted(ex.map(_encaixa, args)), pos9[k]


# ---------------------------------------------------------------- leitura com trocas
class DecF(C.Dec):
    """Dec que le os bits com um conjunto de trocas aplicado."""
    def __init__(self, bits, trocas):
        self.trocas = trocas; C.Dec.__init__(self, bits)
    def _rd(self):
        x = (self.b[self.p] if self.p < len(self.b) else 0) ^ (1 if self.p in self.trocas else 0)
        self.p += 1; return x


def fluxo_corrigido(K, S=None, obs=None):
    """Prefixo recodificado (limpo) ate o MB K + o arquivo dali em diante.

    Devolve: (bits, bit onde o MB K comeca)."""
    S = S or hipoteses(); obs = obs or obs11()
    pre = C.codifica(S, QP, MODELO, n_mb=K)
    return pre + obs[len(pre):], len(pre)


def le(K, trocas, ate):
    """Decodifica o 11 do MB K ao ATE-1 a partir do estado exato, com trocas.

    Devolve: lista de (mb, bit inicial, bit final, sintaxe) ou para no primeiro
    elemento fora do alcance."""
    bits, _ = fluxo_corrigido(K)
    ctx = C.contextos(QP, MODELO); d = DecF(bits, frozenset(trocas)); mbs = {}; st = {'dq': 0, 'nref': NREF}
    for m in range(K): C.processa_mb(d, ctx, mbs, m, None, st, None)
    out = []
    for m in range(K, ate):
        p0 = d.p
        try: se, eos = C.processa_mb(d, ctx, mbs, m, None, st, None)
        except C.ForaDoAlcance as e: out.append((m, p0, d.p, 'fora: %s' % e)); break
        out.append((m, p0, d.p, se))
    return out


# ---------------------------------------------------------------- feixe sobre trocas
class Viz:
    """Vizinhos: base compartilhada (MBs antes da regiao) + os MBs do caminho."""
    __slots__ = ('base', 'over')
    def __init__(self, base, over): self.base = base; self.over = over
    def get(self, k, d=None):
        v = self.over.get(k)
        return v if v is not None else self.base.get(k, d)
    def __setitem__(self, k, v): self.over[k] = v


def plausivel_borda(se):
    """Filtro de fisica para a fileira de borda tarja/campo (2 linhas de tarja no
    alto do MB): intra so horizontal e sem residuo; inter so 16x16, ref <= 2,
    mvd 0, luma so nos 4 blocos de cima; niveis |x| <= 3."""
    d = {}; res = []
    for k, v in se:
        if k in C.NOME_BLOCO: res.append((k, v))
        else: d.setdefault(k, v[0] if v else None)
    if d.get('mb_skip_flag') == 0: return False
    if any(abs(v[0]) > 3 for k, v in res): return False
    t = d.get('mb_type')
    if t is not None and t >= 7:
        if any(v != (0, 0) for k, v in res) or (t - 7) // 4 or t != 8: return False
    if t == 1:
        if d.get('ref_idx_l0', 0) > 2 or d.get('mvd0_l0') or d.get('mvd1_l0'): return False
        if d.get('coded_block_pattern', 0) & 12: return False
        nb = 0
        for k, v in res:
            if k == 'Luma sng':
                if v == (0, 0): nb += 1
                elif nb % 4 >= 2: return False
    return True


_G = {}
def _globais(K, fixas):
    if not _G:
        S = hipoteses(); obs = obs11(); bits, _ = fluxo_corrigido(K, S, obs)
        ctx = C.contextos(QP, MODELO); d = DecF(bits, fixas); mbs = {}; st = {'dq': 0, 'nref': NREF}
        for m in range(K): C.processa_mb(d, ctx, mbs, m, None, st, None)
        _G.update(S=S, OBS=obs, BITS=bits, BASE=mbs, INI=(d.p, d.rng, d.off, ctx, {}, st))
    return _G


def _le_mb(cfg, estado, trocas, m):
    g = _globais(cfg['K'], cfg['fixas'])
    p, rng, off, ctx, over, st = estado
    d = DecF.__new__(DecF); d.b = g['BITS']; d.trocas = trocas; d.p = p; d.rng = rng; d.off = off
    cx = copy.deepcopy(ctx); ov = dict(over); st2 = dict(st)
    try: se, eos = C.processa_mb(d, cx, Viz(g['BASE'], ov), m, None, st2, None)
    except C.ForaDoAlcance: return None
    if eos or not plausivel_borda(se): return None
    return se, (d.p, d.rng, d.off, cx, ov, st2)


def _expande(args):
    cfg, i, (trocas, estado, sint) = args
    m = cfg['K'] + len(sint); p = estado[0]; out = []
    jn = cfg['jan0'] if m == cfg['K'] else cfg['jan']
    jan = [x for x in range(p, p + jn) if x not in trocas]
    for nf in range(cfg['maxf'] + 1):
        for novas in itertools.combinations(jan, nf):
            r = _le_mb(cfg, estado, trocas | frozenset(novas), m)
            if r is None: continue
            se, e2 = r
            out.append((len(trocas) + nf, e2[0], e2[1], e2[2], i, novas, tuple(tuple(x) for x in se)))
    return out


def _materializa(args):
    cfg, (trocas, estado, sint), novas = args
    tr = trocas | frozenset(novas)
    se, e2 = _le_mb(cfg, estado, tr, cfg['K'] + len(sint))
    return (tr, e2, sint + [se])


def _fecho(args):
    cfg, (trocas, _, sint) = args
    g = _globais(cfg['K'], cfg['fixas']); obs = g['OBS']; K, KF = cfg['K'], cfg['KF']
    S2 = dict(g['S'])
    for i, se in enumerate(sint): S2[K + i] = se
    pos2 = []; bb = C.codifica(S2, QP, MODELO, n_mb=KF + 60, posicoes=pos2)
    a, b = pos2[K], min(pos2[KF + 59], len(obs))
    reg = sum(1 for i in range(a, pos2[KF]) if bb[i] != obs[i]); dep = sum(1 for i in range(pos2[KF], b) if bb[i] != obs[i])
    return reg, pos2[KF] - a, dep, b - pos2[KF], sorted(trocas - cfg['fixas']), sint


def feixe(K, KF, largura, jan, maxf, fixas=(), jan0=None, procs=8, ponto=None):
    """Decodificacao em feixe da regiao [K, KF) do 11 (ver o cabecalho do modulo).

    So os `largura` sobreviventes carregam estado completo; os filhos voltam dos
    processos como resumo leve e o estado dos escolhidos e refeito depois da
    poda. O juiz final e o molde DEPOIS da regiao: a sintaxe lida do arquivo,
    recodificada, reproduz o arquivo e nao pode julgar a si mesma.

      K, KF    regiao de MBs
      largura  caminhos mantidos
      jan      bits a frente onde uma troca nova pode cair (por MB)
      maxf     trocas novas por MB
      fixas    trocas ja assumidas
      jan0     janela do primeiro MB (o grande), se diferente
      ponto    arquivo do ponto de retomada (salvo a cada MB)

    Devolve: lista de (discordancia na regiao, bits, discordancia depois, bits,
    trocas novas, sintaxe) ordenada pelo trecho de depois."""
    cfg = dict(K=K, KF=KF, jan=jan, jan0=jan0 or jan, maxf=maxf, fixas=frozenset(fixas))
    if ponto and os.path.exists(ponto):
        m0, beam = pickle.load(open(ponto, 'rb')); print('retomando no MB %d' % m0, flush=True)
    else:
        m0, beam = K, [(cfg['fixas'], _globais(K, cfg['fixas'])['INI'], [])]
    with ProcessPoolExecutor(procs) as ex:
        for m in range(m0, KF):
            leves = [f for fs in ex.map(_expande, [(cfg, i, b) for i, b in enumerate(beam)]) for f in fs]
            n_filhos = len(leves)
            leves.sort(key=lambda f: (f[0], f[1]))          # menos trocas; depois, sintaxe mais barata
            vistos = set(); escolhidos = []
            for f in leves:
                chave = (f[1], f[2], f[3], f[6])
                if chave in vistos: continue
                vistos.add(chave); escolhidos.append((cfg, beam[f[4]], f[5]))
                if len(escolhidos) >= largura: break
            del leves
            beam = list(ex.map(_materializa, escolhidos))
            print('MB %d: %d filhos, feixe %d, menos trocas %d' % (m, n_filhos, len(beam), len(beam[0][0]) if beam else -1), flush=True)
            if not beam: return []
            if ponto:
                pickle.dump((m + 1, beam), open(ponto + '.tmp', 'wb')); os.replace(ponto + '.tmp', ponto)
        return sorted(ex.map(_fecho, [(cfg, b) for b in beam[:60]]), key=lambda r: r[2] / max(r[3], 1))


# ---------------------------------------------------------------- feixe sobre valores de sintaxe
# Em vez de enumerar trocas de bit, enumera a SINTAXE elemento a elemento e
# pontua pelos bits ja emitidos: os bits trocados viram so custo. Metrica de
# Fano para um canal com EPS de bits trocados: bit que bate soma
# log2((1-EPS)/0,5), bit que diverge soma log2(EPS/0,5). Sintaxe errada diverge
# ~50% e afunda em poucos bits; caminhos de comprimentos diferentes ficam
# comparaveis. Bits ja emitidos nao mudam com os simbolos seguintes, entao um MB
# parcial (blocos ainda nao escolhidos = zero) pode ser pontuado ate a marca do
# ultimo elemento escolhido.
import math
EPS = 0.04
GANHO, PERDA = math.log2((1 - EPS) / 0.5), math.log2(EPS / 0.5)
# Custo de complexidade (log2 do prior, em bits): sem ele a gramatica rica
# "explica" qualquer sequencia de bits -- vira um decodificador, e a sintaxe
# lida do arquivo reproduz o arquivo (armadilha da autoconsistencia). A borda e
# quase toda intra horizontal; inter e coeficiente nao nulo custam.
CUSTO_INTER = float(os.environ.get('F11_CUSTO_INTER', '8'))
CUSTO_COEF = float(os.environ.get('F11_CUSTO_COEF', '4'))
# numa fileira uniforme o encoder repete a escolha: trocar o modo de croma entre
# vizinhos intra custa (sem isso a liberdade de croma absorve bits trocados)
CUSTO_CROMA = float(os.environ.get('F11_CUSTO_CROMA', '6'))


def _custo(h, esc, ant):
    """log2 do prior (negativo) de um MB de borda com as escolhas feitas.

      h, esc  cabecalho e escolhas do MB
      ant     sintaxe do MB anterior (para a troca de modo de croma)"""
    if h is None or h[0] == 'F': return 0.0
    if h[0] == 'I':
        d = dict(ant) if ant else {}
        if d.get('mb_type', (0,))[0] == 8 and d.get('intra_chroma_pred_mode', (h[1],))[0] != h[1]: return -CUSTO_CROMA
        return 0.0
    return -CUSTO_INTER - CUSTO_COEF * sum(1 for v in esc if v for x in v if x)


def _vetores(n_pos, max_nz, valores, n_total):
    """Vetores de coeficientes (em ordem de varredura) com ate max_nz nao nulos
    nas n_pos primeiras posicoes, valores do conjunto dado.

    Devolve: lista de tuplas de comprimento n_total (a primeira e toda zero)."""
    out = [tuple([0] * n_total)]
    for k in range(1, max_nz + 1):
        for ps in itertools.combinations(range(n_pos), k):
            for vs in itertools.product(valores, repeat=k):
                v = [0] * n_total
                for p_, x in zip(ps, vs): v[p_] = x
                out.append(tuple(v))
    return out


GRAMATICA = {
    'zero': [None],
    'luma_topo': _vetores(6, 2, (1, -1, 2, -2), 16),
    'dc': _vetores(4, 2, (1, -1, 2, -2, 3, -3), 4),
    'ac': _vetores(10, 2, (1, -1, 2, -2), 15),
}


# Restricoes medidas (opcionais, pelo ambiente):
#   F11_INTER_ATE  ultimo MB onde inter e permitido (nos irmaos, so as colunas 0-1)
#   F11_FIM_BIT    bit do slice data onde o MB KF comeca (pelo encaixe do molde):
#                  caminho que passa dele e podado, e o fecho tem que cair nele
INTER_ATE = int(os.environ['F11_INTER_ATE']) if 'F11_INTER_ATE' in os.environ else None
FIM_BIT = int(os.environ['F11_FIM_BIT']) if 'F11_FIM_BIT' in os.environ else None


def cabecalhos_borda(m=None):
    """MBs possiveis numa fileira de borda tarja/campo (a borda nas 2 primeiras
    linhas do MB): intra horizontal sem residuo (croma 0-3), ou inter 16x16 com
    ref 0-2, mvd 0 e CBP de luma so na metade de cima.

    Devolve: lista de ('I', croma) e ('P', ref, cbp)."""
    hs = [('I', cm) for cm in range(4)]
    if INTER_ATE is not None and m is not None and m > INTER_ATE: return hs
    hs += [('P', r, cl + cc) for r in range(3) for cl in (0, 1, 2, 3) for cc in (0, 16, 32)]
    return hs


def blocos_de(h):
    """Blocos de residuo do MB na ordem de codificacao do JM.

      h  cabecalho ('I', croma) ou ('P', ref, cbp)

    Devolve: lista de (restricao, tipo de bloco); restricao e a chave da GRAMATICA."""
    if h[0] == 'F': return []
    if h[0] == 'I': return [('zero', C.LUMA_16DC)]
    cbp = h[2]; out = []
    for b8 in range(4):
        if not (cbp >> b8) & 1: continue
        for j in range(2):
            for i in range(2): out.append(('luma_topo' if (b8 < 2 and j == 0) else 'zero', C.LUMA_4x4))
    if cbp > 15: out += [('dc', C.CHROMA_DC)] * 2
    if cbp >> 4 == 2:
        for comp in range(2): out += [('ac', C.CHROMA_AC)] * 2 + [('zero', C.CHROMA_AC)] * 2
    return out


def monta_mb(h, escolhas):
    """Sintaxe no formato do trace de um MB de borda.

      h         cabecalho ('I', croma) ou ('P', ref, cbp)
      escolhas  vetores de coeficientes dos primeiros blocos (os demais = zero)

    Devolve: a lista de elementos. ('F', m) e o MB m do molde, fixo."""
    if h[0] == 'F': return _GS['S'][h[1]]
    if h[0] == 'I':
        return [('mb_skip_flag', (1,)), ('mb_type', (8,)), ('intra_chroma_pred_mode', (h[1],)), ('mb_qp_delta', (0,)),
                ('DC luma 16x16', (0, 0)), ('end_of_slice_flag', (0,))]
    se = [('mb_skip_flag', (1,)), ('mb_type', (1,)), ('ref_idx_l0', (h[1],)), ('mvd0_l0', (0,)), ('mvd1_l0', (0,)),
          ('coded_block_pattern', (h[2],))]
    if h[2]: se.append(('mb_qp_delta', (0,)))
    for k, (restr, tipo) in enumerate(blocos_de(h)):
        v = escolhas[k] if k < len(escolhas) else None
        se.extend(C._pares(tipo, list(v)) if v and any(v) else [(C.NOME_TIPO[tipo], (0, 0))])
    return se + [('end_of_slice_flag', (0,))]


_GS = {}
def _gs(K):
    """Estado do codificador no MB K (prefixo das hipoteses), uma vez por processo."""
    if not _GS:
        S = hipoteses(); obs = obs11()
        ctx = C.contextos(QP, MODELO); e = C.Enc(); mbs = {}; st = {'dq': 0, 'nref': NREF}
        for m in range(K): C.processa_mb(e, ctx, mbs, m, S[m], st, 0)
        _GS.update(S=S, OBS=obs, BASE=mbs, SNAP=((e.low, e.rng, e.outst, e.primeiro, tuple(e.bits)), ctx, {}, dict(st)))
    return _GS


def _codifica_mb(K, snap, m, se):
    """Codifica um MB a partir de um instantaneo do codificador.

    Devolve: (Enc, marcas -- bits emitidos ao fim de cada elemento --, ctx, vizinhos, st)."""
    g = _gs(K)
    (low, rng, outst, pr, bits), ctx, over, st = snap
    e = C.Enc(); e.low, e.rng, e.outst, e.primeiro, e.bits = low, rng, outst, pr, list(bits)
    cx = copy.deepcopy(ctx); ov = dict(over); st2 = dict(st); marcas = []
    C.processa_mb(e, cx, Viz(g['BASE'], ov), m, se, st2, 0, marca=lambda: marcas.append(len(e.bits)))
    return e, marcas, cx, ov, st2


def _metrica(bits, obs, a, b):
    b = min(b, len(obs)); x = sum(1 for i in range(a, b) if bits[i] != obs[i])
    return (b - a - x) * GANHO + x * PERDA


def _avalia(args):
    """Filhos de um item do feixe para um lote de candidatos.

    Devolve: lista de (pontos, indice do pai, cabecalho, escolhas, MB completo)."""
    K, i, item, cands = args
    g = _gs(K); obs = g['OBS']
    pts0, snap, sint, h, esc = item
    m = K + len(sint); ini = len(snap[0][4]); out = []
    ant = sint[-1] if sint else g['S'][K - 1]
    for c in cands:
        h2, esc2 = (c, []) if h is None else (h, esc + [c])
        try: e, marcas, _, _, _ = _codifica_mb(K, snap, m, monta_mb(h2, esc2))
        except (C.ForaDoAlcance, AssertionError): continue
        bl = blocos_de(h2)
        completo = all(r == 'zero' for r, t in bl[len(esc2):])
        fim = len(e.bits) if completo else marcas[len(esc2)]
        out.append((pts0 + _metrica(e.bits, obs, ini, fim) + _custo(h2, esc2, ant), i, h2, esc2, completo))
    return out


def _fecha_mb(args):
    """Item com estado completo depois de um MB terminado."""
    K, item, h, esc = args
    g = _gs(K); obs = g['OBS']
    pts0, snap, sint, _, _ = item
    m = K + len(sint); ini = len(snap[0][4])
    se = monta_mb(h, esc)
    e, _, cx, ov, st = _codifica_mb(K, snap, m, se)
    ant = sint[-1] if sint else g['S'][K - 1]
    return (pts0 + _metrica(e.bits, obs, ini, len(e.bits)) + _custo(h, esc, ant), ((e.low, e.rng, e.outst, e.primeiro, tuple(e.bits)), cx, ov, st),
            sint + [se], None, [])


def feixe_sintaxe(K, KF, KT, largura=64, procs=8):
    """Feixe sobre valores de sintaxe na regiao de borda [K, KF), com o molde do 9
    de KF a KT como juiz final.

      K, KF    regiao (MBs de borda, gramatica de cabecalhos_borda e GRAMATICA)
      KT       fim do trecho de molde usado no fecho
      largura  itens mantidos por passo
      procs    processos

    Devolve: lista de (pontos, sintaxe da regiao, discordancias no molde depois,
    bits comparados, discordancias na regiao, bits da regiao), pelo molde depois."""
    # o molde de KF a KT entra no proprio feixe como MBs fixos: o comprimento
    # errado da regiao desalinha o molde e o caminho afunda antes de ser escolhido
    g = _gs(K); n = KF - K; nt = KT - K
    beam = [(0.0, g['SNAP'], [], None, [])]
    with ProcessPoolExecutor(procs) as ex:
        while any(len(it[2]) < nt for it in beam):
            tarefas = []
            for i, it in enumerate(beam):
                if len(it[2]) >= nt: continue
                if len(it[2]) >= n: cands = [('F', K + len(it[2]))]
                else: cands = cabecalhos_borda(K + len(it[2])) if it[3] is None else GRAMATICA[blocos_de(it[3])[len(it[4])][0]]
                for j in range(0, len(cands), 96): tarefas.append((K, i, it, cands[j:j + 96]))
            filhos = [f for fs in ex.map(_avalia, tarefas) for f in fs]
            prontos = [(it[0], it) for it in beam if len(it[2]) >= nt]
            filhos.sort(key=lambda f: -f[0])
            vistos = set(); esc = []
            for f in filhos:
                chave = (f[1], f[2], tuple(f[3]))
                if chave in vistos: continue
                vistos.add(chave); esc.append(f)
                if len(esc) >= largura: break
            novos = list(ex.map(_fecha_mb, [(K, beam[f[1]], f[2], f[3]) for f in esc if f[4]]))
            if FIM_BIT is not None:
                # a regiao inteira cabe ate FIM_BIT; ao fechar o ultimo MB dela, tem que cair ali (+-2)
                def cabe(it):
                    nb = len(it[1][0][4]); k = len(it[2])
                    if k < n: return nb <= FIM_BIT + 2
                    if k == n: return abs(nb - FIM_BIT) <= 2
                    return True
                novos = [it for it in novos if cabe(it)]
            for f in esc:
                if not f[4]:
                    pts0, snap, sint, _, _ = beam[f[1]]
                    novos.append((f[0], snap, sint, f[2], f[3]))
            beam = sorted(novos + [it for _, it in prontos], key=lambda it: -it[0])[:largura]
            it0 = beam[0]
            print('passo: %d filhos; melhor %.1f pontos, %d MBs prontos, cabecalho %s, %d blocos escolhidos' % (
                len(filhos), it0[0], len(it0[2]), it0[3], len(it0[4])), flush=True)
    res = []
    for it in beam:
        S2 = dict(g['S'])
        for j, se in enumerate(it[2][:n]): S2[K + j] = se
        pos = []; bb = C.codifica(S2, QP, MODELO, n_mb=KT, posicoes=pos)
        a, b = pos[KF], min(pos[KT - 1], len(g['OBS']))
        dep = sum(1 for x in range(a, b) if bb[x] != g['OBS'][x]); reg = sum(1 for x in range(pos[K], a) if bb[x] != g['OBS'][x])
        res.append((it[0], it[2][:n], dep, b - a, reg, a - pos[K]))
    return sorted(res, key=lambda r: r[2] / max(r[3], 1))


# ---------------------------------------------------------------- fileira 8: busca por estagios com a fisica
def _pontua_mb960(args):
    """Pontos (Fano) dos bits do MB 960 ate a marca dada, para varias escolhas."""
    K, escolhas_lista, ate_bloco = args
    g = _gs(K); obs = g['OBS']; snap = g['SNAP']; ini = len(snap[0][4]); out = []
    for esc in escolhas_lista:
        e, marcas, _, _, _ = _codifica_mb(K, snap, K, monta_mb(('P', 0, 35), esc))
        fim = marcas[ate_bloco + 1] if ate_bloco + 1 < len(marcas) else len(e.bits)
        out.append((_metrica(e.bits, obs, ini, fim), esc))
    return out


def _fecho_fileira(args):
    """MB 960 dado + MB 961 + fileira horizontal de croma c + campo: pontos e alinhamento."""
    K, esc960, se961, c, KT = args
    g = _gs(K); obs = g['OBS']
    S2 = dict(g['S']); S2[K] = monta_mb(('P', 0, 35), esc960); S2[K + 1] = se961
    h = monta_mb(('I', c), [])
    for m in range(K + 2, 1080): S2[m] = h
    pos = []; bb = C.codifica(S2, QP, MODELO, n_mb=KT, posicoes=pos)
    ini, fim = pos[K], min(pos[KT - 1], len(obs))
    return (_metrica(bb, obs, ini, fim), pos[1080], sum(1 for i in range(ini, pos[1080]) if bb[i] != obs[i]),
            sum(1 for i in range(pos[1080], fim) if bb[i] != obs[i]), fim - pos[1080], esc960, se961, c)


def busca_fileira8(larg=48, procs=8, KT=1320):
    """Tentativa 1 do passo 3 para a borda da fileira 8 do frame 11.

    A borda tarja/campo e uniforme na horizontal: o MB 960 e inter, ref 0,
    CBP 35, com a luma da fisica (4 blocos de cima com coeficientes nas
    posicoes 0 e 2, valor 1 -- confirmada no passo 3); o DC de croma de cada
    componente so tem o termo medio e o vertical (a, 0, b, 0); o AC de croma
    tem os 2 blocos de cima iguais e os de baixo zero. O resto da fileira e
    horizontal com um unico modo de croma; o MB 961 e horizontal ou inter
    (familia do 9). Estagios: DC (U e V juntos), AC de U, AC de V -- cada um
    pontuado pelos bits emitidos ate ali (Fano), mantendo `larg`. No fim, cada
    MB 960 candidato e codificado com a fileira inteira e o campo ate KT; o
    campo tem que comecar no bit FIM_BIT (medido: 3.122).

      larg   candidatos mantidos por estagio
      KT     fim do trecho de campo usado no juiz

    Devolve: lista de (pontos, bit do MB 1080, fora na fileira, fora no campo,
    bits de campo, MB 960, MB 961, croma), melhor primeiro."""
    K = 960; g = _gs(K)
    L = tuple([1, 0, 1] + [0] * 13)
    base = [L, L, None, None, L, L, None, None]
    dcs = [tuple([a, 0, b, 0]) for a in range(-4, 5) for b in range(-4, 5)]
    acs = GRAMATICA['ac']
    def esc(dcu, dcv, acu=None, acv=None):
        return base + [dcu, dcv, acu, acu, None, None, acv, acv, None, None]
    with ProcessPoolExecutor(procs) as ex:
        def estagio(lista, ate):
            lotes = [lista[i:i + 64] for i in range(0, len(lista), 64)]
            r = [x for rs in ex.map(_pontua_mb960, [(K, l_, ate) for l_ in lotes]) for x in rs]
            r.sort(key=lambda x: -x[0]); return r[:larg]
        s1 = estagio([esc(u, v) for u in dcs for v in dcs], 9)
        print('DC: melhores', [(round(p, 1), e[8], e[9]) for p, e in s1[:3]], flush=True)
        s2 = estagio([esc(e[8], e[9], a) for _, e in s1 for a in acs], 11)
        print('AC U: melhores', [(round(p, 1), e[10]) for p, e in s2[:3]], flush=True)
        s3 = estagio([esc(e[8], e[9], e[10], a) for _, e in s2 for a in acs], 17)
        print('AC V: melhores', [(round(p, 1), e[14]) for p, e in s3[:3]], flush=True)
        q9 = sintaxe(20)
        op961 = [monta_mb(('I', c), []) for c in range(4)] + [q9[961], q9[960]]
        tarefas = [(K, e, s961, c, KT) for _, e in s3 for s961 in op961 for c in range(4)]
        res = list(ex.map(_fecho_fileira, tarefas, chunksize=4))
    fim = FIM_BIT if FIM_BIT is not None else 3122
    res.sort(key=lambda r: (abs(r[1] - fim) > 2, -r[0]))
    return res


def _txt(se):
    cab = [x for x in se if x[0] not in ('mb_skip_flag', 'end_of_slice_flag') and x[0] not in C.NOME_BLOCO]
    res = ' '.join(('|' if v == (0, 0) else '%s%s' % (k[0], v)) for k, v in se if k in C.NOME_BLOCO)
    return '%s %s' % (cab, res)


if __name__ == '__main__':
    sub = sys.argv[1]; a = sys.argv[2:]
    if sub == 'encaixe':
        k, k2 = map(int, a[0].split(':')); N = int(a[1]) if len(a) > 1 else 1500
        res, p9 = encaixe(k, k2, N)
        print('MBs %d-%d (no 9 comeca no bit %d):' % (k, k2 - 1, p9))
        for d_, p, R, L in res[:5]: print('  %d de %d fora (%.1f%%), inicio no bit %d, range %d' % (d_, L, 100 * d_ / L, p, R))
    elif sub == 'perfil':
        A, B, passo, N = map(int, a[:4])
        for k in range(A, B, passo):
            res, p9 = encaixe(k, k + 60, N, ranges=(300, 400, 500), procs=3)
            d_, p, R, L = res[0]
            print('MB %4d: no 9 em %5d, no 11 em %5d (desloc %+4d)  %2d de %d fora' % (k, p9, p, p - p9, d_, L))
    elif sub == 'le':
        K = int(a[0]); tr = [int(x) for x in a[1].split(',') if x]; ate = int(a[2])
        for m, p0, p1, se in le(K, tr, ate):
            print('%d [%d-%d] %s' % (m, p0, p1, se if isinstance(se, str) else _txt(se)[:220]))
    elif sub == 'feixe':
        K, KF, larg, jan, maxf = map(int, a[:5]); fix = [int(x) for x in a[5].split(',')] if len(a) > 5 and a[5] else []
        jan0 = int(os.environ['JAN0']) if 'JAN0' in os.environ else None
        ponto = os.path.join(os.environ.get('TMPDIR', RAIZ + '/logs'), 'f11_feixe_%d_%d_%d_%d_%d_%s.pkl' % (K, KF, larg, jan, maxf, jan0))
        res = feixe(K, KF, larg, jan, maxf, fix, jan0, int(os.environ.get('PROCS', '8')), ponto)
        for reg, nr, dep, nd, tr, sint in res[:5]:
            print('regiao %d de %d fora, depois %d de %d fora; trocas novas %s' % (reg, nr, dep, nd, tr))
        if res:
            for i, se in enumerate(res[0][5]): print(K + i, _txt(se)[:220])
    elif sub == 'fileira8':
        res = busca_fileira8(int(a[0]) if a else 48, int(os.environ.get('PROCS', '8')))
        for pts, p1080, fr, fc, nc, e960, s961, c in res[:8]:
            print('%.1f pontos; campo no bit %d; fileira %d fora; campo %d de %d fora (%.1f%%); croma %d; 961 %s' % (
                pts, p1080, fr, fc, nc, 100 * fc / max(nc, 1), c, dict(s961).get('mb_type')))
        if res:
            print('MB 960:', _txt(monta_mb(('P', 0, 35), res[0][5]))[:300])
            print('MB 961:', _txt(res[0][6])[:300])
    elif sub == 'sintaxe':
        K, KF, KT, larg = map(int, a[:4])
        res = feixe_sintaxe(K, KF, KT, larg, int(os.environ.get('PROCS', '8')))
        for pts, sint, dep, nd, reg, nr in res[:5]:
            print('%.1f pontos; regiao %d de %d fora; molde depois %d de %d fora (%.1f%%)' % (pts, reg, nr, dep, nd, 100 * dep / max(nd, 1)))
        if res:
            for i, se in enumerate(res[0][1]): print(K + i, _txt(se)[:220])
    else:
        print(__doc__ if __doc__ else open(__file__, encoding='utf-8').read().split('import sys')[0]); sys.exit(1)
