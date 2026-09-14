"""cabecalhos.py - acha erro de cabecalho com prova aritmetica.

Todo criterio do reparador.c e proxy visual ("isto parece imagem?"), e o ARMADILHAS.md
conta como a busca aprendeu a burlar cinco deles seguidos. Os campos
do slice header nao sao proxy: dada a posicao do frame no GOP, a norma fixa
frame_num e o encoder fixa poc_lsb. Erro neles se demonstra por aritmetica.

O modelo e POSICIONAL, e essa e a razao de funcionar: as posicoes dos IDR saem do
moov, que esta intacto. Nada aqui depende de byte corrompido. Encadear frame_num
ao longo do arquivo NAO funciona -- foi tentado e deu 504 falsos, porque os
ref_idc que alimentariam a cadeia tambem estao corrompidos.

Cadencia medida no arquivo, periodo 4:  IDR, P, B, B, B, P, B, B, B, ...
  posicao 0      -> IDR          frame_num 0        poc_lsb 0
  posicao 4k-3   -> P (k-esimo)  frame_num k        poc_lsb 8k
  posicao 4k-2+r -> B (r=0,1,2)  frame_num k+1      poc_lsb 8(k-1)+2+2r

Nao decodifica nada e nao escreve patch: so lista candidatos e localiza o bit.

  python cabecalhos.py <orig.mp4> <index.txt> <patches.txt> [estado_frames.txt]
"""
import sys

SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821")

class BR:
    def __init__(s, d): s.d = d; s.p = 0
    def u1(s):
        if s.p >= len(s.d) * 8: raise ValueError("fim do buffer")
        b = (s.d[s.p >> 3] >> (7 - (s.p & 7))) & 1; s.p += 1; return b
    def u(s, n):
        v = 0
        for _ in range(n): v = (v << 1) | s.u1()
        return v
    def ue(s):
        z = 0
        while s.u1() == 0:
            z += 1
            if z > 32: raise ValueError("ue fora de faixa")
        return (1 << z) - 1 + s.u(z) if z else 0

def rbsp_map(b):
    """tira o escape 00 00 03 e devolve o mapa indice_rbsp -> indice_bruto"""
    o = bytearray(); m = []; i = 0
    while i < len(b):
        if i + 2 < len(b) and b[i] == 0 and b[i+1] == 0 and b[i+2] == 3:
            o += b[i:i+2]; m += [i, i+1]; i += 3
        else:
            o.append(b[i]); m.append(i); i += 1
    return bytes(o), m

def le_sps():
    r = BR(rbsp_map(SPS[1:])[0])
    r.u(8); r.u(8); r.u(8); r.ue()
    log2fn = r.ue() + 4
    poc_type = r.ue()
    log2poc = r.ue() + 4 if poc_type == 0 else 0
    return log2fn, poc_type, log2poc

def le_header(payload, log2fn, log2poc):
    pl, mapa = rbsp_map(payload)
    nh = pl[0]
    r = BR(pl[1:])
    h = {"nal": nh, "ref_idc": (nh >> 5) & 3, "tipo": nh & 0x1f, "mapa": mapa}
    r.ue()
    h["st"] = r.ue()
    r.ue()
    h["bit_fn"] = 8 + r.p
    h["frame_num"] = r.u(log2fn)
    if h["tipo"] == 5: r.ue()
    h["bit_poc"] = 8 + r.p
    h["poc_lsb"] = r.u(log2poc)
    return h

def varre_fn(seq, largura):
    """Passada para frente sobre os frame_num de um GOP, em ordem de
    decodificacao. A norma diz: comeca em 0 no IDR e sobe no maximo 1 por frame
    (so referencia incrementa). Nao supoe cadencia -- serve para qualquer
    estrutura de GOP, que neste arquivo varia.

    So acusa quem VIOLA. Valor observado dentro de {base, base+1} e aceito e
    vira a nova base; nada e proposto para ele. Isso e o que impedia a versao
    anterior de acusar 55 frames que decodificam perfeitamente.

    Devolve {indice: valor_proposto} so onde a proposta e unica."""
    base = 0
    out = {}
    for j, obs in enumerate(seq):
        if j == 0:
            base = 0
            continue
        if obs is not None and obs in (base, base + 1) and obs <= j:
            base = obs
            continue
        if obs is None: continue
        vs = [v for v in (base, base + 1)
              if v <= j and bin(v ^ obs).count("1") == 1]
        if len(vs) == 1:
            out[j] = vs[0]
            base = vs[0]        # segue com o valor corrigido
    return out

def cands_poc(vals, j, largura):
    """Os N frames de um GOP tem que ocupar exatamente {0,2,...,2(N-1)}: e a
    ordem de exibicao, uma permutacao. Independe de cadencia. Um valor fora do
    conjunto so e candidato se dista 1 bit de um valor FALTANDO, e se esse for
    o unico jeito de fechar o conjunto."""
    n = len(vals)
    esperados = set(range(0, 2*n, 2))
    presentes = [v for v in vals if v is not None]
    faltando = esperados - set(presentes)
    obs = vals[j]
    if obs is None or obs in esperados: return []
    if presentes.count(obs) != 1: return []
    return [f for f in sorted(faltando) if bin(f ^ obs).count("1") == 1]

def localiza(h, bit_campo, largura, k, off_nal):
    """bit k (0 = menos significativo) de um campo -> (offset absoluto, bit)"""
    bit_pl = bit_campo + (largura - 1 - k)
    return off_nal + 4 + h["mapa"][bit_pl // 8], 7 - (bit_pl % 8)

def main():
    mp4, f_ix, f_pt = sys.argv[1], sys.argv[2], sys.argv[3]
    f_est = sys.argv[4] if len(sys.argv) > 4 else None
    log2fn, poc_type, log2poc = le_sps()

    ix = [tuple(map(int, l.split())) for l in open(f_ix)]
    d = bytearray(open(mp4, "rb").read())
    npt = 0
    for l in open(f_pt):
        o, b = l.split(); d[int(o)] ^= 1 << int(b); npt += 1
    d = bytes(d)
    print("[+] %d patches aplicados antes de ler os cabecalhos" % npt)

    estado = {}
    if f_est:
        for l in open(f_est):
            c = l.split()
            if len(c) == 4 and c[0].isdigit(): estado[int(c[0])] = c[1]
        print("[+] estado de %d frames carregado" % len(estado))

    # agrupa por GOP
    gops, atual = [], None
    for i, off, size, idr in ix:
        if idr:
            if atual: gops.append(atual)
            atual = []
        if atual is not None: atual.append((i, off, size))
    if atual: gops.append(atual)

    cands, flags_bons = [], []
    for g in gops:
        H = []
        for i, off, size in g:
            try: H.append(le_header(d[off+4:off+4+min(size-4, 64)], log2fn, log2poc))
            except Exception: H.append(None)
        fns  = [h["frame_num"] if h else None for h in H]
        pocs = [h["poc_lsb"]   if h else None for h in H]
        prop_fn = varre_fn(fns, log2fn)
        for j, (i, off, size) in enumerate(g):
            h = H[j]
            if h is None: continue
            bom = estado.get(i) in ("real", "propagado", "uniforme")
            for campo, lista, bitc, larg in (
                    ("fn",  [prop_fn[j]] if j in prop_fn else [],  h["bit_fn"],  log2fn),
                    ("poc", cands_poc(pocs, j, log2poc), h["bit_poc"], log2poc)):
                if len(lista) != 1: continue          # ambiguo nao serve
                v = lista[0]
                obs = h["frame_num"] if campo == "fn" else h["poc_lsb"]
                k = (v ^ obs).bit_length() - 1
                a, b = localiza(h, bitc, larg, k, off)
                reg = (i, g[0][0], j, campo, obs, v, a, b, estado.get(i, "?"))
                (flags_bons if bom else cands).append(reg)

    print()
    print("[+] CONFERENCIA: frames que HOJE decodificam nao podem ser acusados.")
    print("    acusados indevidamente: %d %s" %
          (len(flags_bons), "" if not flags_bons else "  <-- regra furada"))
    for r in flags_bons[:10]: print("      ", r)

    print()
    print("[+] candidatos de 1 bit em frame quebrado: %d" % len(cands))
    with open("candidatos_header.txt", "w", newline="\n") as gf:
        for c in cands:
            gf.write(" ".join(map(str, c)) + "\n")
    print("[+] gravados em candidatos_header.txt "
          "(NAO sao patches: nenhum foi testado no decoder)")

main()
