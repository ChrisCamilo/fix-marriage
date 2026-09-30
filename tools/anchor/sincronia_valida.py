# sincronia_valida.py -- etapa 0b do plano 4: falsa sincronia do juiz de QP em IDR integro.
# Medido em 2026-09-30 (8 corridas, 640 trocas cada): a troca certa chega ao corte
# em todas; as erradas que chegam: 3368 0-0, 3397 7-17, 3426 5-71, 2333 86-89.
# uso (da raiz): python tools/anchor/sincronia_valida.py <idr> <recuo_da_planta> <janela_bytes> [corte_recuo=60] [seed=1]
# Planta 1 bit trocado a `recuo` bytes do fim dos dados, corta o NAL `corte_recuo`
# bytes antes do fim, e testa toda troca de 1 bit em [planta-janela, planta+janela).
# Alcance = byte do primeiro MB (desde a fileira da planta) com QP diferente do
# QP do quadro, I_PCM, nao lido, ou que comeca alem do corte.
import sys, os, subprocess, tempfile, shutil, random
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sincronia_1773 as S

t = int(sys.argv[1]); recuo = int(sys.argv[2]); jan = int(sys.argv[3])
crec = int(sys.argv[4]) if len(sys.argv) > 4 else 60
random.seed(int(sys.argv[5]) if len(sys.argv) > 5 else 1)
_, O, SZ, _ = S.ix[t]
n = S.d[O + 4:O + SZ]; f = len(n)
while f >= 3 and n[f-3] == 0 and n[f-2] == 0 and n[f-1] == 3: f -= 3
while f > 0 and n[f-1] == 0: f -= 1
fim = f + 4                                  # fim dos dados, relativo a amostra
corte = fim - crec
planta = (O + fim - recuo, random.randrange(8))


def roda(trocas):
    nal = bytearray(S.d[O + 4:O + corte])
    for off, bit in [planta] + trocas: nal[off - O - 4] ^= 1 << bit
    st = b'\0\0\0\1' + S.SPS + b'\0\0\0\1' + S.PPS + b'\0\0\0\1' + bytes(nal)
    tmp = tempfile.mkdtemp(dir=S.TMPD)
    try:
        shutil.copy(S.CFG, tmp + r'\decoder.cfg'); open(tmp + r'\in.h264', 'wb').write(st)
        subprocess.run([S.LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=tmp, capture_output=True,
                       env=dict(os.environ, JM_MBINFO='mb.txt'), timeout=60)
        mbs = {}
        for l in open(tmp + r'\mb.txt'):
            p = l.split('|')[0].split()
            if len(p) >= 8: mbs.setdefault(int(p[0]), (int(p[1]), int(p[5]), int(p[6])))
    except Exception:
        mbs = {}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return mbs


ref = roda([(planta[0], planta[1])])          # sem a planta: o quadro verdadeiro
QP = ref[0][1]
ini = min(m for m in ref if ref[m][2] >= planta[0] - O - 4 - 400)   # MB uns 400 B antes da planta


def alcance(trocas):
    mbs = roda(trocas)
    for m in range(ini, 8160):
        if m not in mbs: return corte if False else mbs and -1
        tipo, qp, pos = mbs[m]
        if pos >= corte - 4: return corte
        if qp != QP or tipo == 14: return pos
    return corte


base = alcance([])
cands = [(O + b, k) for b in range(planta[0] - O - jan, planta[0] - O + jan) for k in range(8)]
with ThreadPoolExecutor(12) as ex:
    notas = list(ex.map(lambda c: alcance([c]), cands))
certo = notas[cands.index(planta)]
chegam = [c for c, a in zip(cands, notas) if a >= corte]
print('IDR %d QP %d fim %d corte %d planta rel %d bit %d | base %d | certo %d | %d de %d chegam ao corte (certo incluido: %s)'
      % (t, QP, fim, corte, planta[0] - O, planta[1], base, certo, len(chegam), len(cands), planta in chegam))
melhores = sorted(zip(notas, cands), reverse=True)[:5]
print('  5 melhores:', [(a, c[0] - O, c[1]) for a, c in melhores])
