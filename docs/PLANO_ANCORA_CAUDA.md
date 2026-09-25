# Plano 2 — Âncora pela cauda

**Estado: passos 1–3 feitos** (proposto em 2026-09-23; executado em
2026-09-24 — ver "Resultados" no fim). Ferramentas em `tools/anchor/`. Um dos três planos para o
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

JM com o patch `tools/jm_mbinfo.patch`, que grava por MB o estado do
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
- **Censo nos 131 IDRs** (`data/censo_cauda.txt`, colunas: IDR, enchimento,
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

`tools/anchor/cauda_lote.py` nos 51 IDRs quebrados cuja cauda não
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
fim do `patches.txt`, registro em `data/patches_cauda.txt` (o `verify` o
pula, como os cabeçalhos), log em `data/cauda_lote.log`. `verify` segue 0
válidos / 12 falsos, agora com 1.328 pulados; o `mapa` do filme inteiro sai
idêntico antes e depois.

### Extensão aos quadros P e B (2026-09-24)

**A tarja inferior de P/B é só skip.** Medido com o JM (o patch agora grava
também o tipo de slice, o flag de skip e os contextos de skip de P e de B) nos
28 P/B do GOP 3319: fileiras 61–67 com **840 de 840 MBs skip** em todos, o
contexto do skip saturado (estado 62) já na fileira 62, e as fileiras 61–67
ocupando só ~10–13 bytes. Cada MB é "skip + não é fim de slice", sempre o
símbolo mais provável: a saída é praticamente só zeros até a terminação. Não
há incógnita de coluna 0 — o contexto depende de o vizinho **não** ser skip,
e na tarja todos são.

`tools/anchor/ancora_pb.c` recodifica as fileiras 62–67 com o
`codIRange` de entrada **e o estado do contexto do skip** como incógnitas
(32 mil hipóteses). O estado entrou depois de uma primeira passada que o
fixava saturado: esse contexto só é usado de verdade a partir da fileira 62,
e num quadro com movimento pode chegar ali sem saturar — mudaria quantos
zeros saem e pareceria dano de 1–3 bits sem ser.

- **Validação:** distância 0 nos P/B íntegros de 4 GOPs (2340, 2350, 3320–3347, 3400, 3430).
- **Controle:** 60 caudas aleatórias ficam a **4–13**. A cauda de P/B tem só
  ~30 bits testáveis, então o limiar aqui é **2**, não 3.
- **Censo dos 3.314 P/B** (`data/censo_cauda_pb.txt`): **2.541 com a cauda
  intacta**, 330 com correção certa, 440 sem correção certa, 3 ambíguos.
- **Teste que o IDR não permitia:** um P/B que hoje decodifica inteiro leu a
  cauda até o fim, e bit trocado numa sequência de skips quase sempre quebraria
  a decodificação — então correção proposta num quadro inteiro denunciaria
  falso positivo. **Só 1 dos 330** (o quadro 10) decodifica inteiro, e ele tem
  enchimento danificado no fim (zona de 4,5% do mapa de dano), onde o
  alinhamento pelo stop bit não vale. Tirados da proposta os 5 com enchimento
  no fim: 10, 11, 560, 970, 1119.

| nível | critério | quadros | bits |
|---|---|---|---|
| A | distância 0 (só o escape restaurado) ou 1 | 189 | 196 |
| B | distância 2 | 136 | 279 |

**Aplicado em 2026-09-24**, aprovado pelo usuário (níveis A e B): 475 linhas
no fim do `patches.txt` (2.679–3.153), registro em `data/patches_cauda_pb.txt`
(o `verify` o pula), log em `data/cauda_pb.log`. `verify` com `BASE_N=1338`
segue 0 válidos / 12 falsos, agora com 1.803 pulados. O `mapa` do filme inteiro
muda em **um** quadro só: o 2189 (P, dist 1), cujo erro passa do MB 7356 para
o 7358. Não é piora nem sinal de correção errada: o 2189 já decodificava lixo
antes da tarja (o erro está na fileira 61, fora do trecho recodificado), e o
bit corrigido fica 3 bytes antes do fim do NAL — o decodificador
dessincronizado lê esses bytes finais antes de estourar, então o bit mexe no
ponto em que ele desiste. Nos outros 324 quadros o erro vem antes de a cauda
ser lida, e a imagem não muda.

### Diagnóstico das 26–28 divergências do 1773: é o modelo, não dano (2026-09-24)

**A tarja pura na imagem não é tarja pura na sintaxe.** O IDR 3348 é íntegro
(decodifica os 8.160 MBs, imagem boa) e a tarja dele é 16,00 em todos os
pixels — mas no JM a **coluna 1 de todas as fileiras de tarja (61–67) usa
predição de croma modo 2**, não DC. O resultado é o mesmo pixel; o encoder só
escolheu outro modo de custo igual, e a escolha desce pela coluna. O modelo
(tudo DC) não prevê isso, e o 3348 fica a **23 bits** do modelo desde a
fileira 63 — a mesma ordem dos 26 do 1773. Os outros 5 IDRs bons e o 0 e o 29
têm a tarja pura também na sintaxe e encaixam com distância 0.

**O desenho das divergências é o mesmo nos dois.** Byte a byte, a cauda
observada do 1773 é zero com **uma rajada por fileira** (rel 34.296–298,
34.301–303, 34.306–307), com o mesmo espaçamento e o mesmo comprimento total
do modelo; as rajadas só são maiores que as que o modelo gera na coluna 0.
Dano aleatório não se alinha a uma rajada por fileira. No 3348 as 23
divergências caem do mesmo jeito, ~uma a cada 4 MBs na segunda metade de
cada fileira. E a tarja **de cima** do 1773, que decodifica certa, é pura na
sintaxe (fileiras 0–7, os 120 MBs `(I16x16, DC, croma DC, cbp 0)`) — o
desvio é só embaixo, onde a cena de cima escolhe os modos.

Conclusão: as 26–28 divergências das fileiras 63–66 do 1773 **não indicam
dano** — são, com alta probabilidade, uma coluna (ou poucas) com modo de
predição diferente, como no 3348. O escape da última fileira (`00 00 02 21`)
prova que **há** dano ali, mas não qual: `02→06` (1 bit) também deixa o NAL
válido. Quem escolheu `02→03` + `21→01` foi o modelo puro na última fileira
— ver a auditoria abaixo. (Corrigido em 2026-09-25: a primeira versão desta
seção dizia que o escape sozinho determinava os 2 bits.)

**Consequência para o lote aplicado.** No 3348, a partir da fileira 66 o
modelo acha **distância 1** — uma "correção" num quadro sem dano. O lote não
a propõe só porque as 3 saídas distintas discordam (a regra de unicidade
barrou). Mas o mesmo mecanismo pode ter passado nos IDRs corrigidos só com as
fileiras 66–67, que são justamente os que não encaixam desde a 63: **16 IDRs,
32 bits** (fileira 66: 410, 485, 901, 2275, 2757; fileira 67: 99, 323, 352,
439, 1011, 1773, 1833, 1949, 2420, 2525, 2971). Nos
P/B o risco foi medido: dos ~980 P/B que decodificam inteiros, só o 10 (já
excluído) recebeu proposta.

**Próximo passo:** estender o codificador com colunas de modo variante
(croma 1–3, luma 0–3), validar no 3348 com os estados de contexto que o JM
grava, e com ele (a) auditar os 30 bits de entrada 66–67 — correção que o
modelo estendido explica sem troca sai, com aprovação do usuário — e (b)
reencaixar o 1773 desde a fileira 61, o que daria a âncora de trás para a
busca de dois lados.

### Auditoria com o modelo de coluna variante (2026-09-25)

**Modelo estendido.** `tools/anchor/cabac_enc2.py` codifica a tarja com
o modo I16x16 e o modo de croma livres por MB, com a seleção de contexto do JM
(`mb_type[0][a+b]`, 4, 5, 7, 8; `cipr[a+b]` e `cipr[3]`; `dqp[0]`;
`bcbp[0][left+2·upper]`). Com os estados e a sintaxe que o JM grava, dá
**distância 0** no 3348 (coluna 1 em croma 2) desde a fileira 63, e no 2333 e
no 3319; o modelo puro dá 23 no 3348. `tools/anchor/ancora3.c` é a busca
em C: range, k1 e k20 exaustivos, os dois estados de `cipr` sorteados (ou
fixos), uma coluna variante dada.

**Uma fileira só não distingue.** No 3348 às cegas, a última fileira encaixa
com distância 0 em qualquer variante testada (colunas 1, 5, 50); desde a 63,
a certa fica a 6 e as erradas a 10–11.

**Falso positivo da regra do lote, medido.** Caudas sintéticas **sem dano**,
cada uma com uma coluna variante sorteada (modo de croma 1–3 ou modo luma 0,
1, 3; estados e range sorteados), passadas pela mesma regra que aprovou o lote
(modelo puro, distância ≤ 3, todas as saídas distintas nos mesmos bits).
Controle: tarja pura com 1 bit trocado.

| entrada | variante, sem dano: propõe correção | controle: propõe | controle: acerta |
|---|---|---|---|
| fileira 67 | **33 de 150 (22%)** | 130 de 150 | 130 |
| fileira 66 | **7 de 150 (4,7%)** | 115 de 150 | 115 |
| fileira 65 | 1 de 100 | — | — |
| fileira 63 | 0 de 100 | — | — |

A regra é certeira em tarja pura (245 de 245 correções certas no controle) e
falha só diante da sintaxe variante, e muito mais quanto menos fileiras
entram. Entrada 63–65: seguro, como previsto.

**Os 32 bits de entrada 66–67 não estão provados.**
- Fileira 67 (11 IDRs, 20 bits): 22% de falso positivo sob variante, e o
  critério de seleção deles — não encaixar desde a 63 — é exatamente o sinal
  de variante.
- **2757: a correção é inválida.** Ela cria `00 00 00 01` dentro do NAL, o que
  o encoder nunca grava (teria escapado para `00 00 03 00 01`, um byte a mais,
  que troca de bit não produz). A checagem de escape do lote começava 1 byte
  depois do início da violação.
- 1773 e 2420: a violação de escape prova dano naqueles bytes, mas não qual
  bit — `02→06` no 1773 e trocar o `03` no 2420 também validam o NAL com 1 bit.
- Fileira 66 (410, 485, 901, 2275): 4,7% sob variante.

**Retiradas em 2026-09-25**, aprovado pelo usuário: as 32 linhas saíram do
`patches.txt` (3.153 → 3.121), registro em `data/cauda_auditoria.txt`, e
ficam comentadas com `# RETIRADA` no `data/patches_cauda.txt`. `mapa` do
filme inteiro idêntico antes e depois; `verify` 0 válidos, 12 falsos, 1.771
pulados. Ficam 33 bits de cauda de IDR, em 13 IDRs, todos com encaixe desde
as fileiras 63–65.

**Para o 1773 (âncora de trás):** o modelo estendido explica o tipo de
divergência, mas achar a configuração exata desde a fileira 63 custa
~6×10¹⁰ hipóteses por configuração (range × k1 × k20 × dois estados de
`cipr`). O refinamento por coordenadas leva a configuração certa do 3348 a 3
(as erradas ficam em 10) e não chega a 0. Fica em aberto.
