# Plano 5 — Frame 11: recodificar o quadro inteiro

**Estado: passo 1 feito** (proposto e iniciado em 2026-09-25 — ver
"Resultados do passo 1" no fim). Generaliza o método do
[`PLANO_ANCORA_CAUDA.md`](PLANO_ANCORA_CAUDA.md) — recodificar com CABAC um
trecho de sintaxe conhecida e comparar com o arquivo — da tarja para o quadro
inteiro, num quadro cujo conteúdo é todo conhecido. Histórico do frame 11 no
RASTREIO.md ("GOP 0, frames 10 a 12", "Frame 11 — por que nenhum par de bits
pode funcionar", "3 bits na janela dos macroblocos") e em `data/janela_f11.txt`.

## O dano

- **Quadro P de 2.832 bytes** (amostra em 147.689), o último do fade inicial.
- **Inteiro dentro de uma zona de dano** (0,147–0,150 MB, 4–4,5% dos bits
  trocados — `data/mapa_dano.txt`): **~600 bits errados**, espalhados. Na
  parte periódica do fluxo dá para contar direto: 66 bits fora em 240 bytes
  (~3%), contra 4–12 nos frames 3–9.
- **Quebra no MB 15** da primeira fileira, ~50 bytes depois do início.
- **O que se vê é ocultação, não imagem:** tarja 15 e campo 43 são o peso do
  fade aplicado à cópia do frame 9 (16 × 40/32 − 5 = 15). Os 15 MBs que ele
  decodifica de verdade também saem 15, porque o ffmpeg descarta o quadro
  inteiro no erro.
- **O cabeçalho também está na zona.** Ele parseia e bate com os irmãos, mas o
  peso da referência 1, `(84, −7)`, é anômalo (produziria 82 onde o alvo é
  43) — provável dano que não se manifesta porque o quadro quebra antes de
  usá-la.
- **Sete vias fechadas** (RASTREIO.md): 1, 2 e 3 bits em várias janelas,
  `slice_qp_delta`, `num_ref_idx_override`, `pred_weight_table`, comparação de
  bits com quadros bons. Esperado: com ~600 bits trocados, nenhuma busca de
  poucos bits chega lá.

## Por que é mais fácil que o 1773

1. **A imagem é gabarito total.** Tarja 16,000 e campo 43,000 — o campo pela
   rampa do fade (`16 18 21 23 25 27 29 32 34 36 38 41 43` em ordem de
   exibição; RASTREIO.md, "A rampa decide entre 43 e 44"). Nenhum pixel é
   desconhecido.
2. **A sintaxe se lê nos irmãos.** Medido no trace do JM (passo 1): nos
   quadros P do fade com o tamanho do 11, **7.919 dos 8.160 MBs** são o mesmo
   MB — I16x16 com predição DC, croma DC, sem resíduo —, na tarja **e no
   campo**: a imagem uniforme se propaga pela predição DC. Só 241 MBs fogem
   disso, sempre nos mesmos lugares (MB 0 e as fileiras de borda 8 e 59).
   Detalhe em "Resultados do passo 1". (A primeira versão deste plano dizia
   que o campo era skip; isso vale só para os quadros P pequenos do fade.)
3. **O fluxo é quase todo um padrão fixo.** Autocorrelação de bytes com período
   5: **75%** no frame 9, 73% no 7, **40%** no 11. O padrão é `c6 31 8c 63 18`
   — em bits, `11000` repetido — os MBs de tarja idênticos com contextos
   saturados. No 9 ele aparece limpo, com poucos "eventos" reais; no 11,
   salpicado de bits trocados.
4. **O ponto de partida é exato.** No início do slice o estado do CABAC vem
   inteiro do cabeçalho: contextos iniciais por `cabac_init_idc` e QP do slice,
   range 510, low 0. Não há a incógnita de estado de entrada das caudas.
5. **Dano denso não atrapalha.** O codificador gera o fluxo certo a partir da
   sintaxe, sem ler o fluxo danificado. A comparação com o arquivo aguenta 4%
   de bits trocados, porque hipótese errada fica perto de 50%.

## Passos

### 1. A sintaxe exata do frame 9 (e do 7)

JM compilado com trace (`ENABLE_TRACING`, scratchpad da sessão de 2026-09-23,
`JM/`; leitor `jmtrace.py`). Decodificar o GOP 0 até o 9 e extrair, por MB:
skip, `mb_type`, modos intra (luma e croma), CBP, `mb_qp_delta`, coeficientes,
`ref_idx`, `mvd`. Descobrir por que o JM parou depois de 9 quadros (dano no
10/11 ou falta de referência) e decodificar só o que for preciso.

Resultado esperado: um **molde** do quadro P do fade — quais MBs são intra,
quais são skip, quais são inter, e o que os MBs de borda (fileiras 8 e 59, com
2 linhas de tarja e 14 de campo) codificam.

### 2. Codificador CABAC de quadro P

Estender o `tools/anchor/cabac_enc2.py` (validado no 3348) com:
- `mb_skip_flag` (contextos 11–13, pelo vizinho não-skip);
- `mb_type` de P com prefixo intra (contextos 14–20, depois os de I em 17+);
- `ref_idx` e `mvd` para os MBs inter;
- resíduo (CBF, mapa de significância, níveis) para os MBs de borda que
  tiverem coeficientes;
- inicialização dos contextos a partir de `cabac_init_idc` e QP do slice
  (tabelas m, n da norma).

**Validação obrigatória:** recodificar o frame 9 (e o 7) a partir da sintaxe do
passo 1 tem que reproduzir o arquivo **bit a bit** (distância 0), como no 3348.
Sem isso, nada do passo 3 vale.

### 3. Gerar o frame 11

- Cabeçalho: os campos que não mudam vêm dos irmãos; os que mudam (`frame_num`,
  POC, QP se diferir, pesos) vêm da rampa — os pesos são calculados a partir do
  campo das referências (43 a partir do 38 do frame 9, com a mesma fórmula
  ponderada que os quadros bons usam). Cada campo vira hipótese com
  comprimento conhecido; a distância ao arquivo decide.
- Corpo: o molde do passo 1 com os valores do 11. Os MBs de borda têm
  coeficientes que dependem do valor 43; as fileiras são uniformes, então
  bastam poucas hipóteses por MB, escolhidas pela distância ao arquivo nos
  bits seguintes (busca local, como a do `ancora3.c`).
- Recodificar do primeiro bit do slice ao último, com o estado inicial exato.

### 4. Provar

- **Distância:** o arquivo e o fluxo recodificado diferem em ~3–4% dos bits,
  **espalhados por igual**, compatível com a zona de dano. Trecho longo de
  discordância alta = sintaxe errada naquele trecho, não dano: voltar ao passo
  3 ali, sem afirmar nada.
- **Decodificação:** o frame 11 corrigido passa no critério rigoroso do
  `reparador.c` (8.160 MBs, zero ocultados, zero erros), com imagem
  **exatamente** tarja 16,000/0,000 e campo 43,000/0,000.
- **Cadeia:** o frame 12 (B, gêmeo do 8), que depende do 11, sai no 41 que a
  rampa prevê, com tarja 16; a cadeia do GOP 0 para de errar por causa do 11
  (o `verify` hoje tem 2 falsos no 12 por isso — AGENTS.md).
- **Os 167 quadros bons** byte a byte iguais; `mapa` e regressão.

Só então os ~600 bits entram no `patches.txt`, **com aprovação do usuário**,
num registro próprio (`data/patches_f11.txt`), com a distância de cada trecho.

## Custo e retorno

- **Custo:** 1–2 dias de implementação (trace do JM, codificador de P,
  resíduo). Máquina: quase nada.
- **Retorno direto:** pequeno em segundos — o frame 11 e o 12 —, mas destrava
  a cadeia do GOP 0: hoje o 11 derruba tudo atrás dele, e os 16 quadros do GOP
  0 depois dele passam a ser verificáveis (RASTREIO.md, "Os 19 verificáveis").
- **Retorno maior:** o método vale para **qualquer quadro de conteúdo
  conhecido, com qualquer densidade de dano**. Candidatos: o fade do fim do
  filme (o `remontar.py` já o preenche por média), quadros pretos, cartelas.
  Depois do 11, fazer o censo desses quadros.

## Riscos

- **Coeficientes dos MBs de borda imprevisíveis** (quantização do encoder com
  zona morta ou trellis): resolve-se com busca local por MB, pequena porque as
  fileiras são uniformes.
- **Sintaxe do 11 diferente da do 9 em algum trecho** (por exemplo um MB que o
  encoder decidiu codificar diferente): aparece como faixa de discordância alta
  na comparação; o passo 4 localiza, e o trecho vira busca local.
- **Cabeçalho com dano em campo de comprimento variável**: desloca o início do
  slice. A primeira distância grande logo no começo denuncia; a solução é
  gerar o cabeçalho inteiro a partir dos irmãos e da rampa e comparar por
  bloco.

## Onde registrar

Resultados de cada passo no RASTREIO.md (seção do frame 11) e neste arquivo;
números medidos no README.md e no RESULTADOS.md; ferramentas em
`tools/anchor/`, com bloco de documentação (AGENTS.md).

## Resultados do passo 1 (2026-09-25)

JM 19.1 com trace (build `release` do scratchpad de 2026-09-23), GOP 0 do
quadro 0 ao 9; leitor em `tools/anchor/jm_trace.py`.

**O molde dos quadros P do fade.** Duas estruturas, e o tamanho do NAL diz qual:

| quadros P | POC | bytes | campo (fileiras 9–58) | fora do padrão |
|---|---|---|---|---|
| 1, 7, 9 | 4, 16, 20 | 2.816–2.964 | I16x16 DC, sem resíduo | 241–242 MBs |
| 3, 5 | 8, 12 | 942–952 | **skip** | 6.241 (os skips) |

O frame 11 tem 2.832 bytes: é da primeira estrutura. Nela, o que foge do
padrão (I16x16 DC, croma DC, `mb_qp_delta` 0, CBF do DC 0):
- **MB 0:** inter (P16x16) com resíduo — não há vizinho para a predição DC;
- **fileiras 8 e 59** (a borda tarja/campo: 2 linhas de uma, 14 da outra): a
  coluna 0 é inter com resíduo (referência 0 ou 2, CBP 3–35) e as colunas
  1–119 são I16x16 com predição **horizontal**, croma horizontal, sem resíduo
  (o conteúdo da coluna 0 se propaga para a direita). O 7 usa inter em metade
  dessas fileiras;
- às vezes o último MB (8.159).

**O padrão periódico é esse MB repetido.** Em janelas de 40 bits contra
`11000` repetido (qualquer fase):

| quadro | bytes | janelas exatas | a 1–4 bits | bits fora nelas | longe (> 4) | violações de escape |
|---|---|---|---|---|---|---|
| 1 | 2.820 | 76% | 10% | 140 | 13% | 0 |
| 7 | 2.964 | 75% | 6% | 99 | 19% | 0 |
| 9 | 2.834 | 74% | 8% | 110 | 18% | 0 |
| **11** | 2.832 | **16%** | **58%** | **658** | 26% | **4** |

Nos irmãos, os ~100–140 bits "fora" são eventos reais (a rajada da coluna 0
de cada fileira, as bordas). No 11 sobram ~550 bits a mais só nas janelas
próximas do padrão — compatível com os ~600 bits de dano da zona de 4%.
Visto em bytes, o dano no padrão é troca de 1 bit isolada: `63 18 c6 31 8c`
aparece como `62 18 c6 35 8c 63 28` etc.

**Por que o JM parava no GOP 0:** ele recusa NAL com violação de escape
(`Invalid startcode emulation prevention found`). O frame 10 (B, 262 bytes)
tem 6, nos bytes 15–205; o **frame 11 tem 4, nos bytes 2.682, 2.733, 2.745 e
2.778** — dano certo, no fim do quadro. Com o GOP cortado no 9, os 10
quadros decodificam.

**O que isso muda no passo 2.** O codificador precisa de: `mb_skip_flag`;
`mb_type` de P com prefixo intra e os bins de I16x16 nos contextos de P;
modo de croma; `mb_qp_delta`; CBF do DC; `end_of_slice`. Isso cobre 7.919
MBs. Os 241 inter com resíduo (MB 0 e coluna 0 das bordas) precisam também de
`ref_idx`, `mvd`, CBP e resíduo — mas são poucos e as fileiras são
uniformes: a sintaxe deles no 11 deve repetir a do 9 com outros valores, e
entra como hipótese local. Validar primeiro no 9, bit a bit.
