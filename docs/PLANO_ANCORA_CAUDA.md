# Plano 2 — Âncora pela cauda

**Estado: passos 1–3 feitos** (proposto em 2026-09-23; executado em
2026-09-24 — ver "Resultados" no fim). Ferramentas em `ferramentas/ancora/`. Um dos três planos para o
dano denso dos IDRs quebrados; os outros são
[`PLANO_JUIZ_ENCODER.md`](PLANO_JUIZ_ENCODER.md) e
[`PLANO_SUBSTITUICAO_REFERENCIA.md`](PLANO_SUBSTITUICAO_REFERENCIA.md).

## Por que este plano existe

- **O gabarito forte está no fim.** A tarja inferior (linhas 962–1079, `Y=16`,
  `U=V=128`) é o único gabarito que nunca cedeu, e tarja certa implica quadro
  inteiro certo. Mas ela é a última coisa do slice, e em rajada nenhuma cadeia
  de bits chega até ela — então ela só julga no final, nunca guia.
- **O 1773 tem dano na própria cauda.** Três violações de escape nos últimos
  60 bytes do NAL (rel 34.257, 34.285 e 34.312). Nos quadros bons essa classe
  só aparece nos últimos 4–7 bytes; se forem dano, são mais 3 bits além do da
  frente, e nenhuma busca de 2 bits fecharia o quadro (RASTREIO.md, "IDR 1773
  no JM").

## A ideia

A tarja de um IDR tem **sintaxe conhecida**: I16x16 com predição DC, CBP 0,
`mb_qp_delta = 0`, croma DC, sem coeficientes — repetida em ~840 MBs
(fileiras 61–67; a fileira 60 ainda tem 2 linhas de cena). Símbolos quase
certos custam quase nada em CABAC: a tarja inteira ocupa **algumas dezenas de
bytes no fim do NAL** — justamente onde estão as violações do 1773.

Recodificando essa sequência conhecida de símbolos com um codificador CABAC,
dá para achar **em que bit e em que estado do decodificador a tarja começa**,
e corrigir a cauda com o valor certo — correção determinística, não busca por
imagem. E o estado de entrada vira um **teste intermediário**: o conserto do
meio tem que chegar ao início da fileira 61 exatamente naquele bit e naquele
estado. São ~18 bits de estado mais a posição exata: acaso na casa de 1 em
10⁸.

## Passos

1. **Medir a tarja nos 6 IDRs íntegros com o JM** (`ldecod` com trace, no
   scratchpad `JM/`).
   - Confirmar que as fileiras 61–67 têm sintaxe fixa.
   - Instrumentar o decodificador aritmético do JM (uma linha em
     `biaridecod.c`) para imprimir, no início de cada MB, a posição em bits e
     o estado: `codIRange`, `codIOffset` e os contextos usados pela tarja.
   - Saber quantos bytes a tarja ocupa e em quantos MBs os contextos saturam.
2. **Codificador CABAC mínimo** só para os elementos da tarja (`mb_type`,
   `intra_chroma_pred_mode`, `mb_qp_delta`, `coded_block_flag` do DC,
   `end_of_slice_flag`, terminação), com as tabelas do JM.
   - **Teste que decide o plano:** partindo do estado medido na fileira 61,
     reproduzir **bit a bit** o fim do NAL dos 6 IDRs íntegros.
3. **Nos IDRs quebrados, achar o ponto de entrada:** varrer posição em bits ×
   estado aritmético (`codIRange`, `codIOffset`), com os contextos saturados
   depois de poucos MBs de tarja, e ficar com o que reproduz a cauda do
   arquivo com o menor número de bits trocados.
   - Saída: a correção da cauda (valor certo) e o estado de entrada da tarja.
4. **Usar o estado como teste no meio:** candidato só passa se o decodificador
   chegar ao início da fileira 61 no bit e no estado do passo 3. Combina com o
   juiz rápido do plano 1: um guia a busca, o outro confirma.
5. **No 1773:** explicar as 3 violações de escape como trocas específicas, e
   rodar a busca das fileiras 57–60 com o teste do passo 4.

## Custo e riscos

- **Custo:** 2–3 dias; o passo 2 é a maior parte.
- **Risco:** se os contextos não saturarem rápido, o passo 3 vira busca sobre
  estados de contexto também, e cresce. O passo 1 mede isso antes de tudo.
- **Limite:** a correção da cauda sozinha não faz o quadro decodificar — o
  dano do meio continua. O ganho é transformar a tarja de juiz final em teste
  intermediário forte, e provar bits da cauda.
- Correção de cauda que entrar no `patches.txt` passa pelo usuário, como
  qualquer linha.

## Onde registrar

Medidas da tarja e do codificador no CABAC.md (seção 4, "a tarja não é
separável" — este plano é a exceção a verificar); corridas no RASTREIO.md;
bits provados, com a prova, no `patches.txt` depois de aprovados.

## Resultados

### Passo 1 — a tarja nos 6 IDRs íntegros (2026-09-24)

JM com o patch `ferramentas/jm_mbinfo.patch`, que grava por MB o estado do
decodificador aritmético e dos contextos da tarja.

- **Sintaxe:** fileiras 61–67 (840 MBs) todas I16x16, predição DC, croma DC,
  CBP 0, em 4 dos 6 IDRs; 1 e 7 exceções nos outros dois (3319, 3348).
- **Tamanho:** as fileiras 61–67 ocupam **46–55 bytes** no fim do NAL.
- **Contextos:** na entrada da fileira 61 **não** estão saturados, e variam
  de IDR para IDR. A partir da fileira 62, todo MB fora da coluna 0 usa
  contextos **iguais nos 6** (saturados). Os MBs da coluna 0 usam dois
  contextos que nunca saturam (1º bit do tipo de MB com vizinho esquerdo
  ausente, e o flag do DC com a mesma condição): uma incógnita por fileira.

### Passo 2 — codificador e validação (2026-09-24)

`ancora.c` (scratchpad): recodifica com CABAC a sintaxe da tarja a partir de
um MB de entrada, para todo estado do codificador (`codILow` 0–1023,
`codIRange` 256–510, bits pendentes 0–8), e compara com o fim do RBSP
alinhando pelo bit de parada. Entrada na última fileira, coluna 1 (119 MBs,
~40 bits), onde todos os contextos são conhecidos.

| | melhor distância |
|---|---|
| 6 IDRs íntegros | **0** em todos (vários estados empatam: 40 bits não determinam o estado) |
| controle: 1 bit trocado de propósito | **1**, no bit trocado |
| caudas aleatórias (12) | 9 a 18 |
| **1773** | **2** |

### O 1773: dois bits provados na cauda

Todos os estados de distância mínima apontam os mesmos bits. A correção que
deixa o fluxo válido:

| rel | offset absoluto | bit | arquivo | correto |
|---|---|---|---|---|
| 34.314 | 59.364.902 | 0 | `02` | `03` |
| 34.315 | 59.364.903 | 5 | `21` | `01` |

`e1 00 00 02 21 6f` → `e1 00 00 03 01 6f`: o encoder tinha `00 00 01` no RBSP
e o escapou corretamente como `00 00 03 01`; o dano trocou 2 bits. É a
violação de escape que estava anotada em 34.312 (o `00 00 02` começa ali).
Comparar no RBSP sem considerar o escape dá distância 2 com uma correção
inválida (`00 00 00 01`); com o escape restaurado, distância 1 mais o próprio
escape.

**Não entraram no `patches.txt`** — precisam de aprovação e sozinhos não
fazem o quadro decodificar (o dano da frente continua). As outras duas
violações (34.257 e 34.285) ficam antes da última fileira, no trecho em que
entram os MBs da coluna 0.

### Passo 3 — várias fileiras, com a coluna 0 como incógnita (2026-09-24)

O RBSP da tarja é **quase todo zero**: com símbolos que são o mais provável do
contexto, o codificador só desloca o `low`, sem somar nada. Cada fileira gera
uma pequena rajada, que vem do MB da coluna 0 — ali os dois contextos que
nunca saturam produzem símbolos menos prováveis. Então a cauda inteira fica
determinada por poucos números: `codIRange` na entrada, o estado dos dois
contextos da coluna 0 e o `low` inicial.

`ancora2.c` busca (`codIRange` × estado de `mb_type[1]` × estado de
`bcbp[0][1]`, 4 milhões de hipóteses, OpenMP, ~4 s) a partir da fileira 63,
onde todos os outros contextos já estão iguais nos IDRs íntegros.

- **Validação:** 2333 e 3426 reproduzem os ~170 bits das fileiras 63–67 com
  distância 0; no 2333 a busca **recupera o estado real** do contexto do DC
  (45, igual ao que o JM grava). Controle aleatório: 65.
- **Censo nos 131 IDRs** (`dados/censo_cauda.txt`, colunas: IDR, enchimento,
  distância e bits da última fileira, distância e bits das fileiras 63–67):

  | | última fileira = 0 | fileiras 63–67 = 0 |
  |---|---|---|
  | IDRs bons (7) | 7 | 6 (o 0 é o fade todo preto) |
  | **IDRs quebrados (124)** | 90 | **73** |

  **73 IDRs quebrados têm a cauda intacta e explicada bit a bit**, com o
  estado de entrada da fileira 63 determinado. Os outros 51 têm dano também
  na cauda.
- **O 1773 é um dos 51.** Encaixa na última fileira depois dos 2 bits
  provados, mas as fileiras 63–66 ficam a 26–28 bits do modelo, e a
  diferença cresce a cada fileira para trás (3, 9, 22, 26). As duas outras
  violações de escape (34.257, 34.285) ficam antes desse trecho — as
  fileiras 63–67 ocupam só os ~21 bytes finais. Ou seja: além do dano da
  frente e das 3 violações, há mais dano dentro das próprias fileiras de
  tarja. Consistente com o 1773 estar numa zona de dano, e com as opções
  a/b/c de 2 bits não poderem fechar.

**O que isto dá ao projeto:** para os 73 IDRs de cauda intacta, a posição
exata em bits e o estado aritmético no início da fileira 63 — o teste
intermediário do passo 4 (um conserto do meio só vale se chegar ali naquele
bit e naquele estado). O que isto **não** dá: redução da busca no meio, onde
as sondas mostraram dano denso nos 85 IDRs.

### Lote de correções de cauda — proposta (2026-09-24)

`ferramentas/ancora/cauda_lote.py` nos 51 IDRs quebrados cuja cauda não
encaixou perfeitamente no censo (70 min). Regras de certeza:

- trecho mais longo de tarja que encaixa com **no máximo 3** diferenças;
- o `ancora2.exe` percorre **todas** as hipóteses na distância mínima e agrupa
  as saídas distintas; só vale se **todas** apontam exatamente os mesmos bits;
- a correção aplicada tem que dar distância 0 e não deixar violação de escape
  no trecho; se exigir um escape que não existe, testa antes restaurar o
  escape danificado (caso do 1773).

**29 IDRs com correção certa, 65 bits; 22 sem.** Nenhum conflita com o
`patches.txt`. Controle com caudas aleatórias, mesmo teste: distância mínima
7–18 (só a última fileira), 20–32 (desde a 66), 32–47 (65), 59–79 (63) —
nenhuma chega a 3.

| nível | critério | IDRs | bits |
|---|---|---|---|
| A | trecho explicado > 64 bits (2+ fileiras) | 17 | 42 |
| B | só a última fileira | 12 | 23 |

O nível B é mais fraco pelo único risco que sobra: uma sintaxe diferente nos
últimos MBs muda só bits da terminação e pareceria dano. Nos 7 IDRs íntegros e
em 90 dos 124 quebrados a última fileira é tarja pura, então é improvável, mas
não está excluído.

**Aplicado em 2026-09-24**, aprovado pelo usuário (níveis A e B): 65 linhas no
fim do `patches.txt`, registro em `dados/patches_cauda.txt` (o `verify` o
pula, como os cabeçalhos), log em `dados/cauda_lote.log`. `verify` segue 0
válidos / 12 falsos, agora com 1.328 pulados; o `mapa` do filme inteiro sai
idêntico antes e depois.
