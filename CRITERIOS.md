# Critérios de julgamento, em ordem de precedência

Como decidir se um candidato entra no `patches.txt`. A ordem importa: **o juiz
da imagem manda**, e o da tarja é subordinado a ele.

O objetivo do projeto é assistir ao filme. Uma tarja com defeito de sete linhas
é um risco fino piscando por 1/30 de segundo; um frame ausente é um segundo de
nada. Entre os dois, a imagem ganha.

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
