"""remontar.py - monta video ASSISTIVEL preenchendo frames quebrados.

ISTO NAO E REPARO. E ocultacao, no sentido do H.264: inventa um quadro
plausivel para o buraco em vez de recuperar os bits verdadeiros. Os dois
objetivos sao legitimos e nao podem se misturar:

  patches.txt   afirma "este bit estava corrompido". So entra o que passa no
                criterio rigoroso. E a fonte de verdade.
  remontar.py   produz algo que da para assistir. Nao afirma nada sobre bits,
                nao toca no MP4 e nao escreve no patches.txt.

O frame inventado e a media dos dois vizinhos mais proximos na ordem de
EXIBICAO (poc), ponderada pela distancia. Numa cena parada -- que e o caso do
final do filme, um fade -- isso fica visualmente suave. Em cena com movimento
vai borrar, e esse e o preco.

Cada frame sintetizado fica registrado em `remontados.txt`, para ninguem
confundir depois o que foi recuperado com o que foi inventado.

  python remontar.py <faixa.yuv> <mapa.txt> <orig.mp4> <index.txt> <patches.txt> <saida.yuv>
"""
import sys, os

W, H = 1920, 1080
NY, NC = W * H, (W // 2) * (H // 2)
QUADRO = NY + 2 * NC

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_cab = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "cabecalhos.py"), encoding="utf-8").read()
exec(_cab.split("def varre_fn")[0])          # le_sps, le_header, rbsp_map, BR

def poc_dos_frames(mp4, f_ix, f_pt, ini, fim):
    log2fn, _, log2poc = le_sps()
    ix = [tuple(map(int, l.split())) for l in open(f_ix)]
    d = bytearray(open(mp4, "rb").read())
    for l in open(f_pt):
        o, b = l.split(); d[int(o)] ^= 1 << int(b)
    d = bytes(d)
    poc, gop = {}, None
    for i, off, size, idr in ix:
        if idr: gop = i
        if not (ini <= i <= fim): continue
        try:
            h = le_header(d[off+4:off+4+min(size-4, 64)], log2fn, log2poc)
            poc[i] = (gop, h["poc_lsb"])
        except Exception:
            poc[i] = (gop, 9999)
    return poc

def main():
    f_yuv, f_map, mp4, f_ix, f_pt, f_out = sys.argv[1:7]
    estado = {}
    for l in open(f_map):
        t, e = l.split(); estado[int(t)] = e
    ini, fim = min(estado), max(estado)
    poc = poc_dos_frames(mp4, f_ix, f_pt, ini, fim)

    # ordem de exibicao: por (gop, poc)
    ordem = sorted(estado, key=lambda t: poc.get(t, (0, 0)))
    bons = [t for t in ordem if estado[t] == "ok"]
    print(f"[+] {len(ordem)} frames na faixa, {len(bons)} decodificam")

    dados = open(f_yuv, "rb")
    def leia(t):
        dados.seek((t - ini) * QUADRO)
        return bytearray(dados.read(QUADRO))

    pos = {t: k for k, t in enumerate(ordem)}
    saida = open(f_out, "wb")
    # Anexa, nao sobrescreve: montar o filme em varios trechos fazia a
    # ultima corrida apagar o registro das anteriores, e o arquivo existe
    # justamente para ninguem confundir depois o que foi recuperado com o
    # que foi inventado.
    novo = not os.path.exists("data/remontados.txt")
    reg = open("data/remontados.txt", "a", newline="\n")
    if novo:
        reg.write("# frame sintetizado <- media ponderada dos vizinhos de exibicao\n")
    n_syn = 0
    for t in ordem:
        if estado[t] == "ok":
            saida.write(leia(t)); continue
        k = pos[t]
        ant = next((ordem[j] for j in range(k-1, -1, -1) if estado[ordem[j]] == "ok"), None)
        prox = next((ordem[j] for j in range(k+1, len(ordem)) if estado[ordem[j]] == "ok"), None)
        if ant is None and prox is None:
            saida.write(bytearray(QUADRO)); continue
        if prox is None and ant is not None:
            # Cauda do filme: nao ha vizinho depois. Copiar congela a imagem no
            # meio de um fade. Como o final e campo uniforme escurecendo, da
            # para EXTRAPOLAR a reta dos dois ultimos bons e gerar o campo no
            # valor previsto. Continua sendo invencao, mas invencao que segue o
            # que o filme estava fazendo.
            ant2 = next((ordem[j] for j in range(pos[ant]-1, -1, -1)
                         if estado[ordem[j]] == "ok"), None)
            A = leia(ant)
            am = A[:NY:997]; ya = sum(am) / len(am)
            if ant2 is not None:
                B = leia(ant2); bm = B[:NY:997]
                yb = sum(bm) / len(bm)
                passo = (ya - yb) / max(1, pos[ant] - pos[ant2])
            else:
                passo = 0.0
            delta = passo * (k - pos[ant])
            M = bytearray(A)
            for i in range(NY):
                v = A[i] + delta
                M[i] = 16 if v < 16 else (255 if v > 255 else int(v + 0.5))
            saida.write(M)
            reg.write(f"{t} <- {ant} escurecido em {delta:+.1f} "
                      f"(extrapolacao do fade; sem vizinho depois)\n")
            n_syn += 1; continue
        if ant is None:
            saida.write(leia(prox))
            reg.write(f"{t} <- copia de {prox} (sem vizinho antes)\n")
            n_syn += 1; continue
        da, dp = k - pos[ant], pos[prox] - k
        wa, wp = dp / (da + dp), da / (da + dp)
        A, B = leia(ant), leia(prox)
        M = bytearray(int(A[i] * wa + B[i] * wp + 0.5) for i in range(QUADRO))
        saida.write(M)
        reg.write(f"{t} <- {wa:.2f}*{ant} + {wp:.2f}*{prox}\n")
        n_syn += 1
    saida.close(); reg.close(); dados.close()
    print(f"[+] {n_syn} frames sintetizados, listados em remontados.txt")
    print(f"[+] {f_out} pronto: {len(ordem)} frames em ordem de exibicao")

main()
