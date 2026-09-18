# Os IDRs deste arquivo

Tudo o que se sabe sobre os quadros-chave: quantos são, onde estão, como é o
cabeçalho deles e o que já foi tentado. Um IDR é o único quadro que decodifica
sozinho, sem referência — por isso ele governa o que é reparável, e um GOP com
IDR quebrado está bloqueado por dependência, não por dano próprio.

## 1. Quantos são — e o `stss` erra dos dois lados

**O filme tem 131 ou 132 IDRs, não os 128 que o `stss` marca.** As duas
contagens divergem em um único quadro, o 1595, explicado na seção 3.

Descoberto varrendo os 3445 frames contra o molde do cabeçalho (seção 4). O
teste separa sem ambiguidade:

| | distância ao molde |
|---|---|
| IDR verdadeiro | **0 a 3 bits** |
| quadro comum | **8 a 14 bits** |

| | quadros |
|---|---|
| marcados no `stss` | 128 |
| **marcados que NÃO são IDR** | 1452 |
| **IDRs sem marcação** | 1437, 1466, 2441, 2554, 2913 |
| em disputa | 1595 |

O `stss` mora no `moov`, que é justamente o átomo corrompido por bit-rot. O
`ESTADO.md` só garante `stsz`, `stco`, `stsc` e `ctts` — nunca houve motivo
para confiar nele.

Os cinco recuperados casam por **quatro sinais independentes**: distância 1–3
ao molde, tamanho de quadro intra (67–229 KB contra 8–70 KB dos comuns ao
redor), cadência de exatos 29 frames, e o `idr_pic_id` previsto pela sequência.

O 1452 falha pelos mesmos quatro: 14.766 B entre IDRs de 67–190 KB, fora da
cadência, distância **9**.

## 2. A causa: um desempate por ordem de lista

O `ferramentas.py base` encaixa o byte de cabeçalho do NAL no valor mais
próximo de `PREF = [0x01, 0x41, 0x65, 0x21, 0x61]` por distância de Hamming.
Quando dava empate, **desempatava pela posição na lista**.

O byte `0x45` fica a exatamente 1 bit de `0x41` (comum) e de `0x65` (IDR). A
ordem dava `0x41` — e foi assim que 1437, 1466, 2554 e 2913 viraram quadro
comum. Pior: isso também explica por que varrer o arquivo procurando NAL tipo 5
não os encontrava. **A evidência tinha sido apagada pela nossa própria
ferramenta antes de qualquer busca.**

No sentido inverso, `0x25` empata entre `0x65` e `0x21`, e o 1452 era promovido
a IDR sem ter cabeçalho de IDR.

**Corrigido:** o empate agora se resolve pelos 17 bits fixos que seguem o byte
NAL num IDR, que são evidência e estão no arquivo, e o critério é simétrico —
decide para os dois lados. Regerar o base muda exatamente 5 bits em 5 quadros.

## 3. O caso 1595

**RESOLVIDO em 2026-09-18: o 1595 NÃO é IDR.** A terceira fonte que a tabela
abaixo dizia não existir existe: os cabeçalhos dos quadros seguintes. Dos 9
cabeçalhos legíveis de 1596 a 1610, **9 continuam a sequência do IDR 1582**
(`frame_num` 5–8, POC 26–54) e **nenhum reinicia** a partir do 1595. Ele é o P
daquela posição (`frame_num` 4, POC 32), com o byte NAL corrompido de `0x41`
para `0x65` (2 bits). Consertá-lo não fecha: com o byte certo o cabeçalho segue
inválido e não há solução em até 2 bits a mais — fica como está, mas nenhuma
ferramenta deve mais contá-lo como IDR (o `cabecalho_slice.py` já não conta).

Tratá-lo como IDR custou caro uma vez: o lote de cabeçalhos de 2026-09-18
"consertou" `frame_num`/POC certos em 6 quadros contando a partir dele, e os 6
perderam a imagem. Revertido no mesmo dia (`dados/patches_cabecalho.txt`).

O registro anterior, mantido como estava:

Único em disputa, e fica **como o arquivo o tem**:

| a favor de ser IDR | contra |
|---|---|
| byte NAL cru é `0x65`, tipo 5, sem patch nenhum | 62.953 B, abaixo da faixa dos IDRs vizinhos |
| — | está 13 frames depois do 1582, que por sua vez está a 29 do 1611 |
| — | a cadeia `1582=60 → 1611=61` não deixa vaga de `idr_pic_id` para ele |

A norma permite repetir `idr_pic_id` quando os IDRs não são unidades de acesso
consecutivas, então a sequência não é prova absoluta. Diante de evidência que
se contradiz, **a decisão é não fabricar em nenhuma direção**: o byte cru fica
intocado e nenhum cabeçalho de IDR é forçado nele.

Houve dano nosso aqui, já desfeito: seis bits forçando o prefixo canônico de
IDR foram anexados e depois revertidos.

## 4. O molde do cabeçalho de slice

Medidos os 12 campos nos IDRs que decodificam, **dez são constantes**:

```
first_mb=0   slice_type=7   pps_id=0   frame_num=0   poc_lsb=0
no_output_prior=0   long_term_ref=0   deblk_idc=0   alpha_div2=-1   beta_div2=-1
```

Livres só `idr_pic_id` e `slice_qp_delta`. Logo o cabeçalho inteiro se gera de
dois números — e o gerador do `molde_idr.py` **reproduz bit a bit** os
cabeçalhos íntegros, inclusive os comprimentos, que variam de 60 a 66 bits
porque os dois livres são de tamanho variável.

Isso transforma conserto de cabeçalho em aritmética de bitstream. O decoder não
entra.

As flags que governam o formato vêm do PPS `68eb7352`, decodificado:
`entropy_coding_mode=1` (CABAC), `bottom_field_poc_present=0`,
`deblocking_control_present=1` (por isso os campos de deblocking existem no
cabeçalho), `redundant_pic_cnt_present=0`, `pic_init_qp=26`.

### `idr_pic_id` é o ordinal do IDR

Incrementa de 1 a cada IDR, **sem exceção**. Valia em 126 dos 131 na primeira
medição, e as cinco exceções eram quadros onde nós mesmos tínhamos escrito
valor degenerado — corrigidos, a regra fecha.

Levou tempo a enxergar porque a medição estava contaminada: incluir cabeçalhos
danificados no ajuste fazia a regra parecer uma função degrau não monotônica de
quatro parâmetros. **Ajustar regra com dado danificado faz a regra parecer mais
complicada do que é** — é a armadilha 15 numa forma nova.

### `slice_qp_delta` não é previsível

Varia por quadro, entre **−17 e −2** nos 97 cabeçalhos nunca danificados
(QP 9 a 24), concentrado em −14/−12. Dá para limitar a faixa, não para prever o
valor.

## 5. Estado dos cabeçalhos

**130 dos 131 estão fechados.** Sobra um:

| IDR | situação |
|---|---|
| 3290 | dois cabeçalhos empatados em 5 bits: `qp_delta` −6 (QP 20) ou −3 (QP 23) |

Não é problema de tempo. São dois valores igualmente distantes do que está no
arquivo, e o `qp_delta` é o único campo sem aritmética que o preveja. Deixar o
decoder desempatar não funciona: o 3290 morre no byte 10 e produz campo
uniforme com qualquer cabeçalho.

Fica como está. Cabeçalho ambíguo não custa nada, e se o corpo dele algum dia
for atacado, testar os dois candidatos é trivial — e aí haverá com que comparar.

## 6. O que os IDRs quebrados mostram na tela

Triagem feita com **linhas idênticas**, não com blocagem — blocagem não separa
listra de cena, e foi por isso que o IDR 1712 entrou uma vez na lista dos
promissores sendo 71% propagação (armadilha 18).

| estado do quadro | IDRs |
|---|---|
| não produz imagem nenhuma | 17 |
| 100% listra | 20 |
| 75–100% | 3 |
| 50–75% | 74 |
| 25–50% | 6 |
| **abaixo de 25%** | **1** — o IDR 1773 |

**Um único IDR quebrado tem imagem quase íntegra.** O 1773 decodifica 97,6% do
slice e tem 11 linhas de listra, concentradas em 933–949; o defeito é que a
tarja inferior não existe, porque o conteúdo da última fileira vaza para baixo.

Os 17 sem imagem morrem todos entre o byte **10 e o 12**, ou seja dentro do
cabeçalho de slice. Com `LOG=1` o decoder nomeia o campo: `slice_qp_delta` no
3290, `disable_deblocking_filter_idc` no 1452, `slice_alpha_c0_offset_div2` no
2786.

## 7. O que já foi tentado e não funciona

| tentativa | resultado |
|---|---|
| 1 bit no NAL inteiro do 1773 (274.496 candidatos) | 26.865 "soluções", **todas** com tarja entre 126 e 221 contra 16,00 |
| 1 bit no NAL inteiro do 1143 (405.040 candidatos) | 10 soluções, todas reprovadas por tarja e por imagem |
| 1, 2 e 3 bits na janela do cabeçalho dos 17 sem imagem | **zero**, com aceite de imagem correto |
| as 45 "soluções" antigas do `idr_full.txt` | **44 de 45 não fazem mais nada** — são de antes do juiz de imagem |

**Consertar o cabeçalho não recupera imagem.** Os cinco IDRs recuperados do
`stss` decodificam cinza liso, igual aos 17 — o dano continua pelo corpo do
slice. Coerente com o dano deste arquivo ser em rajada e não espalhado
(armadilha 20): onde a rajada pega, pega o NAL inteiro.

O ganho dos cabeçalhos é outro, e vale por si: **cabeçalho provado é verdade
append-only que nunca precisa ser refeita**, ao contrário de reconstrução de
pixels, que se descarta a cada mudança a montante. Toda varredura futura nesses
quadros parte de cabeçalho correto em vez de procurar bit num NAL que já tem
bits sabidamente errados.

## 8. Números de hoje — 2026-09-17

| | |
|---|---|
| IDRs no índice | **132** |
| que **analisam** os 8.160 macroblocos (`mapa`) | **59** |
| que não analisam | **73** |
| que decodificam limpo **e com imagem** (critério rigoroso) | **7** — 1683, 2333, 3319, 3348, 3368, 3397, 3426. **O 1683 é listrado da metade para baixo** (armadilha 56): imagem boa de verdade são 6 |
| cabeçalhos fechados | **130 de 131** |
| `patches.txt` | **2.613** linhas (1.832 + 781 de cabeçalho de slice, 2026-09-18) |
| `deterministicos.txt` | **479** |
| `verify` com `BASE_N=1338` | **3 válidos, 10 falsos, 1.262 pulados** (479 determinísticos + 783 de cabeçalho) — os 10 estão explicados no `AGENTS.md` |

**"Analisa 100%" não é "tem imagem".** Dos 132 IDRs, 59 percorrem os 8.160
macroblocos sem erro, mas só 7 produzem quadro que passa no critério rigoroso.
A diferença são 52 IDRs em dessincronização silenciosa — armadilha 38.

O IDR 1683 decodifica mas é **34,3% listra** — conta como quadro, não como
cena. Os utilizáveis de verdade são **6**.

> A tabela anterior desta seção dizia `patches.txt` 1467 linhas,
> `deterministicos.txt` 118 e `verify` 7/4/118. Eram os números de uma medida
> antiga apresentados como atuais.

## 9. Frentes conferidas que não abriram nada

Registradas porque fechar uma frente vale tanto quanto abrir, e sem isso alguém
refaz.

**O censo está completo.** A distância ao prefixo fixo do cabeçalho, medida nos
3445 frames, tem um vão limpo: **131 quadros em 0, nenhum em 1 nem em 2**, e o
grupo seguinte só aparece em 3. Os 21 desse degrau, testados contra o molde
completo, ficam entre **7 e 12** — território de quadro comum. Não há IDR
escondido.

**Não existem SPS/PPS embutidos no fluxo.** Nenhuma amostra de tipo 7, e a
única de tipo 8 é o frame 859 com **14.640 bytes** — um PPS de verdade tem ~4,
e o byte dele está a 3 bits de `0x41`. É corrupção, não parâmetro. Confirma que
o SPS e o PPS do `avcC` valem para o filme inteiro, e que nenhum IDR usa
parâmetros próprios.

**O byte NAL sozinho engana muito.** 157 amostras leem tipo 5 no arquivo cru,
mas só 131 são IDR: as outras 26 são bytes corrompidos que caem em 5 por acaso.
É a medida do ruído, e justifica o desempate por cabeçalho da seção 2.

## 10. As variáveis que não podem ser medidas

Quatro, e cada uma é imensurável por um motivo diferente. Distinguir os motivos
importa: dois se resolveriam com mais dados, e dois não.

**`slice_qp_delta` de um IDR danificado — não há invariante.** É o único campo
do cabeçalho que o encoder escolhe pelo controle de taxa, a partir do conteúdo
da cena, que é justamente o que se perdeu. Não há aritmética, vizinho, nem
norma que o fixe. É o caso do 3290 (seção 5), e nenhuma quantidade de tempo de
máquina resolve.

**O conteúdo de um macrobloco destruído — não há gabarito.** Sem vizinho
temporal e sem aritmética que fale de conteúdo, todo objetivo vira proxy, e
proxy contra centenas de milhares de candidatos sempre acha o patológico. Foi o
IDR 1683, três vezes seguidas.

**Se o 1595 é IDR — a evidência se contradiz.** Diferente das duas anteriores:
aqui não falta medida, as medidas discordam entre si (seção 3). Não existe
terceira fonte no arquivo, e a norma permite os dois casos.

**Se um IDR que decodifica limpo está byte a byte correto — não há
verificação.** Os 7 bons passam no critério, mas um bit errado que não dispare
erro no decoder é indetectável: a imagem pareceria certa e nada acusaria. A
tarja cobre parte disso, com 227 mil pixels de valor conhecido, e o molde cobre
o cabeçalho — mas o **corpo do slice** não tem conferência independente.

Na prática não muda nada: quadro que decodifica com tarja perfeita serve para
assistir e para ancorar. Mas **"decodifica limpo" é ausência de evidência de
erro, não prova de integridade**, e a diferença importa no dia em que um reparo
parecer bom e não for.

## 11. Ferramentas

| arquivo | papel |
|---|---|
| `molde_idr.py` | gera e confere o cabeçalho de IDR; acha e conserta dano por aritmética |
| `reparador.c` modo `cortes` | ponto de corte e percentual de listra de todo IDR quebrado |
| `reparador.c` modo `varrek` | varredura exaustiva de k bits numa faixa, com aceite por imagem (`IMAGEM=1`) |
| `reparador.c` modo `campo` | mede tarja e imagem de uma lista de candidatos |
