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

Inteiros com a correção: 263, 601, 780, 813, 1638, 1647, 2068, 2459, 3260 — **ao
menos 263, 1638 e 2459 eram artefato** (ver "Revisto" no fim).
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
`patches.txt`; 3.993–4.935 desde 2026-09-29), registradas em `data/patches_cabeca.txt`, que o `verify` pula.
Medido depois: o `mapa` muda só nos 232 quadros do lote, todos para a frente
(184 passam da tarja, 45 avançam, e 3 que pareciam inteiros; parados na tarja de
cima 789 → 602; +207.583 MBs); `serie` 165, os mesmos; `verify` 0 / 12 / 3.587. O nível B
(362 quadros, 703 bits) segue só na proposta: a folga curta é justamente o
que as 3 correções falsas do controle tinham.

Reproduzir: `python tools/anchor/cabeca.py censo …`, `mapa` com os candidatos,
`python tools/anchor/cabeca.py niveis …` (uso no topo da ferramenta).

## Revisto em 2026-09-29: o escape depois da janela

Os "3 inteiros" do lote aplicado (263, 1638, 2459) eram artefato: logo depois
da janela testada o arquivo tem `00 00 02`, proibido num NAL; o ffmpeg corta o
NAL ali e o quadro é lido todo pulado (armadilha 62). No IDR 2971 a correção da
linha 4.834 formou um `00 00 01` pelo mesmo motivo — o escape seguinte (`01`
onde devia ser `03`) ficou 1 byte fora da janela. As correções da cabeça estão
certas; faltava o escape.

Restaurado o `03` nesses 4 e em 59, 407 e 1610 (linhas 5342–5348, aprovadas
pelo usuário): os falsos inteiros param no dano real, o 2971 vai de 960 a 998,
`serie` igual. O `cabeca.py` agora inclui esse byte: quando o esperado termina
a janela em `00 00` e o arquivo tem ali `00 00 0x` (x < 3), o byte só pode ser o
escape `03`. Testado nos 7: acha os 7 e nada mais. Guarda para todo lote novo:
`python tools/proibidas.py` — nenhuma sequência criada por patch.

## Nível B aplicado (2026-09-29)

Recalibrado o risco que deixou o nível B de fora: nos 26 quadros de controle
com evento real na cabeça (MB não pulado que passa da tarja), corta-se a
janela em todo ponto possível e conta-se quando a rajada verdadeira passaria
como 1–3 trocas. Probabilidade média por evento: folga ≥ 6 → 0,5%; ≥ 8 →
0,07%; **≥ 10 → 0** (máximo 0 nos 26). O espaçamento entre trocas não importa.
Com ~1% de quadros com evento, nem a folga 6 daria 0,02 falso esperado; fica
10, com margem.

Censo refeito no buffer do dia (com a guarda do escape): 365 candidatos P/B
parados na tarja; com folga ≥ 10, **163 quadros, 325 bits**, todos andam no
`mapa` (121 passam da tarja), nenhum piora; `serie` igual. Aplicado com
aprovação do usuário: `data/patches_cabeca_b.txt`. Os 198 com folga < 10 são o
nível C, fora. O `cabeca.py niveis` agora separa A, B e C.

## MB 0 pela física no censo (2026-09-29)

Nos P ponderados o censo lia o MB 0 do arquivo; quando o próprio MB 0 está
danificado (o frame 15), nenhuma hipótese sobrava. O `cabeca.py` agora deriva
o MB 0 dos pesos do cabeçalho (`mb0_fisica`): para cada referência, a predição
ponderada da tarja (16 / 128), o resíduo e os níveis de DC que o reconstroem
exato. **Validado sozinho em 11 quadros do GOP 0** (P 1, 3, 5, 7, 9, 11, 13,
15, 21 e 23): a hipótese física bate com o arquivo em 0 bits em cada um, com a
referência e os níveis que o encoder escolheu (ref0, ref1 ou ref2 conforme o
peso). No 15 foi o que achou os 48 bits aplicados.

No filme inteiro não aparece candidato novo: nos outros P ponderados a leitura
do MB 0 já dava a resposta. O 17 e o 19 ficam sem hipótese (a física erra
22–26%), e nenhuma troca de 1 bit no cabeçalho deles faz a tarja bater —
dano múltiplo no cabeçalho ou na tarja, dentro da zona densa.
