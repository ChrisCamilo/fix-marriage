# Censo da cabeça — a tarja de cima recodificada

**Estado: aplicado em 2026-09-28, níveis A e I, aprovados pelo usuário** — 943
linhas em 232 quadros; o nível B ficou de fora. Ferramenta:
`tools/anchor/cabeca.py`. Dados: `data/censo_cabeca.txt` (medida) e
`data/patches_cabeca_proposta.txt` (proposta com os três níveis); o que entrou
está em `data/patches_cabeca.txt`.

## Por quê

789 quadros param dentro da tarja de cima (MB < 960) no `mapa`. Na cabeça do
slice o estado do CABAC vem inteiro do cabeçalho — sem a incógnita de encaixe
das caudas. Com a sintaxe da tarja conhecida, o codificador gera os bits
certos sem ler o arquivo; cada bit diferente é um bit trocado enquanto a
hipótese valer (hipótese errada diverge para ~50%).

## Método

- `cabac_p.py` ganhou slice I (mb_type de I16x16) e B (só MB pulado).
  Validado contra o trace do JM: IDRs 0, 2333, 3319 e B 2, 4, 6 — 960 MBs,
  **0 diferenças** nos dois sentidos; P 3, 5, 7, 9 seguem com 0.
- Sintaxe medida nos quadros que passam da fileira 8: I = I16x16 DC sem resíduo
  (só o MB 0 tem DC de luma, nível pela física 128 → 16), 90 de 90 sem
  variante; B = tudo pulado; P = tudo pulado, ou (fades) MB 0 especial + I16x16 DC.
- **Comparação no NAL, não no RBSP.** Cabeça de P/B é quase só zeros, então o
  NAL é `00 00 03 00 00 03…`; bit trocado num escape (`03`→`07`) desloca o RBSP
  e dava um falso "11" nos bits 22–23 em ~310 B. Comparando byte a byte na
  posição do arquivo, isso some.

## Resultado (3.445 quadros, 287.696 bits testados)

| tipo | d = 0 | d 1–3 | d 4–6 | d ≥ 7 | param < 960 entre os d ≥ 1 |
|---|---|---|---|---|---|
| B | 1.841 (69 param < 960) | 433 | 22 | 16 | 457 de 471 |
| P | 608 (6) | 137 | 25 | 48 | 197 de 210 |
| I | 92 (0) | — | — | 39 | 39 de 39 |

184 com cabeçalho inválido ficam fora. Separação quase perfeita: distância 0
passa da tarja; distância ≥ 1 para nela.

**Controle:** nos ~2.500 quadros que já passavam da tarja, só 3 "correções"
com d 1–3 (59, 3122, 3127) — MB real perto do fim da janela, rajada cortada;
todas com folga ≤ 9 bits. Daí o nível A exigir folga ≥ 16.

**IDRs:** 35 dos 39 têm dano uniforme (1,5–6,6%, ≤ 8 por janela de 64 bits) —
o argumento do frame 11 em escala pequena; 4 divergem (901, 1011, 1437, 2130,
~30%) e ficam fora.

## Proposta (juiz: `mapa` com a correção aplicada; parava na tarja e avança)

| nível | quadros | bits | inteiros | passam da tarja | avançam dentro dela |
|---|---|---|---|---|---|
| A — P/B, folga ≥ 16, trocas ≥ 8 bits uma da outra | 197 | 243 | 3 | 150 | 44 |
| B — P/B, o resto de d 1–3 | 362 | 703 | 6 | 246 | 110 |
| I — IDRs de dano uniforme | 35 | 700 | 0 | 34 | 1 |

Inteiros com a correção: 263, 601, 780, 813, 1638, 1647, 2068, 2459, 3260.
Fora pelo juiz: 3 já passavam, 5 pioram, 3 não mudam. "Avança dentro da
tarja" = mais dano logo depois da janela (nos bits pendentes: numa tarja em
skip cada bit carrega ~24 MBs). A correção prova os bits dela; não fecha o
quadro — como as caudas.

**Com a proposta inteira aplicada (A + B + I):** `serie 0 3444` (`TARJA=1`)
segue **165**, os mesmos quadros; nenhum quadro ganha nem perde a tarja 16
exata no `panorama` (1.808 mudam de estatística, todos em GOPs de imagem já
quebrada). Ou seja: a proposta não piora nada do que está bom e, sozinha, não
traz quadro bom novo — é o mesmo tipo de lote das caudas (prova bits; o dano
do meio continua).

## Aplicado (2026-09-28)

Níveis **A e I**, aprovados pelo usuário: 943 linhas (3.995–4.937 do
`patches.txt`), registradas em `data/patches_cabeca.txt`, que o `verify` pula.
Medido depois: o `mapa` muda só nos 232 quadros do lote, todos para a frente
(3 inteiros, 184 passam da tarja, 45 avançam; parados na tarja de cima 789 →
602; +207.583 MBs); `serie` 165, os mesmos; `verify` 0 / 12 / 3.587. O nível B
(362 quadros, 703 bits) segue só na proposta: a folga curta é justamente o
que as 3 correções falsas do controle tinham.

Reproduzir: `python tools/anchor/cabeca.py censo …`, `mapa` com os candidatos,
`python tools/anchor/cabeca.py niveis …` (uso no topo da ferramenta).
