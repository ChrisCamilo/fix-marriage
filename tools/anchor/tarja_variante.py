# tarja_variante.py -- a tarja de baixo de um IDR recodificada pelo fim, com
# UMA coluna de modo variante (tools/anchor/ancora4.c), e a tarja pura
# (tools/anchor/ancora2.c). Contextos de entrada: os saturados do IDR integro
# 3426 (iguais nos 6 a partir da fileira 63), lidos do JM (JM_MBINFO).
#
# Validado (2026-10-01): 3426 pela tarja pura, distancia 0; 3348 integro (coluna
# 1 em croma modo 2, armadilha 61) com a coluna 1 varrida: o padrao real com
# distancia 0, a pura 23, a melhor alternativa 10.
# Tempo medido (entrada na fileira 63, 12 threads): ~4,6 s por padrao sem croma
# variante, ~30 s com (NS=8 pares de contextos de croma sorteados); a varredura
# de todas as colunas (1.921 padroes) leva ~16 h.
#
# Compilar (da raiz), no scratch:
#   gcc -O2 -fopenmp -o <scratch>/ancora2.exe tools/anchor/ancora2.c
#   gcc -O2 -fopenmp -o <scratch>/ancora4.exe tools/anchor/ancora4.c
# uso (da raiz):
#   python tools/anchor/tarja_variante.py <scratch> <idr> <fileira> [COLS=a:b] [extra=off:bit,...] [top]
import sys, os, subprocess, tempfile, shutil, time
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(R)
sys.path.insert(0, os.path.join(R, 'tools')); sys.path.insert(0, os.path.join(R, 'tools', 'anchor'))
import f11
import sincronia_tarja as J
UCRT = r'C:\msys64\ucrt64\bin'
AQ = sys.argv[1] if __name__ == '__main__' and len(sys.argv) > 1 else os.getcwd()
d, ix = f11.buffer()


def ctx_mbinfo(nal):
    """mb -> lista dos 29 contextos (JM_MBINFO, depois do '|')."""
    st = b'\0\0\0\1' + J.SPS + b'\0\0\0\1' + J.PPS + b'\0\0\0\1' + bytes(nal)
    t = tempfile.mkdtemp(); out = {}
    try:
        shutil.copy(J.CFG, os.path.join(t, 'decoder.cfg')); open(os.path.join(t, 'in.h264'), 'wb').write(st)
        subprocess.run([J.LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=t, capture_output=True,
                       env=dict(os.environ, JM_MBINFO='mb.txt'), timeout=60)
        for l in open(os.path.join(t, 'mb.txt')):
            a, b = l.split('|'); out.setdefault(int(a.split()[0]), list(map(int, b.split())))
    finally:
        shutil.rmtree(t, ignore_errors=True)
    return out


def rbsp(nal):
    """RBSP em bits ate o rbsp_stop_one_bit, e o indice byte do RBSP -> byte do NAL."""
    out = bytearray(); idx = []; z = 0
    for i, b in enumerate(nal):
        if z >= 2 and b == 3: z = 0; continue
        out.append(b); idx.append(i); z = z + 1 if b == 0 else 0
    s = ''.join(format(b, '08b') for b in out)
    return s[:s.rindex('1') + 1], idx


CTX = {}
def ctx29(ent):
    """Os 29 contextos do 3426 na entrada `ent` (MB de coluna 0)."""
    if not CTX:
        _, o, s, _ = ix[3426]; C = ctx_mbinfo(d[o + 4:o + s])
        for e in range(7200, 8160, 120): CTX[e] = C[e]
    return CTX[ent]


def pura(nal, ent, top=200):
    """[(dist, nbits, range, k1, k20)] das melhores hipoteses da tarja pura (ancora2.exe)."""
    bits, _ = rbsp(nal); jan = bits[-1500:]
    cv = [ctx29(ent)[i] for i in (2, 4, 5, 7, 8, 11, 15, 19)]
    inp = '%d %d\n%s\n%d %s\n' % (8160 - ent, ent % 120, ' '.join(map(str, cv)), len(jan), jan)
    env = dict(os.environ, TOP=str(top)); env['PATH'] = UCRT + ';' + env['PATH']
    r = subprocess.run([os.path.join(AQ, 'ancora2.exe')], input=inp, capture_output=True, text=True, env=env).stdout
    return [(int(p[1]), int(p[3]), int(p[5]), int(p[7]), int(p[9])) for p in (l.split() for l in r.split('\n'))
            if len(p) >= 10 and p[0] == 'dist' and int(p[1]) < (1 << 29)]


def variantes(nal, ent, cols='0:119', ns=8):
    """Uma linha do ancora4.exe por padrao (col -1 = pura), e o tempo."""
    bits, _ = rbsp(nal); jan = bits[-1500:]
    inp = '%d %d\n%s\n%d %s\n' % (8160 - ent, ent % 120, ' '.join(map(str, ctx29(ent))), len(jan), jan)
    env = dict(os.environ, COLS=cols, NS=str(ns)); env['PATH'] = UCRT + ';' + env['PATH']
    t0 = time.time()
    r = subprocess.run([os.path.join(AQ, 'ancora4.exe')], input=inp, capture_output=True, text=True, env=env).stdout
    out = []
    for l in r.split('\n'):
        p = l.split()
        if len(p) >= 22:
            out.append(dict(col=int(p[1]), i16=int(p[3]), cm=int(p[5]), d=int(p[7]), zeros=int(p[9]),
                            rng=int(p[11]), k1=int(p[13]), k20=int(p[15]), c1=int(p[17]), c3=int(p[19]), nb=int(p[21])))
    return out, time.time() - t0


if __name__ == '__main__':
    t = int(sys.argv[2]); ent = int(sys.argv[3]) * 120
    cols = sys.argv[4] if len(sys.argv) > 4 else '0:119'
    extra = [tuple(map(int, x.split(':'))) for x in sys.argv[5].split(',')] if len(sys.argv) > 5 and sys.argv[5] else []
    top = int(sys.argv[6]) if len(sys.argv) > 6 else 8
    _, o, s, _ = ix[t]; nal = bytearray(d[o + 4:o + s])
    for off, b in extra: nal[off - o - 4] ^= 1 << b
    out, dt = variantes(nal, ent, cols)
    p = [x for x in out if x['col'] < 0]
    print('IDR %d, entrada na fileira %d, colunas %s: %d padroes em %.1f s; pura: dist %s'
          % (t, ent // 120, cols, len(out), dt, p[0]['d'] if p else '-'))
    for x in sorted(out, key=lambda x: (x['d'], -x['zeros']))[:top]:
        print('   col %3d i16 %d cm %d -> dist %d (zeros %d) em %d bits' % (x['col'], x['i16'], x['cm'], x['d'], x['zeros'], x['nb']))
