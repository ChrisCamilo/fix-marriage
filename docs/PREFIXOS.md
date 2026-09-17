# O prefixo AVCC — uma classe de dano que nenhuma varredura tocou

## O achado

Cada amostra do MP4 começa com 4 bytes de comprimento (AVCC, `nalLengthSize=4`)
que devem valer **tamanho da amostra − 4**. No frame 11 eles valem 133.900 para
uma amostra de 2.832 bytes.

**702 dos 3.445 quadros têm o prefixo divergente.**

## Por que não é mal-entendido de formato

A objeção óbvia é que a amostra poderia conter mais de um NAL, e aí um prefixo
menor seria legítimo. Testado: percorrendo os NALs de cada amostra e vendo se
eles ladrilham o tamanho exato,

| | |
|---|---|
| amostras que fecham exatamente com **um** NAL | **2.743** |
| amostras que fecham com **vários** NALs | **0** |
| amostras que **não fecham** | **702** |

Nenhuma amostra do filme tem mais de um NAL. E a corrupção tem a assinatura de
bit rot, não de outro formato: **1,6 bits por quadro em média, mediana 1,
máximo 5** — 1.107 bits em 702 quadros.

## Por que ninguém tinha visto

O modo `avanco` tem `if (ini_k < 5) ini_k = 5` cravado, e todo `corta` reescreve
o prefixo antes de medir. **Nenhuma varredura deste projeto jamais alterou os
bytes 0 a 4 de um NAL.** A janela de busca sempre começou depois deles.

E isto não é problema de busca: o valor correto é determinado pelo índice, não
procurado. São 1.107 bits conhecidos, não candidatos.

## O que corrigir os 702 faz

| | antes | depois |
|---|---|---|
| quadros que analisam 100% (`mapa`) | 2.376 | **2.650** |
| quadros que melhoram | — | **274** |
| quadros que pioram | — | **0** |

Entre os 274 estão o **frame 11** (macrobloco 15 → 8160) e o **frame 19**
(macrobloco 1 → 8160), dois alvos que consumiram sessões inteiras.

## O QUE NÃO FECHA, e é por isso que nada foi promovido

O `panorama` anda para o outro lado: **872 quadros deixam de emitir imagem** e
120 passam a emitir. E **468 dos 872 são quadros cujo prefixo eu não toquei**,
então é propagação pela cadeia de referências, não dano direto.

O frame 11 é o caso limpo do paradoxo: passa a analisar os 8.160 macroblocos sem
erro e **continua sem produzir quadro nenhum**, com `FOLGA` de 2 a 16.

Duas leituras possíveis, e nenhuma medida ainda as separa:

1. a captura por PTS do `reparador` é que quebra, e as imagens estão lá;
2. o prefixo grande demais fazia o ffmpeg tratar o pacote de outro jeito, e
   corrigi-lo muda a estrutura de referências de um jeito que ainda não entendi.

**Antes de promover qualquer coisa daqui é preciso decidir entre as duas.** O
teste natural é remontar o filme com os 702 aplicados e assistir — se as imagens
aparecem, o problema é da captura; se somem, o remendo está errado.

Os 1.107 bits estão em `dados/prefixos_avcc.txt`, prontos e não aplicados.
