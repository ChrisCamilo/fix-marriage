# predicao_intra.py -- predicoes intra do H.264 (9 modos 4x4, 4 modos 16x16,
# 4 de croma) vetorizadas sobre o quadro inteiro, e o posto do modo que o
# encoder escolheu em cada bloco.
#
# As predicoes sao EXATAS: no IDR 2333, os 8.280 blocos 4x4 sem residuo batem
# pixel a pixel com a predicao do modo escolhido. Servem para qualquer medida
# que precise da predicao intra (plano 2, reconstrucao, conferencias).
#
# Como JUIZ de lixo ("o modo escolhido e o otimo?") foi REFUTADO em
# 2026-09-24 -- ver docs/PLANO_JUIZ_ENCODER.md. O motivo e estrutural: a
# imagem decodificada e, por construcao, predicao do modo escolhido mais o
# residuo; no lixo o residuo tende a ser pequeno (o modelo de contextos prefere
# zeros), entao o modo escolhido sai "o melhor" tanto no lixo quanto no bom.
#
# Entradas: imagem ANTES do deblocking (ffmpeg -ec 0 -skip_loop_filter all) e,
# por MB, a saida do JM com o patch tools/jm_mbinfo.patch (JM_MBINFO).
import numpy as np

W, H = 1920, 1080
MBW, MBH = 120, 68            # 68 fileiras: a ultima tem so 8 linhas visiveis
I4MB, I16MB, IPCM = 9, 10, 14


def le_mbinfo(path):
    """Uma linha por MB do JM_MBINFO -> dict mb -> (tipo, i16, croma, cbp, qp, pos, bl, rng, val, modos[16])."""
    out = {}
    for l in open(path):
        p = l.split()
        if len(p) < 26: continue
        v = list(map(int, p))
        out[v[0]] = (v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10:26])
    return out


def _pad(Y):
    """Imagem com 16 linhas extras embaixo (1080 -> 1088) replicando a ultima."""
    h, w = Y.shape
    return np.concatenate([Y, np.repeat(Y[-1:], 1088 * (h // 1080) - h, axis=0)]) if h % 16 else Y


def pred4x4(Y):
    """Predicao dos 9 modos para todo bloco 4x4 do quadro.
    Devolve P[9, by, bx, 4, 4] (float, nan onde o modo nao esta disponivel) e o
    bloco reconstruido B[by, bx, 4, 4]."""
    Y = _pad(Y).astype(np.int32)
    Hh, Ww = Y.shape
    nby, nbx = Hh // 4, Ww // 4
    B = Y.reshape(nby, 4, nbx, 4).transpose(0, 2, 1, 3)
    # vizinhos de cima (8: 4 + 4 do canto superior direito), esquerda (4), canto
    top = np.zeros((nby, nbx, 8), np.int32); left = np.zeros((nby, nbx, 4), np.int32); tl = np.zeros((nby, nbx), np.int32)
    Yp = np.pad(Y, ((1, 0), (1, 4)), mode='edge')        # linha -1 e coluna -1, 4 colunas a direita
    for j in range(nby):
        r = Yp[4 * j]                                       # linha y0-1 (deslocada de 1 pela borda)
        for k in range(8):
            top[j, :, k] = r[1 + np.arange(nbx) * 4 + k]
        tl[j] = r[np.arange(nbx) * 4]
        for k in range(4):
            left[j, :, k] = Yp[4 * j + 1 + k, np.arange(nbx) * 4]
    by = np.arange(nby)[:, None]; bx = np.arange(nbx)[None, :]
    temT = np.broadcast_to(by > 0, (nby, nbx)); temL = np.broadcast_to(bx > 0, (nby, nbx))
    # canto superior direito: disponivel so se o bloco de la ja foi decodificado.
    # Dentro do MB (bloco em coordenadas locais xl, yl) o padrao da norma e:
    #   indisponivel para (xl,yl) = (1,1) (3,1) (1,3) (3,3) (3,2) e para xl=3 fora da fileira 0
    xl = (np.arange(nbx) % 4)[None, :]; yl = (np.arange(nby) % 4)[:, None]
    tr_ok = np.ones((4, 4), bool)                            # [yl, xl]
    for (x, y) in [(1, 1), (3, 1), (1, 3), (3, 3), (3, 2)]: tr_ok[y, x] = False
    temTR = tr_ok[yl, xl] & temT
    # xl=3, yl=0: canto superior direito esta no MB de cima-direita: existe se nao for a ultima coluna de MBs
    ult = (np.arange(nbx) // 4 == nbx // 4 - 1)[None, :]
    temTR = temTR & ~((xl == 3) & (yl == 0) & ult)
    top = np.where(temTR[..., None] | (np.arange(8) < 4), top, top[..., 3:4])   # substitui o canto por p[3,-1]
    temTL = temT & temL
    T = lambda k: top[..., k] if k >= 0 else tl                     # p[k,-1], k=-1 e o canto
    L = lambda k: left[..., k] if k >= 0 else tl                    # p[-1,k]
    P = np.full((9, nby, nbx, 4, 4), np.nan)
    ys, xs = np.meshgrid(range(4), range(4), indexing='ij')
    for y in range(4):
        for x in range(4):
            P[0, :, :, y, x] = T(x)                                  # vertical
            P[1, :, :, y, x] = L(y)                                  # horizontal
            if x == 3 and y == 3: P[3, :, :, y, x] = (T(6) + 3 * T(7) + 2) >> 2
            else: P[3, :, :, y, x] = (T(x + y) + 2 * T(x + y + 1) + T(x + y + 2) + 2) >> 2
            if x > y: P[4, :, :, y, x] = (T(x - y - 2) + 2 * T(x - y - 1) + T(x - y) + 2) >> 2
            elif x < y: P[4, :, :, y, x] = (L(y - x - 2) + 2 * L(y - x - 1) + L(y - x) + 2) >> 2
            else: P[4, :, :, y, x] = (T(0) + 2 * tl + L(0) + 2) >> 2
            z = 2 * x - y
            if z >= 0 and z % 2 == 0: P[5, :, :, y, x] = (T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 1) >> 1
            elif z >= 0: P[5, :, :, y, x] = (T(x - (y >> 1) - 2) + 2 * T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 2) >> 2
            elif z == -1: P[5, :, :, y, x] = (L(0) + 2 * tl + T(0) + 2) >> 2
            else: P[5, :, :, y, x] = (L(y - 1) + 2 * L(y - 2) + L(y - 3) + 2) >> 2
            z = 2 * y - x
            if z >= 0 and z % 2 == 0: P[6, :, :, y, x] = (L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 1) >> 1
            elif z >= 0: P[6, :, :, y, x] = (L(y - (x >> 1) - 2) + 2 * L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 2) >> 2
            elif z == -1: P[6, :, :, y, x] = (L(0) + 2 * tl + T(0) + 2) >> 2
            else: P[6, :, :, y, x] = (T(x - 1) + 2 * T(x - 2) + T(x - 3) + 2) >> 2
            if y % 2 == 0: P[7, :, :, y, x] = (T(x + (y >> 1)) + T(x + (y >> 1) + 1) + 1) >> 1
            else: P[7, :, :, y, x] = (T(x + (y >> 1)) + 2 * T(x + (y >> 1) + 1) + T(x + (y >> 1) + 2) + 2) >> 2
            z = x + 2 * y
            if z in (0, 2, 4): P[8, :, :, y, x] = (L(y + (x >> 1)) + L(y + (x >> 1) + 1) + 1) >> 1
            elif z in (1, 3): P[8, :, :, y, x] = (L(y + (x >> 1)) + 2 * L(y + (x >> 1) + 1) + L(y + (x >> 1) + 2) + 2) >> 2
            elif z == 5: P[8, :, :, y, x] = (L(2) + 3 * L(3) + 2) >> 2
            else: P[8, :, :, y, x] = L(3)
    st, sl = top[..., :4].sum(-1), left.sum(-1)
    dc = np.where(temT & temL, (st + sl + 4) >> 3, np.where(temL, (sl + 2) >> 2, np.where(temT, (st + 2) >> 2, 128)))
    P[2] = dc[..., None, None]
    # disponibilidade de cada modo
    ok = np.stack([temT, temL, np.ones_like(temT), temT, temTL, temTL, temTL, temT, temL])
    P[~ok] = np.nan
    return P, B


def pred16(Y):
    """Predicao dos 4 modos 16x16 (0 V, 1 H, 2 DC, 3 Plano) por MB. Devolve P[4, mby, mbx, 16, 16], B."""
    Y = _pad(Y).astype(np.int32)
    Hh, Ww = Y.shape; my, mx = Hh // 16, Ww // 16
    B = Y.reshape(my, 16, mx, 16).transpose(0, 2, 1, 3)
    Yp = np.pad(Y, ((1, 0), (1, 0)), mode='edge')
    top = np.stack([Yp[16 * j, 1 + 16 * np.arange(mx)[:, None] + np.arange(16)] for j in range(my)])
    left = np.stack([Yp[16 * j + 1 + np.arange(16)[None, :], 16 * np.arange(mx)[:, None]] for j in range(my)])
    tl = np.stack([Yp[16 * j, 16 * np.arange(mx)] for j in range(my)])
    temT = np.broadcast_to((np.arange(my) > 0)[:, None], (my, mx)); temL = np.broadcast_to((np.arange(mx) > 0)[None, :], (my, mx))
    P = np.full((4, my, mx, 16, 16), np.nan)
    P[0] = top[:, :, None, :]; P[1] = left[:, :, :, None]
    st, sl = top.sum(-1), left.sum(-1)
    P[2] = np.where(temT & temL, (st + sl + 16) >> 5, np.where(temL, (sl + 8) >> 4, np.where(temT, (st + 8) >> 4, 128)))[..., None, None]
    t = lambda k: np.where(k >= 0, top[..., max(k, 0)], tl); l = lambda k: np.where(k >= 0, left[..., max(k, 0)], tl)
    Hs = sum((i + 1) * (t(8 + i) - t(6 - i)) for i in range(8)); Vs = sum((i + 1) * (l(8 + i) - l(6 - i)) for i in range(8))
    a = 16 * (left[..., 15] + top[..., 15]); b = (5 * Hs + 32) >> 6; c = (5 * Vs + 32) >> 6
    yy, xx = np.meshgrid(range(16), range(16), indexing='ij')
    P[3] = np.clip((a[..., None, None] + b[..., None, None] * (xx - 7) + c[..., None, None] * (yy - 7) + 16) >> 5, 0, 255)
    P[0][~temT] = np.nan; P[1][~temL] = np.nan; P[3][~(temT & temL)] = np.nan
    return P, B


def pred_croma(C):
    """Predicao dos 4 modos de croma (0 DC, 1 H, 2 V, 3 Plano) por MB, bloco 8x8. Devolve P[4, my, mx, 8, 8], B."""
    C = C.astype(np.int32)
    if C.shape[0] % 8: C = np.concatenate([C, np.repeat(C[-1:], 8 - C.shape[0] % 8, axis=0)])
    Hh, Ww = C.shape; my, mx = Hh // 8, Ww // 8
    B = C.reshape(my, 8, mx, 8).transpose(0, 2, 1, 3)
    Cp = np.pad(C, ((1, 0), (1, 0)), mode='edge')
    top = np.stack([Cp[8 * j, 1 + 8 * np.arange(mx)[:, None] + np.arange(8)] for j in range(my)])
    left = np.stack([Cp[8 * j + 1 + np.arange(8)[None, :], 8 * np.arange(mx)[:, None]] for j in range(my)])
    tl = np.stack([Cp[8 * j, 8 * np.arange(mx)] for j in range(my)])
    temT = np.broadcast_to((np.arange(my) > 0)[:, None], (my, mx)); temL = np.broadcast_to((np.arange(mx) > 0)[None, :], (my, mx))
    P = np.full((4, my, mx, 8, 8), np.nan)
    for (bj, bi) in ((0, 0), (0, 1), (1, 0), (1, 1)):
        st = top[..., 4 * bi:4 * bi + 4].sum(-1); sl = left[..., 4 * bj:4 * bj + 4].sum(-1)
        both = (st + sl + 4) >> 3; so_t = (st + 2) >> 2; so_l = (sl + 2) >> 2
        if (bj, bi) in ((0, 0), (1, 1)):
            v = np.where(temT & temL, both, np.where(temL, so_l, np.where(temT, so_t, 128)))
        elif (bj, bi) == (0, 1):                               # direita-cima: prefere o de cima
            v = np.where(temT, so_t, np.where(temL, so_l, 128))
        else:                                                  # esquerda-baixo: prefere o da esquerda
            v = np.where(temL, so_l, np.where(temT, so_t, 128))
        P[0, :, :, 4 * bj:4 * bj + 4, 4 * bi:4 * bi + 4] = v[..., None, None]
    P[1] = left[:, :, :, None]; P[2] = top[:, :, None, :]
    t = lambda k: np.where(k >= 0, top[..., max(k, 0)], tl); l = lambda k: np.where(k >= 0, left[..., max(k, 0)], tl)
    Hs = sum((i + 1) * (t(4 + i) - t(2 - i)) for i in range(4)); Vs = sum((i + 1) * (l(4 + i) - l(2 - i)) for i in range(4))
    a = 16 * (left[..., 7] + top[..., 7]); b = (34 * Hs + 32) >> 6; c = (34 * Vs + 32) >> 6
    yy, xx = np.meshgrid(range(8), range(8), indexing='ij')
    P[3] = np.clip((a[..., None, None] + b[..., None, None] * (xx - 3) + c[..., None, None] * (yy - 3) + 16) >> 5, 0, 255)
    P[1][~temL] = np.nan; P[2][~temT] = np.nan; P[3][~(temT & temL)] = np.nan
    return P, B


def _rank(sads, escolhido):
    """Posto do modo escolhido entre os disponiveis (0 = o melhor; empate conta a favor)."""
    s = sads[escolhido]
    return int(np.sum(sads[~np.isnan(sads)] < s))


def notas(Y, U, V, info):
    """Por MB: dict com
       r4   posto medio dos 16 modos 4x4 escolhidos (so MB I4x4), 0 = sempre o melhor
       f4   fracao dos 16 blocos em que o escolhido NAO esta entre os 2 melhores
       r16  posto do modo 16x16 (so MB I16x16)
       rc   posto do modo de croma
       zero SAD do escolhido nos blocos sem residuo (cbp) -- tem que ser 0 num decode certo
    """
    P4, B4 = pred4x4(Y); S4 = np.abs(P4 - B4[None]).sum(axis=(-1, -2))          # [9, by, bx]
    P16, B16 = pred16(Y); S16 = np.abs(P16 - B16[None]).sum(axis=(-1, -2))
    Pu, Bu = pred_croma(U); Pv, Bv = pred_croma(V)
    SC = np.abs(Pu - Bu[None]).sum(axis=(-1, -2)) + np.abs(Pv - Bv[None]).sum(axis=(-1, -2))
    out = {}
    for m, (tipo, i16, cm, cbp, qp, *_r, modos) in info.items():
        my, mx = divmod(m, MBW)
        d = {'tipo': tipo, 'cbp': cbp, 'qp': qp}
        if tipo == I4MB:
            rk = []; zero = []
            for k in range(16):
                jj, ii = divmod(k, 4); by, bx = 4 * my + jj, 4 * mx + ii
                s = S4[:, by, bx]; e = modos[k]
                if not (0 <= e < 9) or np.isnan(s[e]): rk.append(8); continue
                rk.append(_rank(s, e))
                q = (jj // 2) * 2 + (ii // 2)                         # quadrante 8x8
                if not (cbp >> q) & 1: zero.append(s[e])
            d['r4'] = float(np.mean(rk)); d['f4'] = float(np.mean(np.array(rk) >= 2)); d['zero'] = zero
        elif tipo == I16MB and 0 <= i16 < 4:
            s = S16[:, my, mx]
            d['r16'] = _rank(s, i16) if not np.isnan(s[i16]) else 3
            if cbp & 15 == 0 and not np.isnan(s[i16]): d['zero16'] = float(s[i16])   # sem AC luma: so o DC muda
        if 0 <= cm < 4 and my < SC.shape[1]:
            s = SC[:, my, mx]; d['rc'] = _rank(s, cm) if not np.isnan(s[cm]) else 3
        out[m] = d
    return out
