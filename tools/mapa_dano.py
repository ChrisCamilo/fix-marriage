"""mapa_dano.py -- onde o arquivo esta danificado, medido em conteudo CONHECIDO.

Duas reguas, as duas lidas do MP4 ORIGINAL (so leitura), para contar o dano
total e nao o que sobrou depois do patches.txt:

  1. O enchimento cabac_zero_word no fim dos NALs, que pela norma e 00 00 03
     repetido. Existe em poucos quadros (fades), mas cada um da uma taxa local
     de bits trocados. O padrao escorrega de fase no meio (ha um `33 e0` que o
     proprio codificador escreve), entao a contagem usa a melhor fase por bloco
     de 24 bytes. Os 2 ultimos bytes do NAL ficam de fora: em varios quadros eles
     nao sao enchimento, e isso se repete -- e estrutura, nao dano.

  2. Os 5 primeiros bytes de cada quadro (prefixo AVCC + cabecalho do NAL), que
     tem conteudo 100% conhecido e sao exatamente onde caem os 1.338 patches
     deterministicos. Cobre o filme inteiro.

    python tools/mapa_dano.py
"""
import bisect
from collections import Counter
from math import comb

MP4 = 'Caio & Lizandra - Making- Caio-Balu.mp4'
IX = [tuple(map(int, l.split()[:3])) for l in open('index.txt') if l.strip()]
P = [tuple(map(int, l.split()[:2])) for l in open('patches.txt')
     if l.strip() and l[0].isdigit()]
N_DET = 1338          # os primeiros N_DET do patches.txt sao a base deterministica


def enchimento(nal):
    """Inicio do enchimento no fim do NAL, ou None.

      nal  bytes do NAL, sem o prefixo AVCC

    Devolve: o indice do primeiro `00 00` do enchimento, ou None se o NAL nao
    termina em enchimento. Anda de tras para frente em blocos de 24 bytes
    enquanto o bloco erra menos de 15% dos bits contra 00 00 03 -- dado CABAC
    real erra ~50%, a separacao e larga.
    """
    n = len(nal)
    melhor = None
    for fase in range(3):
        pat = lambda i: 3 if (n - 1 - i + fase) % 3 == 0 else 0
        ini = n
        while ini - 24 >= 0 and sum(bin(nal[i] ^ pat(i)).count('1')
                                    for i in range(ini - 24, ini)) <= 24 * 8 * 0.15:
            ini -= 24
        if ini < n - 24 and (melhor is None or ini < melhor):
            melhor = ini
    if melhor is None:
        return None
    # O inicio exato: o primeiro `00 00 03` cujos 24 bytes seguintes ficam a no
    # maximo 8 bits do padrao. Sem isto o dado que vem antes, que tambem pode ter
    # `00 00 03` por prevencao de emulacao, entra na conta como dano.
    i = max(0, melhor - 8)
    while True:
        i = nal.find(b'\x00\x00\x03', i)
        if i < 0 or i >= len(nal) - 26:
            return None
        if bits_fora(nal, i, i + 24) <= 8:
            return i
        i += 1


def bits_fora(nal, ini, fim):
    """Bits que diferem de 00 00 03, com a melhor fase por bloco de 24 bytes.

      nal       bytes do NAL
      ini, fim  a faixa [ini, fim) a medir

    Devolve: o numero de bits fora do padrao.
    """
    b = 0
    for a in range(ini, fim, 24):
        z = min(fim, a + 24)
        b += min(sum(bin(nal[k] ^ (3 if (k - a + ph) % 3 == 2 else 0)).count('1')
                     for k in range(a, z)) for ph in range(3))
    return b


f = open(MP4, 'rb')
print('1. ENCHIMENTO (00 00 03) -- bits trocados, MP4 original')
print('   quadro   offset MB   bytes   bits    taxa')
for t, off, tam in IX:
    f.seek(off + 4)
    nal = f.read(tam - 4)
    i = enchimento(nal)
    if i is None or len(nal) - 2 - i < 30:
        continue
    fim = len(nal) - 2
    b = bits_fora(nal, i, fim)
    print('   %5d   %9.3f   %5d   %4d   %5.2f%%' % (t, off / 1e6, fim - i, b, 100 * b / (8 * (fim - i))))

print()
print('2. OS 5 PRIMEIROS BYTES DE CADA QUADRO (40 bits conhecidos)')
offs = [o for _, o, _ in IX]
por_q = Counter()
for o, _ in P[:N_DET]:
    k = bisect.bisect_right(offs, o) - 1
    assert o - IX[k][1] < 5, 'patch deterministico fora dos 5 bytes: %d' % o
    por_q[k] += 1
hist = Counter(por_q[k] for k in range(len(IX)))
n = len(IX)
r = N_DET / (n * 40)
print('   %d bits trocados em %d quadros -> %.2f%% dos bits' % (N_DET, n, 100 * r))
print('   trocas/quadro   observado   se fosse uniforme (binomial 40, %.2f%%)' % (100 * r))
for k in range(0, 7):
    esp = n * comb(40, k) * r ** k * (1 - r) ** (40 - k)
    print('        %d          %5d         %7.1f' % (k, hist[k], esp))
