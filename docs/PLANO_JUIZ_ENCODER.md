# Plano 1 — Juiz de "escolha ótima do encoder"

**Estado: REFUTADO no passo 2** (proposto em 2026-09-23, testado em
2026-09-24 — ver "Resultado" no fim). Um dos três planos para o
dano denso dos IDRs quebrados; os outros são
[`PLANO_ANCORA_CAUDA.md`](PLANO_ANCORA_CAUDA.md) e
[`PLANO_SUBSTITUICAO_REFERENCIA.md`](PLANO_SUBSTITUICAO_REFERENCIA.md).

## Por que este plano existe

Medido nesta sessão (MELHORIAS.md, item 5; RASTREIO.md):

- **O gargalo é o atraso dos juízes.** Com dano sintético, o pixel muda já no
  MB do bit errado, mas o melhor juiz de imagem (degrau de borda) só denuncia
  7–27 MBs depois, e o QP 20 MBs depois (mediana).
- **Em rajada o bit certo ganha pouco.** Com dois bits a ~245 bytes, o
  candidato certo ganha só 7–12 MBs limpos, e o lixo dos rivais passa ~50 MBs
  pelo juiz. O certo fica em 1º em 1–4 de 30 casos; o oráculo, 30 de 30.
- **A sintaxe do lixo parece normal**: o decodificador alimentado com bits
  errados amostra do próprio modelo de contextos. Detector bom tem que usar o
  que o modelo **não** codifica.

## A ideia

O encoder escolheu cada modo de predição intra e cada coeficiente para
minimizar custo diante da **imagem real**. Então cada bloco 4×4 genuíno é
quase ótimo em relação aos vizinhos já reconstruídos: o modo escolhido está
perto do melhor, e o resíduo explica o que a predição não explica. No lixo, o
modo e o resíduo são sorteados do modelo de contextos, sem relação com o
conteúdo.

Com 16 blocos 4×4 por MB, a medida tem estatística suficiente **dentro de um
único MB** — é o atraso de 1–2 MBs que falta para o encadeamento funcionar em
rajada. E explora exatamente o que o modelo do CABAC não sabe: pixels.

## Passos

1. **Medida em Python, sobre a imagem em modo limpo** (`-ec 0
   -skip_loop_filter all`, armadilha 60).
   - Versão só de pixels: para cada 4×4, o custo (SAD) dos 9 modos intra a
     partir dos vizinhos reconstruídos; nota = quão perto do melhor modo o
     bloco reconstruído está, normalizada pela energia do resíduo. Idem 4 modos
     16×16 para MB I16x16.
   - Versão com o trace do JM (`jmtrace.py`): o modo que o encoder de fato
     escolheu e sua distância ao ótimo; coeficientes contra o erro de
     predição.
2. **Calibrar no banco sintético** (`calib.py`: 900 bits aleatórios nos 6 IDRs
   bons, rótulo exato por comparação com o original).
   - Meta: **atraso mediano ≤ 2 MBs** com **zero disparo falso** nos 6
     quadros íntegros. Referência de hoje: 7–27 MBs.
3. **Repetir o teste decisivo** (`ranking2.py`: 2 bits a ~245 bytes, 512
   candidatos, 30 casos).
   - Meta: **certo em 1º em ≥ 50%** e **no top 5 em ≥ 80%**. Hoje: 4/30 e
     10/30.
   - Conferir também no regime espaçado (`ranking2_longe.py`), onde o juiz de
     borda já acerta 7–9 de 12.
4. **Se passar:** implementar como juiz no `reparador.c` (com o bloco de
   documentação da função, regra do AGENTS.md) e montar a busca por fronteira
   do MELHORIAS.md, item 5: janela nos bytes dos MBs antes da fronteira, feixe
   de 10–20 ramos, nota = MBs limpos acrescentados, veto a quem sujar MB limpo.
5. **Aplicar no 1773 e nos IDRs de fronteira mais tardia.** Nada entra no
   `patches.txt` sem: tarja 16/0, QP constante nos 8.160 MBs, zero ocultados e
   o slice acabando exatamente no fim dos dados — e sem passar pelo usuário.

## Custo e riscos

- **Custo:** ~1 dia para os passos 1–3, que já decidem se vale.
- **Risco principal:** textura real também é pouco previsível. A nota tem que
  ser normalizada pelo que o encoder pagou no bloco (coeficientes, tipo de
  MB); sem isso, cena detalhada vira falso positivo.
- **Risco de burla:** é mais um proxy de imagem. A defesa é a mesma de sempre:
  ele só guia a busca; quem aprova é o gabarito (tarja, QP, fim do slice).

## Onde registrar

Resultados de calibração no MELHORIAS.md (item 5); corridas no RASTREIO.md;
se virar juiz do `reparador.c`, no CRITERIOS.md.

## Resultado — 2026-09-24: refutado no passo 2

**O que foi feito.**

- **JM instrumentado** (`tools/jm_mbinfo.patch`, em `image.c`): com
  `JM_MBINFO=<arquivo>` grava uma linha por MB — tipo, modo 16x16, modo de
  croma, CBP, QP, os 16 modos 4x4 **e o estado do decodificador aritmético no
  início do MB** (posição, bits restantes, `Drange`, `Dvalue`; serve ao plano
  2). Build sem trace: **0,13 s por quadro**, contra segundos do trace
  completo. Compilar com `-DENABLE_TRACING=OFF` e
  `-DCMAKE_C_STANDARD_LIBRARIES=-lws2_32`.
- **Predições intra exatas** (`tools/predicao_intra.py`): no IDR 2333,
  **8.280 de 8.280** blocos 4x4 sem resíduo batem pixel a pixel com a predição
  do modo escolhido.
- **No quadro íntegro o encoder escolhe o melhor modo** quase sempre: 16x16 em
  99,2% dos MBs, croma em 97,7%, 4x4 com posto mediano 0,25.

**Calibração** (600 bits aleatórios nos 6 IDRs bons, treino em 3 e teste em 3,
limiar sem disparo falso nos quadros íntegros):

| janela | pega | atraso mediano |
|---|---|---|
| 1 MB | 29–35% | 52–84 MBs |
| 3 MBs | 19–33% | 67–72 MBs |
| 5 MBs | 19–47% | 56–67 MBs |

Meta era atraso mediano ≤ 2 MBs. **Pior que o degrau de borda** (7–27).

**Por quê — e é estrutural.** Nos MBs de lixo o modo escolhido também sai o
melhor: 16x16 em 84% (bom: 98%), croma em 85% (bom: 97,5%), 4x4 com posto médio
0,52 (bom: 0,41). A imagem decodificada é, por construção, a predição do modo
escolhido mais o resíduo; no lixo o resíduo é pequeno (o modelo de contextos
prefere zeros), então a imagem **se ajusta ao modo** em vez de o modo se
ajustar à imagem. O encoder foi ótimo em relação ao **original**, que não
temos.

**A lição que fica:** juiz calculado só a partir da saída do próprio
decodificador (sintaxe + pixels decodificados) é autoconsistente por
construção. O que separa lixo de verdade precisa vir **de fora do quadro
decodificado**: política fixa do encoder que o modelo não aprende (QP
constante), conteúdo conhecido (tarja), vizinho temporal, ou o estado do
decodificador num ponto conhecido (plano 2). Os passos 3–5 não foram feitos.
