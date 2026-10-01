# sincronia_tarja.py -- 2o juiz do plano 4: a fileira 60 tem que ser tarja pura,
# no lugar certo. Roda o JM (JM_MBINFO) em cada candidato com a amostra cortada
# `recuo` bytes antes do fim e exige, ao mesmo tempo:
#   1. sincronia nas fileiras 57-59: o QP do quadro em todo MB, nenhum I_PCM;
#   2. a fileira 60 comecando entre 77 e 95 bytes antes do fim do NAL -- a faixa
#      dos 6 IDRs integros medidos (2333, 3319, 3348, 3368, 3397, 3426);
#   3. todo MB da fileira 60 lido antes do corte em I16x16 (tipo 10): nos
#      integros a fileira 60 e so tarja.
# O recuo padrao (64) e o do 1773, que tem de ser cortado antes da 1a violacao
# de escape (byte 34.253 de 34.317).
#
# uso (da raiz):
#   python tools/anchor/sincronia_tarja.py <idr> <lista> <saida> [recuo=64] [extra=off:bit,...] [mb_min mb_max]
#     lista   saida do avanco ("off bit off bit   mb N"); com mb_min/mb_max so
#             entram os de N na faixa (o 1o juiz, CORTE_ALVO)
#     extra   trocas aplicadas a todos (o dano plantado no controle sintetico)
#   saida: "ok|falha motivo  pos_fil60  off bit ..."
import sys, os, subprocess, tempfile, shutil, re
from concurrent.futures import ThreadPoolExecutor
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, R + r'\tools'); sys.path.insert(0, R + r'\tools\anchor')
os.chdir(R)
import f11
OLD = r'C:\Users\chris\AppData\Local\Temp\claude\C--Users-chris-Documents-Workspace-fix-marriage\7b294410-6095-41a4-88a5-a6b03a96b3ba\scratchpad'
LDEC = OLD + r'\JM\bin\ninja\gcc-mingw-16.1\x86_64\relwithdebinfo\ldecod.exe'
CFG = OLD + r'\JM\cfg\decoder.cfg'
SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821"); PPS = bytes.fromhex("68eb7352")
d, ix = f11.buffer()


def mbinfo(nal):
    st = b'\0\0\0\1' + SPS + b'\0\0\0\1' + PPS + b'\0\0\0\1' + bytes(nal)
    t = tempfile.mkdtemp()
    mbs = {}
    try:
        shutil.copy(CFG, t + r'\decoder.cfg'); open(t + r'\in.h264', 'wb').write(st)
        subprocess.run([LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=t, capture_output=True,
                       env=dict(os.environ, JM_MBINFO='mb.txt'), timeout=60)
        for l in open(t + r'\mb.txt'):
            p = l.split('|')[0].split()
            if len(p) >= 8: mbs.setdefault(int(p[0]), (int(p[1]), int(p[5]), int(p[6])))
    except Exception:
        pass
    finally:
        shutil.rmtree(t, ignore_errors=True)
    return mbs


def julga(t, trocas, recuo):
    """Devolve (ok, motivo, posicao da fileira 60 no NAL)."""
    _, o, s, _ = ix[t]
    nal = bytearray(d[o + 4:o + s])
    for off, bit in trocas:
        if 0 <= off - o - 4 < len(nal): nal[off - o - 4] ^= 1 << bit
    fim = len(nal)
    corte = fim - recuo
    mbs = mbinfo(nal[:corte])
    if 0 not in mbs: return False, 'sem_quadro', -1
    qp = mbs[0][1]
    for m in range(6840, 7200):
        if m not in mbs: return False, 'parou_%d' % m, -1
        tipo, q, pos = mbs[m]
        if pos >= corte - 4: return False, 'corte_antes_fil60_%d' % m, -1
        if q != qp or tipo == 14: return False, 'qp_%d' % m, -1
    if 7200 not in mbs: return False, 'sem_fil60', -1
    p60 = mbs[7200][2]
    if not (fim - 95 <= p60 <= fim - 77): return False, 'fil60_fora_%d' % (fim - p60), p60
    for m in range(7200, 7320):
        if m not in mbs or mbs[m][2] >= corte - 16: break   # o CABAC le adiante (no 3319, 10 bytes)
        if mbs[m][0] != 10 or mbs[m][1] != qp: return False, 'fil60_nao_tarja_%d' % m, p60
    return True, 'ok', p60


if __name__ == '__main__':
    t, lista, saida = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    recuo = int(sys.argv[4]) if len(sys.argv) > 4 else 64
    extra = [tuple(map(int, x.split(':'))) for x in sys.argv[5].split(',')] if len(sys.argv) > 5 and sys.argv[5] else []
    lo, hi = (int(sys.argv[6]), int(sys.argv[7])) if len(sys.argv) > 7 else (-1, 10 ** 9)
    cands = []
    for l in open(lista):
        a, _, b = l.partition('mb')
        v = re.split(r'\s+', a.strip())
        if len(v) < 2 or not (lo <= int(b) <= hi): continue
        cands.append([(int(v[i]), int(v[i + 1])) for i in range(0, len(v) - 1, 2)])
    with ThreadPoolExecutor(12) as ex, open(saida, 'w') as f:
        for c, (ok, mot, p60) in zip(cands, ex.map(lambda c: julga(t, extra + c, recuo), cands)):
            f.write('%s %s %d  %s\n' % ('ok' if ok else 'falha', mot, p60, ' '.join('%d %d' % x for x in c)))
    print(len(cands), 'candidatos')
