"""encadeia.py - busca por etapas usando o modo avanco, com a porta da tarja.

Num slice CABAC nao ha ressincronizacao, entao tudo antes do primeiro erro esta
correto e "ate que macrobloco chegou" e progresso monotono. Este roteiro
encadeia etapas: acha os bits que empurram o erro para a frente, fixa os
melhores, e varre de novo a partir dali.

Duas salvaguardas contra subir num degenerado:

  PORTA  candidato que para antes do macrobloco 960 errou dentro da tarja, e a
         tarja nao tem o que errar. Descarte, nao progresso.
  RAMPA  candidato que fecha o quadro (macrobloco 8160) so vence se o campo for
         uniforme e igual a 40, que e o que a rampa do fade manda. Sem isso o
         all-skip -- copia do frame 9, campo 38 -- venceria toda vez.

E o feixe e de largura > 1 de proposito: guloso puro entra em otimo local.

  python encadeia.py <alvo> <etapas> <largura>
"""
import os, subprocess, sys, tempfile

MP4 = "Caio & Lizandra - Making- Caio-Balu.mp4"
SB  = os.environ.get("SB", ".")
EXE = "./reparador.exe"
PORTA, CAMPO_ALVO = 960, 40
ERROS_BASE = "2"          # os dois erros que o frame 11 emite em toda a cadeia


def patch_de(flips, nome):
    with open(nome, "w", newline="\n") as g:
        g.write(open("patches.txt").read())
        for o, b in flips:
            g.write(f"{o} {b}\n")
    return nome


def avanco(alvo, patch, saida):
    env = dict(os.environ, VISUAL="0", PORTA=str(PORTA))
    p = subprocess.run([EXE, MP4, "index.txt", patch, "avanco", str(alvo),
                        "1", "5", "1145", saida], capture_output=True, text=True, env=env)
    base = melhor = None
    for l in p.stdout.splitlines():
        if "base sem flip" in l:   base = int(l.split("macrobloco")[1].split("[")[0])
        if "melhor macrobloco" in l: melhor = int(l.split(":")[1].split()[0])
    cands = []
    if os.path.exists(saida):
        for l in open(saida):
            q = l.split()
            if len(q) >= 4: cands.append((int(q[0]), int(q[1]), int(q[3])))
    return base, melhor, cands


def julga(alvo, patch, cands, saida):
    """Mede campo dos candidatos que fecham o quadro. Devolve os que dao 40."""
    if not cands: return []
    with open(saida, "w", newline="\n") as g:
        for o, b, _ in cands: g.write(f"{o} {b}\n")
    env = dict(os.environ, VISUAL="0", ERROS_BASE=ERROS_BASE)
    p = subprocess.run([EXE, MP4, "index.txt", patch, "campo", str(alvo), saida],
                       capture_output=True, text=True, env=env)
    lin = [l.split() for l in p.stdout.splitlines() if l.strip()]
    dados = [l for l in lin if l and l[0].isdigit() and len(l) > 11]
    bons, k = [], 0
    for d in dados:
        cmin, cmax = int(d[2]), int(d[3])
        if cmin == cmax == CAMPO_ALVO: bons.append((int(d[0]), int(d[1]), cmin))
        k += 1
    return bons


def main():
    alvo    = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    etapas  = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    largura = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    feixe = [[]]                       # cada ramo e uma lista de flips fixos
    vistos = set()
    for etapa in range(1, etapas + 1):
        print(f"\n===== etapa {etapa} — {len(feixe)} ramo(s) no feixe =====", flush=True)
        novos = []
        for r, flips in enumerate(feixe):
            patch = patch_de(flips, f"{SB}/enc_{etapa}_{r}.txt")
            base, melhor, cands = avanco(alvo, patch, f"{SB}/enc_{etapa}_{r}_c.txt")
            fecham  = [c for c in cands if c[2] >= 8160]
            avancam = [c for c in cands if PORTA <= c[2] < 8160]
            print(f"  ramo {r} ({len(flips)} bits fixos): base mb {base}, "
                  f"{len(avancam)} avancam, {len(fecham)} fecham o quadro", flush=True)
            if fecham:
                bons = julga(alvo, patch, fecham, f"{SB}/enc_{etapa}_{r}_j.txt")
                if bons:
                    print(f"\n*** ACHOU: campo {CAMPO_ALVO} uniforme ***")
                    for o, b, c in bons:
                        print(f"    {flips + [(o, b)]}  -> campo {c}")
                    return
                print(f"      os {len(fecham)} que fecham dao campo != {CAMPO_ALVO} "
                      f"(all-skip / copia), descartados", flush=True)
            for o, b, mb in avancam:
                ch = tuple(sorted(flips + [(o, b)]))
                if ch in vistos: continue
                vistos.add(ch)
                novos.append((mb, list(ch)))
        if not novos:
            print("\n[!] nenhum ramo avanca — a busca de 1 bit por etapa esgotou aqui")
            return
        novos.sort(key=lambda x: -x[0])
        feixe = [f for _, f in novos[:largura]]
        print(f"  melhores: {[m for m, _ in novos[:largura]]}", flush=True)


if __name__ == "__main__":
    main()
