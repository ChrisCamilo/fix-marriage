"""molde_slice.py - conserta cabecalho de frame COMUM por prova, nao por busca.

Irmao do molde_idr.py, para os quadros que nao sao IDR. A parte provavel do
cabecalho sao os 15 primeiros bits, e eles bastam para corrigir o erro mais
comum do arquivo:

    byte NAL (8)  first_mb_in_slice (1)  slice_type (5)  pic_parameter_set_id (1)

Tres invariantes, medidos nos 160 frames cuja tarja prova que decodificam
certo, com ZERO excecao:

    first_mb_in_slice = 0        um slice por quadro no filme inteiro
    pic_parameter_set_id = 0     so existe um PPS
    slice_type:  ref_idc != 0 -> 5 (P)      ref_idc == 0 -> 6 (B)

O slice_type tem comprimento fixo de 5 bits nos dois casos (ue(5) = 00110,
ue(6) = 00111), entao os 15 bits ficam em posicao fixa e o frame_num e o
poc_lsb, que vem depois, nao entram na comparacao. Isso e limitacao e nao
descuido: eles variam por quadro e nao ha invariante que os fixe sem conhecer a
cadencia do GOP, que muda ao longo do filme.

QUANDO NAO SE APLICA: se o ref_idc do quadro nao e 0 nem 2, ele proprio esta
corrompido -- os 160 frames verificados so usam esses dois valores em quadro
comum. Prever slice_type a partir de ref_idc errado seria construir sobre
dado falso, entao esses ficam de fora.

  python molde_slice.py <orig.mp4> <index.txt> <patches.txt> <panorama.txt>

Nao escreve patch: lista o que deveria ser anexado.
"""
import sys

MAX_BITS = 3          # acima disto nao e conserto de campo, e reescrever


def ue(v):
    z = (v + 1).bit_length() - 1
    return "0" * z + bin(v + 1)[2:]


def cabecalho15(nal_byte, slice_type):
    """Os 15 primeiros bits do cabecalho de um frame comum."""
    return f"{nal_byte:08b}" + ue(0) + ue(slice_type) + ue(0)


def main():
    mp4, f_ix, f_pt, f_pano = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    ix = {}
    for l in open(f_ix):
        i, off, size, idr = map(int, l.split())
        ix[i] = (off, size, idr)
    pt = {}
    for l in open(f_pt):
        o, b = l.split()
        pt.setdefault(int(o), []).append(int(b))
    f = open(mp4, "rb")

    def le(t):
        off = ix[t][0]
        f.seek(off + 4)
        d = bytearray(f.read(3))
        for k in range(3):
            for b in pt.get(off + 4 + k, []):
                d[k] ^= (1 << b)
        return "".join(f"{x:08b}" for x in d)[:15], d[0]

    # Conferencia obrigatoria: nos frames que a tarja prova corretos, o molde
    # tem que dar distancia 0. Se acusar um deles, a regra esta errada e nao se
    # ajusta a referencia -- corrige-se a regra.
    verificados = []
    for l in open(f_pano):
        p = l.split()
        if p[0] == "frame":
            continue
        t = int(p[0])
        if abs(float(p[3]) - 16) < 0.01 and float(p[4]) < 0.01:
            verificados.append(t)

    falsos = 0
    for t in verificados:
        real, nb = le(t)
        if (nb & 0x1f) == 5:
            continue
        ref = (nb >> 5) & 3
        if ref not in (0, 2):
            continue
        alvo = cabecalho15(nb, 5 if ref else 6)
        if alvo != real:
            falsos += 1
            print(f"  [!] frame {t} tem tarja correta e o molde o acusa -- REGRA ERRADA")
    print(f"[+] conferencia: {len(verificados)} frames verificados, "
          f"{falsos} acusados indevidamente")
    if falsos:
        raise SystemExit("[!] abortando: a regra nao pode acusar quadro comprovadamente bom")

    novos, fora_ref, grandes, ok = [], 0, 0, 0
    for t in sorted(ix):
        real, nb = le(t)
        if (nb & 0x1f) == 5:
            continue                      # IDR e o molde_idr.py
        ref = (nb >> 5) & 3
        if ref not in (0, 2):
            fora_ref += 1                 # o proprio ref_idc esta corrompido
            continue
        alvo = cabecalho15(nb, 5 if ref else 6)
        dif = [k for k, (a, b) in enumerate(zip(alvo, real)) if a != b]
        if not dif:
            ok += 1
            continue
        if len(dif) > MAX_BITS:
            grandes += 1
            continue
        off = ix[t][0]
        for k in dif:
            novos.append((off + 4 + k // 8, 7 - (k % 8)))

    print(f"[+] {ok} frames comuns com os 15 bits ja corretos")
    print(f"[+] {fora_ref} pulados: ref_idc fora de 0 e 2, corrompido ele mesmo")
    print(f"[+] {grandes} pulados: mais de {MAX_BITS} bits, seria reescrever")
    print(f"[+] {len(novos)} bits a anexar")
    if novos:
        with open("saidas/novos_slices.txt", "w", newline="\n") as g:
            for o, b in novos:
                g.write(f"{o} {b}\n")
        print("[+] gravados em saidas/novos_slices.txt (NAO anexados)")


if __name__ == "__main__":
    main()
