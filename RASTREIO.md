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

## Triagem dos 121 IDRs quebrados por listra

Refeita com **linhas idênticas** em vez de blocagem, depois que o IDR 1712
mostrou que a blocagem não separa listra de cena (armadilha 18). Uma
decodificação por IDR, **11 s** no total — não é varredura.

| estado do quadro | IDRs |
|---|---|
| não produz imagem nenhuma | 17 |
| 100% listra | 20 |
| 75–100% | 3 |
| 50–75% | **74** |
| 25–50% | 6 |
| abaixo de 25% | **1** |

**O filtro antigo aprovou 84 e apenas 1 sobrevive.** Dos 84 "com corte
confiável" pela blocagem, 77 têm mais de 50% de listra e 83 têm mais de 25%.
Nenhum tem 0%. As 5,1 h estimadas para varrer os 84 iriam quase inteiras para
quadros que morrem no primeiro quarto do slice.

Os 17 sem imagem saem com `-1` na coluna, e não devem ser lidos como "pouca
listra": são os de corte no byte ~10, o perfil do IDR 2525, que varreu 505.912
candidatos de NAL inteiro e deu zero.

### IDR 1773 — o melhor alvo do filme inteiro

Único com imagem praticamente íntegra, e a blocagem jamais o apontaria: 1,151,
no meio do bolo junto com os de 70% de listra.

| medida | 1773 | genuínos |
|---|---|---|
| bytes do NAL | 34.317 | — |
| ponto de corte | **33.489 (97,6% do slice)** | — |
| linhas idênticas | **11 de 813 (1,4%)** | 0 |
| onde estão | **933 a 949** — a última fileira de macrobloco | — |
| tarja de topo | **16,000 / desvio 0,000** | 16,00 / ≤ 0,41 |
| tarja inferior | 190,66 / desvio 38,47 | 16,00 / ≤ 0,41 |

Inspecionado em resolução cheia: a cena está nítida e completa até a linha 932.
O defeito é que **a tarja inferior não existe** — o conteúdo da última fileira
vaza para baixo e preenche onde deveria haver preto.

Isso reúne as três coisas que faltaram no IDR 1683:

1. **O dano é pequeno** — 17 linhas e a tarja, contra 368 linhas do 1683.
2. **Existe gabarito que não se burla.** A tarja inferior tem valor conhecido a
   priori (16,00, desvio ~0). Não é proxy de imagem, é conteúdo sabido.
3. **É IDR**, então o conserto destrava o GOP inteiro.

Varredura de NAL inteiro: 274.496 candidatos, **~6,5 min**. Pela armadilha 9 o
bit errado fica ~2600 bytes antes do corte, então `[30800,34317)` são 28.136
candidatos e ~40 s — mas o NAL inteiro é barato o bastante para não valer o
recorte.

### IDR 1773 varrido: 26.865 soluções, nenhuma devolve a tarja

NAL inteiro `[5,34317)`, 274.496 candidatos, **211 s**. Deu **26.865 soluções**
— 9,8% de todas as inversões, o critério trivializado outra vez.

Mas aqui existe gabarito, e ele decide sem conversa. Medidas de todas as 26.865
com o modo `campo`, em 32 s:

| tarja inferior | candidatos |
|---|---|
| média abaixo de 80 | **0** |
| média 80–150 | 12 |
| média 150–300 | 26.853 |
| genuínos | 16,00 |

O **menor desvio entre as 26.865 é 13,308**, contra ≤ 0,41 dos genuínos e 38,47
do próprio original quebrado. A melhor média é 126,27 — e essa tem desvio
74,78, ou seja, baixou a média espalhando mais. Nenhuma se aproxima de uma
tarja plana.

Restringindo à janela da armadilha 9 (rel ≥ 30389, ~2600 bytes antes do corte),
sobram 4.618 soluções e o melhor desvio é **22,80**. Não melhora.

**Conclusão: nenhuma inversão de 1 bit em lugar nenhum do NAL conserta o IDR
1773.** Como a varredura cobriu o NAL inteiro, isto é definitivo para 1 bit —
o dano precisa de 2 ou mais.

Todos os 26.865 mantêm a tarja de topo em 16,006, o que confirma que o defeito
é só na parte final do slice.

**Dois bits na janela cirúrgica** é a continuação natural, e é a única do
projeto hoje com gabarito não burlável: ±500 B do corte são 8.000 bits, 32 M
pares, **~6,8 h** à taxa medida de 1.301 cand/s. Alargar para ±2 KB já vira
3,5 dias.

Medidas guardadas em `medidas1773.txt` no scratchpad (26.865 linhas, 20
colunas) — dá para refiltrar por qualquer critério sem revarrer.

### Os 6 da faixa de 25–50%

Segunda fila, todos com corte confiável e metade do quadro real:

| IDR | bytes | corte | candidatos | listra |
|---|---|---|---|---|
| 1143 | 50.635 | 18.887 | 159.248 | 42,7% |
| 1802 | 78.007 | 19.346 | 162.920 | 43,2% |
| 1949 | 165.389 | 60.787 | 494.448 | 44,5% |
| 3076 | 198.642 | 23.031 | 192.400 | 44,5% |
| 2583 | 112.373 | 25.675 | 213.552 | 48,1% |
| 2801 | 86.667 | 22.280 | 186.392 | 49,0% |

Somados com o 1773: 1,82 M candidatos, **43 min**. É a lista que substitui as
5,1 h dos 84.

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

### IDR 1683: três tentativas de otimização, três burlas

Ele passa no critério e mostra metade da imagem — consome 49% do NAL e
decodifica até a linha 581 de 949, com 277 linhas propagadas de 813.

| tentativa | faixa | nota | veredito |
|---|---|---|---|
| `cresce` sem guardas | `[5,81752)` 654 mil | 277 → **153** | lixo colorido saturado, croma `U 56-171` |
| `cresce` com guarda de faixa de croma | idem | 277 → **227** | lixo pastel, croma `U 105-144`, dentro da guarda |
| `cresce` com guarda de média e desvio, calibrada pela metade íntegra do próprio quadro | idem | 277 → **235** | lixo dessaturado, `U 122,8±7,0` contra `121,4±5,5` do íntegro |

**Descartado depois de três tentativas.** Cada guarda que se aperta, a busca
acha lixo que a satisfaz: sem guarda, saturado; com faixa de valores, pastel;
com média e desvio, dessaturado. **Estatística de imagem não determina imagem** —
milhões de quadros diferentes têm a mesma média e o mesmo desvio, e com 654 mil
candidatos sempre existe um que imita o que se escolheu medir.

A região danificada não tem gabarito — sem vizinho temporal (o
GOP e o anterior estão quebrados) e sem aritmética que fale de conteúdo. Todo
objetivo vira proxy, e proxy contra 654 mil candidatos acha o patológico. Ver
armadilha 17.

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
| 1712 | 185.831 | `[5,8336)` | 66.648 | **4** | reprovados — o quadro é listra, ver abaixo |

### IDR 1712: 4 soluções, e a triagem dos 84 está errada

Escolhido por ser o vizinho seguinte do GOP 1683 com corte confiável (7312,
blocagem 1,040). Varredura em `[5,8336)`: 66.648 candidatos em **45 s**,
**4 soluções** — `56851303 1`, `56851498 6`, `56851952 7`, `56852100 3`.

As quatro são reprovadas pela tarja sem margem para discussão:

| candidato | tarja média | tarja desvio | tarja min/max |
|---|---|---|---|
| `56851303 1` | 113,66 | 62,14 | 0 / 255 |
| `56851498 6` | 107,16 | 77,98 | 0 / 255 |
| `56851952 7` | 108,89 | 67,75 | 0 / 254 |
| `56852100 3` | **120,05** | **48,44** | 9 / 251 |
| genuínos | 16,00 | ≤ 0,41 | — |

A tarja inferior não é tarja: é conteúdo de faixa cheia. O melhor dos quatro
está 7x acima da média e 118x acima do desvio.

**Mas o achado que importa é outro.** Inspecionando o quadro em resolução
cheia, o original do 1712 **já não tem imagem**: tarja de topo correta e, a
partir da linha 181, listra vertical até embaixo. O candidato troca a listra
apagada por listra arco-íris saturada.

| | linhas idênticas | primeira propagada |
|---|---|---|
| 1712 original | **71,2%** | 181 |
| 1712 + `56852100 3` | 69,1% | 205 |
| 1683, o listrado conhecido | 34,3% | 581 |

O 1712 é **mais listrado que o 1683**, e ainda assim entrou na lista dos "84
com corte confiável". A causa está na armadilha 18: a triagem usou blocagem, e
listra bloca igual a cena. **O número 84 não vale** — refazer com linhas
idênticas antes de gastar as 5,1 h estimadas.

Confirmado de passagem que `par705.txt` e `par3278.txt` estão **com 0 bytes**:
aquelas duas corridas nunca terminaram. Onde este arquivo dizia "rodando" e "na
fila", leia-se **não feita**.

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
