# cabeca.py -- censo da tarja de CIMA: recodifica os MBs 0-959 de cada quadro a
# partir do estado exato do CABAC e compara com o arquivo.
#
# Na cabeca do slice o estado do CABAC vem inteiro do cabecalho (contextos por
# QP e cabac_init_idc, range 510, low 0): nao ha a incognita de encaixe das
# caudas. Se a sintaxe da tarja e conhecida, o codificador gera os bits certos
# sem ler o arquivo, e cada bit que difere e um bit trocado -- enquanto a
# hipotese valer. Hipotese errada diverge do ponto do erro em diante (~50%).
#
# A tarja de cima (fileiras 0-7, linhas 0-127) medida nos quadros que passam
# da fileira 8 (explora, 2026-09-28):
#   I   I16x16 DC, croma DC, sem residuo; so o MB 0 tem o DC de luma (a
#       predicao dele e 128 e o alvo 16) -- 90 de 90 IDRs, nenhuma variante
#   B   tudo pulado (1.607 quadros); B com MB nao pulado na cabeca fica fora
#   P   tudo pulado (516); nos quadros com predicao ponderada (fades) o MB 0
#       e especial e os outros sao I16x16 DC sem residuo (41); em 5 so o MB 0
#       foge e o resto e pulado
# Hipoteses por quadro (fica a de menor distancia):
#   I   MB 0 com cada nivel de DC que leva 128 a 16 exato no QP do slice
#   P   'skip' (tudo pulado), 'mb0+dc' e 'mb0+skip' (MB 0 como o arquivo o le)
#       e, com predicao ponderada, 'fis rN ...' (MB 0 pela fisica, abaixo)
#   B   'skip'
# Com o MB 0 lido do arquivo, os bits dele nao sao testados: a distancia so
# conta depois deles.
#
# O MB 0 pela fisica (2026-09-29, frame 15): nos P ponderados do fade o MB 0 e
# inter 16x16 com vetor zero, da referencia cujo peso da o residuo mais barato
# -- so o DC de cada bloco, que leva a tarja de volta a 16 e o croma a 128 (9:
# ref0, luma +1; 11: ref2, croma U +3 V -3; 13: ref0, luma +1 e U +3). Para
# cada referencia: a predicao ponderada da tarja (8.4.2.3), o residuo, e os
# niveis que o reconstroem exato (residuo.py). O arquivo nao entra: quando o
# proprio MB 0 esta danificado, a leitura dele falha e so a fisica testa.
#
# A comparacao e feita no NAL, byte a byte na mesma posicao, e nao no RBSP: a
# cabeca de B e P e quase so zeros, entao o NAL dela e `00 00 03 00 00 03 ...`
# -- um terco dos bytes e escape. Bit trocado num escape (03 -> 07) ou num zero
# antes dele faz a remocao de escapes errar e desloca o RBSP; no arquivo, o
# bit-rot troca bits sem deslocar bytes. Entao o esperado e o cabecalho do
# arquivo + a tarja recodificada, com os escapes que o encoder poria, e cada
# bit diferente e um bit trocado na posicao exata do arquivo.
#
# NAO escreve no patches.txt: o censo e medida; a proposta e o que tem
# distancia pequena, e quem julga e o `mapa` com a proposta aplicada (o resto
# do quadro, que a hipotese nao usa).
#
# Candidatos: P e B com 1 <= distancia <= 3 (a cabeca deles tem ~40 bits);
# I com qualquer distancia desde que o dano seja uniforme -- no maximo 12
# diferencas em qualquer janela de 64 bits (a hipotese errada passa de 35).
#
# Um MB de verdade no meio de skips deixa uma rajada de ~9 bits (o low do
# codificador saindo) e depois zeros de novo; bit trocado deixa um bit isolado.
# Perto do fim da janela testada a rajada aparece cortada e parece troca: o
# controle (quadros que ja passavam da tarja) tem 3 "correcoes" assim, todas
# com folga <= 9 bits. Dai os niveis de P/B:
#   A  folga >= 16 bits iguais depois da ultima troca e trocas a >= 8 bits
#      uma da outra (aplicado em 2026-09-28)
#   B  folga >= 10 (aplicado em 2026-09-29). Calibrado nos 26 quadros de
#      controle com evento real na cabeca: cortando a janela em todo ponto
#      possivel, nenhuma rajada verdadeira passa como troca com folga >= 10
#      (com 8, 0,07%; com 6, 0,5%). O espacamento entre trocas nao importa.
#   C  o resto (folga < 10), fora
# e em todos: o quadro parava dentro da tarja e o `mapa` com o candidato
# aplicado para mais adiante (nunca igual, nunca antes).
#
# uso (da raiz):
#   python tools/anchor/cabeca.py censo <censo.txt> <candidatos.txt> [quadro ...]
#     censo.txt       uma linha por quadro: quadro tipo hipotese bits_testados
#                     distancia 1o_byte_testado posicoes (bit do NAL: 8*byte +
#                     bit contado do mais significativo)
#     candidatos.txt  `offset bit` dos candidatos, no formato do patches.txt
#   ./reparador.exe "$MP4" index.txt <patches.txt + candidatos> mapa > mapa_com.txt
#   python tools/anchor/cabeca.py niveis <censo.txt> <mapa_antes.txt> <mapa_com.txt> <proposta.txt>
#     proposta.txt    os candidatos que o juiz aceita, com o nivel de cada quadro
import sys, os
from concurrent.futures import ProcessPoolExecutor
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI); sys.path.insert(0, os.path.dirname(AQUI))
import cabac_p as C, f11
import cabecalho_slice as CS
from residuo import V, reconstroi_luma4x4, reconstroi_croma

N_TARJA = 960                      # fileiras 0-7
SKIP = [('mb_skip_flag', (0,)), ('end_of_slice_flag', (0,))]


def niveis_dc(qp):
    """Niveis do DC de luma de I16x16 que levam a predicao 128 a 16 exato.

    So o coeficiente (0,0) da Hadamard: todos os DCs dos blocos 4x4 saem
    iguais, e o bloco reconstruido e (dcY + 32) >> 6 em todo pixel (8.5.10 e
    8.5.12, matriz plana).

      qp  QP do MB

    Devolve: lista de niveis, do mais proximo de zero ao mais longe."""
    ls = 16 * V[qp % 6][0]; out = []
    for L in range(-1, -4000, -1):
        f = L
        dcy = (f * ls) << (qp // 6 - 6) if qp >= 36 else (f * ls + (1 << (5 - qp // 6))) >> (6 - qp // 6)
        r = (dcy + 32) >> 6
        if 128 + r == 16: out.append(L)
        elif 128 + r < 16: break
    return out


def _mb_i(tipo, nivel=None):
    """MB de tarja I16x16 DC sem residuo (com o DC de luma do MB 0, se dado)."""
    dc = [('DC luma 16x16', (nivel, 0)), ('DC luma 16x16', (0, 0))] if nivel else [('DC luma 16x16', (0, 0))]
    if tipo == 'I':
        return [('mb_type', (3,)), ('intra_chroma_pred_mode', (0,)), ('mb_qp_delta', (0,))] + dc + [('end_of_slice_flag', (0,))]
    return [('mb_skip_flag', (1,)), ('mb_type', (9,)), ('intra_chroma_pred_mode', (0,)), ('mb_qp_delta', (0,))] + dc + [('end_of_slice_flag', (0,))]


QPC = [29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39]
CROMA_QP_OFFSET = 1                   # chroma_qp_index_offset do PPS


def _pesa(x, w, o, logwd):
    """Predicao ponderada explicita de uma amostra (8.4.2.3.2), com o recorte."""
    v = ((x * w + (1 << (logwd - 1))) >> logwd) + o if logwd >= 1 else x * w + o
    return max(0, min(255, v))


def _niveis_luma(res, qp, n=2):
    """Niveis do DC de um bloco 4x4 inter que reconstroem o residuo uniforme res (ate n, do menor)."""
    out = []
    for L in sorted(range(-40, 41), key=abs):
        if L and all(v == 16 for lin in reconstroi_luma4x4([[16 - res] * 4] * 4, [L] + [0] * 15, qp) for v in lin):
            out.append(L)
            if len(out) == n: break
    return out


def _niveis_croma(res, qpc, n=2):
    """Niveis do DC 2x2 de croma (so o primeiro) que reconstroem o residuo uniforme res."""
    out = []
    for L in sorted(range(-40, 41), key=abs):
        if L and all(v == 128 for lin in reconstroi_croma([[128 - res] * 8] * 8, [L, 0, 0, 0], [[0] * 15] * 4, qpc) for v in lin):
            out.append(L)
            if len(out) == n: break
    return out


def mb0_fisica(h, qp):
    """Hipoteses do MB 0 de um P ponderado pela fisica da tarja (16 / 128).

      h   cabecalho do slice (le_cabecalho: pesos, luma_denom, chroma_denom, n0)
      qp  QP do slice

    Devolve: lista de (nome, sintaxe do MB 0)."""
    qpi = min(51, qp + CROMA_QP_OFFSET); qpc = qpi if qpi < 30 else QPC[qpi - 30]
    ld, cd = h['luma_denom'], h['chroma_denom']; out = []
    for r, (lw, cw) in enumerate(h['pesos']):
        wl, ol = lw if lw else (1 << ld, 0)
        cb_w, cb_o, cr_w, cr_o = cw if cw else (1 << cd, 0, 1 << cd, 0)
        ry = 16 - _pesa(16, wl, ol, ld)
        rcb = 128 - _pesa(128, cb_w, cb_o, cd); rcr = 128 - _pesa(128, cr_w, cr_o, cd)
        ly = _niveis_luma(ry, qp) if ry else [0]
        lcb = _niveis_croma(rcb, qpc) if rcb else [0]
        lcr = _niveis_croma(rcr, qpc) if rcr else [0]
        for a in ly:
            for b in lcb:
                for c in lcr:
                    se = [('mb_skip_flag', (1,)), ('mb_type', (1,))]
                    if h['n0'] > 1: se.append(('ref_idx_l0', (r,)))
                    se += [('mvd0_l0', (0,)), ('mvd1_l0', (0,))]
                    cbp = (15 if a else 0) + (16 if (b or c) else 0)
                    se.append(('coded_block_pattern', (cbp,)))
                    if cbp: se.append(('mb_qp_delta', (0,)))
                    if a:
                        for _ in range(16): se += [('Luma sng', (a, 0)), ('Luma sng', (0, 0))]
                    if cbp & 16:
                        se += ([('2x2 DC Chroma', (b, 0))] if b else []) + [('2x2 DC Chroma', (0, 0))]
                        se += ([('2x2 DC Chroma', (c, 0))] if c else []) + [('2x2 DC Chroma', (0, 0))]
                    out.append(('fis r%d L%d U%d V%d' % (r, a, b, c), se + [('end_of_slice_flag', (0,))]))
    return out


def hipoteses(tipo, qp, mb0, h=None):
    """As sintaxes candidatas da tarja de cima.

      tipo  'I', 'P' ou 'B'
      qp    QP do slice
      mb0   sintaxe do MB 0 lida do arquivo (ou None se nao deu para ler)
      h     cabecalho do slice; num P com pesos, entram as hipoteses do MB 0
            pela fisica (mb0_fisica), testadas desde o bit 0

    Devolve: lista de (nome, {mb: sintaxe}, mb do primeiro bit testado)."""
    if tipo == 'I':
        return [('dc%d' % L, dict([(0, _mb_i('I', L))] + [(m, _mb_i('I')) for m in range(1, N_TARJA)]), 0)
                for L in niveis_dc(qp)]
    out = [('skip', {m: SKIP for m in range(N_TARJA)}, 0)]
    if tipo == 'P' and mb0 is not None:
        out.append(('mb0+dc', dict([(0, mb0)] + [(m, _mb_i('P')) for m in range(1, N_TARJA)]), 1))
        out.append(('mb0+skip', dict([(0, mb0)] + [(m, SKIP) for m in range(1, N_TARJA)]), 1))
    if tipo == 'P' and h is not None and h.get('pesos'):
        resto = [(m, _mb_i('P')) for m in range(1, N_TARJA)]
        out += [(nome, dict([(0, se)] + resto), 0) for nome, se in mb0_fisica(h, qp)]
    return out


def codifica(sint, tipo, qp, modelo, nref):
    """Bits que saem da tarja (so os ja emitidos: os pendentes dependem do que vem depois).

    Devolve: (bits, posicoes) com posicoes[m] = primeiro bit emitido no MB m."""
    e = C.Enc(); ctx = C.contextos(qp, modelo, 'I' if tipo == 'I' else 'P'); mbs = {}
    st = {'dq': 0, 'nref': nref, 'tipo': tipo}; pos = []
    for m in range(N_TARJA):
        pos.append(len(e.bits)); C.processa_mb(e, ctx, mbs, m, sint[m], st, 0)
    return e.bits, pos


def escapa(rb):
    """Prevencao de emulacao (7.4.1): RBSP -> bytes do NAL.

      rb  bytes do RBSP (a partir do byte de cabecalho do NAL)

    Devolve: (nal, idx) com idx[k] = byte do NAL onde foi parar o byte k do RBSP."""
    out = bytearray(); idx = []; z = 0
    for x in rb:
        if z >= 2 and x <= 3: out.append(3); z = 0
        idx.append(len(out)); out.append(x); z = z + 1 if x == 0 else 0
    return bytes(out), idx


def quadro(t):
    """Censo de um quadro.

    Devolve: (t, tipo, hipotese, bits testados, distancia, primeiro byte
    testado do NAL, [(bit do NAL, offset no arquivo, bit no byte)]) -- tipo
    traz o motivo quando nao ha teste ('cab_invalido')."""
    d, ix = _G
    o, s_ = ix[t][1], ix[t][2]; nal = bytes(d[o + 4:o + s_])
    try: h = CS.le_cabecalho(nal)
    except CS.Invalido: return (t, 'cab_invalido', '-', 0, -1, 0, [])
    tipo = {0: 'P', 1: 'B', 2: 'I'}[h['slice_type'] % 5]
    rb, idx = f11.rbsp(nal); obs = [int(x) for x in rb[h['bits']:]]
    qp = 26 + h['qp_delta']; mod = h.get('cabac_init', 0); nref = h.get('n0', 1)
    mb0 = None
    if tipo == 'P':
        try:
            lido = C.decodifica(obs, qp, mod, n_mb=1, nref=nref, tipo='P')
            mb0 = lido.get(0)
        except Exception: mb0 = None
    melhor = None
    for nome, sint, m_ini in hipoteses(tipo, qp, mb0, h):
        try: bits, pos = codifica(sint, tipo, qp, mod, nref)
        except (C.ForaDoAlcance, AssertionError, KeyError, TypeError): continue
        # esperado: cabecalho do arquivo + a tarja recodificada, so bytes inteiros
        eb = [int(x) for x in rb[:h['bits']]] + bits
        rbe = bytes(int(''.join(map(str, eb[k:k + 8])), 2) for k in range(0, len(eb) - 7, 8))
        nale, idxe = escapa(rbe)
        a = idxe[(h['bits'] + pos[m_ini]) >> 3]; b = min(len(nale), len(nal))
        dif = [8 * i + j for i in range(a, b) for j in range(8) if ((nale[i] ^ nal[i]) >> (7 - j)) & 1]
        # O escape logo depois da janela: o esperado acaba em 00 00 e o arquivo
        # tem ali 00 00 0x (x < 3), proibido num NAL -- o byte so pode ser o
        # 03 de escape (o resto da cabeca pulada e zeros). Sem isto o NAL
        # corrigido fica cortado ali e o quadro "fecha" sem ler os dados
        # (armadilha 62: 263, 1638 e 2459 em 2026-09-28; o 2971 ganhou um 00 00 01).
        nb = 8 * (b - a)
        if 2 <= b < len(nal) and nale[b - 2] == 0 and nale[b - 1] == 0 and nal[b] < 3:
            dif += [8 * b + j for j in range(8) if ((nal[b] ^ 3) >> (7 - j)) & 1]; nb += 8
        if melhor is None or len(dif) < len(melhor[3]): melhor = (nome, nb, a, dif)
    if melhor is None: return (t, tipo, '-', 0, -1, 0, [])
    nome, n, a, dif = melhor
    locs = [(k, o + 4 + (k >> 3), 7 - (k & 7)) for k in dif]
    return (t, tipo, nome, n, len(dif), a, locs)


def _ini():
    global _G
    _G = f11.buffer()


MAXD_PB, JAN_I, MAX_JAN_I = 3, 64, 12
FOLGA_A, VAO_A, FOLGA_B = 16, 8, 10


def janela_max(ps, a0, n):
    """Maior numero de diferencas em uma janela de JAN_I bits (passo 8) do trecho testado."""
    return max((sum(1 for p in ps if w <= p < w + JAN_I) for w in range(a0, a0 + max(n - JAN_I, 0) + 1, 8)), default=0)


def candidato(tipo, dist, ps, a0, n):
    """Se o quadro entra na lista de candidatos (antes do juiz)."""
    if dist < 1: return False
    if tipo in 'PB': return dist <= MAXD_PB
    return tipo == 'I' and janela_max(ps, a0, n) <= MAX_JAN_I


def le_censo(caminho):
    """Linhas do censo: [(quadro, tipo, hipotese, bits testados, distancia, 1o byte, [bits do NAL])]."""
    out = []
    for l in open(caminho):
        if l[0] == '#': continue
        t, tipo, nome, nb, dist, a, locs = l.split()
        out.append((int(t), tipo, nome, int(nb), int(dist), int(a), [int(x) for x in locs.split(',')] if locs != '-' else []))
    return out


def censo(saida, candidatos, quadros=None):
    """Roda o censo em todos os quadros (ou nos dados) e grava o censo e os candidatos."""
    n = len([l for l in open(os.path.join(f11.RAIZ, 'index.txt')) if l.strip()])
    alvos = quadros or list(range(n))
    with ProcessPoolExecutor(int(os.environ.get('PROCS', 8)), initializer=_ini) as ex:
        res = list(ex.map(quadro, alvos, chunksize=16))
    with open(saida, 'w') as f:
        f.write('# censo da tarja de cima (tools/anchor/cabeca.py censo) -- MEDIDA, nao sao patches\n')
        f.write('# quadro tipo hipotese bits_testados distancia 1o_byte_testado posicoes(bit do NAL)\n')
        for t, tipo, nome, nb, dist, a, locs in res:
            f.write('%d %s %s %d %d %d %s\n' % (t, tipo, nome, nb, dist, a, ','.join(str(x[0]) for x in locs) or '-'))
    with open(candidatos, 'w') as f:
        f.write('# candidatos do censo da cabeca -- NAO sao patches; o juiz e o `cabeca.py niveis`\n')
        for t, tipo, nome, nb, dist, a, locs in res:
            if candidato(tipo, dist, [x[0] for x in locs], 8 * a, nb):
                for _, off, bit in locs: f.write('%d %d\n' % (off, bit))
    return res


def _mapa(caminho):
    """{quadro: MB de parada} da saida do modo `mapa` (8160 = inteiro, -1 = sem imagem)."""
    m = {}
    for l in open(caminho):
        x = l.split()
        if len(x) == 5 and x[0].isdigit(): m[int(x[0])] = int(x[2])
    return m


def niveis(censo_txt, mapa_antes, mapa_com, saida):
    """Julga os candidatos pelo `mapa` e grava a proposta com o nivel de cada quadro.

      censo_txt   saida do subcomando censo
      mapa_antes  `mapa` com o patches.txt
      mapa_com    `mapa` com o patches.txt + todos os candidatos

    Devolve: {motivo ou nivel: numero de quadros}."""
    from collections import Counter
    _, ix = f11.buffer()
    a, b = _mapa(mapa_antes), _mapa(mapa_com); cont = Counter(); linhas = []
    for t, tipo, nome, nb, dist, a0, ps in le_censo(censo_txt):
        if not candidato(tipo, dist, ps, 8 * a0, nb): continue
        if a[t] >= 960: cont['fora: ja passava da tarja'] += 1; continue
        if b[t] < a[t]: cont['fora: o mapa piora'] += 1; continue
        if b[t] == a[t]: cont['fora: o mapa nao muda'] += 1; continue
        if tipo == 'I': nv = 'I'
        else:
            folga = 8 * a0 + nb - max(ps)
            vao = min((ps[i + 1] - ps[i] for i in range(len(ps) - 1)), default=99)
            nv = 'A' if folga >= FOLGA_A and vao >= VAO_A else ('B' if folga >= FOLGA_B else 'C')
        cont[nv] += 1
        for p in ps:
            linhas.append('%d %d # quadro %d %s %s nivel %s mapa %d->%d\n' % (
                ix[t][1] + 4 + (p >> 3), 7 - (p & 7), t, tipo, nome, nv, a[t], b[t]))
    with open(saida, 'w') as f:
        f.write('# proposta do censo da cabeca (tools/anchor/cabeca.py niveis) -- NAO aplicada\n')
        f.write('# offset bit # quadro tipo hipotese nivel mapa antes->com a correcao\n')
        f.writelines(linhas)
    return cont


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'censo':
        res = censo(sys.argv[2], sys.argv[3], [int(x) for x in sys.argv[4:]] or None)
        from collections import Counter
        c = Counter((r[1], min(r[4], 9) if r[4] >= 0 else -1) for r in res)
        for k in sorted(c, key=str): print(k, c[k])
    elif len(sys.argv) == 6 and sys.argv[1] == 'niveis':
        for k, v in sorted(niveis(*sys.argv[2:]).items()): print(k, v)
    else:
        print(__doc__ or open(__file__).read().split('\nimport')[0])
