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
    """Bits do slice data do frame 11 como estao no arquivo (remendado).

    Devolve: lista de 0/1 a partir do byte 21 do NAL (inclui o enchimento do fim)."""
    d, ix = buffer(); o, s = ix[11][1], ix[11][2]
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
    else:
        print(__doc__ if __doc__ else open(__file__, encoding='utf-8').read().split('import sys')[0]); sys.exit(1)
