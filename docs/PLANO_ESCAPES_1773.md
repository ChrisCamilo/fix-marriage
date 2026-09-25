# Plano 4 — IDR 1773: encadear pelas violações de escape

**Estado: não iniciado** (proposto em 2026-09-25). Continua o trabalho no 1773
depois dos planos [`PLANO_ANCORA_CAUDA.md`](PLANO_ANCORA_CAUDA.md) (a tarja e a
auditoria das caudas) e [`PLANO_JUIZ_ENCODER.md`](PLANO_JUIZ_ENCODER.md)
(refutado). Se este plano concluir que o dano é denso demais, o caminho é o
[`PLANO_SUBSTITUICAO_REFERENCIA.md`](PLANO_SUBSTITUICAO_REFERENCIA.md).

Todos os bytes abaixo são **relativos ao início da amostra** (com o prefixo
AVCC de 4 bytes), como no resto do RASTREIO. O NAL do 1773 tem 34.317 bytes e
começa no offset absoluto 59.330.588.

## Por que este plano existe

- **O 1773 é o único IDR quebrado com imagem quase íntegra:** fileiras 0–56
  exatas (QP 20 em todos os MBs, conferido no JM), dano a partir do MB ~6.939,
  byte ~32.640. Consertado, é 1 quadro garantido; o GOP tem 20 dos 28 P/B com
  cauda intacta, que passariam a ter chance.
- **A busca de bits se esgotou pelo critério antigo.** As opções a, b e c
  (17,2 M pares, 2 bits) não fecham nenhum par, e agora se sabe por quê: há
  pelo menos 3 danos certos no fim, além do da frente.
- **O que faltava era um juiz sem atraso.** Imagem e QP denunciam lixo com 20 a
  50 MBs de atraso (MELHORIAS.md, item 5), e em rajada o candidato certo ganha
  menos que isso. As violações de escape são dano **certo em byte conhecido**:
  um conserto certo da frente tem que levar o decodificador em sincronia até
  elas. É um juiz de atraso zero no fim do trecho.
- **A tarja do 1773 provavelmente está intacta na sintaxe:** as 26–28
  divergências do modelo puro eram coluna de modo variante (armadilha 61), não
  dano.

## O que se sabe do dano

**Danos certos (existência provada, bit não):**

| rel | bytes | o que viola | consertos de 1 bit que validam |
|---|---|---|---|
| 34.257 | `00 00 03 13` | `03` seguido de byte > 3 | 23 (um dos zeros, o `03`, ou `13→03`) |
| 34.285 | `00 00 03 83` | idem | 23 (um dos zeros, o `03`, ou `83→03`) |
| 34.312 | `00 00 02 21` | `00 00 02` é proibido | 22 de 1 bit (`02→06/0a/12/22/42/82`, um dos zeros) + o par `02→03`, `21→01` |

**A frente:** o 1º bit fica em ~`[32.560, 32.720)` (curva do `corta`, imagem
em modo limpo e avanço concordam). O melhor candidato, `59363305 b7`, age no
MB 6.939 e o QP já sai errado no 6.941 — a frente tem pelo menos 2 bits, a
poucos bytes um do outro.

**O trecho entre a frente e o 34.257** (~1.620 bytes, ~380 MBs de cena das
fileiras 57–60 e o começo da tarja) **é invisível para a régua de escape**:

| trecho | bytes `00` | trocas de 1 bit que criam violação |
|---|---|---|
| cena (fileiras 50–60) | 0,3–0,5% | 0,00% |
| começo da tarja (60–62) | 10% | 3,3% |
| fim da tarja (63–67) | 33% | 5,1% |

**Indícios de bloco denso no fim do 1773:**

1. Três violações em ~120 bytes onde só 3–5% das trocas criam violação: pela
   estatística de Poisson, algo como 2% ou mais dos bits trocados ali.
2. O 1774 (logo depois no arquivo) tem 3 trocas nos 9 primeiros bytes (~4%) e
   morre no MB 24.
3. A zona de dano mais próxima medida no `mapa_dano` fica em 59,8–60,4 MB,
   ~2% — a ~0,5 MB dali.
4. Mas é local: o fim do 1774 está intacto (cauda P/B), e o 1775 e o 1776
   decodificam inteiros. A vizinhança (1755–1835) tem 7 de 80 quadros com 3+
   trocas no cabeçalho, próximo da média do filme (349 de 3.445).

**Correção de registro:** os "~245 bytes entre bits" citados antes eram o
espaçamento escolhido no teste sintético (MELHORIAS.md, item 5), não uma
medida do arquivo.

## Cenários

| cenário | bits trocados no trecho | o plano funciona? |
|---|---|---|
| **A** — só a frente (2–3 bits) e o fim da tarja | ~5–6 no total | sim, como está |
| **B** — alguns aglomerados pequenos (2–3 bits cada) no meio | ~10–15 | talvez: busca por aglomerado, cara mas possível |
| **C** — um bloco de 2–5% cobrindo o trecho | ~260–650 | não: nenhuma busca de bits resolve |

Os indícios pesam para C pelo menos no fim; a dúvida é onde o bloco começa.
Por isso o plano **mede antes de buscar**.

## Etapas

### Etapa 0 — medir o dano (~1–2 h). Portão de decisão.

**0a. Geometria dos blocos de dano.** Em aberto desde o mapa de dano
(ESTADO.md, seção 6). Medir o comprimento e a nitidez das bordas dos blocos
onde há conteúdo conhecido:
- frame 11: tarja periódica, 66 bits trocados em 240 bytes (~3%) —
  `dados/janela_f11.txt`;
- enchimento `00 00 03` do 1802 e do 1831 (zona de 59,8–60,4 MB);
- os 5 primeiros bytes de quadros consecutivos (a distribuição por quadro não é
  binomial: blocos);
- as trocas provadas (cabeçalhos, caudas de IDR e de P/B) como pontos de dano
  com posição exata: distância entre trocas vizinhas, alinhamento a 512 B /
  2 KB / 4 KB.

Se os blocos tiverem tamanho típico, um bloco que termina no começo do 1774
(~59.364.9xx) tem começo previsível — e isso decide entre A/B e C.

**0b. Validar o critério de sincronia** em dano sintético. Em IDRs íntegros
(2333, 3319, 3368, 3397, 3426), plantar 2 bits na frente e uma violação de
escape ~1.700 bytes adiante, e medir:
- se o conserto certo chega em sincronia até a violação;
- a taxa de **falsa sincronia**: consertos errados que também chegam;
- como o JM se comporta com o NAL cortado logo antes da violação (ele recusa
  escape inválido; o corte é obrigatório) — o fim dos dados não pode contar
  como anomalia.

**Sincronia** = do MB da frente até o byte de corte, nenhum MB com:
QP ≠ 20; `mb_qp_delta` ≠ 0; I_PCM; modo intra que usa vizinho indisponível;
`end_of_slice` antes da hora. O JM instrumentado (`ferramentas/jm_mbinfo.patch`,
`JM_MBINFO`) grava QP, tipo, modos e a posição CABAC de cada MB; 0,14 s por
decodificação do 1773 cortado.

**Portão 0:** se a geometria mostrar bloco denso cobrindo o trecho (cenário
C), ou se a falsa sincronia passar de ~1%, **parar aqui** e ir para o plano 3.

### Etapa 1 — repontuar os 123 mil candidatos salvos (~25 min)

No scratchpad da sessão de 2026-09-22/23: `f1773_a.txt`, `f1773_b.txt`,
`f1773_c.txt` (40.000 cada, formato `off bit off bit   mb N`) e
`f1773_av1.txt` (3.000, 1 bit). Cada um passa no JM com o NAL cortado em
34.250 e sai com o **alcance de sincronia**: o byte do primeiro MB anômalo.

- **Sucesso:** algum candidato em sincronia até ~34.200.
- **Limitação:** as listas foram cortadas pela nota do ffmpeg (MB alcançado),
  não por sincronia. O certo para perto das fileiras 60–61 e deve estar entre
  os melhores, sem garantia.
- **O que se aprende mesmo sem sucesso:** a distribuição do alcance diz o
  espaçamento dos primeiros danos. Se nem os melhores passam de poucos MBs além
  da frente, é cenário C.

**Etapa 1b** (se a 1 não achar): pares com o 1º bit em `[32.560, 32.720)` e o
2º nos ~200 bytes seguintes (~1,3 M pares). Filtro rápido no `reparador`
(1.150/s, ~20 min) e JM só nos que passam da fileira 58.

### Etapa 2 — encadear até o 34.257 (se houver dano no meio: cenário B)

Quando o melhor alcance parar antes do 34.257, a quebra seguinte é outro
aglomerado:
- **Janela** posta pela primeira anomalia: o atraso do juiz de QP tem p90 de
  62 MBs, ~260 bytes nesta cena. A busca vai na janela `[anomalia − 260 B,
  anomalia]`.
- **Por aglomerado, não por bit:** 1 bit por janela ~2 mil candidatos; 2 bits
  ~1,3 M; 3 bits passa de 10⁸ e é inviável.
- **Busca em feixe:** guardar as K melhores sequências (K ~100–1.000) por
  alcance de sincronia, não só a melhor — em rajada o certo pode ficar atrás de
  lixo por um trecho.
- **A âncora fecha a prova:** só conta caminho que chegue ao 34.257 exatamente
  em sincronia. O feixe não aceita lixo no fim.
- **Parada:** duas etapas seguidas achando dano a menos de ~260 bytes do
  anterior = densidade de cenário C. Parar.

### Etapa 3 — as três âncoras de escape (segundos a ~30 min)

Com a frente e o meio em sincronia até o 34.257:
1. os 23 consertos da 1ª violação, NAL cortado antes da 2ª: o certo leva a
   sincronia até o 34.285;
2. os 23 da 2ª, cortado antes da 3ª;
3. os 22 de 1 bit mais o par `02→03` + `21→01` da 3ª, com o NAL inteiro.

Entre âncoras há só ~28 bytes de tarja, e mais de um conserto pode passar;
então testar as combinações até o fim — no máximo ~12 mil decodificações.

### Etapa 4 — prova e entrega

Só entra no `patches.txt`, **com aprovação do usuário**, o conjunto que passar
em tudo:
- critério rigoroso do `reparador.c`: 8.160 MBs, zero ocultados, zero erros;
- QP 20 e `mb_qp_delta` 0 nos 8.160 MBs, nenhum I_PCM;
- tarja 16,00 com desvio 0;
- o slice terminando exatamente no fim dos dados;
- o modelo de tarja estendido (`cabac_enc2.py`) encaixando as fileiras 63–67
  com distância 0;
- as imagens dos 167 quadros bons byte a byte iguais; `mapa` e regressão.

Depois: decodificar o GOP 1773 com o IDR consertado e medir os P/B.

## Se o dano for denso (cenário C)

Não gastar máquina com bits. Ir para o plano 3 com o 1773 como primeiro alvo:
fileiras 0–56 exatas e tarja conhecida são 95% do IDR; só ~50 linhas de
imagem (912–961) precisam ser preenchidas, registradas como reconstrução. Um
avanço parcial das etapas 1–2 (um par que sincroniza certo por mais uma
fileira) pode servir de fonte visual para o substituto — **nunca** para o
`patches.txt`, que exige prova.

## Custo

| etapa | máquina | observação |
|---|---|---|
| 0 | ~1–2 h | portão: decide se segue |
| 1 | ~25 min | 123 mil decodificações no JM |
| 1b | ~20 min + JM | só se a 1 falhar |
| 2 | ~40 min por aglomerado | K × janela; número de aglomerados desconhecido |
| 3 | segundos a ~30 min | |
| 4 | ~30 min | verify, mapa, regressão |

## Onde registrar

Resultados de cada etapa no RASTREIO.md (seção do 1773) e neste arquivo;
números medidos no ESTADO.md; a geometria dos blocos de dano no ESTADO.md,
seção 6, e em `dados/mapa_dano.txt`. Ferramentas novas em `ferramentas/ancora/`
com bloco de documentação (AGENTS.md).
