# calib_enc.py -- calibra o juiz de escolha otima do encoder com dano sintetico.
# Mesmo banco do calib.py da sessao anterior: 1 bit aleatorio num IDR integro,
# rotulo exato por comparacao com o original. Aqui cada caso passa tambem pelo
# JM instrumentado (JM_MBINFO) para ter o modo escolhido em cada bloco.
#
# uso: python calib_enc.py <scratch> <casos por IDR>   (roda da raiz do projeto)
import sys, os, subprocess, random, tempfile, shutil, pickle, numpy as np
from concurrent.futures import ThreadPoolExecutor
S, N = sys.argv[1], int(sys.argv[2])
sys.path.insert(0, S)
import juiz_encoder as J
OLD = S.replace('2ec6e4cb-6bc6-49f4-8284-db763c3af8cb', '7b294410-6095-41a4-88a5-a6b03a96b3ba')
LDEC = OLD + '/JM/bin/ninja/gcc-mingw-16.1/x86_64/relwithdebinfo/ldecod.exe'
CFG = OLD + '/JM/cfg/decoder.cfg'
SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821"); PPS = bytes.fromhex("68eb7352")
IX = [tuple(map(int, l.split()[:4])) for l in open('index.txt') if l.strip()]
d = bytearray(open('Caio & Lizandra - Making- Caio-Balu.mp4', 'rb').read())
for l in open('patches.txt'):
    p = l.split()
    if len(p) >= 2 and p[0].isdigit(): d[int(p[0])] ^= 1 << int(p[1])
BONS = [2333, 3319, 3348, 3368, 3397, 3426]
W, H = 1920, 1080

def decod_ff(stream):
    p = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-ec', '0', '-skip_loop_filter', 'all',
                        '-threads', '1', '-f', 'h264', '-i', '-', '-frames:v', '1', '-f', 'rawvideo',
                        '-pix_fmt', 'yuv420p', '-'], input=stream, capture_output=True)
    b = np.frombuffer(p.stdout, np.uint8)
    if b.size < W * H * 3 // 2: return None
    return (b[:W*H].reshape(H, W), b[W*H:W*H*5//4].reshape(H//2, W//2), b[W*H*5//4:W*H*3//2].reshape(H//2, W//2))

def decod_jm(stream):
    t = tempfile.mkdtemp(dir=S)
    try:
        shutil.copy(CFG, t + '/decoder.cfg'); open(t + '/in.h264', 'wb').write(stream)
        subprocess.run([LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=t, capture_output=True,
                       env=dict(os.environ, JM_MBINFO='mb.txt'), timeout=60)
        return J.le_mbinfo(t + '/mb.txt') if os.path.exists(t + '/mb.txt') else {}
    finally:
        shutil.rmtree(t, ignore_errors=True)

def arrays(n):
    """dict por MB -> arrays [8160] de r4, f4, r16, rc (nan onde nao se aplica)."""
    out = {k: np.full(8160, np.nan, np.float32) for k in ('r4', 'f4', 'r16', 'rc')}
    for m, v in n.items():
        for k in out:
            if k in v: out[k][m] = v[k]
    return out

def stream_de(nal): return b'\0\0\0\1' + SPS + b'\0\0\0\1' + PPS + b'\0\0\0\1' + nal

def caso(args):
    t, k = args
    o, s = IX[t][1], IX[t][2]; orig = bytes(d[o+4:o+s])
    r = random.Random(t * 100000 + k)
    pos = r.randrange(200, len(orig) - 200); b = r.randrange(8)
    x = bytearray(orig); x[pos] ^= 1 << b
    img = decod_ff(stream_de(bytes(x))); info = decod_jm(stream_de(bytes(x)))
    if img is None or not info: return None
    n = J.notas(*img, info)
    return dict(t=t, pos=pos + 4, bit=b, Y=img[0], U=img[1], V=img[2], n=arrays(n), jm_mbs=max(info) + 1)

if __name__ == '__main__':
    orig = {}
    for t in BONS:
        o, s = IX[t][1], IX[t][2]; st = stream_de(bytes(d[o+4:o+s]))
        img = decod_ff(st); info = decod_jm(st)
        orig[t] = dict(img=img, n=arrays(J.notas(*img, info)))
    res = []
    with ThreadPoolExecutor(8) as ex:
        for r in ex.map(caso, [(t, k) for t in BONS for k in range(N)]):
            if r is None: continue
            Yo, Uo, Vo = orig[r['t']]['img']
            dano = (np.abs(r.pop('Y')[:1072].astype(np.int16) - Yo[:1072]).reshape(67, 16, 120, 16).max(axis=(1, 3))
                    + np.abs(r.pop('U')[:536].astype(np.int16) - Uo[:536]).reshape(67, 8, 120, 8).max(axis=(1, 3))
                    + np.abs(r.pop('V')[:536].astype(np.int16) - Vo[:536]).reshape(67, 8, 120, 8).max(axis=(1, 3)))
            r['dano'] = dano.ravel().astype(np.int16)
            res.append(r)
    pickle.dump({'orig': {t: v['n'] for t, v in orig.items()}, 'casos': res}, open(S + '/calib_enc.pkl', 'wb'))
    print('casos', len(res))
