# cauda_lote.py -- propoe correcoes de cauda para os IDRs quebrados (plano 2).
#
# Para cada IDR: recodifica a tarja (ancora2.exe) a partir de varias fileiras
# de entrada e fica com o trecho mais longo que encaixa com no maximo MAXD
# diferencas. O ancora2.exe percorre TODAS as hipoteses na distancia minima e
# agrupa pelas saidas distintas; a correcao so e aceita se todas as saidas
# distintas apontam exatamente os mesmos bits. Bit trocado e local (uma
# diferenca isolada); tarja que nao e pura faz o codificador divergir e da
# dezenas de diferencas -- por isso so distancia pequena conta como dano.
#
# Depois aplica a correcao e confere: o RBSP corrigido tem que dar distancia 0
# e nao pode sobrar violacao de escape no trecho coberto. Se a correcao exigir
# um escape que nao existe (caso do 1773: 00 00 02 21 -> 00 00 03 01), testa
# antes as variantes de 1 bit que tornam valida cada violacao de escape.
#
# NAO escreve no patches.txt: grava a proposta para o usuario aprovar.
#
# uso (da raiz do projeto):
#   python ferramentas/ancora/cauda_lote.py <scratch> <saida.txt> [IDR ...]
# <scratch> tem que ter o ancora2.exe compilado e o JM com o patch
# ferramentas/jm_mbinfo.patch (ver calib_enc.py).
import sys, os, re, subprocess
S = sys.argv[1]; SAIDA = sys.argv[2]; ALVOS = list(map(int, sys.argv[3:]))
sys.path.insert(0, S); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = [sys.argv[0], S, '0']
import calib_enc as CE, cabac_enc as E
EXE = S + '/ancora2.exe'
MAXD = 3
ENTRADAS = [7560, 7680, 7800, 7920, 8040]        # fileiras 63..67, coluna 0
JANELA = 1500                                    # bits do fim do RBSP que entram na comparacao


def rbsp(nal):
    """Bits do RBSP ate o stop bit, e o mapa byte do RBSP -> offset do NAL (com o prefixo de 4)."""
    out = bytearray(); idx = []; z = 0
    for i, b in enumerate(nal):
        if z >= 2 and b == 3:
            z = 0; continue
        out.append(b); idx.append(i + 4); z = z + 1 if b == 0 else 0
    s = ''.join(format(b, '08b') for b in out)
    return s[:s.rindex('1') + 1], idx


def violacoes(nal, ini):
    """Offsets (com prefixo) de 00 00 0x com x<3, e de 00 00 03 yy com yy>3, a partir de `ini`."""
    v = [m.start() + 4 for m in re.finditer(rb'\x00\x00[\x00-\x02]', nal) if m.start() + 4 >= ini]
    v += [m.start() + 4 for m in re.finditer(rb'\x00\x00\x03[\x04-\xff]', nal) if m.start() + 4 >= ini]
    return sorted(v)


def busca(bits, cv, ent):
    """(dist minima, hipoteses empatadas, [(range, k1, k20) de exemplo de cada saida distinta])."""
    inp = '%d %d\n%s\n%d %s\n' % (8160 - ent, ent % 120, ' '.join(map(str, cv)), len(bits), bits)
    r = subprocess.run([EXE], input=inp, capture_output=True, text=True,
                       env=dict(os.environ, TOP='1', MAXD=str(MAXD))).stdout
    dmin = None; nemp = 0; ex = []
    for l in r.strip().split('\n'):
        p = l.split()
        if not p:
            continue
        if p[0] == 'dist' and dmin is None:
            dmin = int(p[1])
        elif p[0] == 'empatadas':
            nemp = int(p[1])
        elif p[0] == 'distinta':
            ex.append((int(p[3]), int(p[5]), int(p[7])))
    return dmin, nemp, ex


def diffs(bits, cv, ent, rng, k1, k20):
    """Bits (posicao na janela, valor certo) em que a hipotese discorda do observado."""
    e, _ = E.codifica(0, rng, 0, cv, 8160 - ent, ent % 120, k1, k20)
    ini = len(bits) - len(e)
    return frozenset((ini + i, e[i]) for i in range(12, len(e)) if int(bits[ini + i]) != e[i]), ini


def analisa(nal, C):
    """Trecho mais longo que encaixa: dict, ou None."""
    bits, idx = rbsp(nal)
    jan = bits[-JANELA:]; base = max(0, len(bits) - JANELA)
    for ent in ENTRADAS:                          # da mais longa para a mais curta
        cv = [C[ent][i] for i in (2, 4, 5, 7, 8, 11, 15, 19)]
        dmin, nemp, ex = busca(jan, cv, ent)
        if dmin is None or dmin > MAXD:
            continue
        if len(ex) >= 64:
            return dict(estado='saidas demais', ent=ent, dmin=dmin)
        conj = set(); ini_seg = None
        for (rng, k1, k20) in ex:
            dd, ini = diffs(jan, cv, ent, rng, k1, k20); conj.add(dd)
            ini_seg = ini if ini_seg is None else min(ini_seg, ini)
        return dict(estado='ok', ent=ent, dmin=dmin, conj=conj, nemp=nemp, ndist=len(ex),
                    bits=bits, idx=idx, base=base, cv=cv, byte_ini=idx[(base + ini_seg + 12) // 8])
    return None


def aplica(nal, trocas_rbsp, a):
    """Converte trocas na janela do RBSP em (offset do NAL com prefixo, bit) e aplica."""
    x = bytearray(nal); trocas = []
    for (p, v) in trocas_rbsp:
        pb = a['base'] + p; nb = a['idx'][pb // 8]; bit = 7 - (pb % 8)
        x[nb - 4] ^= 1 << bit; trocas.append((nb, bit))
    return bytes(x), sorted(trocas)


def um(t, C):
    o, s = CE.IX[t][1], CE.IX[t][2]; nal = bytes(CE.d[o+4:o+s])
    tentativas = [((), nal)]
    for v in violacoes(nal, s - 80):             # variantes de 1 bit que tornam valida cada violacao
        k = v - 4; b = nal[k + 2]
        for bit in range(8):
            nb = b ^ (1 << bit)
            if nb == 3 or (b == 3 and nb > 3):   # vira escape, ou o "03" era dado
                y = bytearray(nal); y[k + 2] ^= 1 << bit
                tentativas.append((((k + 2 + 4, bit),), bytes(y)))
        if b == 3 and k + 3 < len(nal):          # 00 00 03 yy com yy > 3: yy danificado
            for bit in range(8):
                if nal[k + 3] ^ (1 << bit) <= 3:
                    y = bytearray(nal); y[k + 3] ^= 1 << bit
                    tentativas.append((((k + 3 + 4, bit),), bytes(y)))
    res = []
    for extra, n2 in tentativas:
        a = analisa(n2, C)
        if not a:
            continue
        if a['estado'] != 'ok':
            res.append(dict(estado=a['estado'], extra=extra, ent=a['ent'], dmin=a['dmin'])); continue
        if len(a['conj']) != 1:
            res.append(dict(estado='ambiguo', extra=extra, ent=a['ent'], dmin=a['dmin'], ndist=a['ndist'])); continue
        corrigido, trocas = aplica(n2, next(iter(a['conj'])), a)
        b2, _ = rbsp(corrigido)
        d2, _, _ = busca(b2[-JANELA:], a['cv'], a['ent'])
        ok = d2 == 0 and not violacoes(corrigido, a['byte_ini'])
        res.append(dict(estado='ok' if ok else 'nao fecha', extra=extra, ent=a['ent'], dmin=a['dmin'],
                        nemp=a['nemp'], ndist=a['ndist'], trocas=sorted(list(extra) + trocas),
                        byte_ini=a['byte_ini']))
    oks = sorted([r for r in res if r['estado'] == 'ok'], key=lambda r: len(r['trocas']))
    if not oks:
        return 'sem correcao certa', res
    if len(oks) > 1 and len(oks[0]['trocas']) == len(oks[1]['trocas']) and oks[0]['trocas'] != oks[1]['trocas']:
        return 'ambiguo entre variantes de escape', oks
    return 'ok', oks[0]


if __name__ == '__main__':
    import ancora_py as A
    o, s = CE.IX[2333][1], CE.IX[2333][2]
    C = A.mbinfo_ctx(CE.stream_de(bytes(CE.d[o+4:o+s])))
    with open(SAIDA, 'w') as g:
        g.write('# proposta de correcoes de cauda (plano 2) -- NAO aplicada\n'
                '# offset bit  IDR  fileira_de_entrada  dist  hipoteses_empatadas  saidas_distintas  trecho_desde_rel\n')
        for t in ALVOS:
            st, r = um(t, C)
            if st == 'ok':
                print(t, 'OK fileira', r['ent'] // 120, 'dist', r['dmin'], 'empatadas', r['nemp'],
                      'saidas', r['ndist'], 'desde rel', r['byte_ini'], 'trocas', r['trocas'], flush=True)
                for (off, bit) in r['trocas']:
                    g.write('%d %d  %d  %d  %d  %d  %d  %d\n' % (CE.IX[t][1] + off, bit, t, r['ent'] // 120,
                                                              r['dmin'], r['nemp'], r['ndist'], r['byte_ini']))
            else:
                print(t, st.upper(), [(x['estado'], x['extra'], x['ent'] // 120, x['dmin']) for x in (r if isinstance(r, list) else [r])], flush=True)
