# Critérios de julgamento, em ordem de precedência

Como decidir se um candidato entra no `patches.txt`. A ordem importa: **o juiz
da imagem manda**, e o da tarja é subordinado a ele.

O objetivo do projeto é assistir ao filme. Uma tarja com defeito de sete linhas
é um risco fino piscando por 1/30 de segundo; um frame ausente é um segundo de
nada. Entre os dois, a imagem ganha.

## 0. Gabarito e proxy — por que a ordem é esta

A hierarquia abaixo não é gosto. Ela sai de uma propriedade que separa os juízes
em duas classes, e entender a propriedade evita reinventar critério ruim.

Toda varredura aqui gera dezenas ou centenas de milhares de candidatos e precisa
escolher. **Juiz** é o critério que escolhe. Ele é **burlável** quando, num
conjunto grande, existem candidatos que pontuam bem sem estar certos — e a
busca sempre encontra esses, porque é o que uma busca faz.

A causa é sempre a mesma: o juiz mede algo *correlacionado* com estar certo, em
vez de medir *estar certo*.

**Um juiz é burlável na proporção de quão pouca informação ele confere:**

| juiz | informação conferida | resultado |
|---|---|---|
| blocagem na faixa dos genuínos | **1 número** | burlado |
| croma U e V dentro da faixa | **2 números** | burlado |
| média e desvio calibrados no próprio quadro | **2 números** | burlado |
| linhas idênticas usadas como alvo | **1 número** | burlado 3x |
| **a tarja** | **~227 mil pixels** em valor prescrito | nunca cedeu |

Não existe coincidência que produza 227 mil pixels certos. Por isso nenhum dos
26.865 candidatos do IDR 1773 passou: não é limiar apertado, é que não há como
acertar por acaso.

**Gabarito** é conteúdo conhecido *independentemente da busca*. Existem três
neste projeto, e onde houve reparo, havia um deles:

| gabarito | de onde vem |
|---|---|
| a tarja | medida em 16 frames íntegros: `Y=16`, `U=V=128`, linhas 962–1079 |
| vizinho temporal | o frame 2360 tinha quadros bons dos dois lados |
| aritmética | o nível do fade do 3435, previsto por extrapolação da reta |
| **sintaxe por macrobloco** (2026-09-23) | o QP é **o mesmo nos 8.160 MBs** de um IDR íntegro e só há I16x16 e I4x4: todo MB com QP diferente do slice, ou I_PCM, é lixo. Conferido nos 131 IDRs (2026-09-23): os 7 que decodificam inteiros têm QP constante nos 8.160 MBs e só I16x16/I4x4; nos quebrados o QP só muda nos últimos ≤ 388 MBs antes de o decodificador parar (mediana 49), nunca num trecho que siga decodificando, e os 13 I_PCM estão todos neles |

O quarto gabarito, o de sintaxe, só diz **não**: MB com QP certo pode ser lixo,
porque os contextos do CABAC adaptados a "delta sempre zero" fazem o lixo
continuar dando zero por um tempo. Ele marca o limite de onde a fronteira do
dano pode estar, não o ponto exato. Ler com `ffmpeg -debug qp+mb_type -ec 0
-skip_loop_filter all`.

Onde não há nenhum dos três — o IDR 1683 — **todo juiz vira proxy e todo proxy
cai**. Não adianta procurar um melhor.

### A tarja é gabarito completo, não só local

Os macroblocos são decodificados de cima para baixo, e a tarja inferior é a
**última** coisa do slice. Como o CABAC é serial, é impossível chegar ao último
macrobloco correto atravessando trecho corrompido.

**Tarja certa implica quadro inteiro certo.** Ela não prova só a região que
mede; prova tudo que veio antes. É o verificador mais forte do projeto.

### Duas ressalvas, e a segunda já custou caro

1. **Inburlável não promete que existe solução.** Promete que o "não" e o "sim"
   são confiáveis. No 1773 todos receberam não.

2. **A propriedade é do uso, não da medida.** A tarja é gabarito legítimo, mas
   ordenar os 26.865 candidatos por *distância até 16* e pegar "o melhor"
   converte um teste binário inburlável de volta em proxy burlável — e o
   vencedor matou 368 linhas de imagem boa, porque listra uniforme fica mais
   perto de "plana" do que conteúdo vazado. É a armadilha 19.

   **Gabarito responde sim ou não. Quando responde não para todos, a busca
   acabou — não existe "o menos não".**

## 0.5 A rampa do fade é gabarito aritmético

Os dois fades do filme têm a **mesma inclinação**, medida nos quadros íntegros:

| | inclinação | medida em |
|---|---|---|
| fade-out, GOP 3426 | **−2,222** níveis por quadro | 36 → 16 em 9 passos, todos intactos |
| fade-in, GOP 0 | **+2,250** níveis por quadro | 16 → 43 em 12 passos |

O modelo linear acerta **todos** os quadros medidos do fade-in com erro máximo
de 0,5 — e falha exatamente nos dois que são cópia:

| poc | frame | medido | previsto |
|---|---|---|---|
| 16 | 7 | 34,00 | 34,00 |
| **18** | **10** | 34,00 | **36,25** |
| 20 | 9 | 38,00 | 38,50 |
| **22** | **12** | 38,00 | **40,75** |
| 24 | 11 | 43,00 | 43,00 |

**Isto corrige a leitura da armadilha 23 para quadro de fade.** Lá está escrito
que alvo de campo uniforme não é julgável porque nenhuma medida de imagem separa
candidatos. Vale para quadro isolado — mas **dentro de um fade a rampa fixa o
valor**, e passa a ser gabarito aritmético como o do frame 3435, que se
consertou exatamente assim.

Então quadro de fade **é julgável**, desde que os vizinhos de exibição estejam
íntegros e a rampa possa ser ajustada.

## 1. Juiz da imagem — decide

Mede só a área que se assiste, **linhas 136 a 949**. Um candidato que passa aqui
é aceito mesmo com a tarja fora do limite.

**O critério se calibra sozinho**, sem constante inventada: o frame verdadeiro
está entre dois vizinhos de exibição, então tem que estar **mais perto do ponto
médio deles do que eles estão um do outro**.

```
distância(candidato, média dos vizinhos)  <  distância(vizinho A, vizinho B)
```

Medido no frame 2361, cujos vizinhos de exibição são 2360 (poc 52) e 2358 (56):

| | distância ao ponto médio | veredito |
|---|---|---|
| distância entre 2360 e 2358 | **1,752** | é a régua |
| candidato de 1 bit | **1,308** | passa |
| candidato de 2 bits | 1,908 | reprova — pior que qualquer vizinho |

Mais três conferências, todas contra os vizinhos genuínos:

- **blocagem da área de imagem** não pode subir. Dano de macrobloco a empurra
  para cima; blocagem *baixa* não é defeito. No 2361: candidato 0,80 contra 1,04
  e 1,15 dos vizinhos.
- **média e desvio** têm que bater. No 2361: 166,32 / 63,88 contra 166,19 / 63,99
  e 166,21 / 63,98 — indistinguível.
### O último juiz sou eu olhando — e ele decide

As métricas acima são **peneira**, não veredito. Elas reduzem milhares de
candidatos a um punhado; o punhado eu **abro e olho**, em resolução cheia, e
escrevo o que vejo.

Isso não é formalidade. As duas coisas se pegam mutuamente:

- **O olho pegou o que o número não viu:** o frame 3428 tinha blocagem 1,429
  contra 1,399 do pior vizinho — margem que parecia estreita. Ampliado em
  resolução cheia, mostrava degraus de 16×16 na borda da perna e manchas
  quadradas no chão. Foi removido depois de aplicado.
- **O número pegou o que o olho não viu:** o vazamento de sete linhas na tarja
  do 2361 é invisível em imagem reduzida. Só apareceu esticando 18x e marcando
  em vermelho os pixels fora de faixa.

**O que eu procuro ao olhar**, em recorte de resolução cheia sem redução:

- nitidez de borda onde o original é fino — fios de cabelo na testa, dedos
  separados, dobras de tecido, o desenho do papel de parede;
- degraus na grade de 16 pixels, que é a assinatura de macrobloco quebrado;
- listra horizontal ou vertical, borrão, vazamento de cor;
- coerência de movimento com os vizinhos de exibição: o quadro tem que estar
  *entre* eles também no gesto, não só na estatística.

**Limite honesto:** não dá para olhar milhares. Por isso a ordem é peneira
numérica primeiro, olho depois — e por isso a peneira não pode ser tão apertada
que descarte antes de eu ver, que foi o erro cometido com a tarja.

**O veredito visual vai escrito no commit, em palavras.** "Rosto nítido, dedos
separados, papel de parede com borda afiada, sem bloco" vale mais que um número,
porque daqui a um ano ninguém vai reproduzir a métrica — mas a frase continua
dizendo o que foi visto.

- **olhar em resolução cheia**. Métrica agregada esconde dano local: foi assim
  que o frame 3428 quase entrou com quebra de macrobloco visível.

## Geometria da tarja — medida, não estimada

Medida nos 16 frames que **nunca precisaram de reparo** (4 do GOP 2333, 12 do
bloco final). A geometria é idêntica nos 16, sem uma linha de variação, e é
simétrica — 130 + 820 + 130 = 1080:

| faixa | o que é |
|---|---|
| 0 – 124 | tarja de topo, **exatamente 16** em todos |
| 125 – 129 | borda: resíduo do deblocking, pixels de 8 a 27 |
| **130 – 949** | **imagem** |
| 950 – 961 | borda: resíduo do deblocking, pixels de 3 a 27 |
| 962 – 1079 | tarja inferior, **exatamente 16** em todos |

A borda existe porque o filtro de deblocking suavisa o degrau entre a última
fileira de macrobloco da imagem e a primeira da tarja. Quem mede a fronteira
procurando "primeira linha que não é 16" acha 126–128 no topo e 951–958
embaixo — **está lendo resíduo, não conteúdo**.

Consequências práticas:

- Reconstruir tarja é seguro a partir da **950**, e o valor exato (`Y=16`,
  `U=V=128`) só vale sem ressalva de **962** em diante.
- A área de imagem usada abaixo (136 a 949) é conservadora em 6 linhas no topo.
  O limite de baixo, 949, está exato.
- Em quadro de fade o teste não funciona: o campo inteiro é quase uniforme e
  não há contraste entre imagem e tarja para separar as duas.

## 2. Juiz da tarja — subordinado

Vale onde o juiz da imagem não alcança: **IDR em GOP escuro, sem vizinho íntegro
nenhum**. Ali é o único gabarito que existe, e decide sozinho.

Duas regiões, com tolerâncias muito diferentes — ver armadilha 15:

| região | genuínos (144 quadros) | tolerância |
|---|---|---|
| transição 950–956 | desvio até 1,36, pior pixel 14, até 0,238% fora de ±10 | **frouxa** — só reprova se o juiz da imagem também reprovar |
| fundo 957–1079 | desvio até 0,40, pior pixel 7, **zero** fora de ±10 | **estrita** — vale sempre |

O fundo continua rigoroso de propósito: os candidatos de IDR reprovados tinham
tarja média de 90 a 180 ali, ordens de grandeza fora. Afrouxar a transição não
os reabilita.

## 3. Juiz da aritmética — necessário, não suficiente

`frame_num`, `poc_lsb` e o molde do cabeçalho de IDR (`65 88 80`). Erro aqui é
**provado**, não estimado.

Mas cabeçalho correto não garante frame correto, e o 2361 é a prova: o candidato
de 2 bits tem o cabeçalho aritmeticamente perfeito e a **pior** imagem das duas
tentativas. Se os juízes discordarem, vale a imagem.

## O que isto NÃO afrouxa

- O critério sintático do `reparador.c` continua igual: flush, quadros ==
  pacotes, zero linhas de log.
- O fundo da tarja continua estrito.
- Frame que não produz imagem nenhuma não passa por lugar nenhum.
- Patch aceito com defeito conhecido tem o defeito **escrito no commit**, em
  números. Nunca entra como se fosse perfeito.

## O croma é por macrobloco — e o que dá e o que não dá para prever

Medido em **94.080 macroblocos de 16 quadros intactos**, dos GOPs 2333 e 3319.

### Como o formato define

O H.264 dá **uma predição de croma por macrobloco** — `intra_chroma_pred_mode`
(DC, horizontal, vertical, plano) num macrobloco intra, ou vetores derivados dos
de luma num inter — mais um `coded_block_pattern` de croma que diz se há resíduo
**DC apenas** ou **DC+AC**. Sem AC, o bloco 8×8 inteiro de croma é **um valor só**.

### Dentro do macrobloco: baixa variação, mas não constante

| desvio do croma U dentro do macrobloco | quadros bons |
|---|---|
| chapado, < 0,05 | **14,2%** — sem resíduo AC |
| quase, < 0,5 | 16,3% |
| com textura, < 2 | 58,3% |
| acima de 2 | 11,2% |

### Entre vizinhos: previsível em distribuição, não por bloco

| diferença para o macrobloco à esquerda | percentil |
|---|---|
| 0,81 | 50 |
| 1,78 | 75 |
| 3,42 | 90 |
| 5,12 | 95 |
| 10,98 | 99 |
| **60,08** | máximo |

**Não dá para prever um macrobloco individual.** O 1% que difere mais de 11 são
as bordas reais da imagem, e o máximo chega a 60. Quem tentar usar "o croma tem
que parecer com o do vizinho" como regra por bloco vai condenar toda borda.

**Mas a distribuição é apertada e independente da cena**, e é isso que serve de
juiz:

| estado | viz p50 | viz p90 | **viz p99** | dentro do MB |
|---|---|---|---|---|
| **5 quadros bons** | 0,50–0,98 | 3,03–4,58 | **8,48–14,31** | 0,91–1,33 |
| borrão | 0,50 | 1,50 | **3,69** | 0,33 |
| lixo | 1,00–1,22 | 7,38–8,56 | **27,12–33,59** | 1,78–2,51 |

**Os dois são de duas pontas**, e é o ponto da armadilha 39: borrão fica
**abaixo** porque repetir linha achata a cor, lixo fica **acima** porque estoura,
e a imagem real está no meio. Nenhuma métrica do tipo "quanto mais melhor"
funciona aqui.

**Juiz proposto:** p99 da diferença entre vizinhos em `[7, 18]` **e** desvio
médio dentro do macrobloco em `[0,7, 1,6]`. Os dois medidos na mesma faixa de
linhas, e os dois calibrados contra quadro verificado.

### Estado da implementação do juiz de croma

Reescrito por macrobloco, com as duas estatísticas e faixa de duas pontas:

```
dentro do MB em [0,70; 1,60]   e   p99 entre vizinhos em [7,0; 18,0]
```

**Verificado contra medição independente em python, batendo em três casos:**

| quadro | ferramenta | python | veredito |
|---|---|---|---|
| 2333 bom | 0,97 / 12,09 | 0,97 / 12,09 | DENTRO |
| 3326 bom | 1,04 / 11,11 | 1,04 / 11,11 | DENTRO |
| 3047 borrão | 0,33 / 3,69 | 0,33 / 3,69 | **FORA** — chato demais |

**Defeito encontrado e corrigido:** a primeira versão nunca foi ligada. O
`piso_croma` estava declarado, lido do ambiente e implementado, mas a linha que
o chama **não existia na cadeia de pontuação** — um `replace` de script não
casou o padrão, não alterou nada e mesmo assim imprimiu "ok". Por isso
`PISO_CROMA=1` aceitava lixo: não estava julgando coisa nenhuma.

**A divergência que eu tinha registrado não existia — era eu lendo a linha
errada.** O `croma_real` imprime uma linha de depuração a cada chamada, e ele é
chamado tanto pelos *workers*, um por candidato, quanto pela base. Eu pegava a
última linha com `tail`, e a intercalação entre `stdout` e `stderr` no pipe não é
determinística: às vezes a última era a de um candidato quebrado, com 0,00.

Listando a sequência inteira em ordem, o que se vê é:

```
[croma] dentro_MB=0.00  p99=0.00      <- um worker, candidato quebrado
[base]  ... u[200][500]=122 ...
[croma] dentro_MB=2.51  p99=33.59     <- a BASE, igual ao python
[+] croma da base: FORA da faixa      <- reprovado corretamente
```

E os bytes de croma batem exatamente entre os dois caminhos — 131/131/131 no
borrão e 122/122/122 no estado de lixo. **O juiz estava certo desde que foi
ligado.**

**Validação final, seis casos, todos com o veredito certo:**

| caso | veredito |
|---|---|
| 2333 bom | **DENTRO** |
| 3326 bom | **DENTRO** |
| 3374 bom | **DENTRO** |
| 3047 borrão (chato demais) | **FORA** |
| 3047 lixo, 4 bits | **FORA** |
| 3047 lixo, 3 bits | **FORA** |

O piso **pode ser usado**. A lição que fica é de leitura de saída, não de código:
ferramenta que imprime a mesma linha de vários lugares precisa **marcar de onde
vem**, senão `tail` mente.

**E o QP confere:** o IDR 3047 tem QP de croma **29**, idêntico ao do 2333, e a
calibração cobre 25 a 29. A faixa vale para ele.
## O juiz do IDR 3047 — três versões, e o que sobrou de cada uma

Este juiz foi reescrito três vezes na mesma sessão. O que está abaixo é a
versão medida; as duas anteriores estão registradas porque cada uma produziu uma
conclusão que eu apresentei como certa e não era.

### A versão que vale

| # | critério | medida | limiar |
|---|---|---|---|
| 1 | **libera** | `fronteira_borrao` com tolerância 1 tem que descer | qualquer descida |
| 2a | **limpa na luma** | `blocagem_faixa` na faixa liberada | `<= 1,45` |
| 2b | **limpa na cor** | p99 da diferença entre pixels vizinhos de croma, janela **fixa** de 64 linhas a partir da fronteira da base | `<= 8` |
| 3 | **ainda é borrão** | cobertura de linhas-cópia abaixo da fronteira | `>= 0,90` |
| 4 | **croma plausível** | `croma_real` por macrobloco, 160–639 | faixa calibrada |

O critério 2b é o que seleciona. O 1 ordena. O 3 e o 4 são pisos baratos contra
o caso degenerado; nenhum dos dois escolhe entre candidatos bons.

### O que cada versão errou

**Versão 1 — "as três partes".** Fronteira, blocagem e estrutura do borrão
(densidade de trechos, comprimento médio, maior trecho). Selecionou 1 candidato
em 28.400, o mesmo que o olho tinha escolhido, e eu apresentei isso como
validação do juiz inteiro. Dois dos três critérios não mediam nada:

- a **estrutura** era artefato da igualdade exata (armadilha 40) — com a
  tolerância certa o borrão é um trecho só, e não há estrutura para comparar;
- o piso de trechos era **absoluto sobre uma quantidade proporcional à área**
  (armadilha 42), e descartava 801 de 920 candidatos por liberarem demais.

Quem reprovava o candidato descartado pelo olho era a **blocagem, sozinha**.

**Versão 2 — densidade em vez de contagem.** Corrigiu a armadilha 42 e passou de
119 para 920 sobreviventes na etapa 2. Mas continuava com a igualdade exata, e
por isso aprovou **591 candidatos "sem borrão nenhum"** que na tela são o mesmo
quadro listrado. Foi aí que eu abri o YUV e olhei, em vez de ler número.

**Versão 3 — a que vale.** Tolerância 1 na comparação entre linhas, estrutura
substituída por cobertura, e o respingo de croma como critério de seleção.

### A calibração do respingo

Medido em 20 faixas de 64 linhas, em 5 quadros intactos: **p99U de 0 a 6, p99V
de 0 a 3.** O teto de 8 é "o dobro do pior intacto".

Nos candidatos da etapa 1, na mesma janela:

| | fronteira | blocagem | respingo | veredito |
|---|---|---|---|---|
| original (nada aplicado) | 264 | — | 2 | — |
| `103420006 b4` | 292 | 1,385 | **5** | passa |
| `103419946 b5` — escolha do olho | 296 | 1,375 | **7** | passa |
| `103420002 b3` | 308 | 1,386 | **8** | passa |
| `103420047 b7` | 292 | 1,362 | 9 | reprova |
| `103420052 b0` | 328 | 1,254 | 9 | reprova |
| `103419932 b2` | 296 | 1,345 | 11 | reprova |
| `103419965 b7` — descartado pelo olho | 360 | 2,306 | 14 | reprova |
| `103420026 b0` | 308 | 1,194 | 19 | reprova |

Os dois de **melhor blocagem** são reprovados pelo respingo — e são justamente
os que têm salpico colorido na linha da fronteira, visível na tela. A blocagem é
média de luma e dilui artefato pequeno em área.

### O que isto NÃO diz

Nenhum candidato tem faixa liberada com cara de imagem real. O melhor,
`103420006 b4`, tem respingo 5 contra o máximo 6 dos intactos — mas o que ele
libera são listras pálidas, não mais cena. A barra do usuário é "a faixa
liberada precisa destravar a imagem real"; o juiz hoje sabe **reprovar o que o
olho reprova** e **ordenar como o olho ordena**, e isso não é a mesma coisa que
achar o conserto.

## Os nomes das funções mudaram — mapa para os documentos antigos

Item 3 do `docs/REFATORACAO.md`. Os documentos deste projeto citam os nomes
antigos e **não foram reescritos**: eles registram o que a função se chamava
quando aquela medida foi feita, e falsificar isso custaria mais do que ganharia.

Este é o mapa.

| nome antigo | nome novo | o que mede |
|---|---|---|
| `croma_respingo` | `croma_salto_p99` | p99 do salto de croma entre pixels **vizinhos** |
| `croma_dp` | `croma_desvio_plano` | desvio padrão dos planos U e V |
| `croma_stat` | `croma_media_desvio` | média **e** desvio de U e V, passo 2 |
| `tarja_des` | `tarja_baixo_desvio` | desvio da tarja de baixo |
| `croma_real` | `croma_mb_na_faixa` | **predicado**: variação por macrobloco dentro da faixa calibrada |
| `croma_sao` | `croma_bate_referencia` | **predicado**: bate com a referência do próprio trecho |
| `tarja_perfeita` | `tarja_baixo_e_16` | **predicado**: uniforme **e** igual a 16 |
| `base_uniforme` | `tarja_baixo_uniforme` | **predicado**: uniforme, valor livre |
| `topo_uniforme` | `tarja_topo_uniforme` | **predicado**: uniforme, valor conforme `piso_topo` |

O critério do nome é a distinção que mais me confundiu nesta sessão: **medida
devolve número, predicado devolve sim ou não.** `croma_real` soava como "o croma
verdadeiro" e é um predicado sobre uma faixa calibrada; `croma_dp` não dizia de
quê era o desvio. Os dois me levaram a comparar grandezas diferentes achando que
eram a mesma.

E os dois nomes quase iguais agora dizem em que diferem: `tarja_baixo_uniforme`
aceita qualquer valor, `tarja_baixo_e_16` exige o 16. Eram a mesma função com
uma constante trocada, e o nome não dava pista disso.
