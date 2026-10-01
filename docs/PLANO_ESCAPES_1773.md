# Plano 4 — IDR 1773: encadear pelas violações de escape

**Estado: etapas 0b e 1 feitas em 2026-09-30; a busca da etapa 1b com a âncora de posição foi validada no 3368 e o teste reduzido no 1773 (2 bits próximos) deu zero em 2026-10-01** (ver "Andamento" no fim). Proposto em 2026-09-25. Continua o trabalho no 1773
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
(README.md, seção 6). Medir o comprimento e a nitidez das bordas dos blocos
onde há conteúdo conhecido:
- frame 11: tarja periódica, 66 bits trocados em 240 bytes (~3%) —
  `data/janela_f11.txt`;
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
`end_of_slice` antes da hora. O JM instrumentado (`tools/jm_mbinfo.patch`,
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
números medidos no README.md; a geometria dos blocos de dano no README.md,
seção 6, e em `data/mapa_dano.txt`. Ferramentas novas em `tools/anchor/`
com bloco de documentação (AGENTS.md).

## Andamento (2026-09-30)

**Juiz de sincronia** (`tools/anchor/sincronia_1773.py`): JM com `JM_MBINFO`,
NAL cortado em 34.250; alcance = primeiro MB desde a fileira 57 com QP ≠ 20,
I_PCM, não lido, ou que começa além do corte (o JM lê zeros depois do fim do
dado, e zeros parecem tarja — sem esse limite três candidatos de 1 bit
"sincronizavam" até o MB 8.160 lendo até o byte 61.204). Base de hoje: MB
6.950, byte 32.922.

**Etapa 0b — o QP sozinho não basta** (`tools/anchor/sincronia_valida.py`).
Em IDRs íntegros, 1 bit plantado a ~1.700 bytes do fim e toda troca de 1 bit
em ±40 bytes (640): a troca certa chega ao corte em sincronia nas 8 corridas;
as erradas que também chegam: 3368 0 e 0, 3397 7 e 17, 3426 5 e 71, 2333 86 e
89. MB sem resíduo não tem `mb_qp_delta`, e o lixo passa.

**A âncora de posição** resolve isso no 1773. Nos 6 IDRs íntegros a fileira
60 é **tarja pura** (só I16x16), custa 30–40 bytes e começa 77–95 bytes antes
do fim; as fileiras 61–67 ocupam 46–55. As fileiras 57–59 custam ~5 B/MB (a 56
do 1773 custa 5,2). Então o caminho certo do 1773 lê a fileira 60 a partir de
~34.229 do NAL. Cortando a amostra 100 bytes antes do fim, o ffmpeg para no MB
7.188–7.198 nos quatro íntegros medidos (3368, 3426, 3397, 2333).

**Etapa 1 — repontuados os 123 mil candidatos de setembro:** 1 de 3.000 de 1
bit e 8 de 120.000 de 2 bits chegam ao corte em sincronia de QP; só um no
lugar da tarja, `59363224 5` + `59363619 6` (MB 7.331 no corte). **Falso pela
âncora:** lê as fileiras 57–59 a 2,6–3,3 B/MB e a fileira 60 com 712 bytes e
39 MBs I4x4 (cena, não tarja). Não é conclusivo: as listas b e c estão cheias
(40.000 cada, todas com MB ≥ 7.302 no ffmpeg) e foram gravadas antes de
2026-09-23, quando o arquivo guardava as primeiras 40.000 na ordem da
combinação, não as melhores — o par certo pode ter ficado de fora.

**Etapa 1b refeita com a âncora (a rodar):** o `avanco` ganhou `CORTE_ALVO=n`
(corta a amostra do alvo no byte n, base e candidatos). Com o corte em 34.217
(fim − 100), o caminho certo tem que parar no MB ~7.185–7.200; base de hoje,
6.960. Os que caem nessa faixa vão ao JM conferir a fileira 60 (tarja pura).

    export PATH=/c/msys64/ucrt64/bin:$PATH; MP4="Caio & Lizandra - Making- Caio-Balu.mp4"
    # controle sintetico no 3368: 2 bits plantados em 29434 b3 e 29634 b6
    #   (patches.txt + "<off+29434> 3" e "<off+29634> 6"; off do 3368 no index.txt)
    JANELA2=29394:29794 CORTE_ALVO=31034 PORTA=7180 TETO=0 ./reparador.exe "$MP4" index.txt pt_sint.txt avanco 3368 2 29394 29474 sint.txt
    #   1.842.880 pares; esperado: o par plantado com MB ~7.188, e contar os falsos em [7180,7205]
    # a busca real no 1773 (~16 M pares, 2 a 5 h pela velocidade de IDR)
    JANELA2=32560:34217 CORTE_ALVO=34217 PORTA=7180 TETO=0 ./reparador.exe "$MP4" index.txt patches.txt avanco 1773 2 32560 32720 f1773_ancora.txt
    # depois: os de MB em [7180,7205] no JM (sincronia_1773.py) e a fileira 60 tarja pura

**Controle sintético rodado em 2026-10-01** (3368, 2 bits plantados em
29.434 b3 e 29.634 b6; 1,84 M pares em 28 min, ~1.100/s):

| juiz | sobram | o par plantado |
|---|---|---|
| 1º: `CORTE_ALVO` em fim − 100, MB em [7.180, 7.205] (ffmpeg) | 26.950 (1,5%) | MB 7.188, o mesmo do 3368 íntegro |
| 2º: `tools/anchor/sincronia_tarja.py` (JM, corte em fim − 64): QP do quadro nas fileiras 57–59, fileira 60 entre 77 e 95 bytes do fim, e tarja pura | **2** (11 min) | passa |

O outro que passa (`29.470 b1` + `29.751 b2`) põe a fileira 60 seis bytes
adiante (31.052 contra 31.046) — sobra para olhar a imagem. Calibração do 2º
juiz: os 6 IDRs íntegros passam, o 3368 sem conserto falha; a conferência da
tarja para 16 bytes antes do corte, porque o JM lê adiante (no 3319 o MB que
começa 10 bytes antes do corte já lê além do dado).

Projeção para o 1773: ~16 M pares (~8,7× o controle) → ~4 h no `avanco`,
~240 mil na faixa → ~1,6 h no JM, e da ordem de 10 falsos sobrando para a
imagem. Se o dano entre a frente e o byte 34.217 tiver mais que o par da
frente (cenários B/C), nenhum passa — e isso também responde o portão.

**Teste reduzido no 1773 (2026-10-01): nenhum par.** 1º bit em
`[32.560, 32.720)`, 2º depois dele até 32.800 (os dois a até 80–240 bytes um do
outro, como a observação de setembro sugeria), 1,64 M pares em 24 min; 36.628
na faixa da âncora; no JM, **0 passam**: 36.570 perdem o QP nas fileiras 57–59
(18.430 na 57, 17.121 na 58, 1.019 na 59), 45 chegam à fileira 60 centenas de
bytes adiantados (cena lida barato demais), o resto para antes. Com o controle
do 3368 (o par plantado passa), isso exclui "2 bits próximos na frente".

Estimativa antes do teste: 5–10% de o dano da frente ser só 2 bits — o fim do
1773 tem dano denso (2 em ~42 bits na última fileira; 3 violações de escape
nas fileiras 60–67), os blocos de dano medidos no arquivo têm 2–4% em 1–3 KB,
e o trecho quebrado (do byte 32.640 do 1773 ao ~10 do 1774, cujo começo também
tem ~4%) tem o tamanho de um bloco. Depois do teste a chance que sobra é a de 2
bits **distantes** (o 2º entre 32.800 e 34.217), com a busca inteira de ~5,5 h.
Recomendação: parar os bits no 1773 e ir para o plano 3 (substituir a
referência), com o 1773 como primeiro alvo.

**A tarja de baixo pelo fim (2026-10-01).** `ancora2.c` (tarja pura, contextos
saturados do 3426, entrada nas fileiras 63–67; controle 3426: 0 nas duas
entradas). No 1773, com o `21` → `01`: fileira 67, **0** em 42 bits; 66, 3 em
82; 65, 9 em 118; 64, 22 em 156; 63, 26 em 166. Das 23 trocas de 1 bit que
desfazem a violação final (`00 00 03 21`), **só o `21` → `01`** fecha a 67 em 0
(as outras 3–4) e é a melhor desde a 66 (3 contra 9–11): aplicado com
aprovação do usuário (`59364903 5`, em `data/deterministicos.txt`). Da 66
para cima as diferenças se concentram na coluna 0 de cada fileira, onde o
modelo puro tem incógnitas, e as hipóteses de entradas diferentes discordam:
não dá para separar dano de sintaxe variante (armadilha 61) com a tarja pura.
Ir além pede a busca com variantes por coluna (`cabac_enc2.py`) em C — prova
bits em ~40 bytes de tarja, sem imagem.
