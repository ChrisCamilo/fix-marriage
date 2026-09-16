# Resultados medidos

Números medidos do reparo. Atualizar depois de qualquer corrida que mude
resultado, sempre com o filtro da armadilha 12 aplicado (ver `ARMADILHAS.md`).

Medido em 2026-09-13 com `report` (ffmpeg 8.1.1). O critério agora tem **duas
partes**, e a segunda é indispensável: além do sintático (flush, quadros ==
pacotes, zero logs), exige-se que a imagem não seja propagação vertical — ver
armadilha 7 do `ARMADILHAS.md`. Sem ela o número fica ~2x inflado por lixo.

| classificação | frames | o que é |
|---|---|---|
| **real** | **149** | decodifica limpo **e** tem imagem de verdade |
| propagado | 137 | "limpo", mas slice terminou cedo: listra vertical |
| uniforme | 16 | imagem chapada (preto/fade); pode ser legítima |
| quebrado | 3143 | falha no critério sintático |

Os 149 reais estão em **apenas 4 trechos contínuos** — não em 53, como a medida
antiga sugeria:

| trecho | frames | duração | início | origem |
|---|---|---|---|---|
| **3319–3444** | **126** | **4,20 s** | 110,74 s | íntegro, **o final do filme inteiro** |
| 2333–2359 | 27 | 0,90 s | 77,84 s | íntegro por conta própria |
| 1138–1141 | 4 | 0,13 s | 37,97 s | — |

**Vídeo real hoje: 5,10 s de 114,95 s.** Confirmado por inspeção visual: o frame
2333 mostra o noivo ajustando a gravata diante do espelho; o 3319 mostra o noivo
calçando o sapato.

**O trecho final está completo, sem nenhum frame corrompido.** São 5 GOPs
seguidos (IDRs 3319, 3348, 3368, 3397, 3426) e os 126 frames decodificam. Os
últimos ~10 frames aparecem como `uniforme`/`propagado` nas classificações
automáticas, mas **são o fade para preto que encerra o filme**, não dano: o
brilho médio cai monotonicamente 54,5 → 44,5 → 36,5 → 32,7 → 25,9 → 22,8 → 16,0
e o número de tons vai de 110 a 1. Não tentar "consertar" esses frames.

**Os 5 reparos do `patches.txt` não produziram nenhum frame com imagem.** Eles
pertencem aos frames **2362–2366**, e os cinco estão classificados como
`propagado` — listra vertical. O trecho real de 0,90 s é o 2333–2359, que não
tem patch algum: estava íntegro por conta própria. A anotação antiga de "1,03 s
a partir de 77,84 s exigiu 4 reparos" confundia as duas coisas, porque só media
pelo critério sintático.

Ou seja: **todo o vídeo real que existe hoje sobreviveu sozinho.** O trabalho de
reparo até aqui rendeu zero segundo de imagem.

A medida anterior dizia "306 perfeitos, 5,61 s assistíveis". Dos 306, **137 eram
listra vertical** e 16 chapados. E a região intacta encolheu de 4,20 s para
3,87 s: os frames 3435–3444 também são propagados.

**Atenção:** a contagem de keyframes abaixo usa só o critério sintático, de
antes da armadilha 7 ser conhecida. Os IDRs 29, 128, 215 e 323 foram inspecionados
visualmente e são **listra vertical**, apesar de constarem como "perfeitos". O
número real de IDRs com imagem é muito menor que 57 e precisa ser remedido.

**Keyframes: 57 dos 128 IDRs decodificam perfeitos**, espalhados por todo o
filme (0,0 / 1,0 / 4,3 / 6,2 / 7,2 / 10,8 / 12,7 / 13,7 / 14,6 / 15,2 / 17,2 /
24,5 / 25,5 / 27,2 / 31,8 / 36,9 / 37,9 / 40,1 / 41,0 / 42,3 / 43,3 / 44,1 /
47,0 / 49,9 / 52,8 / 56,2 / 59,1 / 61,2 / 62,1 / 65,0 / 66,0 / 67,0 / 67,9 /
69,1 / 72,0 / 74,0 / 77,8 / 78,8 / 80,7 / 86,2 / 89,1 / 90,1 / 91,0 / 93,5 /
95,4 / 96,2 / 98,2 / 100,0 / 101,7 / 104,3 / 106,5 / 107,4 / 110,7 / 111,7 /
112,4 / 113,3 / 114,3 s). A anotação antiga de "7 keyframes" é de antes da
correção do SPS/PPS.

**Isto tem consequência estratégica.** Todo reparo ancora no IDR anterior
(`ancora_de`), então um GOP cujo IDR está quebrado é irreparável enquanto o IDR
não for consertado — os ~27 frames dele estão bloqueados. Há 57 GOPs com âncora
limpa, prontos para reparo, e **71 GOPs bloqueados pelo próprio IDR**. Consertar
IDR quebrado rende muito mais que consertar frame comum: destrava o GOP inteiro.

Os 5 reparos reais foram revalidados sob o ffmpeg 8.1.1: `verify` com
`BASE_N=1338` dá **5 válidos, 0 falsos**.

Rodar `verify` sem `BASE_N` julga também os 1338 base e reporta 1328 "falsos".
Isso é esperado e não indica problema: o teste pergunta "sem este bit o frame
quebra e com ele fecha perfeito?", e um frame com outra corrupção no corpo nunca
fecha, por mais correto que esteja o cabeçalho. Dado útil desse run: **10 dos
1338 base validam sozinhos** — frames cuja corrupção era só de cabeçalho.
Reforça a anomalia do início do NAL do `INVESTIGACOES.md`.

**A anotação antiga de "2808 dos 3445 frames" era a contagem do ffmpeg** — a
métrica que a armadilha 1 do `ARMADILHAS.md` desmascara. O número rigoroso é 306.

Que 47 dos 53 trechos tenham apenas 2 a 5 frames diz que o dano é **denso e bem
distribuído**, não concentrado: o decoder trava, recupera por poucos frames e
trava de novo, ao longo do filme inteiro.

### Medição de 2026-09-14 (depois da correção do pts)

O casamento do quadro por `pts` e a exigência de imagem mudaram os números. Não
é o arquivo que mudou, é a régua — e a régua anterior lia o quadro errado.

| estado | frames | |
|---|---|---|
| `real` | **186** | decodifica limpo e tem conteúdo |
| `uniforme` | 2 | |
| `propagado` | **0** | agora reprovam no critério, viram `quebrado` |
| `quebrado` | 3257 | |

**Depois dos seis reparos de 2026-09-14** — frames 2360, 2361, 3435, 3439 e 3442
— **149 frames com imagem confiável** (4,97 s).

Os 4 quadros do GOP 1683 que a classificação conta como bons são **listra**: o
IDR 1683 decodifica mas tem 34,3% das linhas idênticas à anterior. Ver armadilha
16. São **6 IDRs utilizáveis** no filme, não 7. O `verify` dá `7 válidos, 4
falsos, 31 determinísticos pulados`.

O número bruto do modo `estado` é 193, mas **40 deles não valem**: estão em GOPs
cujo IDR não decodifica, e saem em listras verticais mesmo passando no critério.
Ver armadilha 12 do `ARMADILHAS.md`. Contagem honesta é sempre com o filtro do IDR.

**Medir trecho contínuo em ordem de decodificação está errado** — quem assiste vê
em ordem de exibição, e dentro do GOP elas não coincidem. Corrigido aqui;
ordenando por `poc`:

| frames | duração | em decodificação |
|---|---|---|
| 108 | **3,60 s** | 3319–3426 |
| **29** | **0,97 s** | **2333–2361 — GOP COMPLETO** |
| 10 | **0,33 s** | 3431–3442 |

**4,90 s assistíveis** (eram 4,54 s no começo do dia).

### Cinco GOPs completos: 2333, 3319, 3348, 3368, 3397

O **2333 fechou em 29 de 29** com os reparos dos frames 2360 e 2361, e é o
primeiro GOP íntegro fora do bloco final — 0,97 s de cena real, o noivo diante
do espelho ajustando a gravata.

Os 29 cabeçalhos do GOP são agora **aritmeticamente impecáveis**: zero violações
da regra da norma, com `frame_num` incrementando só nas referências e `poc_lsb`
subindo de 2 em 2. Os dois frames reparados caem exatamente nos valores
previstos:

```
2359  ref_idc 0   frame_num 8   poc_lsb 50
2360  ref_idc 0   frame_num 8   poc_lsb 52   <- reparado
2361  ref_idc 0   frame_num 8   poc_lsb 54   <- reparado
2358  ref_idc 2   frame_num 7   poc_lsb 56
```

Isso é **confirmação independente**: o bit do 2360 foi achado por varredura
exaustiva e escolhido pelos juízes de imagem, sem a aritmética participar da
decisão — e o resultado pousou no valor que ela prevê.

O final do filme em ordem de exibição, com `*` marcando quebrado:

```
3426 3428* 3427 3430* 3429 3432* 3431 3434 3433 3436 3435 3438 3437 3440 3439 3442 3441* 3444* 3443*
```

Só **três buracos** — 3428, 3430 e 3432 — separam os 3,60 s do trecho novo.
Fechá-los emenda tudo em ~4,0 s contínuos, e é o alvo de maior retorno no final.

### O final do filme é um fade, e isso é medida, não impressão

Medindo só a área útil (linhas 136–943, sem as tarjas), o desvio padrão cai:
41,9 no frame 3420 · 21,2 no 3423 · 6,4 no 3427 · 1,7 no 3429 · **0,00 do 3431
em diante**. Dali para frente `min == max`: são campos de um único valor.

A cadência do GOP 3426 é de **período 2**, e a ordem de exibição não é a de
decodificação. Por `poc`: `3434 3433 3436 [3435] 3438 3437 3440 [3439] …`. Os
valores caem linearmente: 43, 41, 38, 36, **34**, 32, 29, 27.

Isso dá um gabarito aritmético — o valor de um frame quebrado é a média dos
vizinhos de exibição. Foi assim que saiu o reparo do 3435. **Cuidado:** ancorar
nos vizinhos de decodificação dá o frame errado; esse erro foi cometido e
elegeu o candidato errado antes de ser pego.

Assinatura de um frame bom do fade, que é o que um candidato precisa reproduzir:

- topo (linhas 0–129) uniforme 16;
- campo (136–**949**) uniforme, no valor previsto — inclusive as seis últimas
  linhas, que valem o mesmo que o resto;
- borda **seca** na linha 950, sem rampa: a rampa esfumada é o borrão da
  ocultação;
- tarja inferior (951–1079) com **média 16,00 e desvio ≤ 0,18**.

**A tarja NÃO é chapada** — corrigido em 2026-09-14 depois de medir o filme
inteiro. Nos frames bons ela varia de **9 a 24**, com média exatamente 16,00 no
topo e embaixo, em qualquer ponto do filme: é grão que o encoder preservou.
Filtrar por "tarja uniforme 16" rejeita frame realista e premia frame liso
demais. Esse erro foi cometido e descartou 15 candidatos corretos do frame 3442
antes de ser pego.

**A tarja é o gabarito mais útil que este projeto tem**, e não estava sendo
usada. Ela cobre ~25% de cada quadro (259 de 1080 linhas), o conteúdo dela é
conhecido *a priori* e vale para **todo frame do filme** — inclusive onde não há
vizinho íntegro, que é onde todos os outros critérios falham. E localiza o erro:
o topo é decodificado primeiro e a tarja de baixo por último, então topo limpo
com tarja suja diz que o bit ruim está no fim do NAL.

### Nenhum frame do fade é reparável com 1 bit

Varredura exaustiva do NAL inteiro dos seis quebrados do trecho final:

| frame | bytes | soluções de 1 bit | melhor candidato |
|---|---|---|---|
| 3435 | 963 | **14** | **REPARADO** (`114237506 4`): campo 34, tarja com desvio 0,41 (vizinhos ≤ 0,18) |
| 3439 | 988 | 349 | **REPARADO** (`114244542 6`): imagem **perfeita**, linhas 136–949 uniformes em 25, como os vizinhos bons. 300 candidatos empatam nisso; a tarja desempatou |
| 3441 | 940 | **1** | campo 16–19 e tarja 19–20: errado |
| 3442 | 2939 | 959 | 15 com campo 23 e tarja perfeita, mas a fileira de macroblocos 944–949 sai +1,3 a +2,0 acima do campo (nos frames bons ela iguala o campo) |
| 3443 | 261 | **0** | |
| 3444 | 266 | 2 | borda borrada |
| 3428 | 5211 | 25537 | ambíguo demais |
| 3430 | 7223 | 17448 | ambíguo demais |

Todos precisam de **mais de um bit**, e isso foi reconfirmado com o critério
corrigido da tarja. A busca linear pelo segundo bit do 3435 (fixando o primeiro,
já aplicado, e varrendo o resto) achou 1244 soluções e **nenhuma** melhora a
tarja — 1205 delas dão a tarja idêntica, o que diz que aquele defeito não está
neste NAL. Cada solução de 1 bit conserta uma parte
do quadro e deixa defeito em outra — é o sinal de que o dano é múltiplo, e a
peneira por região do quadro é o que revela isso. Sem olhar a tarja inteira, o
3442 passaria por resolvido com 15 candidatos "limpos".

### Descartado: "um bit conserta a imagem, outro conserta a tarja"

Hipótese natural depois de ver que as soluções de 1 bit acertam o campo e erram
a tarja. **Testada e negativa** no frame 3435, em 2026-09-14.

Método (e vale reaproveitar, porque é linear em vez de quadrático): fixar cada
uma das 14 soluções de 1 bit como âncora e varrer o NAL inteiro atrás de um
segundo bit. São 1244 a 3381 segundos bits por âncora, **35.291 pares no total,
e nenhum melhora a tarja**. Com a âncora aplicada no `patches.txt`, 1205 dos
1244 dão a tarja *idêntica* — mil flips que não mexem naquela região dizem que
o defeito não está codificado ali.

Alcance do resultado, para não virar conclusão maior do que é: só cobre pares em
que **um dos bits é, sozinho, solução completa**. Um par em que nenhum dos dois
funciona isolado ficaria de fora, e esse espaço tem 29,4 milhões de pares — 0,12%
foi testado. Mas foi o subconjunto que a hipótese previa.

A hipótese só se aplicaria a 3 dos 6 frames do fade de qualquer forma: 3435,
3439 e 3442 têm solução de 1 bit que acerta o campo (34, 25 e 23, os valores que
o fade prevê); 3441 e 3444 não, e o 3443 não tem solução de 1 bit nenhuma.

Custo da varredura exaustiva de pares, para quem for tentar: o `varre2` roda
~170 pares/s por frame pequeno. O 3443 (261 bytes) são 2,1 milhões de pares,
~3,5 h — foi iniciado e interrompido por decisão de prioridade. O 3442 (2939
bytes) seriam 275 milhões, ~450 h. Não é caminho para NAL grande.

### `verify`: 4 patches ficaram insuficientes, não falsos

Com o critério atual dá `2 válidos, 4 falsos`; com `VISUAL=0`, que é o critério
da época em que entraram, dá `5 válidos, 1 falso`. Os patches dos frames 2362,
2364, 2365 e 2366 continuam satisfazendo a sintaxe — o que mudou foi a régua,
que ganhou a exigência de imagem e a correção do pts. **Não removê-los:** podem
ser bits necessários de um reparo de vários bits. O append-only está certo aqui.

O espelho disso é o patch novo do 3435, que aparece como falso sob `VISUAL=0`
com `com=0 sem=0`: aquele frame sempre passou na sintaxe e só falhava na imagem.
Os dois critérios medem coisas diferentes; nenhum sozinho decide.

## Classificação por GOP — medida hoje, frame a frame

Reclassificados os 7 GOPs cujo IDR decodifica, com o índice de 132 IDRs e os
cabeçalhos corrigidos. `bom` = decodifica limpo pelo critério do `reparador.c`.

| GOP | frames | bons | quebrados |
|---|---|---|---|
| 1683 | 29 | **0** | **29** — ver abaixo |
| **2333** | 29 | **29** | 0 |
| **3319** | 29 | **29** | 0 |
| **3348** | 20 | **20** | 0 |
| **3368** | 29 | **29** | 0 |
| **3397** | 29 | **29** | 0 |
| 3426 | 19 | 13 | 6 — 3428, 3430, 3432, 3441, 3443, 3444 |

**Cinco GOPs completos**, um a mais do que a contagem anterior: o frame 2361
está reparado e o GOP 2333 fechou em 29 de 29.

### O GOP 1683 está inteiramente danificado

Medido pelo `panorama`: dos 26 frames que o decoder emite, **nenhum tem tarja
correta**. Todos ficam entre 125 e 133 com desvio ~60, contra 16,000 / 0,000 dos
genuínos, e a razão V/H fica entre 0,29 e 0,77 contra o piso 0,78.

**Os "4 frames bons" que a classificação contava nunca foram bons.** Eles
passam no critério sintático e têm tarja em 132,8. Inspecionados em resolução
cheia, mostram ~30% de conteúdo no topo e o resto esticado em colunas, sem
tarja preta nenhuma.

Isto encerra o GOP 1683 como alvo: não é o IDR listrado bloqueando frames bons,
é o GOP inteiro perdido.

**Nenhum GOP está disponível para reparo hoje.** Cinco estão completos, o 3426
só tem fade, e o 1683 está perdido. Abrir um novo exige consertar um IDR — linha
que está em 13 varridos e zero reparos.

## Panorama do filme — medido em 11 s, todo quadro emitido

O modo `panorama` decodifica cada GOP numa passada e mede todo quadro que o
decoder emite. Critério de aceite: **tarja em 16,000 com desvio 0,000**, que é
gabarito conhecido a priori e não se burla, mais cena de verdade (listra < 5% e
razão V/H ≥ 0,70) ou campo uniforme legítimo de fade.

| | frames |
|---|---|
| o decoder emite | 2.623 de 3.445 |
| com tarja perfeita | 160 |
| **com imagem confiável** | **158** |
| destes, que passam no critério rigoroso | 144 |
| **destes, que NÃO passam** | **14** |

Os 14 são exatamente as duas pontas do filme:

| trecho | frames | o que é |
|---|---|---|
| abertura | 0, 1, 2, 4, 5, 7, 8, 9, 12 | fade-in a partir do preto |
| encerramento | 3428, 3430, 3441, 3443, 3444 | fade-out para o preto |

Em ordem de exibição, os dois fades são rampas monotônicas perfeitas — o
encerramento vai 36, 34, 32, 29, 27, 25, 23, 21, 19, 16, com desvio de campo
0,00 e tarja 16,0000 em todos. **Os frames 3441, 3443 e 3444, que foram alvo de
varredura e tiveram candidatos reprovados, já produziam o valor certo.**

**A hipótese de que havia muito mais filme assistível estava errada.** De 3.445
frames, o critério rigoroso e o gabarito da tarja discordam em 14. O critério
está bem calibrado; o ganho é modesto e real.

Trechos contíguos em ordem de decodificação: **3319–3428 (110 frames)**,
**2333–2361 (29)**, **0–12 (11)**, mais alguns isolados na cauda.

## Vídeo assistível — 168 frames, 5,6 s

Montado com `TARJA=1`, que faz o `serie` aceitar também o quadro cuja imagem
está certa mas não passa no critério rigoroso.

| trecho | frames | segundos | o que é |
|---|---|---|---|
| 0–12 | 13 (11 reais + 2 sintetizados) | 0,43 | fade-in de abertura |
| 2333–2361 | **29 de 29** | 0,97 | o noivo diante do espelho |
| 3319–3444 | **126 de 126** | 4,20 | bloco final, fade-out incluído |
| **total** | **168** | **5,61** | |

O bloco final passou de 120 para **126 de 126** — o `TARJA=1` recuperou 3428,
3430, 3432, 3441, 3443 e 3444, que antes entravam como buraco. **Nenhum deles
precisou de reparo**: já produziam o valor certo.

Só 2 frames em 168 são sintetizados (os 10 e 11 da abertura), e estão
registrados no `remontados.txt`.

## O mapa do dano — três ilhas, não manchas

Procurei sobreviventes fora dos trechos conhecidos, testando a hipótese de que
**NAL pequeno teria menos superfície para a rajada atingir**. Ela é falsa, e o
resultado é definitivo:

| tamanho do NAL | frames fora dos trechos | com tarja correta |
|---|---|---|
| até 3 KB | 8 | **0** |
| 3–10 KB | 519 | **0** |
| 10–50 KB | 2.082 | **0** |
| acima de 50 KB | 668 | **0** |
| **total** | **3.277** | **0** |

A correlação que parecia existir era confusão: os NALs pequenos do filme são os
quadros de fade, e os fades ficam dentro das regiões que já estavam boas.
**Tamanho não protege.**

E o que sobra é isto:

| ilha | frames | posição no arquivo |
|---|---|---|
| abertura | 0–12 | 0,1% |
| o espelho | 2333–2361 | 66,9% a 67,9% |
| bloco final | 3319–3444 | 97,4% a 100% |
| *buraco 1* | *13–2332* | *76,3 MB* |
| *buraco 2* | *2362–3318* | *33,7 MB* |

**168 frames de 3.445 — 4,9% do filme.** De 114 MB, cerca de 110 MB estão
perdidos.

Vale corrigir a linguagem que este projeto vinha usando: o dano não é "rajada"
no sentido de mancha num arquivo são. **O arquivo está quase todo destruído, e
há três ilhas que escaparam.** Os 5 reparos bem sucedidos da história do projeto
estão todos nas bordas dessas ilhas, o que é coerente com a armadilha 20 — mas
em escala oposta à que ela descrevia.

**Consequência para o planejamento:** não existem sobreviventes escondidos. A
busca por frames bons fora das três ilhas está encerrada com medida, não com
estimativa.


## Bloco de abertura (GOP 0) — estado em 16/09/2026

Os treze primeiros quadros do filme, que abrem no fade-in a partir do preto:

| quadro | campo | tarja | como chegou aqui |
|---|---|---|---|
| 0–9 | 16 a 38 | 16,000 | intactos ou consertados pelos cabeçalhos |
| **10** | 36,00 | 16,000 | **reparado** — 3 bits, campo previsto pela rampa antes da busca |
| **11** | 43,00 | 16,000 | imagem própria; **tarja repintada** (ocultação registrada) |
| **12** | **41,00** | **16,000** | **reparado** — 2 bits, `150530 4` + `150538 0` |
| 13 | — | — | quebrado, primeiro quadro de conteúdo real |

**13 de 13 quadros do bloco de abertura decodificam**, cada um com conteúdo
próprio — nenhum hash repetido. Um só deles tem parte sintetizada, e é a tarja
do frame 11, cujo valor verdadeiro é conhecido a priori.

A rampa completa do fade-in, toda medida:

```
16  18  21  23  25  27  29  32  34  36  38  41  43
```
