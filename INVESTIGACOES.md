# Investigações: o que foi resolvido, descartado e o que segue aberto

Hipóteses testadas, com o resultado. O que está marcado RESOLVIDO ou
DESCARTADO não deve ser reinvestigado sem motivo novo.

### RESOLVIDO em 2026-09-13: o critério não localiza o bit — não reinvestigar

A pergunta era "o dano é de 1 ou de 2 bits?". **A pergunta estava mal posta.**
Medido com o modo `unico`, que enumera *todas* as inversões de 1 bit que fazem
o IDR decodificar perfeito, nos 71 IDRs quebrados:

| soluções de 1 bit | IDRs |
|---|---|
| 0 | 26 |
| 1 (única) | 2 |
| 2–9 | 7 |
| **100+** | **36** |

Pior caso: **4467 inversões distintas** de 1 bit no mesmo IDR, todas passando no
critério rigoroso. As soluções saem em blocos de bytes vizinhos em torno do
corte (ex.: `14940/0 14940/1 14940/3 14940/6 14941/0 …`).

Consequência: **decodificação perfeita prova coerência sintática, não que aquele
era o bit corrompido.** É a armadilha 2 do `ARMADILHAS.md` numa forma que ela não
previa — lá o alerta era "o CABAC anda mais um pouco", aqui são decodificações
completas e limpas. Não adianta buscar 2 bits: se 1 bit já tem milhares de
soluções, 2 bits tem ordens de grandeza mais.

Como isto foi descoberto: o IDR 1802 recebeu soluções diferentes em duas
corridas com ordens de varredura distintas (`off 63 bit 2` e `off 19344 bit 0`).
Sem esse acaso, 44 reparos arbitrários teriam entrado no `patches.txt`, passado
no `verify` e produzido imagem errada sem nenhum sinal.

**Onde o critério ainda decide:** nos 9 IDRs com 1 a 9 soluções, todos com o bit
no **início do NAL** e corte pequeno. O `12/0` aparece como solução única em dois
IDRs distintos (2188 e 2612) e também entre as três do 1625 — padrão de slice
header, não coincidência. Isso reforça a anomalia abaixo.

### VALIDADO em 2026-09-14: a tarja decide onde nenhum outro critério alcança

Teste no IDR 2362, que é o caso difícil por definição: GOP inteiro escuro, nenhum
vizinho íntegro, nenhum critério temporal aplicável.

Varredura nas janelas do `INVESTIGACOES.md` (±1024 em volta do corte, que fica no byte
11538, mais 256 no início do NAL): **zero soluções em volta do corte e UMA no
início**. Pelo critério sintático, solução única — o caso mais forte que existe.

A tarja reprovou:

| IDR | topo | tarja | desvio | imagem |
|---|---|---|---|---|
| 2333 (bom) | 15,999 | **16,000** | 0,050 | 167,6 |
| 3319 (bom) | 15,995 | **15,992** | 0,161 | 109,5 |
| 3397 (bom) | 16,002 | **16,002** | 0,080 | 91,2 |
| 2362 candidato | 16,000 | **19,248** | 0,432 | **17,5** |

O candidato produz quadro quase preto com a tarja inferior em 19,25. **Sem a
tarja ele entraria no `patches.txt` como reparo definitivo de um IDR**,
destravaria 28 frames de lixo e passaria no `verify`.

Dois subprodutos que valem tanto quanto:

- **A tarja localiza — mas leia direito.** No candidato do 2362 o topo saiu em
  16,000 (certo) e a base em 19,248 (errado). A leitura apressada foi "o erro
  está no fim do NAL". **Errado:** a *imagem* também saiu errada (média 17,5
  contra 91–168 dos IDRs bons), então só as ~8 primeiras fileiras de macrobloco
  — a tarja superior — decodificaram bem. A divergência é **precoce**.

  Somando com a armadilha 9, que mede o bit ~2600 bytes *antes* do corte: para o
  2362, cujo corte está em 11538, a região a varrer é **[5, 12562)**, que as
  janelas de ±1024 não cobrem. É por isso que ele parecia ter solução única.

  Custo dessa faixa: 100.456 candidatos, **1 a 3 minutos**. O NAL inteiro são
  1.767.088 candidatos, 12 a 58 minutos.
- **Custo irrisório.** A janela do corte levou 32 s e a do início 4 s, com 6
  threads disputando CPU; o teste da tarja em si é uma decodificação por
  candidato. O gargalo nunca foi medir, foi não ter o que medir.

### A tarja disse NAO a 24 candidatos do IDR 2362 — e o IDR precisa de 2+ bits

Continuação do teste acima, varrendo a faixa que a armadilha 9 indica, `[5,
12562)`: **24 soluções de 1 bit** em 176 s (6 threads disputando CPU). A janela
de ±1024 do `INVESTIGACOES.md` tinha achado **uma só**, e no lugar errado.

As soluções se concentram em **rel 8900–10400**, ou seja ~1100 a 2600 bytes antes
do corte (11538). A armadilha 9 acertou a região.

A tarja reprovou **todas as 24**:

| | topo | tarja | desvio | campo |
|---|---|---|---|---|
| IDRs bons | 16,00 | **16,00** | 0,05–0,16 | conteúdo |
| 23 dos 24 candidatos | **16,000** | **90 a 158** | 50 a 72 | 0–255 |
| candidato rel 75 | 16,000 | 19,2 | 0,43 | 16–19 (preto) |

O padrão importa: o **topo sai certo** em 23 deles, e a tarja de baixo sai
destruída. Não é um filtro que rejeita tudo por reflexo — ele distingue as duas
metades do mesmo quadro. A decodificação anda certo e diverge antes do fim.

**Conclusão: o IDR 2362 não é reparável com 1 bit.** Bit depois do corte não
adianta (o decoder nem chega lá), e a faixa antes do corte está varrida. Sem a
tarja, o resultado teria sido "24 soluções, escolha uma" — e qualquer escolha
destravaria 28 frames com a tarja inferior em ruína.

### Frame 2360 reparado: solução única em 260 mil candidatos

Primeiro reparo em **cena de verdade** (não no fade). O 2360 é um dos dois
buracos do GOP 2333, o GOP parcial mais completo do filme com 27 de 29 quadros.

Varredura exaustiva do NAL inteiro — 259.968 candidatos, 64 min, 12 threads:
**uma única solução**, em `rel 8`, dentro do cabeçalho da slice. Os três juízes
concordam, e nenhum marginalmente:

| juiz | candidato | faixa dos genuínos |
|---|---|---|
| tarja | 16,000 / desvio 0,071 | 15,98–16,19 / até 0,41 |
| gabarito (interpolação) | 0,911 | piso 0,66 a 1,25 nesta cena |
| blocagem | 0,992 | 0,993 / 1,009 / 1,018 |

Em resolução cheia a imagem está limpa — rosto, gravata, mangas, papel de
parede, sem um bloco fora do lugar — e o movimento das mãos progride
coerentemente entre 2359 e 2358. Patch `77496463 2`.

### A faixa de corte, medida nos 121 IDRs quebrados (2026-09-14)

O modo `cortes` levanta, para cada IDR que não decodifica, onde o decoder para
de consumir. Resultado:

| | |
|---|---|
| corte mínimo | **5 bytes** |
| corte mediano | **11.538** — 7,6% do NAL |
| corte máximo | 60.787 |
| **39 dos 121** | param antes do byte **1.000** |

**O dano é cedo.** Isso casa com a anomalia da seção anterior: 20% dos
prefixos de NAL e 17% dos slice headers corrompidos contra interior quase limpo.

#### A janela do `INVESTIGACOES.md` erra o alvo em 121 de 121

A varredura antiga usou `[corte-1024, corte+1024]`. A armadilha 9 mediu, com erro
injetado em posição conhecida, que o bit fica **~2600 bytes antes** do corte.
A janela não alcança essa distância em **nenhum** dos 121 IDRs.

Confirmado na prática no IDR 2362: a janela do `INVESTIGACOES.md` achou **1** solução; a
faixa `[5, corte+1024]` achou **24**. Logo, a tabela do `INVESTIGACOES.md` mede outra coisa
que não o que diz medir:

| o registro antigo diz | provavelmente é |
|---|---|
| 26 IDRs "sem solução de 1 bit" | o bit estava fora da janela |
| 2 IDRs "com solução única" | única *naquela janela* |
| 36 IDRs "com 100+ soluções" | esses sim, ambíguos de verdade |

**A faixa certa é `[5, corte+folga]`**, e ela é barata porque o corte é cedo: os
121 somam 13,45 milhões de candidatos, 1,5 a 6,5 h. O modo `cortes` a calcula.

#### Os 39 IDRs de corte precoce: varridos, e nenhum é reparável com 1 bit

337 mil candidatos, 2% do total. Resultado:

| soluções de 1 bit | IDRs |
|---|---|
| 0 | **38** |
| 1 | 1 (o IDR 0) |

A única solução, no IDR 0 em `rel 154`, **a tarja reprovou**: quadro 16–18 com
tarja em 18,248 e desvio 0,432, onde todo frame bom tem 16,00 e desvio ≤ 0,16.
Mesmíssimo padrão do candidato do 2362 — topo certo, base errada.

**Conclusão: IDR cujo decoder para nos primeiros 1.000 bytes não se conserta com
1 bit.** São 38 de 39 sem nenhuma solução, o que é o oposto do problema de
ambiguidade — ali não há o que escolher, há o que não existe. Provavelmente dano
no prefixo ou no slice header exigindo vários bits.

#### Calibração da tarja contra quadros genuínos — e ela aprova

Testado no GOP 3426, o último com quadros intactos, e depois em toda a amostra
disponível. **O limiar que eu vinha usando estava errado**: `média 16,00 ± 0,10,
desvio < 0,25` reprovava **3 de 10 quadros genuínos** daquele GOP.

Calibração correta, em **144 quadros genuínos** (trechos 2333–2361 e 3319–3444,
já com o filtro da armadilha 12 aplicado — sem ele um quadro de GOP quebrado
entrou na amostra com tarja média 183 e arruinou a estatística):

| | |
|---|---|
| tarja média | 15,984 a **16,188** (mediana 16,000) |
| tarja desvio | 0,008 a **0,408** (mediana 0,119) |
| topo média | 15,991 a 16,015 |

**Limiar que aceita 100% dos genuínos: `|média−16| ≤ 0,19` e `desvio ≤ 0,41`.**

Afrouxar não enfraquece nada, porque as rejeições nunca foram apertadas:

| caso | tarja média | erro / tolerância |
|---|---|---|
| candidato do IDR 0 | 18,248 | **12x** |
| candidato rel 75 do 2362 | 19,248 | 17x |
| 23 candidatos do 2362 | 90 a 158 | 390x a **750x** |

**Correção: o reparo do frame 3435 NÃO tem defeito de tarja.** Eu havia
registrado isso no commit e aqui. Ele dá 16,186 / 0,409 e os vizinhos genuínos
3436 e 3438 dão 16,188 / 0,395 e 16,180 / 0,408 — indistinguível. O erro foi
calibrar o limiar contra o miolo do filme, onde a tarja é limpa, e aplicá-lo na
cauda do fade, onde o encoder gasta menos bits e ela fica ruidosa.

Os outros dois reparos ficam no limite: 3439 e 3442 dão desvio 0,468 contra
0,408 do pior genuíno — 15% acima, marginal mas não absurdo.

### Ainda em aberto

- **Anomalia do início do NAL:** 20% dos prefixos e 17% dos slice headers
  corrompidos, contra interior quase limpo. Fator ~400x que nenhum modelo
  uniforme explica. Testei a hipótese de "janela de 16 bytes" em 14 keyframes:
  **0 de 14**. A anomalia é real mas não é uma regra simples de reparo.
- **Segundo critério, independente da sintaxe.** É o que falta para reparar os
  36 IDRs com solução ambígua. O caminho natural é coerência visual: uma
  inversão errada em dados de resíduo produz macroblocos destoantes mesmo
  decodificando limpo. Comparar o keyframe candidato com vizinhos temporais
  discriminaria o que a sintaxe não discrimina.
- **Redundância de cabeçalho como critério CERTO** (2026-09-14, promissor). Todo
  critério tentado até aqui é proxy — "isto parece imagem?" — e a busca aprende a
  burlar. Os campos do slice header não são proxy: eles são **previsíveis pelos
  vizinhos**, então erro neles se prova por aritmética, não por aparência.

  Medido nos frames 2354–2369 (parser de exp-golomb em Python, SPS diz
  `log2_max_frame_num=8`, `poc_type=0`, `log2_max_poc_lsb=8`):

  | frame | tipo | `frame_num` | `poc_lsb` | |
  |---|---|---|---|---|
  | 2358 | P ref | 7 | 56 | |
  | 2359 | B | 8 | 50 | ok |
  | 2360 | B | 8 | 52 | **cabeçalho perfeito**, não decodifica |
  | 2361 | B | **136** | 54 | `frame_num` errado por **1 bit** |
  | 2362 | IDR | 0 | 0 | |
  | 2364 | ? | 232 | 137 | `slice_type` 63, cabeçalho destruído |

  `8 = 0b00001000`, `136 = 0b10001000`: bit alto do campo. Localizado em
  **offset 77528961 bit 0** e conferido por releitura (136 → 8, `poc_lsb` intacto
  em 54). **Não entrou no `patches.txt`**: corrigi-lo não faz o 2361 decodificar,
  então não passa no critério da seção 3. É reparo certo e insuficiente — o 2361
  tem dano também nos dados da slice.

  Os dois resultados que importam para a estratégia:
  - o **2360 tem cabeçalho impecável**, então o dano dele está nos dados da
    slice, não no header — buscar bit no início do NAL não vai achar nada ali;
  - dá para **varrer os 3445 frames** conferindo `frame_num` e `poc_lsb` contra
    a progressão dos vizinhos e listar todo cabeçalho inconsistente. É barato
    (parser em Python, sem decodificar) e devolve reparos com prova aritmética.
    Ainda não feito.

- **Densidade real:** todas as estimativas que circulei (27.000 reparos, depois
  2.100) foram medidas com PPS errado ou métrica furada. Refazer do zero.
