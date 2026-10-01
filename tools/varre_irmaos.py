# varre_irmaos.py -- o que os irmaos de GOP provam, no filme inteiro.
#
# Generaliza o que fechou os GOPs 29 e 58 (2026-09-30):
#   1. cabecalho: P e B de um GOP tem cabecalhos iguais fora frame_num, POC e
#      os parametros do grupo (QP, n0/n1). Um cabecalho invalido ou incoerente
#      (frame_num fora da cadencia, POC repetido, nal_ref_idc 3 num P, QP fora
#      do grupo) e montado de novo pelos campos esperados e comparado bit a bit;
#   2. gemeos: os bytes do comeco do slice (a tarja de cima, os escapes antes da
#      cena) que todos os irmaos SAOS do mesmo grupo tem iguais e o quadro nao;
#   3. censo nivel C: P/B com 1-3 bits fora na tarja pulada que PARAM dentro
#      dela (MB < 960) -- um MB de verdade nao faria o quadro parar ali.
#
# Cada candidato sai cruzado com o que ja foi mexido: o patches.txt (troca
# igual a uma linha existente DESFARIA o patch -- o caso do 63) e as linhas
# retiradas (deterministicos.txt, cauda_auditoria.txt, patches_cauda*.txt).
#
# NAO escreve no patches.txt. Quem julga e o `mapa` com os candidatos
# aplicados (o quadro tem que andar, nunca recuar) e o proibidas.py.
#
# uso (da raiz):
#   python tools/varre_irmaos.py confere                 montadores contra o filme
#   python tools/varre_irmaos.py varre <mapa.txt> <saida.txt>
import sys, os, re, collections
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI); sys.path.insert(0, os.path.join(AQUI, 'anchor'))
import f11
import cabecalho_slice as CS

LFN, LPOC = 8, 8                     # log2_max_frame_num e log2_max_poc_lsb (medidos)


def ue(v):
    z = (v + 1).bit_length() - 1
    return '0' * z + bin(v + 1)[2:]


def se(v):
    return ue(2 * v - 1 if v > 0 else -2 * v)


def monta(h):
    """Bits de um cabecalho de P (weighted_pred, pesos todos ausentes) ou B
    (direto espacial), sem modificacao de lista nem MMCO, alinhado.

      h  dict com nal (byte), tipo 'P'/'B', fn, poc, override, n0, n1, qp,
         cinit e deblock/alpha/beta

    Devolve: string de bits."""
    b = format(h['nal'], '08b') + ue(0) + ue(5 if h['tipo'] == 'P' else 6) + ue(0)
    b += format(h['fn'] % (1 << LFN), '0%db' % LFN) + format(h['poc'] % (1 << LPOC), '0%db' % LPOC)
    if h['tipo'] == 'B': b += '1'                                  # direct_spatial_mv_pred
    b += '1' + ue(h['n0'] - 1) + (ue(h['n1'] - 1) if h['tipo'] == 'B' else '') if h['override'] else '0'
    b += '0' if h['tipo'] == 'P' else '00'                         # sem modificacao de lista
    if h['tipo'] == 'P':
        b += ue(0) + ue(0) + '00' * h['n0']                        # denominadores 0, sem pesos
    if h['nal'] & 0x60: b += '0'                                   # adaptive_ref_pic_marking 0
    b += ue(h['cinit']) + se(h['qp']) + ue(h['deblock'])
    if h['deblock'] != 1: b += se(h['alpha']) + se(h['beta'])
    while len(b) % 8: b += '1'
    return b


def campos(t, h, nal):
    """O dict do montador a partir do cabecalho lido."""
    tipo = {0: 'P', 1: 'B'}.get(h['slice_type'] % 5)
    return dict(nal=nal[0], tipo=tipo, fn=h['frame_num'], poc=h['poc'], override=h.get('override', 0),
                n0=h.get('n0', 1), n1=h.get('n1', 0), qp=h['qp_delta'], cinit=h.get('cabac_init', 0),
                deblock=h['deblock'], alpha=h.get('alpha', 0), beta=h.get('beta', 0))


def le(d, ix, t):
    _, o, s, _ = ix[t]
    nal = bytes(d[o + 4:o + s])
    try: h = CS.le_cabecalho(nal)
    except CS.Invalido: h = None
    return nal, h


def confere():
    """Ida e volta: cabecalho lido -> montado -> igual ao arquivo?"""
    d, ix = f11.buffer()
    cont = collections.Counter(); fora = []
    for t in range(len(ix)):
        if ix[t][3]: continue
        nal, h = le(d, ix, t)
        if h is None: cont['invalido'] += 1; continue
        if h['slice_type'] % 5 not in (0, 1): cont['nao_PB'] += 1; continue
        if h.get('mod_l0') or h.get('mod_l1') or h.get('amrpm') or any(p != (None, None) for p in h.get('pesos', [])):
            cont['fora_do_montador'] += 1; continue
        b = monta(campos(t, h, nal)); r = f11.rbsp(nal)[0]
        if r.startswith(b): cont['igual'] += 1
        else: cont['diferente'] += 1; fora.append(t)
    print(dict(cont)); print('diferentes:', fora[:40])


def mexidos():
    """O que ja foi mexido: (linhas do patches.txt, bytes tocados, retiradas)."""
    R = os.path.dirname(AQUI)
    pt = set(); by = set()
    for l in open(os.path.join(R, 'patches.txt')):
        p = l.split()
        if len(p) >= 2 and p[0].isdigit(): pt.add((int(p[0]), int(p[1]))); by.add(int(p[0]))
    ret = set()
    for f in ('deterministicos.txt', 'cauda_auditoria.txt', 'patches_cauda.txt', 'patches_cauda_pb.txt'):
        for l in open(os.path.join(R, 'data', f), encoding='utf-8', errors='replace'):
            m = re.match(r'#\s*RETIRADA[^:]*:\s*(\d+)\s+(\d)', l) or (f == 'cauda_auditoria.txt' and re.match(r'(\d+)\s+(\d)', l))
            if m: ret.add((int(m.group(1)), int(m.group(2))))
    return pt, by, ret


def gops(ix):
    idr = [i for i, _, _, r in ix if r] + [len(ix)]
    return [list(range(idr[k], idr[k + 1])) for k in range(len(idr) - 1)]


def dif_bits(b, r, o, idx):
    """Trocas (offset, bit) que levam o RBSP r aos bits b, em posicao de arquivo."""
    return [(o + 4 + idx[i >> 3], 7 - (i & 7)) for i in range(len(b)) if b[i] != r[i]]


def cabecalhos(d, ix, g, H):
    """Candidatos de cabecalho num GOP: invalidos e incoerentes, montados de novo.

    Devolve: lista de (quadro, motivo, trocas, n_empatados, campos escolhidos)."""
    out = []
    val = {t: H[t] for t in g if H[t] and H[t]['slice_type'] % 5 in (0, 1) and not ix[t][3]}
    usados = collections.Counter(h['poc'] for h in val.values())
    # cadencia de frame_num em ordem de decodificacao
    fn_exp = {}; ultimo = 0
    for k, t in enumerate(g):
        if ix[t][3]: ultimo = 0; continue
        fn_exp[t] = ultimo + 1
        if t in val: ref = val[t]['nal_ref_idc'] != 0
        else:                                       # desconhecido: o proximo valido decide
            nxt = next((val[u]['frame_num'] for u in g[k + 1:] if u in val), None)
            ref = nxt == ultimo + 2
        if ref: ultimo += 1
    sib = {'P': [], 'B': []}
    for t, h in val.items():
        c = campos(t, h, b''.join([bytes([0x41 if h['nal_ref_idc'] else 0x01])]))
        sib[c['tipo']].append(c)
    for t in g:
        if ix[t][3]: continue
        h = val.get(t); motivo = []
        if H[t] is None: motivo.append('invalido')
        elif t not in val: continue
        else:
            if h['frame_num'] != fn_exp[t]: motivo.append('frame_num')
            if usados[h['poc']] > 1: motivo.append('poc_repetido')
            if h['nal_ref_idc'] == 3: motivo.append('ref_idc_3')
            tipo = 'P' if h['slice_type'] % 5 == 0 else 'B'
            outros = [c['qp'] for c in sib[tipo] if c['fn'] == h['frame_num'] and c is not None]
            if tipo == 'B':
                grupo = [val[u]['qp_delta'] for u in val if u != t and val[u]['frame_num'] == h['frame_num'] and val[u]['slice_type'] % 5 == 1]
                if len(grupo) >= 2 and len(set(grupo)) == 1 and h['qp_delta'] != grupo[0]: motivo.append('qp_do_grupo')
            else:
                ps = sorted(val[u]['qp_delta'] for u in val if u != t and val[u]['slice_type'] % 5 == 0)
                if ps and abs(h['qp_delta'] - ps[len(ps) // 2]) >= 5: motivo.append('qp_dos_P')
        if not motivo: continue
        nal = bytes(d[ix[t][1] + 4:ix[t][1] + ix[t][2]]); r, idx = f11.rbsp(nal)
        livres = [p for p in range(0, 2 * len(g) + 24, 2) if usados[p] == 0 or (h and p == h['poc'] and usados[p] == 1)]
        cand = []
        for tipo in ('P', 'B'):
            if not sib[tipo]: continue
            params = {(c['override'], c['n0'], c['n1'], c['qp'], c['cinit'], c['deblock'], c['alpha'], c['beta']) for c in sib[tipo]}
            if tipo == 'P':
                params |= {(p[0], n0, 0, q, p[4], p[5], p[6], p[7]) for p in list(params) for n0 in (1, 2, 3) for q in range(-20, 16)}
            for p in params:
                for poc in livres:
                    c = dict(nal=0x41 if tipo == 'P' else 0x01, tipo=tipo, fn=fn_exp[t], poc=poc, override=p[0],
                             n0=p[1], n1=p[2], qp=p[3], cinit=p[4], deblock=p[5], alpha=p[6], beta=p[7])
                    b = monta(c)
                    if len(b) > len(r): continue
                    cand.append((sum(x != y for x, y in zip(b, r)), b, c))
        if not cand: continue
        m = min(x[0] for x in cand)
        if m == 0 or m > 7: continue
        melhores = [x for x in cand if x[0] == m]
        trocas = {tuple(dif_bits(x[1], r, ix[t][1], idx)) for x in melhores}
        b, c = melhores[0][1], melhores[0][2]
        out.append((t, '+'.join(motivo), list(sorted(trocas)[0]), len(trocas), c))
    return out


def gemeos(d, ix, g, H, mapa, k_max=3, para_ate=1300):
    """Bytes do comeco do slice que os irmaos saos do mesmo grupo tem iguais.

    Grupo: mesmo tipo, mesmo tamanho de cabecalho, mesmo QP, n0, n1. Sao: o
    mapa passa da fileira 8 (MB >= 1000). Janela: do primeiro byte depois do
    cabecalho ate o primeiro byte em que os saos discordam. Candidato: quadro
    que para antes do MB `para_ate` com 1 a `k_max` bits fora na janela."""
    out = []
    grupos = collections.defaultdict(list)
    for t in g:
        h = H[t]
        if not h or ix[t][3] or h['slice_type'] % 5 not in (0, 1): continue
        if h.get('pesos') and any(p != (None, None) for p in h['pesos']): continue
        grupos[(h['slice_type'] % 5, h['bits'], h['qp_delta'], h.get('n0'), h.get('n1'))].append(t)
    for chave, ts in grupos.items():
        saos = [t for t in ts if mapa.get(t, -1) >= 1000]
        if len(saos) < 2: continue
        nals = {t: d[ix[t][1] + 4:ix[t][1] + ix[t][2]] for t in ts}
        i0 = chave[1] // 8; j = i0
        while all(j < len(nals[t]) for t in saos) and len({nals[t][j] for t in saos}) == 1: j += 1
        if j - i0 < 3: continue
        ref = nals[saos[0]][i0:j]
        for t in ts:
            if t in saos or mapa.get(t, 8160) >= para_ate: continue
            seg = nals[t][i0:j]
            if len(seg) < len(ref): continue
            tr = [(ix[t][1] + 4 + i0 + q, b) for q in range(len(ref)) for b in range(8) if (seg[q] ^ ref[q]) >> b & 1]
            if 1 <= len(tr) <= k_max: out.append((t, 'gemeos_%d_bytes' % (j - i0), tr))
    return out


def varre(caminho_mapa, saida):
    d, ix = f11.buffer()
    mapa = {}
    for l in open(caminho_mapa):
        p = l.split()
        if len(p) == 5 and p[0].isdigit(): mapa[int(p[0])] = int(p[2])
    H = {}
    for t in range(len(ix)): H[t] = le(d, ix, t)[1]
    pt, by, ret = mexidos()
    linhas = []
    def situacao(off, bit):
        if (off, bit) in pt: return 'DESFARIA_PATCH'
        if (off, bit) in ret: return 'RETIRADA'
        if off in by: return 'byte_mexido'
        return 'novo'
    for g in gops(ix):
        for t, mot, tr, nemp, c in cabecalhos(d, ix, g, H):
            linhas.append((t, 'cabecalho:' + mot + (':empate%d' % nemp if nemp > 1 else ''), tr,
                           'fn=%d poc=%d qp=%d n0=%d %s' % (c['fn'], c['poc'], c['qp'], c['n0'], c['tipo'])))
        for t, mot, tr in gemeos(d, ix, g, H, mapa):
            linhas.append((t, mot, tr, ''))
    with open(saida, 'w') as f:
        for t, mot, tr, extra in sorted(linhas):
            f.write('%d mapa=%s %s %s | %s\n' % (t, mapa.get(t), mot, extra, ' '.join('%d:%d:%s' % (o, b, situacao(o, b)) for o, b in tr)))
    print(len(linhas), 'candidatos em', len({x[0] for x in linhas}), 'quadros ->', saida)


if __name__ == '__main__':
    if sys.argv[1] == 'confere': confere()
    elif sys.argv[1] == 'varre': varre(sys.argv[2], sys.argv[3])
