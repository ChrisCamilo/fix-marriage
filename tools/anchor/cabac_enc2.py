# cabac_enc2.py -- codificador CABAC da tarja de IDR com a sintaxe de cada MB
# livre: modo de predicao I16x16 (luma) e modo de croma podem variar por MB.
#
# O cabac_enc.py supoe tarja "pura" (I16x16 DC, croma DC, cbp 0) e so tem os
# contextos que essa sintaxe toca. O IDR integro 3348 mostrou que o encoder
# escolhe outro modo de mesmo custo em algumas colunas (croma modo 2 na coluna 1
# de todas as fileiras de tarja) -- mesmo pixel, outra sintaxe (armadilha 61).
# Aqui a selecao de contexto segue o JM (cabac.c):
#   mb_type I, bin 0      mb_type[0][a+b]  a/b = vizinho esq./cima disponivel e nao I4x4
#   terminacao (nao PCM)  final
#   AC luma               mb_type[0][4]    (0: cbp luma 0)
#   cbp croma             mb_type[0][5]    (0)
#   modo I16x16           mb_type[0][7], mb_type[0][8]  (2 bits do modo)
#   modo de croma         cipr[a+b] (a/b = vizinho com modo != 0), depois cipr[3]
#                         unario truncado em 3: 0 -> "0", 1 -> "10", 2 -> "110", 3 -> "111"
#   mb_qp_delta           dqp[0]           (0; MB anterior tambem com 0)
#   CBF do DC luma        bcbp[0][left + 2*upper]  (vizinho ausente conta 1)
#   end_of_slice          final
#
#   codifica(low, rng, ctx, sintaxe, mb_ini, n_mb, sintaxe_acima)
#     ctx      lista de 29 valores estado*2+MPS na ordem do JM_MBINFO
#              (mb_type[0][0..10], dqp[0..3], cipr[0..3], bcbp[0][0..3], ...);
#              e COPIADA -- o chamador nao a ve mudar
#     sintaxe  funcao (fileira, coluna) -> (modo_i16, modo_croma)
#   Devolve (bits, mb_de_cada_bit). Os primeiros ~10 bits dependem do low da
#   entrada, que ninguem conhece; comparar a partir do 12o, como no ancora2.c.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cabac_enc import RL, MPS, LPS

def codifica(low, rng, ctx, sintaxe, mb_ini, n_mb):
    st = dict(low=low, rng=rng, o=0); bits = []; mbs = []; cur = [0]
    cx = [[v >> 1, v & 1] for v in ctx]
    MBT, DQP, CIPR, BCBP = 0, 11, 15, 19
    def put(b):
        bits.append(b); mbs.append(cur[0])
        while st['o'] > 0: bits.append(1 - b); mbs.append(cur[0]); st['o'] -= 1
    def ren():
        while st['rng'] < 256:
            if st['low'] < 256: put(0)
            elif st['low'] >= 512: st['low'] -= 512; put(1)
            else: st['low'] -= 256; st['o'] += 1
            st['rng'] <<= 1; st['low'] <<= 1
    def eb(i, b):
        c = cx[i]; r = RL[c[0]][(st['rng'] >> 6) & 3]; st['rng'] -= r
        if b != c[1]:
            st['low'] += st['rng']; st['rng'] = r
            if c[0] == 0: c[1] = 1 - c[1]
            c[0] = LPS[c[0]]
        else: c[0] = MPS[c[0]]
        ren()
    def et(b):
        st['rng'] -= 2
        if b:
            st['low'] += st['rng']; st['rng'] = 2; ren(); put((st['low'] >> 9) & 1)
            bits.append((st['low'] >> 8) & 1); mbs.append(cur[0]); bits.append(1); mbs.append(cur[0])
        else: ren()
    for m in range(mb_ini, mb_ini + n_mb):
        cur[0] = m; r, c = divmod(m, 120)
        i16, cm = sintaxe(r, c)
        a = 1 if c > 0 else 0; b = 1 if r > 0 else 0          # vizinhos I16x16 na tarja
        eb(MBT + a + b, 1); et(0)
        eb(MBT + 4, 0); eb(MBT + 5, 0); eb(MBT + 7, i16 >> 1); eb(MBT + 8, i16 & 1)
        ca = 1 if c > 0 and sintaxe(r, c - 1)[1] != 0 else 0
        cb = 1 if r > 0 and sintaxe(r - 1, c)[1] != 0 else 0
        eb(CIPR + ca + cb, 1 if cm else 0)
        if cm: eb(CIPR + 3, 1 if cm > 1 else 0)
        if cm > 1: eb(CIPR + 3, 1 if cm > 2 else 0)
        eb(DQP + 0, 0)
        left = 1 if c == 0 else 0; up = 1 if r == 0 else 0      # CBF do DC dos vizinhos e 0 na tarja
        eb(BCBP + left + 2 * up, 0)
        et(1 if m == mb_ini + n_mb - 1 else 0)
    return bits, mbs
