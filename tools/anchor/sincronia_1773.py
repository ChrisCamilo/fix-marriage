# sincronia_1773.py -- alcance de sincronia do IDR 1773 no JM (plano 4, etapa 1).
# uso (da raiz): python tools/anchor/sincronia_1773.py <lista> <saida> [corte=34250] [procs=12]
#
# ATENCAO (medido em 2026-09-30): o QP sozinho deixa passar falsos -- MB sem
# residuo nao tem mb_qp_delta. Em IDRs integros com 1 bit plantado, 0-14% das
# trocas erradas chegam ao corte "em sincronia" (sincronia_valida.py). No 1773
# o unico par de 120 mil que chegava (59363224 5 + 59363619 6) lia as fileiras
# 57-59 a ~3 B/MB e a fileira 60 como cena: falso pela ancora de posicao (a
# fileira 60 e tarja pura e comeca 77-95 bytes antes do fim nos IDRs integros).
# Use junto com a ancora (CORTE_ALVO no avanco).
#
# O JM instrumentado (tools/jm_mbinfo.patch) esta compilado no scratchpad da
# sessao 7b294410; LDEC e CFG apontam para la.
#   lista: linhas "off bit [off bit ...] [mb N]" (offsets absolutos); "-" = so a base
# Para cada candidato: aplica as trocas no NAL (buffer ja com o patches.txt),
# corta a amostra no byte `corte` (relativo, com o prefixo de 4 bytes), roda o
# JM com JM_MBINFO e devolve o primeiro MB >= 6840 anomalo: QP != 20, I_PCM, ou
# o MB que nao foi lido, ou o que comeca alem do corte. Saida: "alcance byte_do_mb  off bit ...".
import sys, os, subprocess, tempfile, shutil, re
from concurrent.futures import ThreadPoolExecutor
R = r'C:\Users\chris\Documents\Workspace\fix-marriage'
os.chdir(R)
sys.path.insert(0, R + r'\tools'); sys.path.insert(0, R + r'\tools\anchor')
import f11
OLD = r'C:\Users\chris\AppData\Local\Temp\claude\C--Users-chris-Documents-Workspace-fix-marriage\7b294410-6095-41a4-88a5-a6b03a96b3ba\scratchpad'
LDEC = OLD + r'\JM\bin\ninja\gcc-mingw-16.1\x86_64\relwithdebinfo\ldecod.exe'
CFG = OLD + r'\JM\cfg\decoder.cfg'
SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821"); PPS = bytes.fromhex("68eb7352")
T, INICIO, QP = 1773, 6840, 20      # fileira 57: 0-56 exatas
d, ix = f11.buffer()
_, O, S, _ = ix[T]
TMPD = tempfile.gettempdir()          # uma pasta por decodificacao, apagada no fim


def alcance(trocas, corte):
    nal = bytearray(d[O + 4:O + corte])
    for off, bit in trocas:
        r = off - O - 4
        if 0 <= r < len(nal): nal[r] ^= 1 << bit
    st = b'\0\0\0\1' + SPS + b'\0\0\0\1' + PPS + b'\0\0\0\1' + bytes(nal)
    t = tempfile.mkdtemp(dir=TMPD)
    try:
        shutil.copy(CFG, t + r'\decoder.cfg'); open(t + r'\in.h264', 'wb').write(st)
        subprocess.run([LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=t, capture_output=True,
                       env=dict(os.environ, JM_MBINFO='mb.txt'), timeout=60)
        mbs = {}
        if os.path.exists(t + r'\mb.txt'):
            for l in open(t + r'\mb.txt'):
                p = l.split('|')[0].split()
                if len(p) >= 8: mbs.setdefault(int(p[0]), (int(p[1]), int(p[5]), int(p[6])))
    except subprocess.TimeoutExpired:
        mbs = {}
    finally:
        shutil.rmtree(t, ignore_errors=True)
    # o JM le zeros depois do fim do dado: MB que comeca alem do corte nao e
    # sincronia, e o fim do alcance
    for m in range(INICIO, 8160):
        if m not in mbs: return m, -1
        tipo, qp, pos = mbs[m]
        if pos >= corte - 4: return m, corte
        if qp != QP or tipo == 14: return m, pos
    return 8160, corte


def le_lista(p):
    out = []
    if p == '-': return [[]]
    for l in open(p):
        v = re.split(r'\s+', l.split('mb')[0].strip())
        if len(v) >= 2: out.append([(int(v[i]), int(v[i + 1])) for i in range(0, len(v) - 1, 2)])
    return out


if __name__ == '__main__':
    lista, saida = sys.argv[1], sys.argv[2]
    corte = int(sys.argv[3]) if len(sys.argv) > 3 else 34250
    procs = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    cands = le_lista(lista)
    with ThreadPoolExecutor(procs) as ex, open(saida, 'w') as f:
        for c, (m, pos) in zip(cands, ex.map(lambda c: alcance(c, corte), cands)):
            f.write('%d %d  %s\n' % (m, pos, ' '.join('%d %d' % x for x in c)))
