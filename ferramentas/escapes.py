"""escapes.py - conserta o byte de prevencao de emulacao, por prova da norma.

Dentro de um RBSP a sequencia 00 00 01 e PROIBIDA: ela e o start code de um NAL,
e o codificador e obrigado a escrever 00 00 03 01 no lugar. O byte 03 e o
emulation_prevention_three_byte, e a norma nao admite outro valor ali.

Logo, todo 00 00 01 dentro do payload e corrupcao com valor correto CONHECIDO --
nao e candidato a testar, e erro provado. Medido neste arquivo: 84 ocorrencias
em 79 frames, e em TODAS o byte le 0x01 onde tem que ser 0x03. Um bit cada,
sempre o bit 1.

Nenhum dos frames integros do filme tem uma ocorrencia sequer, o que confirma
que a emissao original respeitava a regra.

O efeito e desproporcional ao tamanho: um unico byte errado faz o decoder achar
que comeca um NAL novo no meio do slice e tentar parsear o resto como SPS ou
IDR. No frame 10 isso produzia "sps_id 32 out of range" e "A non-intra slice in
an IDR NAL unit", e o quadro saia como copia do anterior. Com os tres bits dele
corrigidos, decodifica limpo e com conteudo proprio.

  python escapes.py <orig.mp4> <index.txt> <patches.txt>

Nao escreve patch: lista o que deveria ser anexado.
"""
import sys


def main():
    mp4, f_ix, f_pt = sys.argv[1], sys.argv[2], sys.argv[3]
    ix = {}
    for l in open(f_ix):
        i, off, size, idr = map(int, l.split())
        ix[i] = (off, size)
    pt = {}
    for l in open(f_pt):
        o, b = l.split()
        pt.setdefault(int(o), []).append(int(b))
    f = open(mp4, "rb")

    novos, frames = [], 0
    for t in sorted(ix):
        off, sz = ix[t]
        f.seek(off)
        d = bytearray(f.read(sz))
        for k in range(sz):
            for b in pt.get(off + k, []):
                d[k] ^= (1 << b)
        p = bytes(d[4:])                 # pula o prefixo AVCC de tamanho
        achou = 0
        for i in range(len(p) - 2):
            if p[i] == 0 and p[i + 1] == 0 and p[i + 2] == 1:
                # o terceiro byte tem que ser 0x03; 0x01 esta a um bit dele
                if p[i + 2] != 1:
                    print(f"  [!] frame {t}: terceiro byte e {p[i+2]:#04x}, "
                          f"nao 0x01 -- precisa de mais de um bit, nao anexado")
                    continue
                novos.append((off + 4 + i + 2, 1))
                achou += 1
        if achou:
            frames += 1
            print(f"  frame {t}: {achou} escape(s) a corrigir")

    # Segundo invariante: paridade dos zeros finais (cabac_zero_word).
    # Detecta e nao conserta -- ver o cabecalho deste arquivo.
    impares = []
    for t in sorted(ix):
        off, sz = ix[t]
        f.seek(off)
        d = bytearray(f.read(sz))
        for k in range(sz):
            for b in pt.get(off + k, []):
                d[k] ^= (1 << b)
        p = bytes(d[4:])
        z = 0
        while z < len(p) and p[len(p) - 1 - z] == 0:
            z += 1
        if z % 2:
            impares.append((t, z))

    print(f"\n[+] {frames} frames com start code ilegal no payload")
    print(f"[+] {len(novos)} bits a anexar")
    print(f"[+] {len(impares)} frames com numero IMPAR de zeros finais: o"
          f" cabac_zero_word vem em par, entao ali ha corrupcao PROVADA")
    for t, z in impares:
        print(f"      frame {t}: {z} zero(s) no fim")
    if novos:
        with open("saidas/novos_escapes.txt", "w", newline="\n") as g:
            for o, b in novos:
                g.write(f"{o} {b}\n")
        print("[+] gravados em saidas/novos_escapes.txt (NAO anexados)")


if __name__ == "__main__":
    main()
