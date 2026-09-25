# CABAC — o que é, por que não ressincroniza, e o que dá para explorar

Este arquivo existe porque o diagnóstico dos frames 11 e 12 parou em
"dessincronização do CABAC" sem que o projeto tivesse escrito o que isso
significa mecanicamente. Sem isso não dá para separar ideia de reparo que tem
base de ideia que só soa bem.

Tudo aqui vale para o perfil deste filme: **High (100), CABAC ligado
(`entropy_coding_mode_flag = 1`), um slice por quadro, sem FMO, sem ASO, sem
partição de slice** — medido no SPS/PPS, não suposto.

## 1. O estado do decodificador

O CABAC do H.264 é um codificador aritmético binário com contextos adaptativos.
O estado completo, a cada instante, é:

| parte | o que é |
|---|---|
| `codIRange` | largura do intervalo, 9 bits, sempre em [256, 510] depois da renormalização |
| `codIOffset` | posição dentro do intervalo, 9 bits, sempre `< codIRange` |
| ponteiro de bit | onde no payload está a leitura |
| ~460 contextos | cada um um par `(pStateIdx, valMPS)`: 64 estados de probabilidade e qual símbolo é o mais provável |

### Inicialização, no começo de cada slice

Os contextos saem de **`cabac_init_idc` e `SliceQPY`**, por tabela da norma:

```
preCtxState = Clip3(1, 126, ((m * Clip3(0, 51, SliceQPY)) >> 4) + n)
preCtxState <= 63 :  pStateIdx = 63 - preCtxState,  valMPS = 0
preCtxState >  63 :  pStateIdx = preCtxState - 64,  valMPS = 1
```

E o motor aritmético:

```
codIRange  = 510
codIOffset = ler 9 bits
```

**A norma proíbe `codIOffset` valer 510 ou 511.** É a única validação que dá
para fazer sem decodificar nada.

Consequência que importa aqui: `slice_qp_delta` errado não estraga um símbolo,
**estraga os 460 contextos de uma vez**. Não é dano local, é dano global — e é
por isso que mexer nele nunca deu imagem parcial, sempre deu preto.

### Decodificação

```
DecodeDecision(ctx):  qIdx = (codIRange >> 6) & 3
                      rLPS = rangeTabLPS[pStateIdx][qIdx]
                      codIRange -= rLPS
                      se codIOffset >= codIRange -> LPS: bin = !valMPS
                                                    codIOffset -= codIRange
                                                    codIRange = rLPS
                                                    se pStateIdx == 0: valMPS = !valMPS
                                                    pStateIdx = transIdxLPS[pStateIdx]
                      senao                      -> MPS: bin = valMPS
                                                    pStateIdx = transIdxMPS[pStateIdx]
                      RenormD

RenormD:              enquanto codIRange < 256: codIRange <<= 1
                                                codIOffset = (codIOffset << 1) | ler 1 bit

DecodeBypass:         codIOffset = (codIOffset << 1) | ler 1 bit    (sem contexto,
                      se codIOffset >= codIRange -> bin = 1          codIRange intacto)
                                                    codIOffset -= codIRange
                      senao -> bin = 0

DecodeTerminate:      codIRange -= 2
                      se codIOffset >= codIRange -> bin = 1, fim do slice
                      senao -> bin = 0, RenormD
```

## 2. Por que não existe ressincronização

Cada decisão depende de `codIRange`, de `codIOffset` e do contexto — e os três
dependem de **toda** a história anterior. Um bit trocado muda `codIOffset`, que
muda a decisão seguinte, que muda qual contexto é consultado depois, que muda
quantos bits são lidos na renormalização. O ponteiro de bit em si sai do lugar.

Não há marcador de sincronismo dentro do slice. **Os únicos pontos de
ressincronização do H.264 são fronteiras de slice**, onde os contextos são
reinicializados e o motor recomeça com `codIRange = 510`.

Os outros mecanismos da norma que dariam mais pontos de recomeço **não existem
neste arquivo**:

| mecanismo | situação aqui |
|---|---|
| múltiplos slices por quadro | não — `first_mb_in_slice = 0` em todos, um slice por quadro |
| FMO (`num_slice_groups > 1`) | não — o PPS diz 1 |
| ASO / slices redundantes | não — perfil High não permite |
| partição de slice (NAL 2,3,4) | não — só perfil Extended, e não com CABAC |

Então **há exatamente um ponto de ressincronização por quadro: o começo dele.**
Não existe "ressincronizar o CABAC no meio do frame 11". Isso não é limitação
de ferramenta, é o formato.

### A única propriedade aproveitável

Se não há recomeço, também é verdade que **tudo que foi decodificado antes do
primeiro erro está correto**. O prefixo do slice é válido. Daí sai a medida de
progresso que o projeto não estava usando: *até que macrobloco o decodificador
chegou*.

Um candidato que ainda erra pode ser estritamente melhor que outro que também
erra, e a diferença é medível. O critério binário "decodifica limpo" joga isso
fora — ver o modo `avanco` e a armadilha 31.

## 3. O que um decodificador consegue detectar

Nenhuma dessas é soma de verificação: são violações de sintaxe ou de
semântica que o dano acaba produzindo. Detectam, não localizam.

| detecção | mensagem do ffmpeg neste projeto |
|---|---|
| `ref_idx` além da lista ativa | **`Reference 3 >= 3`** (frame 11), **`Reference 4 >= 2`** (frame 12) |
| modo intra pedindo vizinho indisponível | **`left block unavailable for requested intra mode`** (frame 13) |
| falha genérica de macrobloco | **`error while decoding MB x y, bytestream N`** |
| `mb_type` / `cbp` fora de faixa | `mb_type N in P slice too large` |
| ocultação acionada | `concealing N DC, N AC, N MV errors` |
| contagem de macroblocos contra `end_of_slice_flag` | slice termina cedo ou tarde |
| alinhamento de byte e `rbsp_slice_trailing_bits` | bit 1 seguido de zeros até o byte |
| `cabac_zero_word` em pares de `00 00` | **já explorado**, `escapes.py` |
| `00 00 01` ilegal no payload | **já explorado**, 84 bits provados |
| `00 00 02` / `00 00 00` no payload | **ENCERRADO em 2026-09-18** — 93 NALs; ver armadilha 27 (revista). Não reabrir a classe: decide-se caso a caso, pelo critério de ocultação |
| ocultação sem erro (`end_of_slice` cedo) | **explorado em 2026-09-18**, armadilha 59; o critério agora exige zero ocultados |
| `codIOffset` inicial igual a 510 ou 511 | **explorado** no frame 11 — foi o que denunciou o cabeçalho corrompido (`data/janela_f11.txt`) |

Um detalhe que o projeto já mediu e que agora tem explicação: o
`bytestream N` do frame 12 aponta o byte 1.820, mas os dados reais acabam no
1.135. Não é contradição — é o motor aritmético consumindo enchimento depois de
perder o rumo. **O ponto do erro não é o ponto do dano**, e por isso a janela de
varredura nunca deve ser escolhida a partir dele.

## 4. A tarja não é separável do resto do slice

Pergunta natural: já que o valor da tarja é conhecido, dá para achar o bit que
conserta **só** ela e deixar o borrão para depois? Não dá, e por duas razões
independentes.

**Primeira, geral.** Cada símbolo muda `codIRange`, `codIOffset`, o contexto
consultado e quantos bits a renormalização consome. Não existe recorte de bits
que pertença só à tarja: mexer nela desloca tudo o que vem depois. É a mesma
razão pela qual o `slice_qp_delta` não pode ser ajustado de leve.

**Segunda, específica do frame 13.** A tarja de baixo é a fileira de macrobloco
60 a 67, e o quadro quebra na fileira **55**. Ela está **depois** do ponto de
ruptura, e sem ressincronização os bits dela nunca chegam a ser lidos em estado
correto. Não há bit que arrume a fileira 60 deixando a 55 quebrada, porque a 60
depende de todo símbolo anterior.

### E a estrutura interna da tarja é a mesma em todo quadro?

Quase, mas não o suficiente para virar molde. Medido em 42 quadros traçados dos
GOPs 0 e 2333, olhando os 960 macroblocos da tarja de cima:

| natureza | tipos na tarja | quadros |
|---|---|---|
| comum | só `d` (direto, quadro B) | 25 |
| comum | só `S` (skip, quadro P) | 7 |
| comum | `>` e `I` misturados | **4** |
| comum | só `>` | 2 |
| comum | `>` e `d` | 1 |
| IDR | só `I` | 2 |
| IDR | `>` e `I` | 1 |

Na maioria esmagadora os 960 macroblocos são de **um tipo só**, e esse tipo é
skip ou direto — o que se espera de uma região chapada e igual à referência. Mas
**4 quadros comuns usam macrobloco intra na tarja**, então a regra "tarja nunca
é intra fora de IDR" é falsa e não serve de invariante. Foi testada e refutada
antes de virar regra.

### O que sobra: repintura, e o frame 13 ainda não se qualifica

A repintura da tarja (ver o `REPINTA` do `reparador.c`) é pós-decodificação e
não depende do CABAC — é a única via realmente separável. Mas a regra do projeto
é explícita: **só entra quadro cuja imagem foi verificada contra gabarito.**

No frame 11 valia, porque o campo dele batia com a rampa do fade. No frame 13
não vale: as fileiras 55 a 59 são ocultação **dentro da área visível** (linhas
880 a 959), ou seja **5 das 52 fileiras de imagem, 9,6% do que se assiste**.
Repintar a tarja ali seria emoldurar de forma correta um quadro ainda errado.
