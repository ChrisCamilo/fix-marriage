# residuo.py -- transformada, quantizacao e reconstrucao do H.264 para blocos 4x4
# (luma e croma 4:2:0), com matriz de escala plana (16), como no fluxo do filme.
#
# Plano 5 (docs/PLANO_RECODIFICA_F11.md): quando a imagem de um MB e conhecida
# (alvo) e a predicao tambem (o quadro de referencia decodificado), os
# coeficientes que o encoder escreveu deixam de ser livres: sao os que, pela
# reconstrucao da norma, levam a predicao ao alvo. Este modulo faz os dois
# sentidos -- coeficientes -> pixels (reconstrucao exata da norma, 8.5) e
# residuo -> coeficientes (transformada direta e quantizacao com varios
# arredondamentos, para cobrir as escolhas do encoder).
#
#   reconstroi_croma(pred, dc, ac, qpc) -> pixels 8x8 de um componente
#     pred  8x8 (lista de listas) da predicao
#     dc    4 niveis do DC 2x2 (ordem raster)
#     ac    4 listas de 15 niveis (varredura zigue-zague, posicoes 1-15) por bloco 4x4
#     qpc   QP de croma
#   candidatos_croma(pred, alvo, qpc) -> [(dc, ac)] que reconstroem o alvo EXATO
#   reconstroi_luma4x4(pred, niv, qp) / candidatos de luma analogos
#
# Blocos 4x4 de croma: 0 = cima-esquerda, 1 = cima-direita, 2 = baixo-esquerda,
# 3 = baixo-direita; o DC 2x2 em raster na mesma ordem.
import itertools

ZZ = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]      # zigue-zague 4x4 (quadro)
V = [[10, 16, 13], [11, 18, 14], [13, 20, 16], [14, 23, 18], [16, 25, 20], [18, 29, 23]]
MF = [[13107, 5243, 8066], [11916, 4660, 7490], [10082, 4194, 6554], [9362, 3647, 5825], [8192, 3355, 5243], [7282, 2893, 4559]]


def _cls(i, j):
    """Classe da posicao na tabela de escala: 0 (par, par), 1 (impar, impar), 2 (misto)."""
    if i % 2 == 0 and j % 2 == 0: return 0
    if i % 2 == 1 and j % 2 == 1: return 1
    return 2


def _ls(qp, i, j):
    """LevelScale4x4 com matriz plana (16 * normAdjust)."""
    return 16 * V[qp % 6][_cls(i, j)]


def _idct(d):
    """Transformada inversa 4x4 da norma (8.5.12.2) + (x + 32) >> 6."""
    e = [[0] * 4 for _ in range(4)]; f = [[0] * 4 for _ in range(4)]; g = [[0] * 4 for _ in range(4)]
    for i in range(4):
        a = d[i][0] + d[i][2]; b = d[i][0] - d[i][2]
        c = (d[i][1] >> 1) - d[i][3]; dd = d[i][1] + (d[i][3] >> 1)
        f[i] = [a + dd, b + c, b - c, a - dd]
    for j in range(4):
        a = f[0][j] + f[2][j]; b = f[0][j] - f[2][j]
        c = (f[1][j] >> 1) - f[3][j]; dd = f[1][j] + (f[3][j] >> 1)
        g[0][j], g[1][j], g[2][j], g[3][j] = a + dd, b + c, b - c, a - dd
    return [[(g[i][j] + 32) >> 6 for j in range(4)] for i in range(4)]


def _dequant_ac(niv15, qp, dc_val):
    """Nivel da varredura (posicoes 1-15) + DC ja escalado -> matriz 4x4 escalada (8.5.12.1)."""
    c = [0] * 16
    for k, x in enumerate(niv15): c[ZZ[k + 1]] = x
    d = [[0] * 4 for _ in range(4)]
    for idx in range(1, 16):
        i, j = divmod(idx, 4)
        if qp >= 24: d[i][j] = (c[idx] * _ls(qp, i, j)) << (qp // 6 - 4)
        else: d[i][j] = (c[idx] * _ls(qp, i, j) + (1 << (3 - qp // 6))) >> (4 - qp // 6)
    d[0][0] = dc_val
    return d


def reconstroi_croma(pred, dc, ac, qpc):
    """Pixels 8x8 de um componente de croma 4:2:0 a partir da predicao e dos niveis.

      pred  8x8 da predicao
      dc    4 niveis do DC 2x2, raster
      ac    4 listas de 15 niveis, uma por bloco 4x4
      qpc   QP de croma

    Devolve: 8x8, recortado em 0-255."""
    c = [[dc[0], dc[1]], [dc[2], dc[3]]]
    f = [[c[0][0] + c[0][1] + c[1][0] + c[1][1], c[0][0] - c[0][1] + c[1][0] - c[1][1]],
         [c[0][0] + c[0][1] - c[1][0] - c[1][1], c[0][0] - c[0][1] - c[1][0] + c[1][1]]]
    dcs = [((f[i][j] * _ls(qpc, 0, 0)) << (qpc // 6)) >> 5 for i in range(2) for j in range(2)]
    out = [[0] * 8 for _ in range(8)]
    for b in range(4):
        by, bx = divmod(b, 2)
        r = _idct(_dequant_ac(ac[b], qpc, dcs[b]))
        for i in range(4):
            for j in range(4):
                out[4 * by + i][4 * bx + j] = max(0, min(255, pred[4 * by + i][4 * bx + j] + r[i][j]))
    return out


def reconstroi_luma4x4(pred, niv16, qp):
    """Bloco 4x4 de luma (inter): niveis em zigue-zague (16, com DC) -> pixels."""
    c = [0] * 16
    for k, x in enumerate(niv16): c[ZZ[k]] = x
    d = [[0] * 4 for _ in range(4)]
    for idx in range(16):
        i, j = divmod(idx, 4)
        if qp >= 24: d[i][j] = (c[idx] * _ls(qp, i, j)) << (qp // 6 - 4)
        else: d[i][j] = (c[idx] * _ls(qp, i, j) + (1 << (3 - qp // 6))) >> (4 - qp // 6)
    r = _idct(d)
    return [[max(0, min(255, pred[i][j] + r[i][j])) for j in range(4)] for i in range(4)]


def _fdct(x):
    """Transformada direta 4x4 (nucleo inteiro)."""
    t = [[0] * 4 for _ in range(4)]; y = [[0] * 4 for _ in range(4)]
    for i in range(4):
        s0 = x[i][0] + x[i][3]; s1 = x[i][1] + x[i][2]; d0 = x[i][0] - x[i][3]; d1 = x[i][1] - x[i][2]
        t[i] = [s0 + s1, 2 * d0 + d1, s0 - s1, d0 - 2 * d1]
    for j in range(4):
        s0 = t[0][j] + t[3][j]; s1 = t[1][j] + t[2][j]; d0 = t[0][j] - t[3][j]; d1 = t[1][j] - t[2][j]
        y[0][j], y[1][j], y[2][j], y[3][j] = s0 + s1, 2 * d0 + d1, s0 - s1, d0 - 2 * d1
    return y


def _quant(v, qp, i, j, f, extra=0):
    q = 15 + qp // 6 + extra; m = MF[qp % 6][_cls(i, j)]
    a = (abs(v) * m + int(f * (1 << q))) >> q
    return -a if v < 0 else a


def candidatos_croma(pred, alvo, qpc, fs=(0.5, 1 / 3, 1 / 6, 0.0, 2 / 3), viz=1):
    """Niveis de croma que reconstroem o alvo EXATAMENTE a partir da predicao.

    Quantiza o residuo com varios arredondamentos (as escolhas possiveis do
    encoder) e ainda tenta +-viz em cada nivel nao nulo do DC e no maior AC
    de cada bloco; guarda so os que reconstroem o alvo pixel a pixel.

      pred, alvo  8x8 de um componente
      qpc         QP de croma
      fs          arredondamentos da quantizacao
      viz         vizinhanca testada em volta de cada nivel

    Devolve: lista de (dc, ac) distintos, do mais barato (menos niveis) ao mais caro."""
    res = [[alvo[i][j] - pred[i][j] for j in range(8)] for i in range(8)]
    base = set()
    for f in fs:
        ws = []; ac = []
        for b in range(4):
            by, bx = divmod(b, 2)
            w = _fdct([[res[4 * by + i][4 * bx + j] for j in range(4)] for i in range(4)]); ws.append(w)
            ac.append(tuple(_quant(w[ZZ[k] // 4][ZZ[k] % 4], qpc, ZZ[k] // 4, ZZ[k] % 4, f) for k in range(1, 16)))
        c = [[ws[0][0][0], ws[1][0][0]], [ws[2][0][0], ws[3][0][0]]]
        h = [c[0][0] + c[0][1] + c[1][0] + c[1][1], c[0][0] - c[0][1] + c[1][0] - c[1][1],
             c[0][0] + c[0][1] - c[1][0] - c[1][1], c[0][0] - c[0][1] - c[1][0] + c[1][1]]
        dc = tuple(_quant(x, qpc, 0, 0, f, extra=1) for x in h)
        base.add((dc, tuple(ac)))
    cands = set(base)
    for dc, ac in base:
        for k in range(4):
            for dv in range(-viz, viz + 1):
                d2 = list(dc); d2[k] += dv; cands.add((tuple(d2), ac))
    ok = [(dc, ac) for dc, ac in cands if reconstroi_croma(pred, list(dc), [list(a) for a in ac], qpc) == alvo]
    return sorted(ok, key=lambda x: sum(1 for v in x[0] if v) + sum(1 for a in x[1] for v in a if v))
