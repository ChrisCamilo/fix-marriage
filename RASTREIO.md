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
| 2361 | 2333 | 30.449 | NAL inteiro | 243.552 | 75 min | **1** | reprovado — ver abaixo |
| 3428 | 3426 | 5.211 | NAL inteiro | 41.648 | 70 s | 25.537 | **reprovado** — o melhor tem quebra de macrobloco visível |
| 3430 | 3426 | 7.223 | NAL inteiro | 57.744 | 148 s | 17.448 | reprovado — blocagem 2,199 contra 1,40 dos vizinhos |
| 3432 | 3426 | 1.626 | NAL inteiro | 12.968 | 40 s | 6.266 | reprovado — retângulos de macrobloco no mapa de diferença |
| **3435** | 3426 | 963 | NAL inteiro | 7.664 | 28 s | 14 | **reparado** — `114237506 4`, campo uniforme 34 |
| **3439** | 3426 | 988 | NAL inteiro | 7.864 | 36 s | 349 | **reparado** — `114244542 6`, imagem perfeita |
| 3441 | 3426 | 940 | NAL inteiro | 7.480 | 38 s | 1 | reprovado — campo 16–19, devia ser 21 |
| **3442** | 3426 | 2.939 | NAL inteiro | 23.472 | 125 s | 959 | **reparado** — `114260576 3`, erra 6 linhas na borda |
| 3443 | 3426 | 261 | NAL inteiro | 2.048 | 12 s | **0** | sem solução |
| 3444 | 3426 | 266 | NAL inteiro | 2.088 | 12 s | 2 | reprovado — campo 19–21, não uniforme |

### Frame 1684: 75.909 soluções, e o GOP está bloqueado pelo próprio IDR

Varrido o NAL inteiro, 480.168 candidatos em 928 s: **75.909 soluções**, ou seja
**16% de todas as inversões de 1 bit** fazem o frame passar no critério. Isso não
é ambiguidade, é o critério ter virado trivial ali.

A causa: **o IDR 1683 está listrado**. Ele decodifica e passa no critério, mas
34,3% das linhas dele são idênticas à anterior — os dois terços de baixo são
propagação vertical. Amostrando candidatos do 1684, todos herdam o defeito:

| candidato | linhas idênticas |
|---|---|
| `55858726 bit 3` | 38,4% |
| `55858750 bit 3` | 32,8% |
| `55858770 bit 7` | 23,7% |
| `55858791 bit 1` | 17,7% |
| **o próprio IDR 1683** | **34,3%** |

Nenhum reparo do 1684 pode sair limpo enquanto a referência dele for listrada.
**O GOP 1683 não é atacável por baixo** — o IDR tem que cair primeiro.

Conferidos os 7 IDRs que decodificam: **só o 1683 está listrado**, os outros seis
(2333, 3319, 3348, 3368, 3397, 3426) dão 0,0%. Então são **6 IDRs utilizáveis**,
não 7, e o GOP 1683 contribui **zero** quadros — os 4 que a classificação contava
como bons são listra.

### Frame 2361: o zero era artefato, mas o reparo não saiu

A revarredura com a cadeia limpa devolveu **1 solução** onde a anterior dera 0 —
confirmação direta da armadilha 13. Mas ela não passa:

| tentativa | cabeçalho | tarja média / desvio | gabarito |
|---|---|---|---|
| só `77528961 bit 2` | `frame_num=136` (**errado**, devia ser 8) | 16,246 / **5,350** | 1,308 |
| `bit 0` + `bit 2` | `frame_num=8`, `poc_lsb=54` — **corretos** | 17,432 / **13,632** | 1,908 |
| genuínos | | ~16,00 / ≤ 0,41 | piso 0,66 |

Os **dois bits estão no mesmo byte**, `77528961`. Só o do `frame_num` não faz
decodificar; só o outro faz decodificar mas deixa o `frame_num` provadamente
errado; os dois juntos dão cabeçalho correto e **imagem pior**.

Ampliando a tarja inferior em resolução cheia, os dois candidatos mostram um
**borrão claro horizontal** logo abaixo da borda da imagem. Recalibrado o
critério por região (armadilha 15), a rejeição se sustenta com folga:

| | genuínos, pior caso | 2361 de 1 bit |
|---|---|---|
| desvio na transição 950–956 | 1,36 | **45,36** (33x) |
| pior pixel | 14 | **219** (15x) |
| pixels fora de ±10 | 0,238% | **21,7%** (91x) |

O **fundo da tarja do candidato é perfeito** — desvio 0,00, melhor que os
genuínos. O defeito está inteiramente nas sete linhas da transição.

O GOP 2333 fica em **28 de 29**.

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

## Patches determinísticos — provados por invariante, não por busca

`deterministicos.txt` lista patches que entram por **prova**, não por
decodificação: o cabeçalho de um IDR só pode começar com `65 88 80`, porque é a
única codificação de `first_mb=0, slice_type=7, pps_id=0, frame_num=0` — quatro
campos que um IDR não pode ter diferentes. Medido nos 7 IDRs que decodificam:
os três bytes são idênticos em todos.

Dos 121 IDRs quebrados, **104 já têm o prefixo canônico** e 17 divergem:

| bits fora do canônico | IDRs |
|---|---|
| 1 | 11 |
| 2 | 3 |
| 4 | 2 |
| 6 | 1 |

**31 bits anexados** ao `patches.txt` (linhas 1348–1378). **Nenhum dos 17 passa a
decodificar** — o dano vai além do prefixo. O ganho é outro: toda varredura
futura nesses 17 parte de um cabeçalho correto, em vez de procurar bit num NAL
que já tem bits provadamente errados.

O `verify` pula esses patches, senão eles apareceriam como falsos e afogariam o
sinal. Saída esperada hoje:

```
[+] patches validos: 5 | falsos: 4 | deterministicos pulados: 31
```

Os 4 falsos são os antigos dos frames 2362–2366, **insuficientes e não errados**
— ver `INVESTIGACOES.md`.

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
