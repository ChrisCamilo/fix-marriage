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
