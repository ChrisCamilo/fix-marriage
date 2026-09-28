# Plano 5 — Frame 11: recodificar o quadro inteiro

**Estado: feito — os 873 bits entraram no `patches.txt` em 2026-09-28, aprovados pelo usuário** (proposto e iniciado
em 2026-09-25 — ver "Resultados" no fim). Generaliza o método do
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

## Resultados do passo 2 (2026-09-25)

**O codificador de slice P reproduz os quadros do arquivo bit a bit.**
`tools/anchor/cabac_p.py` recebe a sintaxe de cada MB (no formato do
`jm_trace.py`) e escreve o slice data como um codificador da norma, com a
escolha de contexto de cada elemento copiada da função de leitura
correspondente do JM (`source/app/ldecod/cabac.c`) e a inicialização dos
contextos extraída do `ctx_tables.h` para `tools/anchor/ctx_init.json`.
QP, `cabac_init_idc` e o byte de início vêm do cabeçalho de slice que o
próprio trace registra (`jm_trace.cabecalhos`).

| quadro | POC | estrutura | QP | bits | diferenças |
|---|---|---|---|---|---|
| 3 | 8 | campo em skip | 11 | 6.322 | **0** |
| 7 | 16 | campo I16x16 DC, bordas meio inter | 10 | 21.578 | **0** |
| 9 | 20 | campo I16x16 DC (o molde do 11) | 10 | 21.172 | **0** |

Slice inteiro, do primeiro bit depois do alinhamento até o stop bit. Os três
cobrem tudo o que o molde do 11 usa: skip, I16x16 com modos DC e horizontal,
croma com resíduo DC e AC, inter 16x16 com `ref_idx` 0–2, `mvd`, CBP e
blocos 4x4 de luma com níveis positivos, negativos e maiores que 1.

Dois erros apareceram e foram corrigidos na validação: o DC de croma 4:2:0
usa o mapa de significância 4x4 comum (o `4x4c` é de outro formato), e o
trace do JM não registra o `end_of_slice_flag = 1` do último MB — sem forçá-lo
não havia o flush final.

**Fora do alcance, com erro explícito:** partições 16x8/8x16/8x8 e MB I4x4
(o 5 tem 16x8; o 1 tem dois I4x4, nos MBs 960 e 7080). O molde do 11 não usa
nenhum dos dois.

**Próximo (passo 3):** gerar a sintaxe do 11 a partir do molde do 9 — os
7.919 MBs do padrão são fixos; os 241 de borda e o cabeçalho viram hipóteses —
e comparar a saída do `cabac_p.py` com o arquivo.

## Passo 3 — em andamento (2026-09-26)

O `cabac_p.py` passou a decodificar também: cada elemento é escrito uma vez
contra um objeto de E/S que escreve (`Enc`) ou lê (`Dec`) bits. Validado nos
dois sentidos: codifica os quadros 3, 7 e 9 com 0 diferenças e decodifica os
8.160 MBs de cada um com a sintaxe exata do trace do JM.

**Cabeçalho do 11** (JM com o NAL cortado antes das violações de escape):
`frame_num` 6, POC 24, QP 10 (`slice_qp_delta` −16), `cabac_init_idc` 0,
slice data no byte 21 — coerente com os irmãos.

**O frame 11 é o molde do 9 com ~4% de dano, fora três regiões.** Encaixando
trechos do molde com o estado aritmético livre (no regime do padrão o range de
entrada quase não importa — o estado converge):

| trecho | começa no 11 (no 9) | discordância |
|---|---|---|
| MBs 1–959 (tarja de cima) | bit 39 (100) | 4,4% |
| MBs 988–1079 (fim da fileira 8) | desloc. −38/−40 | 1–4% por janela |
| MBs 1080–7079 (campo) | desloc. −38/−39 constante | 2,7–4,3% por janela de 2 fileiras |
| MBs 7082–8159 (fim da 59 e tarja de baixo) | desloc. −62/−63 | 2,2–3,8% |

A zona de dano medida no mapa é 4–4,5%. Os "1.140 bits a mais" do 11 não são
conteúdo: são o enchimento `00 00 03` do fim do NAL, danificado — as 4
violações de escape (bytes 2.682–2.778) caem nele.

**As três regiões que faltam** (comprimento exato, pelos encaixes vizinhos):

| região | bits no 11 | no 9 | estado |
|---|---|---|---|
| MB 0 | 39 | 100 | hipótese: P16x16, ref 2, `mvd` 0, CBP 16, DC de croma U +3 V −3 (6 bits fora nos primeiros 100, 5 deles juntos nos bits 11–20); provisória |
| MBs 960–987 | ~282 | 260 | MB 960: ref 0, CBP 35, luma dos 4 blocos de cima `(1,0)(1,1)` — o que a física da borda prevê — com 3 bits trocados (+28, +50, +64); o croma dele e os MBs 961–987 ainda não |
| MBs 7080–7081 | ~170 | 193 | não atacado |

Com o prefixo recodificado até o MB 959 emendado no arquivo, o JM passa a ler o
11 até o MB 966.

**Lições desta etapa:**
- Sintaxe decodificada do próprio arquivo, recodificada, reproduz o arquivo:
  não pode ser juiz de si mesma. O juiz é o trecho de molde conhecido
  **depois** dela (armadilha de autoconsistência, a mesma do plano 1).
- A fileira 8 do 11 não é igual à de nenhum irmão (1, 3, 5, 7 ou 9) nos MBs
  961–987; do 988 em diante é a do 9.

**Próximo:** busca por bloco nas duas regiões de borda — decodificador a partir
do estado exato, trocas de bit onde a comparação mostra desvio isolado, e o
comprimento exato da região (o molde recomeça num bit conhecido) como fecho.
Depois, a prova do passo 4 decide também a hipótese do MB 0.

### Ferramentas no repositório (2026-09-28)

`tools/anchor/f11.py` junta o que estava no scratchpad: `encaixe` (trecho do
molde com o range livre), `perfil` (janelas curtas), `le` (decodifica do
estado exato com trocas dadas) e `feixe` (decodificação em feixe sobre trocas
de bit, com ponto de retomada e memória limitada — só os sobreviventes carregam
estado completo). Os dados que vinham do JM ficam em `data/f11/`: a sintaxe dos
quadros P do fade (`sintaxe_pocNN.json.gz`) e as hipóteses provisórias do 11
(`hipoteses.json`: o MB 0 e as trocas prováveis do MB 960).

**Borda da fileira 8, até aqui.** O MB 960 é inter, ref 0, CBP 35, com a luma
da física e DC de croma U = +3; os primeiros 84 bits dele batem com o arquivo
com 4 trocas isoladas (bits 2.655, 2.677, 2.696, 2.699 — as duas últimas ainda
incertas). O feixe sobre trocas morre nos MBs grandes (4–5 trocas por MB de
~100 bits, acima do limite de 2 por MB), e o MB 961 é outro inter grande (~90
bits: a região 960–987 tem 282 bits no 11 contra 260 no 9, e os MBs horizontais
da borda custam 3–5 bits cada). Próximo: feixe sobre **valores de sintaxe**,
elemento a elemento, pontuado pelas discordâncias dos bits já emitidos — as
trocas viram só custo, sem enumerá-las.

### Feixe sobre valores de sintaxe (2026-09-28)

`f11.py sintaxe` enumera a sintaxe elemento a elemento (cabeçalho do MB, depois
cada bloco de resíduo) e pontua pelos bits já emitidos com a métrica de Fano
(ε = 4%), mais um custo de complexidade (inter 8, coeficiente 4, troca de modo
de croma 6 bits) e o molde depois da região como MBs fixos no próprio feixe.
O `cabac_p.processa_mb` ganhou o gancho `marca` (bits emitidos ao fim de cada
elemento).

**Validado no frame 9** (`F11_VALIDA=9`, resposta conhecida): acha sozinho a
sintaxe exata da borda da fileira 8 (960: ref 2, CBP 3; 961: ref 2, CBP 35;
962+: horizontal, croma 1) — 0 de 260 bits fora, 0 no molde depois; a segunda
colocada fica em ~40% no molde (506 contra 280 pontos).

**No frame 11 ainda não fecha.** O feixe acumula pontos até o MB 961 e depois
perde ~1,3 ponto por MB horizontal — ritmo de bits aleatórios: nenhum caminho
fica em sincronia depois do 961, ou seja, a escolha para 960/961 está errada e
a verdadeira foi podada antes que o molde a julgasse. Com ~4% de bits trocados,
dois MBs grandes de sintaxe livre sempre têm um lixo que casa melhor que a
verdade com dano dentro do próprio MB.

Medidas que entram na próxima tentativa: a região certa é a fileira 8
inteira (o fim dela também difere do 9: o trecho 988–1079 é ~3 bits mais curto
no 11), o campo começa no bit 3.122, e a primeira fileira do campo depende do
modo de croma da fileira 8 (contexto) — indício de croma ≠ 1 na fileira 8 do 11.

### Tentativa 1 — fileira 8 por estágios com a física (2026-09-28)

`f11.py fileira8`: MB 960 com a luma da física, DC de croma (a, 0, b, 0), AC
com os dois blocos de cima iguais; a fileira horizontal com um modo de croma
só; o 961 horizontal ou da família do 9; o campo tem que começar no bit 3.122.
O melhor candidato alinha o campo (bit 3.121) mas não é o verdadeiro: MB 960
com 12 de 100 bits fora e o 961 com 57 de 95 — o estado aritmético sai errado
do 960 e o 961 já começa quebrado.

A extensão por prefixo limpo (o critério que acertou a luma à mão) também
cede com gramática livre: leva o 960 até o bit +108, mas com 10 desvios
isolados em 108 bits (9%, o dobro da densidade da zona) e valores sem cara de
física (DC de V −3/−1, AC −2).

**Diagnóstico:** o que se provou é a luma do 960 (a física bate bit a bit,
com 3 trocas) e o DC de croma U = +3 como hipótese. O resto do croma do 960 e
o 961 inteiro não se determinam com o dano da zona: nos ~200 bits deles cabem
~8 trocas, e qualquer gramática com liberdade suficiente para conter a verdade
contém também um lixo que encaixa melhor. Falta um gabarito de croma — o valor
de croma da tarja e do campo no fade, que ninguém mediu ainda.

## Resultado: o frame 11 inteiro (2026-09-28)

**O croma dos irmãos destravou.** Medido no decode do GOP 0: o campo do fade
muda de croma em passos de 1 (U 128 → 127 → 126, V 128 → 129 → 130 até o 9); a
tarja fica 128. Com isso o croma dos MBs de borda deixa de ser livre: é o
alvo (fonte uniforme por faixa) menos a predição da referência, quantizado
como um encoder faz (`tools/anchor/residuo.py`: transformada, quantização e
reconstrução da norma, validadas reconstruindo exato MBs do frame 9).

- **Fileira 8:** MB 960 inter ref 0 com a fonte U 128/125, V 128/131 (linha 64 /
  resto) e arredondamento 1/6: 149 bits com 5 fora. MB 961 I16x16 horizontal
  com resíduo de croma a partir da reconstrução do 960 (como o 7081 do 9). A
  fileira inteira: 16 de 497 bits fora.
- **Fileira 59:** a tarja de baixo começa na linha 950 (130 linhas, simétrica à
  de cima) — com isso, MB 7080 16x8 (ref 0 no campo, ref 2 na tarja) e 7081
  horizontal com resíduo de croma: do 7080 ao fim, 96 de 2.937 bits fora. O
  `cabac_p.py` ganhou 16x8/8x16 (validado no frame 5).
- **MB 0 confirmado pela física:** ref 2 com peso (64,−16) leva a tarja a 16
  exato; o croma dela (127/129 com os offsets) pede DC U +1 / V −1 — o CBP 16
  com DC U +3, V −3 da hipótese.

**A prova (passo 4):** slice inteiro com 3,87% de bits fora, uniforme, com a
estatística de trocas independentes; NAL corrigido fechando exatamente no
tamanho do arquivo; JM sem erro com a sintaxe das hipóteses; imagem tarja
16,000 e campo 43,000 exatos; ffmpeg sem erro no GOP 0; mapa do filme inteiro
só muda o 11 (0% → 100%); `serie` no GOP 0 de 11 para 13 quadros bons (o 11 e o
12). **Aplicado em 2026-09-28** (aprovado pelo usuário): as **873 linhas** são as
3.122–3.994 do `patches.txt`, registradas em `data/patches_f11.txt`, que o
`verify` pula (uma a uma nenhuma fecha o quadro; só as 873 juntas).

Reproduzir: `python tools/anchor/f11.py nal <saida.txt>`.
