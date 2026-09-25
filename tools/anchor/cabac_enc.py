# cabac_enc.py -- o mesmo codificador do ancora2.c, em Python, para inspecionar
# uma hipotese: devolve os bits e, para cada bit, em que MB ele saiu.
import re, os
N = os.path.dirname(os.path.abspath(__file__))
tab = open(N + '/tabelas_cabac.h').read()
def _arr(nome):
    blk = tab[tab.index(nome):]; blk = blk[blk.index('{'):blk.index('};')]; return [int(x) for x in re.findall(r'\d+', blk)]
RL = _arr('rLPS_table_64x4'); RL = [RL[i*4:i*4+4] for i in range(64)]
MPS = _arr('AC_next_state_MPS_64'); LPS = _arr('AC_next_state_LPS_64')
def codifica(low, rng, outst, cv, nmb, col0, k1=None, k20=None):
    st = dict(low=low, rng=rng, o=outst); bits = []; mbs = []; cur = [0]
    def put(b):
        bits.append(b); mbs.append(cur[0])
        while st['o'] > 0: bits.append(1 - b); mbs.append(cur[0]); st['o'] -= 1
    def ren():
        while st['rng'] < 256:
            if st['low'] < 256: put(0)
            elif st['low'] >= 512: st['low'] -= 512; put(1)
            else: st['low'] -= 256; st['o'] += 1
            st['rng'] <<= 1; st['low'] <<= 1
    def eb(c, b):
        r = RL[c[0]][(st['rng'] >> 6) & 3]; st['rng'] -= r
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
    c = [[v >> 1, v & 1] for v in cv]
    a = [k1 >> 1, k1 & 1] if k1 is not None else None; b = [k20 >> 1, k20 & 1] if k20 is not None else None
    for m in range(nmb):
        col = (col0 + m) % 120; cur[0] = m
        eb(a if col == 0 else c[0], 1); et(0); eb(c[1], 0); eb(c[2], 0); eb(c[3], 1); eb(c[4], 0)
        eb(c[6], 0); eb(c[5], 0); eb(b if col == 0 else c[7], 0); et(m == nmb - 1)
    return bits, mbs
