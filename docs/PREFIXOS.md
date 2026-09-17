# O prefixo AVCC — uma hipótese de dano, testada e REFUTADA

**Conclusão: não aplicar. `prefixo == tamanho − 4` não é invariante deste
arquivo, e "corrigir" a divergência destrói quadros que hoje decodificam
perfeitamente.**

Este arquivo fica como registro do caminho, porque a hipótese era plausível e a
refutação custou uma remontagem inteira.

## A hipótese

Cada amostra do MP4 começa com 4 bytes de comprimento (AVCC, `nalLengthSize=4`).
Em 2.743 dos 3.445 quadros eles valem exatamente **tamanho da amostra − 4**. Nos
outros **702**, não. No frame 11 dizem 133.900 para uma amostra de 2.832 bytes.

Três coisas apoiavam a hipótese de bit rot:

| | |
|---|---|
| assinatura da divergência | **1,6 bits por quadro, mediana 1, máximo 5** |
| `stsz` conferido contra os offsets de chunk de vídeo **e** áudio | **345 de 345 fecham exatamente** |
| `mapa` com os 702 corrigidos | **+274 quadros analisam 100%, zero pioram** |

E a região nunca tinha sido olhada: o modo `avanco` tem `ini_k >= 5` cravado e o
`corta` reescreve o prefixo antes de medir. **Nenhuma varredura deste projeto
jamais alterou os bytes 0 a 4 de um NAL.**

## A refutação

Remontando o filme e medindo quadro a quadro pelo critério rigoroso:

| | quadros bons de 3.445 |
|---|---|
| estado atual | **209** |
| com os 702 prefixos "corrigidos" | **206** |

Mantidos 206, **perdidos 3** (frames 10, 12 e 2360), **ganhos 0**.

E isolando por quadro, no GOP 0:

| cenário | bons |
|---|---|
| base | 0–10, 12 |
| só o prefixo do frame 10 | 0–9, 12 — **perde o 10** |
| só o prefixo do frame 11 | 0–10, 12 — **nada muda** |
| só o prefixo do frame 12 | 0–10 — **perde o 12** |

Cada correção quebra **exatamente o quadro que ela toca**. Medido no pixel:

| cenário | frame 10 | frame 12 |
|---|---|---|
| base | tarjas 16,000/0,000, campo 36 | tarjas 16,000/0,000, campo 41 |
| prefixo do 10 corrigido | **sem quadro** | 16,000/0,000, campo 41 |
| prefixo do 12 corrigido | 16,000/0,000, campo 36 | **sem quadro** |

Os dois decodificam **perfeitos** com o prefixo divergente — tarjas exatas e
campos na rampa do fade.

## Por que a premissa é falsa

A amostra do MP4 pode conter o NAL **mais enchimento que não faz parte dele**. A
cauda do frame 12 é `... 00 00 03 00 00 03 00 00 03 00 00 03`: `cabac_zero_word`
com o byte de prevenção de emulação. O prefixo 2.931 deixa 4 desses de fora, e é
isso que está certo. `tamanho − 4` inclui o enchimento no NAL e quebra o quadro.

Então `prefixo < tamanho − 4` é legítimo, e a divergência sozinha não prova nada.

## O que sobra de verdadeiro

1. **O `mapa` melhorar não prevê nada.** Ele subiu 274 quadros enquanto o
   critério rigoroso caía de 209 para 206. "Analisa os 8.160 macroblocos sem
   erro" e "produz imagem" são coisas diferentes — armadilha 38, de novo.
2. **O frame 11 é caso à parte.** O prefixo dele (133.900 contra 2.832 de
   amostra) é grande demais para ser enchimento, e corrigi-lo leva o quadro do
   macrobloco 15 para 8.160 **sem quebrar nada** — o GOP 0 mantém os mesmos
   bons. Mas ele continua sem emitir imagem, então não é reparo também.
3. Os 572 prefixos **maiores** que a amostra continuam sem explicação inocente:
   enchimento justifica prefixo menor, não maior.

Os 1.107 bits ficam em `dados/prefixos_avcc.txt` **como registro do que foi
testado e reprovado**, não como candidatos.
