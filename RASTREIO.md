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

### Censo do GOP 1773 — consertar o IDR não destrava 29 frames

Decodificados os 29 frames (1773 a 1801) com a âncora no estado atual:

| frames | estado |
|---|---|
| 1773 | imagem real, 97,6% íntegra |
| 1774 | **cinza liso, todos os pixels em 128** — quadro de ocultação do decoder |
| 1798 | tem conteúdo, mas as tarjas estão em 128 em vez de 16 |
| os outros 26 | não produzem imagem nenhuma |

Corrige a conta de payoff usada até aqui: "consertar um IDR rende ~29 frames"
não vale para o 1773. O ganho **garantido** é 1 quadro; os 28 restantes ficam
apenas *tentáveis*.

E não dá para saber quanto dos 26 é dano próprio e quanto é consequência da
âncora quebrada — essa indistinguibilidade é a razão de ser da regra de ordem
de cadeia (armadilha 13).

### IDR 1143 varrido: 10 soluções, todas reprovadas

NAL inteiro `[5,50635)`, 405.040 candidatos, **304 s**. **10 soluções**, todas
agrupadas em rel 16.936–18.612, ou seja de 275 a 1.951 bytes antes do corte
(18.887) — coerente com a armadilha 9.

| | tarja média | tarja desvio |
|---|---|---|
| as 10 | 144 a 192 | 43 a 71 |
| genuínos | 16,00 | ≤ 0,41 |

Reprovadas pela tarja. E também pela imagem, que é o juiz que manda: as 10
decodificam **mais longe** que o original (1ª linha propagada 557–621 contra
485), e a melhor (`rel 17322 bit 1`) chega à 621. Inspecionada em resolução
cheia, as 136 linhas que ela ganha são **faixa de macrobloco de cor pura
seguida de listra arco-íris**. O original mostra cena real — noivo de colete,
cortina, papel de parede — até a 485.

É o padrão do 1683 e do 1712: "descer mais" não é decodificar mais.

**Contraste que importa para calibrar expectativa:** o 1143 deu 10 soluções em
405 mil candidatos (0,0025%); o 1773 deu 26.865 em 274 mil (9,8%). Quatro
ordens de grandeza. O critério **não** é trivial em toda parte — ele trivializa
quando a quebra está perto do fim do slice, porque aí quase qualquer
perturbação ainda termina o quadro sem o decoder reclamar. Número alto de
soluções é sintoma de quebra tardia, não de frame fácil.

### IDR 2801: 10 soluções, e o ranking por GOP que o elegeu

Escolhido depois de medir **o que há atrás do IDR**, coisa que eu não tinha
feito: o GOP dele tem **28 de 29 frames que decodificam**, contra 3 do GOP 1773
que eu vinha chamando de melhor alvo do filme. Rankear IDR pela imagem do
próprio IDR ignora o prêmio.

| GOP | frames que decodificam | IDR, bytes |
|---|---|---|
| **2801** | **28 de 29** | 86.667 |
| 1802 | 27 de 29 | 78.007 |
| 1949 | 25 de 29 | 165.389 |
| 1143 | 24 de 29 | 50.635 |
| 2583 | 20 de 29 | 112.373 |
| 3076 | 15 de 21 | 198.642 |
| *1773* | *3 de 29* | *34.317* |

Varredura do NAL inteiro: 693.296 candidatos, **471 s**, **10 soluções**.

| cand | rel | listra | V/H | tarja |
|---|---|---|---|---|
| **original** | — | 48,5% | 0,264 | 204,11 |
| 1,2,3,4,5,10 | — | **58 a 64%** | 0,08–0,18 | 145–166 |
| 6, 7 | — | 49% | 0,15–0,22 | 139–155 |
| 8 | 20720 | 36,7% | 0,471 | 197,93 |
| 9 | 20827 | 35,2% | 0,471 | 200,37 |
| genuínos | | 0,0% | 0,78–1,44 | 16,00 |

**Seis dos dez são piores que não fazer nada.** Inspecionados em resolução
cheia, os dois melhores decodificam um pouco mais da cabeça do noivo e enchem o
resto de **campo magenta saturado**. O original mostra a mesma cabeça com o
corpo esticado em tons naturais. Descer mais não é decodificar mais.

**Todos reprovados.**

### Cinco IDRs do meio do filme — 61 soluções, todas ocultação

Escolhidos por ter imagem (listra < 60%), corte confiável e nunca terem sido
varridos. Janela `[5, corte+1024]`.

| IDR | janela | candidatos | soluções | tempo |
|---|---|---|---|---|
| 2641 | `[5,14881)` | 119.008 | 2 | 181 s |
| 1833 | `[5,16317)` | 130.496 | 27 | 213 s |
| 1802 | `[5,20370)` | 162.920 | 19 | 250 s |
| 3076 | `[5,24055)` | 192.400 | 8 | 299 s |
| 2583 | `[5,26699)` | 213.552 | 5 | 318 s |
| | | **818.376** | **61** | **21 min** |

**Nenhuma passa.** A melhor tarja de cada um: 19,26 nos IDRs 2641 e 2583, e de
111 a 123 nos outros, contra 16,000 dos genuínos.

E os dois de 19,26 se denunciam por um detalhe: **produzem saída idêntica** —
99,6% de listra, gradiente vertical e horizontal **0,000**, campo 17,48 ± 1,03 —
sendo IDRs diferentes, com bits diferentes, em partes distintas do filme. Saída
idêntica a partir de entradas diferentes é **quadro de ocultação**, não
decodificação. O mesmo `19,26 / 0,44` já tinha aparecido num candidato do frame
3441.

**Gradiente horizontal zero é o detector mais barato de ocultação que apareceu
até agora:** quadro com cena tem entre 0,7 e 5,2.

A taxa também confirmou o efeito colateral da correção de cabeçalhos: **657
cand/s** onde a estimativa era 1.750, porque os candidatos agora decodificam
fundo em vez de morrer cedo.

### Nenhuma varredura de 1 bit em IDR jamais consertou um

Contagem acumulada: **18 IDRs varridos, zero reparos** — 0, 734, 1143, 1524,
1712, 1773, 1802, 1831, 1833, 2072, 2217, 2362, 2525, 2583, 2641, 2801,
3076 e 3126.

Não é azar acumulado, é o que a armadilha 20 prevê: IDR quebrado está dentro da
rajada, e dentro da rajada o dano é de muitos bits. **Varredura de 1 bit em IDR
deixou de ser linha de ataque plausível** — só vale onde o corte é tardio, que
é o caso dos frames comuns cercados de bons.

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

### GOP 0 — o IDR está INTACTO, e as varreduens foram desperdício

**Retratação.** Esta seção dizia que o IDR 0 errava no macrobloco (119,64) e
poluía a cadeia do GOP. **Ele não tem defeito nenhum:** decodifica limpo pelo
critério sintático e produz campo 16,00 com tarja **16,000 / desvio 0,000**.

O `error while decoding MB 119 64` vinha do `acha_consumo`, que trunca o NAL de
propósito para localizar o ponto de consumo — não é o decode natural. E a
presença dele na lista de quebrados é o juiz de imagem reprovando campo
uniforme (armadilha 22).

**O que se gastou nele, tudo sobre um quadro certo:**

| corrida | custo |
|---|---|
| 1 bit, NAL inteiro (duas vezes) | 15 s |
| 2 bits, `[700,1000)` | 26 min |
| 2 bits, `[600,1100)` | **2h09** |
| três varreduras nos frames 10, 11 e 13, invalidadas por um patch meu | 40 min |

As 16.786 "soluções de 1 bit" eram o original já decodificando: a partir do
byte 500 **todo** bit resolve, inclusive nos bytes 1.500, 2.200 e 2.290, que o
decoder nem consome. Ver armadilha 25 para a conferência de um segundo que
evitaria tudo.

**O que vale do GOP 0**, revarrido depois com o `patches.txt` puro:

| frames | estado |
|---|---|
| 0–9 | decodificam limpo; reprovados só pelo juiz de imagem, e a imagem está certa |
| 10, 11, 13 | **zero soluções de 1 bit** nos dois critérios — 2.056, 22.616 e 292.216 candidatos |
| 14, 15, 17, 19, 21–28 | não produzem imagem nem na passada completa |

### GOP 0, frames 10 a 12 — um conserto e duas vias fechadas

O único reparo real do GOP saiu do **frame 10**, e por previsão em vez de busca:
a rampa do fade fixa o valor **antes** de varrer, e um único candidato em 2.056
o produz. Ver `CRITERIOS.md` seção 0.5.

**Frame 11 — cabeçalho correto, corpo danificado.** Ele já produz o campo certo
(**43**, e a progressão dos quadros P do GOP — 21, 25, 29, 34, 38, 43, passos de
4 e 5 — confirma). O que está errado é a tarja, **15,000 nas duas pontas** onde
deve ser 16,000, mais o erro `Reference 3 >= 3` num macrobloco.

Três vias tentadas e as três fechadas:

| via | resultado |
|---|---|
| 2 bits em `[5,60)`, 96.580 pares | 4.957 decodificam limpo, **nenhum** acerta campo e tarja |
| `slice_qp_delta` para a faixa dos P (−13 a −17) | as 5 variantes produzem **nenhuma imagem** |
| `num_ref_idx_override` com 2 a 5 referências | as 4 variantes produzem **nenhuma imagem** |

| comparar bits com quadros bons | **sem sinal** — ver abaixo |
| `pred_weight_table`, referência 1 | anômala, mas corrigi-la **não muda nada** |

As duas do meio falham pelo mesmo motivo estrutural: esses campos são de
**comprimento variável**, então mudá-los desloca todo o resto do cabeçalho — a
tabela de pesos, o `cabac_init`, o `qp_delta` — e nada mais parseia. **Só dá
para consertar campo de cabeçalho por bit flip quando o comprimento não muda.**

**Comparar bits entre quadros não funciona, e o controle prova.** Dois quadros
**bons** do mesmo GOP diferem em **43%** dos bits entre si — saída de CABAC é
praticamente aleatória. O frame 11 contra o 9 dá 42%, indistinguível da linha de
base. A comparação só serve um nível acima: **decodificar os campos e comparar
valores**, que é o que os moldes fazem.

**A tabela de pesos do frame 11 é anômala e mesmo assim não é o defeito.** Ela
existe para que qualquer referência produza o nível certo, e nos quadros bons as
três convergem:

| frame | alvo | ref 0 | ref 1 | ref 2 |
|---|---|---|---|---|
| 7 | 34 | 33,4 | 34,0 | 33,0 |
| 9 | 38 | 38,7 | 38,2 | 38,4 |
| **11** | **43** | 42,5 | **82,3** | 42,0 |

O peso `(84, −7)` da referência 1 produz 82 onde deveria produzir 43. Mas
corrigi-lo para `(48, −8)` — mesmo comprimento, sem deslocamento — deixa a saída
**idêntica**: o quadro falha no macrobloco 15 da primeira fileira, antes de a
referência 1 ser usada. Testados também `(47, −7)` e `(32, 9)`, mesmo resultado.

### Frame 11 — por que nenhum par de bits pode funcionar

A janela foi ampliada para `[5,200)`: **1,2 M pares, 69 min, 22.243 soluções**.
Medidas 4.096 delas, o padrão de `[5,60)` se repete exato:

| | candidatos | campo | tarja |
|---|---|---|---|
| maioria | 3.591 | **43** (certo) | **15** (errado) |
| minoria | 224 | 38, 44 ou 45 | **16** (certo) |
| **os dois** | **0** | | |

E isto **não é estatística, é aritmética**. A predição ponderada do H.264 é
`(referência × peso)/32 + offset`, uma função linear aplicada ao quadro inteiro.
Com o peso `(40, −5)` que o frame 11 tem para a referência 0:

```
campo:  38 × 40/32 − 5 = 42,5   -> 43, o valor medido
tarja:  16 × 40/32 − 5 = 15,0   -> 15, o valor medido
```

**Os dois saem da mesma fórmula.** Não são dois defeitos, é um. E para acertar
campo 43 e tarja 16 ao mesmo tempo seria preciso `22w/32 = 27`, ou seja
`w = 39,27` — **não inteiro**. Nenhuma combinação de bits satisfaz os dois
enquanto os macroblocos da tarja estiverem sendo inter-preditos.

**Nos quadros bons a tarja não passa por essa fórmula.** O peso do frame 9 daria
`16 × 43/32 − 7 = 14,5` para a tarja, e a tarja dele mede **16,000** exata. Ou
seja: no quadro correto os macroblocos da tarja são **intra** — preto codificado
direto —, e no frame 11 estão decodificando como skip/inter.

**Diagnóstico final:** o defeito não é valor errado em campo nenhum. É **tipo de
macrobloco errado**, por dessincronização do CABAC — e isso explica de uma vez a
tarja em 15, o `Reference 3 >= 3` e a anticorrelação. Consertar exige
ressincronizar o decodificador aritmético, não corrigir um número.

Sete vias fechadas neste frame: 2 bits em duas janelas, `slice_qp_delta`,
`num_ref_idx_override`, `pred_weight_table`, comparação de bits com quadros bons,
e a leitura de que 44 ou 45 seriam o campo certo.

E o `Reference 3 >= 3` não é defeito de cabeçalho: os campos dele batem com os
quadros P bons. É dado de macrobloco pedindo referência que a lista não tem.

**Frame 12 — não avaliável enquanto o 11 estiver quebrado.** O `qp_delta` dele
é **+1** onde os B ficam entre −8 e −11, e −8 está a 1 bit. Mas testá-lo com o
frame 11 quebrado devolve a cópia do frame 9: é a armadilha 13, e eu cai nela
outra vez. Fica como hipótese aberta, para quando o 11 ceder.

Também fica registrado que a janela `[5,200)` que eu ia varrer no frame 12
estava errada: aquele `MB 15 0, bytestream 2778` é do frame **11**
(2828 − 2778 = 50), não do 12. O dano do 12 está no byte **1.820**.

## Frames comuns

| frame | GOP | bytes | faixa | candidatos | tempo | soluções | veredito |
|---|---|---|---|---|---|---|---|
| **2360** | 2333 | 32.501 | NAL inteiro | 259.968 | 64 min | **1** | **reparado** — `77496463 2`, os três juízes aprovam |
| 2361 | 2333 | 30.449 | NAL inteiro | 243.552 | 62 min | 0 | **INVÁLIDA** — rodou com o 2360 quebrado na cadeia |
| **2361** | 2333 | 30.449 | NAL inteiro | 243.552 | 75 min | **1** | **reparado** — `77528961 0` + `77528965 2`, ver abaixo |
| 3428 | 3426 | 5.211 | NAL inteiro | 41.648 | 70 s | 25.537 | **reprovado** — o melhor tem quebra de macrobloco visível |
| 3430 | 3426 | 7.223 | NAL inteiro | 57.744 | 148 s | 17.448 | reprovado — blocagem 2,199 contra 1,40 dos vizinhos |
| 3432 | 3426 | 1.626 | NAL inteiro | 12.968 | 40 s | 6.266 | reprovado — retângulos de macrobloco no mapa de diferença |
| **3435** | 3426 | 963 | NAL inteiro | 7.664 | 28 s | 14 | **reparado** — `114237506 4`, campo uniforme 34 |
| **3439** | 3426 | 988 | NAL inteiro | 7.864 | 36 s | 349 | **reparado** — `114244542 6`, imagem perfeita |
| 3441 | 3426 | 940 | NAL inteiro | 7.480 | 38 s | 1 | reprovado — campo 16–19, devia ser 21 |
| **3442** | 3426 | 2.939 | NAL inteiro | 23.472 | 125 s | 959 | **reparado** — `114260576 3`, erra 6 linhas na borda |
| 3443 | 3426 | 261 | NAL inteiro | 2.048 | 12 s | **0** | sem solução |
| 3444 | 3426 | 266 | NAL inteiro | 2.088 | 12 s | 2 | reprovado — campo 19–21, não uniforme |

### Revarredura do GOP 3426 com a cadeia de hoje

Os 6 frames reprovados foram revarridos depois da correção dos cabeçalhos e da
troca do índice para 132 IDRs. **Os números batem exatamente com os antigos** —
a mudança de cadeia não afetou este GOP, o que é confirmação de que a correção
não mexeu onde não devia.

| frame | bytes | candidatos | soluções | tempo |
|---|---|---|---|---|
| 3428 | 5.211 | 41.648 | 25.537 | — |
| 3430 | 7.223 | 57.744 | 17.448 | — |
| 3432 | 1.626 | 12.968 | 6.266 | — |
| 3441 | 940 | 7.480 | **1** | 42 s |
| 3443 | 261 | 2.048 | **0** | 12 s |
| 3444 | 266 | 2.088 | **2** | 12 s |
| | | | | **320 s no total** |

Os três candidatos de 3441 e 3444 foram inspecionados em resolução cheia, com
amplificação de 12x em torno do preto (são quadros de fade, quase pretos):

| | campo | desvio | esperado | tarja |
|---|---|---|---|---|
| 3442, referência boa | 23,01 | **0,11** | — | 16,00 |
| 3441 `114258749 bit 1` | **17,47** | 1,03 | ~21 | **19,26** |
| 3444 `114262699 bit 5` | 19,69 | 0,95 | < 23 | 16,00 |
| 3444 `114262707 bit 5` | 19,34 | 0,75 | < 23 | 16,00 |

Os frames bons da cauda têm desvio **0,00 a 0,11** — campo liso. Visualmente o
candidato do 3441 mostra quatro ou cinco **bandas horizontais** onde deveria
haver campo uniforme, e os dois do 3444 têm uma faixa mais clara embaixo com um
**degrau vertical no meio da largura**, que é fronteira de macrobloco e não
existe em fade genuíno.

**Os três reprovados.** O 3443, com zero soluções em varredura completa do NAL,
é prova de que precisa de 2 bits — e é o alvo mais forte do projeto hoje.

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

### Frame 2361: reparado — a rejeição anterior está superada

A revarredura com a cadeia limpa devolveu **1 solução** onde a anterior dera 0 —
confirmação direta da armadilha 13. O par `77528961 bit 0` + `77528965 bit 2`
está aplicado, e **medido hoje o quadro passa em tudo**:

| | 2361 hoje | genuínos |
|---|---|---|
| decodificação | limpa | limpa |
| tarja inferior (962–1079) | **16,000 / desvio 0,000** | 16,00 / ≤ 0,41 |
| transição 950–956 | **0,441** | ≤ 1,36 |

Inspecionado em resolução cheia: o noivo diante do espelho, nítido, sem o
borrão horizontal que motivou a rejeição. **O GOP 2333 está em 29 de 29.**

Esta entrada dizia o contrário e ficou desatualizada por algum tempo — o texto
antigo descrevia um par de bits em `77528961` que não é o par aplicado. Lição:
**registrar o offset exato do que entrou**, não a descrição do candidato.

### Busca de segundo bit

| alvo | método | pares | soluções | veredito |
|---|---|---|---|---|
| 3435 | 14 âncoras × NAL inteiro | 35.291 | 0 | derruba a decomposição "um bit a imagem, outro a tarja" |
| 3443 | pares exaustivos | 2,1 M | — | interrompida por decisão de prioridade |

## As 45 "soluções de 1 bit" do `idr_full.txt` estão obsoletas

O `idr_full.txt`, de uma corrida antiga do modo `idr`, registra **45 IDRs com
solução de 1 bit**. Testados os 45 bits contra o critério de hoje, um por um,
aplicando cada um num `patches.txt` temporário:

> **1 de 45 ainda faz o frame decodificar limpo** — e é o do IDR 1773, já
> reprovado pela tarja junto com os outros 26.864.

Os outros 44 não mudam nada: o frame decodifica com erro igual a sem o patch.
Foram achados antes do juiz de imagem existir e antes dos 31 bits
determinísticos entrarem no `patches.txt`.

**Não aplicar aquela lista.** Seriam 44 bits errados num arquivo append-only.
O `idr64.txt` tem a mesma origem e o mesmo problema — os dois servem só como
referência histórica de determinismo da paralelização, que é para o que o
`PARALELIZACAO.md` os cita.

Consequência: **hoje não existe nenhum IDR com solução de 1 bit conhecida.**
Só varrendo.

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

### Os 17 sem imagem: fechados para 1, 2 e 3 bits no cabeçalho

Janela `[5,17)` (96 bits), exaustivo de **exatamente 3 bits**, 142.880
combinações por IDR, **311 s** nos 17. **Zero soluções.**

**Não concluir daí que o cabeçalho precisa de mais de 3 bits.** O critério
exige decodificação perfeita de NALs de 14 KB a 252 KB; se o corpo também
estiver danificado — provável, estando dentro da rajada (armadilha 20) —
nenhum conserto de cabeçalho passa. O que ficou provado é só que ≤3 bits não
deixam o NAL **inteiro** perfeito.

Duas coisas de valor saíram da corrida:

**1. O decoder nomeia o campo quebrado.** Com `LOG=1`:

| IDR | mensagem | campo |
|---|---|---|
| 3290 | `QP 4294966444 out of range` | `slice_qp_delta` (= −852) |
| 1452 | `deblocking_filter_idc 3 out of range` | `disable_deblocking_filter_idc` |
| 2786 | `deblocking filter parameters -118 0 out of range` | `slice_alpha_c0_offset_div2` |

São os **últimos** campos do cabeçalho de slice, ou seja os anteriores
parseiam. Isso é diagnóstico exato, não busca.

**2. Comparar bytes com um IDR bom não mede dano ali.** Os seis bons diferem
muito entre si na mesma região (`01 fc 00 3d…` contra `00 80 00 04…`) porque
`idr_pic_id`, `poc_lsb` e `slice_qp_delta` mudam a cada quadro. Contar bits
diferentes mede variação legítima, não corrupção — é a armadilha 15 outra vez.

**Experimento feito, e fechou a questão.** Trocado o aceite para "produz
imagem", k=1, 2 e 3 na janela `[5,17)` dos 17 IDRs: **zero soluções**, 315 s.

O caminho até esse zero custou duas versões erradas do aceite, e as duas valem
registro porque são fáceis de repetir:

| aceite | o que aprovou | por que é falso |
|---|---|---|
| `cap_w > 0` | 71 candidatos de 1 bit, 2 a 8 por IDR | campo constante 128 — é o quadro de ocultação, **armadilha 1** |
| campo constante por amostra esparsa | 3 candidatos de 2 bits | quadro de ocultação com meia dúzia de macroblocos passa: 98,9%, 100% e 99,5% de listra |
| `linhas_identicas > 400` | **nada** | é o detector que o projeto já confia (armadilha 16) |

**Conclusão: consertar o cabeçalho de slice desses 17 não recupera imagem
nenhuma.** O dano não está confinado ao cabeçalho, continua pelo corpo do
slice — coerente com eles estarem dentro da rajada (armadilha 20).

### E o cabeçalho, dá para garantir mesmo sem imagem?

Dá, mas **por prova, nunca por busca**. A varredura não distingue "cabeçalho
correto" de "cabeçalho diferente que também parseia", então ela é o instrumento
errado para essa pergunta. Foi assim que entraram os 31 bits determinísticos, e
o ganho deles nunca foi decodificar: é toda varredura futura partir de um
cabeçalho correto.

Vale mesmo que o corpo seja irrecuperável, porque cabeçalho provado é verdade
append-only que nunca precisa ser refeita — ao contrário da reconstrução de
pixels, que se descarta a cada mudança a montante.

| campo | provável? | por quê |
|---|---|---|
| `first_mb`, `slice_type`, `pps_id`, `frame_num` | **já feito** | fixos em todo IDR — os 31 bits |
| `poc_lsb` | sim | vale 0 em IDR por definição |
| `idr_pic_id` | provavelmente | parece incrementar de 1 entre os IDRs bons |
| `deblocking_idc`, offsets alpha/beta | sim, se constantes | escolha de encoder; conferir nos 6 bons resolve |
| `slice_qp_delta` | **não** | varia por quadro; só dá para limitar a faixa |

Dos três campos que o decoder acusou, dois são prováveis e um não. O
instrumento é o `cabecalhos.py` estendido, não o `varrek`.

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

## Molde do cabeçalho de IDR — 43 bits anexados por prova

O `molde_idr.py` lê os 12 campos do cabeçalho de slice nos 7 IDRs que
decodificam e encontra **dez constantes**:

```
first_mb=0  slice_type=7  pps_id=0  frame_num=0  poc_lsb=0
no_output_prior=0  long_term_ref=0  deblk_idc=0  alpha_div2=-1  beta_div2=-1
```

Livres só `idr_pic_id` e `slice_qp_delta`. Logo o cabeçalho inteiro se gera de
dois números, e o gerador **reproduz bit a bit os 7 íntegros**, inclusive os
comprimentos, que variam de 60 a 66 bits porque os dois livres são de tamanho
variável. Conserto vira aritmética de bitstream; o decoder não entra.

| | |
|---|---|
| IDRs com cabeçalho já correto | **100 de 128** |
| danificados | 28 |
| ambíguos, pulados por empate (armadilha 19) | 3 |
| bits anexados | **43** |

`patches.txt` foi de 1380 para 1423 linhas, e os 43 entraram também no
`deterministicos.txt` — sem isso o `verify` acusa **47 falsos, exatamente
4 + 43**, porque nenhum deles faz frame decodificar.

Conferido antes de anexar: zero colisão, `verify` mantém 7 válidos e 4 falsos,
os 121 IDRs quebrados continuam 121 e os 7 bons continuam decodificando.

### Censo dos IDRs: são 131, e o `stss` erra dos dois lados

Varridos os 3445 frames pelo prefixo fixo do cabeçalho de IDR (17 bits após o
byte NAL) e depois pelo molde completo. O teste separa sem ambiguidade: IDR
verdadeiro dá distância **0 a 3**, frame comum dá **8 a 14**.

| | |
|---|---|
| IDRs reais | **131** |
| marcados no `stss` | 128 |
| **marcados e que NÃO são IDR** | **2** — 1452 e 1595 |
| **IDRs sem marcação no `stss`** | **5** — 1437, 1466, 2441, 2554, 2913 |

128 − 2 + 5 = 131, e os `idr_pic_id` cobrem 0 a 130: exatamente 131 valores.

**`idr_pic_id == ordinal` vale em 126 dos 131.** As cinco exceções (1379, 2246,
2275, 2499, 2525) são justamente frames onde eu escrevi valor degenerado — ou
seja, a regra é exata e o que sobra de desvio é dano meu, não do arquivo.

O `stss` mora no `moov`, que é o átomo corrompido. Nada garantia que ele
estivesse íntegro; o `ESTADO.md` só vouchsafe `stsz`, `stco`, `stsc` e `ctts`.

Os cinco não marcados casam por quatro sinais independentes: distância 1–3,
tamanho de quadro intra (67–229 KB contra 8–70 KB dos comuns ao redor),
cadência de exatos 29 frames, e o `idr_pic_id` previsto pela sequência.

Os dois falsos falham pelos mesmos quatro: 1452 tem 14.766 B entre IDRs de
67–190 KB e distância **9**; 1595 tem 62.953 B, está 13 frames depois do 1582 —
que por sua vez está a 29 do 1611 — e não sobra vaga de `idr_pic_id` para ele.

**Dano já causado, a corrigir:** o patch base assume o `stss` e forçou o byte
NAL desses dois para `0x65`; depois eu anexei 6 bits em cada um forçando o
prefixo canônico de IDR. São bits escritos em frames que não são IDR.

### O que falta fazer

1. Desfazer os bits indevidos em 1452 e 1595 (anexar os mesmos bits inverte).
2. Corrigir os 5 com `idr_pic_id` degenerado: 1379, 2246, 2275, 2499, 2525.
3. Anexar o cabeçalho dos 5 IDRs recém-identificados.
4. Consertar o `ferramentas.py`, que deriva o byte NAL do `stss` e por isso
   apaga a evidência — foi o que escondeu 2554 e 2913 da varredura por tipo 5.

### Auditoria: 9 dos 25 cabeçalhos anexados têm valor implausível

Com o `idr_pic_id == ordinal` estabelecido, dá para conferir o que foi
anexado. Critério: `idr_pic_id` igual ao ordinal **e** `qp_delta` negativo (os
7 íntegros ficam entre −13 e −2).

**16 dos 25 passam. Nove não:** 1239, 1379, 1553, 1595, 1654, 2246, 2275, 2499,
2525.

O pior é o **1595**: recebeu `idr_pic_id=16` quando a sequência diz 60, e
`qp_delta=22`, ou seja QP 48 quando os íntegros ficam entre 13 e 24. A
minimização de Hamming achou um mínimo degenerado. Conferido que o 1595 é IDR
de verdade — byte NAL `0x65` no arquivo cru, 62.953 B contra 15–27 KB dos
vizinhos — então o erro é do reparo, não da identificação.

**Isto é corrigível.** Append-only não impede: anexar o mesmo bit de novo o
inverte de volta. A correção é rerodar o molde com `idr_pic_id` fixado no
ordinal e `qp_delta` restrito à faixa plausível, e anexar a diferença.

### O `idr_pic_id` incrementa de 1 — e a não-monotonicidade era artefato meu

Primeiro concluí que a regra não estava entendida. Errado, e a causa do erro
vale mais que a conclusão: eu incluía **cabeçalhos danificados** no ajuste, e
neles o `idr_pic_id` recuperado é lixo, porque a minimização de Hamming ajusta
a bits corrompidos.

Olhando a sequência crua, **117 dos 127 passos são exatamente +1**. As 16
anomalias se decompõem:

| origem | quantas |
|---|---|
| cai sobre IDR de cabeçalho danificado | 9 |
| é o IDR seguinte a um danificado — a sequência voltando ao valor certo | 6 |
| **sobra de verdade** | **4** |

As quatro caem em IDRs de espaçamento anômalo (o filme tem 95 vãos de 29):

| transição | vão | efeito |
|---|---|---|
| 2884 → 2942 | **58** = 2×29 | pula 1 |
| 2525 → 2583 | **58** = 2×29 | pula 1 |
| 1452 → 1495 | 43 | pula 1 |
| 1595 → 1611 | 16 | **volta 1** |

Os dois vãos de 58 têm assinatura física: espaçamento dobrado e exatamente um
valor pulado. É um IDR que o encoder emitiu e não está no arquivo, perto dos
frames 2913 e 2554. Não adianta procurar por NAL tipo 5: se existissem como
amostra, o `stsz` os contaria.

**A quarta continua em aberto** — desvio que diminui não se explica por IDR
faltando, e o vão dela é curto.

**Consequência para os 43 bits já anexados:** eles escolheram `idr_pic_id` por
mínima distância, e em alguns dos 25 esse valor quebra a sequência. É
inofensivo e dá para provar: a norma só exige `idr_pic_id` distinto quando
**duas unidades de acesso consecutivas** são ambas IDR, e o menor vão entre
IDRs neste filme é 2 frames. A restrição nunca se aplica aqui. O reparo teria
sido mais fiel fixando o campo em ordinal + desvio, mas como o ganho funcional
é nulo e o `patches.txt` é append-only, fica registrado em vez de corrigido.

**O ganho não é imagem.** Nenhum dos 43 faz IDR decodificar, pelo mesmo motivo
do experimento do `varrek`: o dano continua pelo corpo do slice. O ganho é que
toda varredura futura nesses 25 parte de cabeçalho correto.

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

## Frame 12 — os frames 8 e 12 são gêmeos de bitstream

Medido antes de varrer qualquer coisa, e muda o plano:

| par | tamanho | bytes diferentes | bits | 1 bit | ≥4 bits | última diferença |
|---|---|---|---|---|---|---|
| **8 × 12** | 2935 / 2935 | **288 (9,8%)** | 329 | **250** | **0** | byte **1024** |
| **6 × 10** | 258 / 258 | 71 (27,5%) | 91 | 57 | 1 | byte 255 |
| 2 × 4 | 4474 / 3196 | 3180 (99,5%) | 13978 | 45 | 2602 | byte 3195 |
| 4 × 8 | 3196 / 2935 | 1194 (40,7%) | 4758 | 38 | 747 | byte 1215 |
| 2 × 8 | 4474 / 2935 | 2930 (99,8%) | 12745 | 31 | 2377 | byte 2934 |

Dois fluxos CABAC independentes discordam em ~99% dos bytes e a discordância
típica é de 4 bits ou mais — é o que os controles mostram. Os pares 8 × 12 e
6 × 10 são de outra natureza: concordam em 90% dos bytes e, onde discordam,
**nenhum byte sequer difere em 4 bits**.

Isso não é corrupção nem duplicação de bloco no arquivo. É o codificador
emitindo a **mesma sequência de símbolos** — mesmos tipos de macrobloco, mesmos
vetores, mesmo padrão de resíduo, porque o campo do fade é liso — com um modelo
de probabilidade ligeiramente diferente. Codificação aritmética é contínua nos
parâmetros: modelo parecido, saída parecida, divergindo em um bit aqui e ali e
voltando a casar na renormalização.

### Carga útil real, medida

| frame | NAL | dados | enchimento `00 00 03` | bits varreveis |
|---|---|---|---|---|
| 8 | 2935 | **1135** | 1800 | 9080 |
| 12 | 2935 | **1135** | 1800 | 9080 |
| 4 | 3196 | 1216 | 1980 | 9728 |

O erro do frame 12 é `Reference 4 >= 2` em `MB 35 0, bytestream 1115` — 1820
bytes consumidos. **Os dados reais acabam no byte 1135**, então quando o erro
aparece o decodificador já está lendo o enchimento há 685 bytes. O ponto do erro
não é o ponto do dano: é onde a dessincronização finalmente estourou.

### Duas vias testadas e fechadas

1. **Enxerto integral.** Preservar os 25 primeiros bits do 12 (cabeçalho NAL,
   `first_mb`, `slice_type`, `pps_id`, `frame_num`, `poc_lsb`) e copiar os 328
   bits restantes do frame 8. Resultado: campo 0,00 — preto — e o frame 13
   quebra junto (`left block unavailable, MB 0 55`). Os gêmeos não são o mesmo
   fluxo, são fluxos paralelos.

2. **`num_ref_idx_active_override_flag`.** O frame 12 lê 1 onde o 8, o 6, o 4 e
   o 2 leem 0, e ele é o bit 26 — um flip só. Testado com 1 bit (byte 3 → 0x0d)
   e com 2 bits (byte 3 → 0x1d, igual ao frame 8): **os dois dão preto**. Não é
   defeito de cabeçalho.

### Varredura de 1 bit do frame 12 — fechada, 21 candidatos reprovados

`VISUAL=0 ERROS_BASE=2`, faixa `[5,1145)` = os 9.120 bits de dados reais,
36 s, 12 threads. **21 candidatos decodificam sem erro próprio.**

Todos os 21 caem nos bytes 5 a 18 do NAL — o cabeçalho do slice — e **todos dão
campo 38,00**, que é o valor do frame 9. A rampa manda 40 ou 41. Nenhum deles
produz conteúdo: eles calam o erro transformando o slice em algo que o
decodificador consegue ler como tudo-skip, e tudo-skip num B é exatamente a
cópia da referência.

| candidato | campo | veredito |
|---|---|---|
| `150526 2` | — | frame 12 nem decodifica |
| `150530 0`, `150530 2`, `150530 5`, `150535 7` | 38,00 | hash idêntico ao do frame 9 — cópia declarada |
| os outros 16 | 38,00 | hash próprio, valor errado — cópia com ruído |

O frame 13 quebra em todos os 21 — mas quebra também na linha de base, sem flip
nenhum, então esse juiz não separou nada aqui e não conta contra os candidatos.

**1 bit está fechado para o frame 12.** O próximo passo é a varredura de 2 bits
restrita aos 288 bytes onde os gêmeos 8 e 12 discordam.
