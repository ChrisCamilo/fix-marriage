# Plano 5 — Frame 11: recodificar o quadro inteiro

**Estado: não iniciado** (proposto em 2026-09-25). Generaliza o método do
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
2. **A sintaxe se lê nos irmãos.** No mapa de tipos do ffmpeg
   (`-debug mb_type`) dos quadros P do fade: a tarja (fileiras 0–8 e 59–67,
   ~2.160 MBs) é **intra 16×16**; o campo (fileiras 9–58, 6.000 MBs) é
   **skip** — a predição ponderada sozinha leva o campo da referência ao valor
   novo; sobram poucos MBs inter (`>`) no começo das fileiras 0, 8 e 59. O
   frame 9, o irmão P anterior, tem 2.834 bytes contra 2.832.
   *A conferir no passo 1:* o mapa do ffmpeg mostrou blocos P com o campo
   todo intra em vez de skip, e a correspondência bloco → quadro não ficou
   clara; o JM, por sua vez, parou depois de 9 quadros do GOP 0. O trace do JM
   decide.
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
