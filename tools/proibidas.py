"""proibidas.py -- sequencias 00 00 0x (x < 3) dentro dos NALs, com o patches.txt aplicado.

Dentro de um NAL, 00 00 00, 00 00 01 e 00 00 02 sao proibidos: o codificador
escapa com 03 tudo o que viraria isso, inclusive o cabac_zero_word. O ffmpeg
CORTA o NAL na primeira delas, sem erro -- e o que vem depois nao e lido. Num
B isso produz um quadro todo pulado, a interpolacao das referencias, que num
trecho liso parece perfeita (armadilha 62: os frames 10 e 12, e o 263, o 1638
e o 2459 "fechados" pelo lote da cabeca). A classe foi medida em 2026-09-18
(armadilha 27, revista): decide-se caso a caso; esta ferramenta e a guarda
para rodar depois de todo lote.

Para cada sequencia: quadro, tipo, byte no NAL, onde acabam os dados (antes do
enchimento 00 00 03 do fim), se o arquivo cru ja a tinha ou se um patch a
criou (e quais linhas do patches.txt tocam os 3 bytes), e o estado do quadro
no mapa e no serie, se as saidas forem dadas.

    python tools/proibidas.py [mapa.txt] [serie.txt]

Sinais de alarme: sequencia CRIADA POR PATCH (o lote novo precisa do escape
seguinte) e quadro inteiro no mapa com a sequencia antes do fim dos dados (o
"inteiro" e o corte). So le; nao escreve patch.
"""
import collections
import sys

sys.path.insert(0, 'tools')
import cabecalho_slice as CS

MP4 = 'Caio & Lizandra - Making- Caio-Balu.mp4'


def estado(caminho, campos):
    """{quadro: valor} de uma saida do reparador (mapa: 5 colunas, MB de parada; serie: 2 colunas)."""
    out = {}
    if not caminho: return out
    for l in open(caminho):
        x = l.split()
        if len(x) == campos and x[0].isdigit(): out[int(x[0])] = x[2] if campos == 5 else x[1]
    return out


def varre():
    """Todas as sequencias proibidas do filme remendado.

    Devolve: lista de (quadro, tipo, byte, tamanho do NAL, fim dos dados,
    sequencia, sequencia no cru, ja estava no cru, [linhas do patches.txt])."""
    ix = [tuple(map(int, l.split()[:4])) for l in open('index.txt') if l.strip()]
    raw = open(MP4, 'rb').read(); d = bytearray(raw)
    linhas = collections.defaultdict(list)
    for n, l in enumerate(open('patches.txt'), 1):
        p = l.split()
        if len(p) >= 2 and p[0].isdigit():
            d[int(p[0])] ^= 1 << int(p[1]); linhas[int(p[0])].append(n)
    out = []
    for t, off, sz, _ in ix:
        nal = bytes(d[off + 4:off + sz]); nr = raw[off + 4:off + sz]
        try: tipo = 'PBI'[CS.le_cabecalho(nal)['slice_type'] % 5]
        except CS.Invalido: tipo = '?'
        k = len(nal)
        while k >= 3 and nal[k - 3:k] == b'\x00\x00\x03': k -= 3
        for i in range(len(nal) - 2):
            if nal[i] == 0 and nal[i + 1] == 0 and nal[i + 2] < 3:
                cru = nr[i] == 0 and nr[i + 1] == 0 and nr[i + 2] < 3
                toc = sorted(set(n for j in range(i, i + 3) for n in linhas.get(off + 4 + j, [])))
                out.append((t, tipo, i, len(nal), k, nal[i:i + 3].hex(), nr[i:i + 3].hex(), cru, toc))
    return out


def main():
    mapa = estado(sys.argv[1] if len(sys.argv) > 1 else None, 5)
    serie = estado(sys.argv[2] if len(sys.argv) > 2 else None, 2)
    res = varre()
    print('%d sequencias proibidas em %d quadros' % (len(res), len({r[0] for r in res})))
    print('%d criadas por patch; %d em quadro inteiro no mapa' % (
        sum(1 for r in res if not r[7]), sum(1 for r in res if mapa.get(r[0]) == '8160')))
    print('\nquadro tipo byte/tamanho fim_dados sequencia (cru) linhas mapa serie')
    for t, tipo, i, n, k, s, sr, cru, toc in res:
        marca = ('  <== CRIADA POR PATCH' if not cru else '') + ('  <== inteiro no mapa' if mapa.get(t) == '8160' else '')
        print('%5d %s %6d/%-6d %6d %s (%s) %s %s %s%s' % (t, tipo, i, n, k, s, sr, toc or '-', mapa.get(t, ''), serie.get(t, ''), marca))


if __name__ == '__main__':
    main()
