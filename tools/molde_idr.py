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

Os dois livres nao sao livres de verdade, e a primeira versao disto errou por
supor que fossem:

  idr_pic_id == ORDINAL DO IDR. Vale em 97 dos 100 cabecalhos limpos, uma vez
  que se contem os tres IDRs que perderam a marcacao no stss (2441, 2554,
  2913). Sem eles a regra parece uma funcao degrau nao monotonica, que foi o
  que me enganou.

  slice_qp_delta fica entre -17 e -2 nos 97, sempre negativo. Aqui se aceita
  [-20, 0].

Sem esses dois limites a minimizacao de Hamming acha minimo degenerado: o IDR
1595 recebeu idr_pic_id 16 onde a sequencia diz 60, e qp_delta 22, ou seja
QP 48 onde os integros ficam entre 9 e 24. Nove dos 25 primeiros consertos
sairam assim.

O patches.txt e append-only, mas isso nao impede corrigir: anexar o mesmo bit
de novo o inverte de volta. O alvo e o estado FINAL do arquivo, e o que se
anexa e a diferenca entre ele e o estado atual.

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

EXTRA = [2441, 2554, 2913]        # IDRs que perderam a marcacao no stss
MAX_BITS = 6                      # acima disto nao e conserto, e reescrever o cabecalho
QP_MIN, QP_MAX = -20, -1      # medido nos 97 limpos: -17 a -2; margem de 3 de cada lado

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
        f.seek(off + 4)
        d = bytearray(f.read(n))
        for k in range(n):
            for b in pt.get(off + 4 + k, []):
                d[k] ^= (1 << b)
        return "".join(f"{x:08b}" for x in d)

    idrs = sorted([t for t in ix if ix[t][2]] + EXTRA)
    novos, ruins, ambiguos = [], 0, 0

    for k, t in enumerate(idrs):
        real = bits(t)
        # 1) O cabecalho ja parseia para os dez campos provados? Entao esta
        #    correto, e nao se mexe -- mesmo que o idr_pic_id discorde do
        #    ordinal. A regra do ordinal tem tres contraexemplos limpos
        #    (1495, 1524, 1582) e nao derruba cabecalho consistente.
        # 2) A excecao e qp_delta fora da faixa empirica: QP 48 num quadro
        #    intra de um filme cujos outros 97 IDRs ficam entre QP 9 e 24 nao
        #    e valor plausivel, e ali vale reconsertar.
        livre = min((sum(1 for a, b in zip(cabecalho(pp, qq), real) if a != b), pp, qq)
                    for pp in range(300) for qq in range(-26, 26))
        if livre[0] == 0 and QP_MIN <= livre[2] <= QP_MAX:
            continue
        cands = sorted((sum(1 for a, b in zip(cabecalho(k, q), real) if a != b), q)
                       for q in range(QP_MIN, QP_MAX + 1))
        d, q = cands[0]
        if d == 0:
            continue
        ruins += 1
        if cands[1][0] == d:
            ambiguos += 1
            print(f"  [!] IDR {t}: {d} bits, empate em qp_delta -- nao anexar")
            continue
        if d > MAX_BITS:
            ambiguos += 1
            print(f"  [!] IDR {t}: {d} bits de {len(cabecalho(k, q))} -- dano alem do"
                  f" molde, seria inventar cabecalho, nao anexar")
            continue
        alvo, off = cabecalho(k, q), ix[t][0]
        n = 0
        for j, (a, b) in enumerate(zip(alvo, real)):
            if a != b:
                novos.append((off + 4 + j // 8, 7 - (j % 8))); n += 1
        marca = "  (stss perdido)" if t in EXTRA else ""
        print(f"  IDR {t}: {n} bits  idr_pic_id={k} qp_delta={q}{marca}")

    print(f"\n[+] {len(idrs)} IDRs, {len(idrs)-ruins} com cabecalho ja correto")
    print(f"[+] {ruins} a corrigir, {ambiguos} ambiguos (pulados)")
    print(f"[+] {len(novos)} bits a anexar")
    if novos:
        with open("output/novos_cabecalhos.txt", "w", newline="\n") as g:
            for o, b in novos:
                g.write(f"{o} {b}\n")
        print("[+] gravados em output/novos_cabecalhos.txt (NAO anexados)")

if __name__ == "__main__":
    main()
