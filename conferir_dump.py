"""conferir_dump.py - diz se um despejo do modo `cresce` ainda vale.

As medidas do despejo foram feitas contra um estado do arquivo. Elas valem
enquanto a CADEIA do alvo nao mudar: patch no proprio frame, ou em qualquer um
entre a ancora e ele, invalida tudo. Patch em outra parte do filme nao.

Sem esta conferencia, usar um despejo velho e erro silencioso -- os numeros
parecem bons e estao medindo um estado que nao existe mais. E o mesmo tipo de
armadilha que ja custou uma hora de maquina neste projeto (a 13).

  python conferir_dump.py <despejo.txt> <index.txt> <patches.txt>
"""
import sys

def main():
    f_dump, f_ix, f_pt = sys.argv[1], sys.argv[2], sys.argv[3]
    cab = {}
    for l in open(f_dump):
        if not l.startswith("#"): break
        if " alvo " in l:
            c = l.split()
            # ler por NOME, nao por posicao: "faixa [5,81752)" e um token so e
            # desloca tudo depois dele
            def dep(chave):
                return c[c.index(chave) + 1]
            cab = {"alvo": int(dep("alvo")), "ancora": int(dep("ancora")),
                   "n": int(dep("cadeia_patches")), "hash": dep("cadeia_hash")}
    if not cab:
        print("[!] despejo sem carimbo -- feito antes desta conferencia existir.")
        print("    Nao da para saber contra que estado foi medido. Refazer.")
        return 2

    ix = {}
    for l in open(f_ix):
        i, off, size, idr = l.split()
        ix[int(i)] = (int(off), int(size))
    ini_b = ix[cab["ancora"]][0]
    fim_b = ix[cab["alvo"]][0] + ix[cab["alvo"]][1]

    h = 1469598103934665603
    M = (1 << 64) - 1
    n = 0
    for l in open(f_pt):
        o, b = l.split(); o, b = int(o), int(b)
        if not (ini_b <= o < fim_b): continue
        h = ((h ^ o) * 1099511628211) & M
        h = ((h ^ b) * 1099511628211) & M
        n += 1

    print(f"alvo {cab['alvo']}, ancora {cab['ancora']}, bytes [{ini_b},{fim_b})")
    print(f"  patches na cadeia: carimbo {cab['n']}, agora {n}")
    print(f"  hash: carimbo {cab['hash']}, agora {h:016x}")
    if n == cab["n"] and f"{h:016x}" == cab["hash"]:
        print("[+] o despejo VALE -- a cadeia nao mudou")
        return 0
    print("[!] o despejo NAO vale mais -- a cadeia mudou. Refazer a corrida.")
    return 1

sys.exit(main())
