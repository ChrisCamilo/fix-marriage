# ancora_py.py -- prepara a entrada do ancora.exe a partir do NAL e do JM_MBINFO.
import sys, os, subprocess, tempfile, shutil
N = os.path.dirname(os.path.abspath(__file__)); sys.argv = [sys.argv[0], N, '0'] + sys.argv[1:]
sys.path.insert(0, N)
import calib_enc as CE
def rbsp_bits(nal):
    out = bytearray(); z = 0
    for b in nal:
        if z >= 2 and b == 3: z = 0; continue
        out.append(b); z = z + 1 if b == 0 else 0
    s = ''.join(format(b, '08b') for b in out)
    return s[:s.rindex('1') + 1]
def mbinfo_ctx(stream):
    t = tempfile.mkdtemp(dir=N); shutil.copy(CE.CFG, t + '/decoder.cfg'); open(t + '/in.h264', 'wb').write(stream)
    subprocess.run([CE.LDEC, '-i', 'in.h264', '-o', 'out.yuv'], cwd=t, capture_output=True, env=dict(os.environ, JM_MBINFO='mb.txt'))
    out = {}
    for l in open(t + '/mb.txt'):
        a, b = l.split('|'); out[int(a.split()[0])] = list(map(int, b.split()))
    shutil.rmtree(t, ignore_errors=True); return out
def roda(nal, ctx, entrada, nbits=600, omax=8, top=5):
    col0 = entrada % 120; nmb = 8160 - entrada
    cv = [ctx[i] for i in (2, 4, 5, 7, 8, 11, 15, 19)]; cc = [ctx[1], ctx[20]]
    bits = rbsp_bits(nal)[-nbits:]
    inp = '%d %d\n%s\n%s\n%d %s\n' % (nmb, col0, ' '.join(map(str, cv)), ' '.join(map(str, cc)), len(bits), bits)
    r = subprocess.run([N + '/ancora.exe'], input=inp, capture_output=True, text=True, env=dict(os.environ, OMAX=str(omax), TOP=str(top)))
    return r.stdout
if __name__ == '__main__':
    t = int(sys.argv[3]); entrada = int(sys.argv[4])
    o, s = CE.IX[t][1], CE.IX[t][2]; nal = bytes(CE.d[o+4:o+s])
    ctx = mbinfo_ctx(CE.stream_de(nal))[entrada]
    print('IDR', t, 'entrada MB', entrada, 'contextos', [ctx[i] for i in (2, 4, 5, 7, 8, 11, 15, 19)])
    print(roda(nal, ctx, entrada))
