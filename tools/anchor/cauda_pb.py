# cauda_pb.py -- plano 2 para quadros P e B: censo e proposta de correcoes de cauda.
#
# A tarja inferior de P/B e so skip (ancora_pb.c). Entrada na fileira 62
# (MB 7440, 720 MBs ate o fim), com o codIRange e o ESTADO do contexto do skip
# como incognitas (CS = -1): o contexto pode chegar a fileira 62 sem saturar.
# Mesmas regras de certeza do cauda_lote.py, com o limiar mais apertado:
# a cauda de P/B tem so ~30 bits testaveis e caudas aleatorias chegam a 6-13
# (contra 7-18 na ultima fileira de IDR), entao aqui so vale distancia <= 2.
#   - todas as saidas distintas na distancia minima apontam os mesmos bits;
#   - a cauda corrigida fecha com distancia 0 e sem violacao de escape no trecho;
#   - variantes de 1 bit que tornam valida cada violacao de escape da cauda.
#
# NAO escreve no patches.txt. A saida e a proposta; a versao aprovada e aplicada
# (2026-09-24, niveis A e B, sem os quadros com enchimento no fim: 10, 11, 560,
# 970, 1119) esta em data/patches_cauda_pb.txt.
# uso (da raiz): python tools/anchor/cauda_pb.py <scratch> <saida.txt> <censo.txt>
import sys, os, subprocess
S = sys.argv[1]; SAIDA = sys.argv[2]; CENSO = sys.argv[3]
sys.argv = [sys.argv[0], S, 'x']
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, S)
import cauda_lote as L, cabac_enc as E
CE = L.CE
EXE = S + '/ancora_pb.exe'
MAXD = 2; NMB = 720; CS = -1; JAN = 600          # CS = -1: estado do contexto do skip como incognita

def busca(bits):
    r = subprocess.run([EXE], input='%d %d %d %s\n' % (NMB, CS, len(bits), bits),
                       capture_output=True, text=True).stdout.split('\n')
    p = r[0].split(); dmin = int(p[1]); nemp = int(p[3])
    ex = [(int(l.split()[3]), int(l.split()[7])) for l in r[1:] if l.startswith('distinta')]   # (range, ctx)
    return dmin, nemp, ex

def codifica(rng, cs):
    """mesmo codificador do ancora_pb.c, em Python: bits de saida."""
    st = dict(low=0, rng=rng, o=0); bits = []
    def put(b):
        bits.append(b)
        while st['o'] > 0: bits.append(1 - b); st['o'] -= 1
    def ren():
        while st['rng'] < 256:
            if st['low'] < 256: put(0)
            elif st['low'] >= 512: st['low'] -= 512; put(1)
            else: st['low'] -= 256; st['o'] += 1
            st['rng'] <<= 1; st['low'] <<= 1
    c = [cs >> 1, cs & 1]
    for m in range(NMB):
        r = E.RL[c[0]][(st['rng'] >> 6) & 3]; st['rng'] -= r                              # skip = 1
        if c[1] != 1:                                                                       # 1 e o LPS
            st['low'] += st['rng']; st['rng'] = r
            if c[0] == 0: c[1] = 1 - c[1]
            c[0] = E.LPS[c[0]]
        else: c[0] = E.MPS[c[0]]
        ren()
        st['rng'] -= 2
        if m == NMB - 1:
            st['low'] += st['rng']; st['rng'] = 2; ren(); put((st['low'] >> 9) & 1)
            bits.append((st['low'] >> 8) & 1); bits.append(1)
        else: ren()
    return bits

def analisa(nal):
    bits, idx = L.rbsp(nal); jan = bits[-JAN:]; base = max(0, len(bits) - JAN)
    dmin, nemp, ex = busca(jan)
    if dmin > MAXD: return dict(estado='longe', dmin=dmin)
    conj = set(); ini_seg = None
    for (rng, cs) in ex:
        e = codifica(rng, cs); ini = len(jan) - len(e)
        conj.add(frozenset((ini + i, e[i]) for i in range(12, len(e)) if int(jan[ini + i]) != e[i]))
        ini_seg = ini if ini_seg is None else min(ini_seg, ini)
    return dict(estado='ok', dmin=dmin, nemp=nemp, ndist=len(ex), conj=conj, idx=idx, base=base,
                byte_ini=idx[(base + ini_seg + 12) // 8])

def um(t):
    o, s = CE.IX[t][1], CE.IX[t][2]; nal = bytes(CE.d[o+4:o+s])
    a0 = analisa(nal)
    if a0['estado'] == 'ok' and a0['dmin'] == 0: return 'intacta', a0['dmin'], None
    tentativas = [((), nal)]
    for v in L.violacoes(nal, s - 30):
        k = v - 4; b = nal[k + 2]
        for bit in range(8):
            nb = b ^ (1 << bit)
            if nb == 3 or (b == 3 and nb > 3):
                y = bytearray(nal); y[k + 2] ^= 1 << bit; tentativas.append((((k + 6, bit),), bytes(y)))
        if b == 3 and k + 3 < len(nal):
            for bit in range(8):
                if nal[k + 3] ^ (1 << bit) <= 3:
                    y = bytearray(nal); y[k + 3] ^= 1 << bit; tentativas.append((((k + 7, bit),), bytes(y)))
    oks = []
    for extra, n2 in tentativas:
        a = analisa(n2)
        if a['estado'] != 'ok' or len(a['conj']) != 1: continue
        x = bytearray(n2); trocas = []
        for (p, v) in next(iter(a['conj'])):
            pb = a['base'] + p; nb = a['idx'][pb // 8]; bit = 7 - (pb % 8)
            x[nb - 4] ^= 1 << bit; trocas.append((nb, bit))
        a2 = analisa(bytes(x))
        if a2['estado'] == 'ok' and a2['dmin'] == 0 and not L.violacoes(bytes(x), a['byte_ini']):
            oks.append(dict(trocas=sorted(list(extra) + trocas), dmin=a['dmin'], nemp=a['nemp'],
                            ndist=a['ndist'], byte_ini=a['byte_ini']))
    oks.sort(key=lambda r: len(r['trocas']))
    if not oks: return 'sem correcao certa', a0.get('dmin'), None
    if len(oks) > 1 and len(oks[0]['trocas']) == len(oks[1]['trocas']) and oks[0]['trocas'] != oks[1]['trocas']:
        return 'ambiguo', a0.get('dmin'), None
    return 'ok', a0.get('dmin'), oks[0]

if __name__ == '__main__':
    IDR = {t for t in range(len(CE.IX)) if CE.IX[t][3] and t != 1595}
    with open(SAIDA, 'w') as g, open(CENSO, 'w') as c:
        g.write('# proposta de correcoes de cauda de quadros P/B (plano 2) -- NAO aplicada\n'
                '# offset bit  quadro  dist  hipoteses_empatadas  saidas_distintas  trecho_desde_rel\n')
        for t in range(len(CE.IX)):
            if t in IDR: continue
            st, d0, r = um(t)
            c.write('%d %s %s\n' % (t, st.replace(' ', '_'), d0))
            if st == 'ok':
                for (off, bit) in r['trocas']:
                    g.write('%d %d  %d  %d  %d  %d  %d\n' % (CE.IX[t][1] + off, bit, t, r['dmin'], r['nemp'], r['ndist'], r['byte_ini']))
                print(t, 'OK', r, flush=True)
