# ocultacao.py -- quantos macroblocos o ffmpeg OCULTA em cada quadro, por GOP.
#
# "Decodifica sem erro de macrobloco" nao e "decodifica inteiro": o slice pode
# acabar cedo (end_of_slice = 1 antes do ultimo MB) e o ffmpeg oculta o resto
# sem reclamar. So a mensagem "concealing N" revela. Cada GOP vira um stream
# Annex B proprio; com -debug pict o ffmpeg imprime uma linha por slice, e a
# ocultacao do quadro sai logo depois da linha dele.
#
# uso: python ocultacao.py <scratch> <patches> <gop_ini> [<gop_ini> ...]
import re, subprocess, sys
sys.path.insert(0, 'ferramentas')
import cabecalho_slice as C

S, PT = sys.argv[1], sys.argv[2]
SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821")
PPS = bytes.fromhex("68eb7352")
IX = [tuple(map(int, l.split()[:4])) for l in open('index.txt') if l.strip()]
IDR = [t for t in range(len(IX)) if IX[t][3] and t != 1595]
d = bytearray(open(C.MP4, 'rb').read())
for l in open(PT):
    p = l.split()
    if len(p) >= 2 and p[0].isdigit():
        d[int(p[0])] ^= 1 << int(p[1])

for g in map(int, sys.argv[3:]):
    fim = min([i for i in IDR if i > g] + [len(IX)])
    out = b'\x00\x00\x00\x01' + SPS + b'\x00\x00\x00\x01' + PPS
    for t in range(g, fim):
        o, s = IX[t][1], IX[t][2]
        out += b'\x00\x00\x00\x01' + bytes(d[o + 4:o + s])
    p = '%s/gop_%d.h264' % (S, g)
    open(p, 'wb').write(out)
    log = subprocess.run(['ffmpeg', '-loglevel', 'repeat+debug', '-hide_banner', '-threads', '1',
                          '-debug', 'pict', '-i', p, '-f', 'null', '-'],
                         capture_output=True, text=True, errors='replace').stderr
    linhas = [l for l in log.split('\n') if re.search(r'\] (slice:|concealing|error while)', l)]
    ctx = re.match(r'\[h264 @ (\w+)\]', [l for l in linhas if 'slice:' in l][-1]).group(1)
    linhas = [l for l in linhas if ctx in l]
    quadros, k = {}, -1
    for l in linhas:
        if 'slice:' in l:
            k += 1
            quadros[g + k] = [0, '']
        elif k >= 0:
            m = re.search(r'concealing (\d+) DC', l)
            if m:
                quadros[g + k][0] = int(m.group(1))
            else:
                quadros[g + k][1] = 'erro'
    n = fim - g
    ruins = {t: v for t, v in quadros.items() if v[0] or v[1]}
    print('GOP %d-%d: %d linhas de slice para %d quadros | ocultados: %s'
          % (g, fim - 1, k + 1, n, ' '.join('%d(%d%s)' % (t, v[0], ',erro' if v[1] else '')
                                            for t, v in sorted(ruins.items())) or 'nenhum'))
