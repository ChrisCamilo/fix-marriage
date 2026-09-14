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
- **olhar em resolução cheia**. Métrica agregada esconde dano local: foi assim
  que o frame 3428 quase entrou com quebra de macrobloco visível.

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
