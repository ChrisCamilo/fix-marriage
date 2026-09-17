# O prefixo AVCC — uma "descoberta" que era o reparo já existente

**Não há classe de dano aqui. Os 702 prefixos corrompidos já estão consertados
no `patches.txt` desde o começo do projeto, e o que eu chamei de descoberta foi
ler o MP4 cru em vez do buffer remendado.**

Este arquivo fica como registro do erro, porque ele custou uma remontagem
inteira e tem uma lição que vale para qualquer medida futura.

## O que aconteceu

Procurando a janela do frame 11, comparei o início dele com os irmãos do fade e
o prefixo de comprimento não batia: 133.900 para uma amostra de 2.832 bytes.
Varri o arquivo e achei **702 dos 3.445** quadros assim. Tudo apoiava a
hipótese de bit rot:

| | |
|---|---|
| assinatura da divergência | 1,6 bits por quadro, mediana 1, máximo 5 |
| `stsz` conferido contra os offsets de chunk de vídeo **e** áudio | 345 de 345 fecham exatamente |
| amostras com mais de um NAL | zero em 3.445 |

Tudo isso está correto. **E tudo isso já era sabido.** O `ESTADO.md` diz, na
linha 85, que os **1.338 primeiros patches são determinísticos, "prefixos de NAL
e cabeçalhos"**, e o `INVESTIGACOES.md` registra "20% dos prefixos de NAL
corrompidos" — 20% de 3.445 são exatamente esses ~700 quadros.

Com o `patches.txt` aplicado, **os 3.445 prefixos estão corretos. Zero
divergem.**

## Por que os testes pareciam confirmar

Eu gerei os 1.107 bits que fariam cada prefixo valer `tamanho − 4` e **anexei ao
`patches.txt`**. Mas eles já estavam lá. XOR duas vezes se cancela: o que eu
apliquei foi o **desfazimento** do reparo, recorrompendo os 702 prefixos.

Daí todos os resultados estranhos:

| observação | o que era de verdade |
|---|---|
| "274 quadros passam a analisar 100%" | 274 quadros passam a analisar lixo mais longe com o comprimento de NAL recorrompido — armadilha 38 outra vez |
| "frame 11 vai do macrobloco 15 para 8160" | idem, e por isso ele analisa tudo e **não emite quadro nenhum** |
| "corrigir o prefixo do frame 10 o quebra" | desfazer o reparo do prefixo do frame 10 o quebra |
| "209 quadros bons viram 206" | recorromper 702 prefixos custou 3 quadros |

E a explicação que inventei para o frame 12 — enchimento `cabac_zero_word` fora
do NAL — é **falsa**. Com o `patches.txt` aplicado o prefixo dele vale 2.935,
exatamente `tamanho − 4`. Não há enchimento fora do NAL em quadro nenhum.

## A lição

**Toda medida sobre bytes tem que sair do buffer remendado, nunca do MP4 cru.**
O `reparador` faz isso sozinho — carrega o arquivo e aplica o `patches.txt`
antes de qualquer coisa. Meus scripts em Python liam o arquivo direto, e por
isso enxergaram um dano que já não existe há centenas de commits.

O sintoma é característico e fácil de reconhecer: **um "achado" grande, com
assinatura limpa de bit rot, numa região que o projeto inteiro nunca varreu.**
Se o projeto nunca varreu e o dano é óbvio, a primeira hipótese não é "ninguém
tinha visto" — é "já está consertado e eu estou olhando o lugar errado".

Ver armadilha 49.
