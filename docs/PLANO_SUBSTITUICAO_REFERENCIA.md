# Plano 3 — Substituir a referência para assistir

**Estado: não iniciado** (proposto em 2026-09-23). Um dos três planos para o
dano denso dos IDRs quebrados; os outros são
[`PLANO_JUIZ_ENCODER.md`](PLANO_JUIZ_ENCODER.md) e
[`PLANO_ANCORA_CAUDA.md`](PLANO_ANCORA_CAUDA.md).

**Isto não é reparo de bits.** É reconstrução visual, do lado do
`remontar.py`: não afirma nada sobre bits, não toca no MP4 e não escreve no
`patches.txt`. Todo quadro produzido assim fica registrado no
`data/remontados.txt` como reconstrução.

## Por que este plano existe

- **A busca de bits está esgotada** com os métodos atuais: 85 IDRs quebrados
  sondados, dano denso em todos; 99% dos P/B quebrados dependem de IDR
  quebrado (RASTREIO.md, "Quadros P e B: censo").
- **Muitos P/B têm o próprio dado bom.** 819 quadros P/B dentro de GOP de IDR
  quebrado leem os 8.160 MBs sem erro nem ocultação: a imagem deles só sai
  errada porque a referência está errada.
- **O vídeo assistível hoje tem 168 quadros (5,6 s).**

## A ideia

Decodificar o GOP **trocando a imagem do IDR por uma reconstruída**. Os P/B
íntegros aplicam o movimento e o resíduo **verdadeiros** em cima dela. Onde o
substituto estiver certo — a parte de cima limpa do próprio IDR, que no 1773
são 85% das fileiras — os P/B saem certos; onde for estimado, saem
plausíveis, com erro que se propaga com o movimento.

O JM é código aberto e está compilado no scratchpad: dá para injetar a imagem,
coisa que a libavcodec não permite.

## Passos

1. **Modificar o JM** para aceitar "troque a imagem decodificada do quadro N
   por este YUV" antes de ela entrar no DPB como referência (na gravação da
   imagem decodificada). Mudança pequena, isolada, atrás de uma opção.
2. **Montar o IDR substituto:**
   - fileiras de cima: o próprio IDR decodificado, até a fronteira certa
     (primeiro MB com QP diferente, I_PCM ou parada — IDRS.md, seção 10b);
   - o resto, pela ordem de preferência: o quadro vizinho de exibição, se for
     bom; propagação inversa a partir de um quadro íntegro seguinte, desfazendo
     o movimento com os vetores dos P/B íntegros (a ideia 5 da lista); ou
     preenchimento a partir das bordas;
   - tarja pintada em `Y=16`, `U=V=128`.
3. **Decodificar o GOP com a substituição e medir:**
   - a tarja dos P/B (em quadro P ela é cópia da referência — deve sair 16);
   - continuidade entre quadros consecutivos;
   - inspeção visual por GOP, em resolução cheia.
   - Um P de referência com dado quebrado corta a sequência a partir dele:
     escolher GOPs pelo comprimento das sequências de P/B íntegros.
4. **Começar pelos GOPs de IDR com mais parte de cima limpa:** o 1773 (85%) e
   os de 50–80%. Depois, os demais onde houver fonte para a parte de baixo.
5. **Entregar como vídeo assistível:** estender o `remontar.py`, registrar
   cada quadro no `remontados.txt` (origem do substituto e de onde veio cada
   região) e medir o ganho em segundos assistíveis contra os 5,6 s de hoje.

## Custo e riscos

- **Custo:** 2–3 dias até o primeiro GOP.
- **Risco:** o erro do substituto se propaga com o movimento ao longo do GOP
  (até 29 quadros). Cena parada funciona bem; cena com movimento degrada. O
  critério é visual e fica declarado como reconstrução.
- **Risco de confusão:** nada disto pode ser contado como "quadro consertado".
  RESULTADOS.md separa sempre "consertado por reparo", "já estava intacto" e,
  agora, "reconstruído".

## Onde registrar

Ferramenta e decisões no README.md (seção de arquivos); quadros produzidos no
`data/remontados.txt`; segundos assistíveis no RESULTADOS.md, em linha
própria de reconstrução.
