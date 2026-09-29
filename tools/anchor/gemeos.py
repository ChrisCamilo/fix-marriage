# gemeos.py -- os quadros B 10 e 12 recodificados inteiros pela sintaxe dos gemeos.
#
# No fade do GOP 0 os B nao-referencia repetem a sintaxe do irmao: o 10 e o 6
# sao todo pulados (8.160 MBs), o 12 e o 8 sao pulados na tarja e
# B_Direct_16x16 no campo, com CBP 16 e os mesmos niveis de DC de croma. Com o
# mesmo QP, o mesmo cabac_init e a mesma sintaxe, o CABAC gera os mesmos bits:
# o que difere do arquivo e dano. Diferenca de sintaxe faria o fluxo divergir
# de vez dali em diante; bit trocado e uma diferenca isolada e o fluxo volta a
# casar -- e e isso que aparece.
#
# Controle: o 6 recodificado como todo pulado bate com o arquivo em 0 bits; o
# cabac_p reproduz o 4, o 6 e o 8 do trace do JM bit a bit.
#
# O NAL esperado e o cabecalho do slice do arquivo + o slice data recodificado +
# cabac_zero_word ate fechar no tamanho do arquivo (como o f11.py nal); se
# nenhum numero de palavras fecha, a hipotese esta errada.
#
# As linhas 1.826 (147444 0) e 1.828 (150538 0) do patches.txt trocavam o
# escape 03 do byte 13 do NAL por 02: o `00 00 02` e proibido e o ffmpeg corta
# o NAL ali, e o B vira todo pulado -- a interpolacao das referencias, sem ler
# os dados (a armadilha do 2361). A comparacao e feita com elas desfeitas.
#
# NAO escreve no patches.txt.
#
# uso (da raiz):
#   python tools/anchor/gemeos.py proposta <saida.txt>
#     `offset bit` das trocas do 10 e do 12, e as duas linhas a retirar
import sys, os
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI); sys.path.insert(0, os.path.dirname(AQUI))
import cabac_p as C, f11
import cabecalho_slice as CS

SKIP = [('mb_skip_flag', (0,)), ('end_of_slice_flag', (0,))]
RETIRAR = ((147444, 0), (150538, 0))          # linhas 1.826 e 1.828: o escape 03 -> 02
QUADROS = {10: ('todo pulado, como o gemeo 6', lambda: {m: SKIP for m in range(8160)}),
           12: ('a sintaxe do gemeo 8 (POC 14, trace do JM)', lambda: f11.sintaxe(14))}


def nal_esperado(t, sint, tipo='B', sem=RETIRAR):
    """NAL recodificado de um quadro e as diferencas para o arquivo.

      t     quadro (indice do index.txt)
      sint  {mb: sintaxe} dos 8.160 MBs
      tipo  tipo do slice
      sem   trocas do patches.txt desfeitas antes de comparar

    Devolve: (nal do arquivo, nal esperado, fecha no tamanho, [(offset, bit, byte do NAL)])."""
    d, ix = f11.buffer()
    for o_, b_ in sem: d[o_] ^= 1 << b_
    o, s = ix[t][1], ix[t][2]; nal = bytes(d[o + 4:o + s])
    h = CS.le_cabecalho(nal)
    rb, _ = f11.rbsp(nal)
    bits = rb[:h['bits']] + ''.join(map(str, C.codifica(sint, 26 + h['qp_delta'], h.get('cabac_init', 0), tipo=tipo)))
    bits += '0' * (-len(bits) % 8)
    base = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    for k in range(5000):
        cand = f11._escapa(base + b'\x00\x00' * k)
        if len(cand) >= len(nal): break
    trocas = [(o + 4 + i, bit, i) for i, (a, b) in enumerate(zip(nal, cand)) for bit in range(8) if (a ^ b) >> bit & 1]
    return nal, cand, len(cand) == len(nal), trocas


def proposta(saida):
    """Grava a proposta (trocas do 10 e do 12 e as linhas a retirar) e devolve {quadro: n de trocas}."""
    res = {}
    with open(saida, 'w', newline='\n') as f:
        f.write('# gemeos: os B 10 e 12 recodificados pela sintaxe dos gemeos (tools/anchor/gemeos.py) -- PROPOSTA\n')
        f.write('# RETIRAR do patches.txt: 147444 0 (linha 1.826) e 150538 0 (linha 1.828) -- escape 03 -> 02\n')
        for t, (desc, sint) in QUADROS.items():
            nal, cand, fecha, tr = nal_esperado(t, sint())
            if not fecha: raise ValueError('o NAL do %d nao fecha no tamanho do arquivo' % t)
            f.write('# quadro %d: %s -- %d bits de %d (%.2f%%), NAL fecha em %d bytes\n' % (t, desc, len(tr), 8 * len(nal), 100 * len(tr) / (8 * len(nal)), len(nal)))
            for o_, b_, i in tr: f.write('%d %d\n' % (o_, b_))
            res[t] = len(tr)
    return res


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'proposta': print(proposta(sys.argv[2]))
    else: print(open(__file__).read().split('\nimport')[0])
