# Rastreio das varreduras

Registro de toda varredura de força bruta já feita, por alvo. Serve para não
refazer trabalho e, principalmente, para não tirar conclusão de corrida
**inválida** — várias já rodaram sob condição que impedia qualquer resultado.

**Antes de disparar qualquer varredura, conferir três coisas:**

1. A cadeia até o alvo está limpa? Se qualquer frame entre a âncora e o alvo
   estiver quebrado, nenhum candidato passa — armadilha 13 do `ARMADILHAS.md`.
2. O ponto de corte é medição ou artefato? Só vale se o frame produzir imagem com
   blocagem acima de ~0,05 — armadilha 14.
3. Vai varrer a faixa certa? `[5, corte+1024]`, e não `[corte-1024, corte+1024]`.

**Como ler o veredito:** `reparado` entrou no `patches.txt`; `reprovado` tem
candidato mas nenhum passou nos juízes; `sem solução` não tem candidato nenhum;
`INVÁLIDA` foi medida sob condição impossível e não conta.

## Frames comuns

| frame | GOP | bytes | faixa | candidatos | tempo | soluções | veredito |
|---|---|---|---|---|---|---|---|
| **2360** | 2333 | 32.501 | NAL inteiro | 259.968 | 64 min | **1** | **reparado** — `77496463 2`, os três juízes aprovam |
| 2361 | 2333 | 30.449 | NAL inteiro | 243.552 | 62 min | 0 | **INVÁLIDA** — rodou com o 2360 quebrado na cadeia |
| 2361 | 2333 | 30.449 | NAL inteiro | 243.552 | — | — | refazendo, agora com a cadeia limpa |
| 3428 | 3426 | 5.211 | NAL inteiro | 41.648 | 70 s | 25.537 | **reprovado** — o melhor tem quebra de macrobloco visível |
| 3430 | 3426 | 7.223 | NAL inteiro | 57.744 | 148 s | 17.448 | reprovado — blocagem 2,199 contra 1,40 dos vizinhos |
| 3432 | 3426 | 1.626 | NAL inteiro | 12.968 | 40 s | 6.266 | reprovado — retângulos de macrobloco no mapa de diferença |
| **3435** | 3426 | 963 | NAL inteiro | 7.664 | 28 s | 14 | **reparado** — `114237506 4`, campo uniforme 34 |
| **3439** | 3426 | 988 | NAL inteiro | 7.864 | 36 s | 349 | **reparado** — `114244542 6`, imagem perfeita |
| 3441 | 3426 | 940 | NAL inteiro | 7.480 | 38 s | 1 | reprovado — campo 16–19, devia ser 21 |
| **3442** | 3426 | 2.939 | NAL inteiro | 23.472 | 125 s | 959 | **reparado** — `114260576 3`, erra 6 linhas na borda |
| 3443 | 3426 | 261 | NAL inteiro | 2.048 | 12 s | **0** | sem solução |
| 3444 | 3426 | 266 | NAL inteiro | 2.088 | 12 s | 2 | reprovado — campo 19–21, não uniforme |

### Busca de segundo bit

| alvo | método | pares | soluções | veredito |
|---|---|---|---|---|
| 3435 | 14 âncoras × NAL inteiro | 35.291 | 0 | derruba a decomposição "um bit a imagem, outro a tarja" |
| 3443 | pares exaustivos | 2,1 M | — | interrompida por decisão de prioridade |

## IDRs

| IDR | bytes | faixa | candidatos | soluções | veredito |
|---|---|---|---|---|---|
| 2362 | 220.891 | `[10514,12562)` — janela antiga | 16.384 | 0 | a janela erra o alvo |
| 2362 | 220.891 | `[5,261)` | 2.048 | 1 | reprovado — tarja 19,2 e quadro preto |
| 2362 | 220.891 | `[5,12562)` | 100.456 | **24** | todos reprovados — tarja 90 a 158 |
| 1524 | 138.088 | `[5,1816)` | 14.488 | 0 | sem solução na faixa |
| 1831 | 103.514 | `[5,1864)` | 14.872 | 0 | sem solução na faixa |
| 2072 | 88.572 | `[5,2395)` | 19.120 | 2 | reprovados — tarja 103 e 104 |
| 3126 | 136.529 | `[5,4492)` | 35.896 | 8 | reprovados — tarja 97 a 127 |
| 734 | 236.848 | `[5,4686)` | 37.448 | 4 | reprovados — tarja 19 a 147 |
| 2217 | 121.750 | `[5,5290)` | 42.280 | 5 | reprovados — tarja 19 a 180 |
| 2525 | 63.244 | NAL inteiro, âncora `frame_num` | 505.912 | **0** | descartado — precisa de 3+ bits |
| 705 | 232.555 | NAL inteiro, âncora `poc_lsb` | 1.860.400 | — | rodando |
| 3278 | 244.901 | NAL inteiro, âncora `poc_lsb` | 1.959.168 | — | na fila |

### INVÁLIDA: os 39 IDRs de corte precoce

Varridos em `[5, corte+1024]` com corte menor que 1.000. **Em 37 dos 39 o corte
era artefato** do `acha_consumo` colapsando (armadilha 14), então a faixa foi
escolhida por um número sem significado. Resultado retirado.

O que sobra de válido: o **IDR 0** (2.302 B, `[5,1443)`, 11.504 candidatos) tem
**1 solução** em `rel 154`, reprovada pela tarja — quadro 16–18 com tarja 18,248.

## Varredura de cabeçalho

Não é força bruta: o `cabecalhos.py` deduz o valor certo por aritmética.

| corrida | alvos | achados | veredito |
|---|---|---|---|
| `frame_num` e `poc_lsb` em todos os frames | 3.445 | 536 candidatos de 1 bit | **0 fazem o frame decodificar** |
| molde do cabeçalho de IDR | 121 IDRs quebrados | 89 com cabeçalho perfeito, 32 com algum campo fora | ver `INVESTIGACOES.md` |

## O que falta varrer

**84 IDRs têm corte confiável** e ainda não foram varridos na faixa certa. São
13,1 milhões de candidatos no total, 1,5 a 2,4 h. Cortes de 792 a 60.787,
mediana 17.698.

Os quatro IDRs que erram **um único campo do molde** são a fila prioritária,
porque o valor correto é conhecido — não é candidato a testar, é erro provado:

| IDR | bytes | campo errado | lido | molde |
|---|---|---|---|---|
| 901 | 92.082 | `frame_num` | 192 | 0 |
| 1654 | 160.866 | `alpha` | 0 | −1 |
| 705 | 232.555 | `poc_lsb` | 8 | 0 |
| 99 | 155.407 | `slice_type` | 8 | 7 |
