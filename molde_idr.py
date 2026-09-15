"""molde_idr.py - conserta cabecalho de slice de IDR por prova, nao por busca.

O cabecalho de um IDR neste arquivo tem 12 campos e DEZ sao constantes, medidos
nos 7 IDRs que decodificam:

    first_mb=0  slice_type=7  pps_id=0  frame_num=0  poc_lsb=0
    no_output_prior=0  long_term_ref=0  deblk_idc=0  alpha_div2=-1  beta_div2=-1

Sobram dois livres: idr_pic_id e slice_qp_delta. Logo o cabecalho inteiro se
gera a partir de dois numeros, e o gerador foi validado reproduzindo BIT A BIT
os 7 cabecalhos integros.

Isto NAO e varredura. A armadilha 19 diz que gabarito responde sim ou nao; aqui
o gabarito sao os dez campos constantes, e o que se procura e qual par
(idr_pic_id, qp_delta) produz a string de bits mais proxima do que esta no
arquivo. O decoder nem entra: e aritmetica de bitstream.

Sobre o idr_pic_id: ele acompanha o ordinal do IDR com um desvio que varia de 0
a 3 de forma NAO monotonica, entao a regra nao esta entendida e o campo nao e
tratado como provado. Da para viver com isso: o decoder nao usa o valor, so
exige que difira entre IDRs consecutivos. O que importa dele e o comprimento,
que desloca os campos seguintes.

  python molde_idr.py <orig.mp4> <index.txt> <patches.txt>

Nao escreve patch: lista o que deveria ser anexado.
"""
import sys

def ue(v):
    z = (v + 1).bit_length() - 1
    return "0" * z + bin(v + 1)[2:]

def se(v):
    return ue(2 * v - 1 if v > 0 else -2 * v)

def cabecalho(idr_pic_id, qp_delta):
    """A string de bits exata de um cabecalho de slice de IDR deste arquivo."""
    return ("01100101"          # NAL 0x65: ref_idc 3, tipo 5
            + ue(0)             # first_mb_in_slice
            + ue(7)             # slice_type = I
            + ue(0)             # pic_parameter_set_id
            + "0" * 8           # frame_num = 0 (log2_max_frame_num = 8)
            + ue(idr_pic_id)
            + "0" * 8           # poc_lsb = 0 (log2_max_poc_lsb = 8)
            + "00"              # no_output_of_prior_pics, long_term_reference
            + se(qp_delta)
            + ue(0)             # disable_deblocking_filter_idc = 0
            + se(-1) + se(-1))  # slice_alpha_c0_offset_div2, slice_beta_offset_div2

def main():
    mp4, f_ix, f_pt = sys.argv[1], sys.argv[2], sys.argv[3]
    ix = {}
    for l in open(f_ix):
        i, off, size, idr = map(int, l.split())
        ix[i] = (off, size, idr)
    pt = {}
    for l in open(f_pt):
        o, b = l.split()
        pt.setdefault(int(o), []).append(int(b))
    f = open(mp4, "rb")

    def bits(t, n=14):
        off = ix[t][0]
        f.seek(off + 4)                      # pula o prefixo AVCC de 4 bytes
        d = bytearray(f.read(n))
        for k in range(n):
            for b in pt.get(off + 4 + k, []):
                d[k] ^= (1 << b)
        return "".join(f"{x:08b}" for x in d)

    grade = [(p, q) for p in range(256) for q in range(-26, 26)]
    idrs = [t for t in sorted(ix) if ix[t][2]]
    novos, danificados, ambiguos = [], 0, 0

    for t in idrs:
        real = bits(t)
        cands = sorted((sum(1 for a, b in zip(cabecalho(p, q), real) if a != b), p, q)
                       for p, q in grade)
        d, p, q = cands[0]
        if d == 0:
            continue
        danificados += 1
        if cands[1][0] == d:
            ambiguos += 1
            print(f"  [!] IDR {t}: {d} bits, mas ha empate -- nao anexar")
            continue
        alvo, off = cabecalho(p, q), ix[t][0]
        for k, (a, b) in enumerate(zip(alvo, real)):
            if a != b:
                novos.append((off + 4 + k // 8, 7 - (k % 8)))
        print(f"  IDR {t}: {d} bits  idr_pic_id={p} qp_delta={q}")

    print(f"\n[+] {len(idrs)} IDRs, {len(idrs)-danificados} com cabecalho ja correto")
    print(f"[+] {danificados} danificados, {ambiguos} ambiguos (pulados)")
    print(f"[+] {len(novos)} bits a anexar")
    if novos:
        with open("novos_cabecalhos.txt", "w", newline="\n") as g:
            for o, b in novos:
                g.write(f"{o} {b}\n")
        print("[+] gravados em novos_cabecalhos.txt (NAO anexados)")

if __name__ == "__main__":
    main()
