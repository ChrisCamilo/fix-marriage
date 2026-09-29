# Convenções deste repositório

Recuperação de um vídeo de casamento de 2017 danificado por bit-rot. Leia o
`README.md` antes de qualquer coisa: ele tem os parâmetros já resolvidos, os
comandos e o mapa dos outros documentos. E leia o `docs/ARMADILHAS.md` antes de medir
qualquer coisa — são **61** maneiras de medir errado que já custaram horas aqui.

## 1. Privacidade — inegociável

Este repositório é **privado**. Material pessoal de família.

- Existe **um** remoto, `origin` = `git@github.com:ChrisCamilo/fix-marriage.git`,
  **privado**, criado pelo próprio usuário em 2026-09-18 (nasceu privado, com
  `gh repo create --private`). **Nunca** adicionar outro remoto, nunca mudar a
  visibilidade dele. **O push é sempre do usuário**: o agente faz os commits e
  nunca roda `git push` — nem quando o commit parece pronto para subir.
- **Nunca** publicar nada disto em outro lugar: nem artifact, nem gist, nem
  pastebin, nem serviço de diagrama ou de conversão online.
- Criar público e trocar depois não resolve: o conteúdo fica em cache e
  indexado. Qualquer repositório novo tem que nascer privado.
- **Imagem e vídeo nunca entram no git**, nem no remoto privado: o MP4, o
  reparado e os quadros extraídos (`output/*.png`, `*.pgm`, `*.yuv`) estão no
  `.gitignore`. O que vai para o remoto é só texto — código, patches, índices e
  documentação.

## 2. As duas fontes de verdade

| arquivo | regra |
|---|---|
| `Caio & Lizandra - Making- Caio-Balu.mp4` | **nunca modificar.** Fica fora do git; integridade em `CHECKSUMS.txt` |
| `patches.txt` | cada linha é `offset bit`. **Toda mudança passa pelo usuário antes** — incluindo remoção |

Tudo o mais é derivado e reconstruível: `index.txt`, `reparado.mp4` e o binário
saem desses dois. Se precisar mexer em algo, mexa nos patches, nunca no MP4.

Antes de qualquer operação de git que possa descartar trabalho, confira o
`git status` e o número de linhas do `patches.txt`.

**Linha duplicada se cancela** — XOR duas vezes é identidade. Isso foi usado de
propósito para desfazer reparos aceitos por engano, e hoje há **18 pares assim**
no arquivo. Nenhum é acidente: conferidos um a um, remover qualquer um deles
piora o filme. Não "limpar" duplicata sem medir os dois estados.

## 3. Só entra patch que passa no critério rigoroso

O critério é o do `reparador.c`: **flush do decoder e exigir quadros == pacotes,
com zero linhas de log**. Nada mais conta.

Desde 2026-09-18 o critério tem mais duas partes: a **cadeia de referência boa**
(quadro sem erro sobre referência borrada não conta, armadilha 56) e **zero
macroblocos ocultados** no alvo (slice que acaba cedo sem erro não conta,
armadilha 59). Com as duas, o `serie 0 3444` (`TARJA=1`) dá **165** (medido
em 2026-09-28, depois do frame 11), subconjunto exato dos 168 visualmente bons
(os 167 de antes mais o 11): saem 2359, 2360 e 2361 (slice encerrado cedo).
Antes do frame 11 eram 163 — o 11 e o 12 entraram juntos. `ACEITA_OCULTO=1` desliga a parte da ocultação.

**A outra via é a do cabeçalho provado.** Patch que prova o cabeçalho não faz o
quadro passar — o dano segue no corpo — e por isso não passa pelo critério
acima. Entra por invariante: os determinísticos (`data/deterministicos.txt`) e,
desde 2026-09-18, os cabeçalhos de slice consertados por coerência
(`data/patches_cabecalho.txt`, gerados pelo `tools/cabecalho_slice.py`).
Condição de entrada deste segundo lote, aprovada pelo usuário: solução **única**
de 1 ou 2 bits, e as imagens dos 167 quadros bons **byte a byte iguais** com o
lote aplicado. **E o parse de nenhum quadro pode piorar sem que a imagem de antes
fosse borrão** — essa faltou na primeira vez e deixou passar 6 correções erradas
(armadilha 57). Medir parse, não emissão: a emissão muda com a reordenação. O
`verify` pula os dois arquivos.

Desde 2026-09-24 há um terceiro lote do mesmo tipo: as **caudas de IDR**
provadas pela tarja recodificada com CABAC (`data/patches_cauda.txt`, gerados
pelo `tools/anchor/cauda_lote.py`, plano em `docs/PLANO_ANCORA_CAUDA.md`).
Condição de entrada aprovada pelo usuário: distância de no máximo 3 bits,
**todas** as hipóteses empatadas apontando os mesmos bits, a cauda corrigida
fechando com distância 0 e sem violação de escape; o `mapa` do filme inteiro
idêntico antes e depois. O `verify` pula este também. **Em 2026-09-25 saíram
32 das 65 linhas** (16 IDRs cujo encaixe começava nas fileiras 66–67),
aprovado pelo usuário: a tarja pode ter uma coluna com outro modo de predição
(armadilha 61), e sob ela a regra propõe correção falsa em 22% (fileira 67) e
4,7% (66) — medido em caudas sintéticas; 1% na 65 e 0% na 63. Ficam 33 linhas,
13 IDRs, todas com encaixe desde as fileiras 63–65. **Encaixe de cauda de IDR
só vale com 3 fileiras ou mais.**

No mesmo dia entrou o quarto: as **caudas de quadros P/B** (`data/patches_cauda_pb.txt`,
gerados pelo `tools/anchor/cauda_pb.py`), onde a tarja de baixo é só
skip. Condição aprovada pelo usuário: distância de no máximo **2** (a cauda de
P/B tem só ~30 bits testáveis; caudas aleatórias ficam a 4–13), as mesmas
regras de hipóteses e de fechamento, e fora os quadros com enchimento no fim.
O `mapa` muda só no 2189 (erro 2 MBs adiante, explicado no plano). O `verify`
pula este também.

Em 2026-09-28 entrou o quinto, de outra natureza: **o frame 11 inteiro**
(`data/patches_f11.txt`, 873 linhas, gerado pelo `python tools/anchor/f11.py
nal`, plano em `docs/PLANO_RECODIFICA_F11.md`). O conteúdo do quadro é todo
conhecido (fade: tarja 16, campo 43), então a sintaxe foi reconstruída pela
física — resíduo = alvo − predição da referência, quantizado como um encoder
faz — e recodificada com CABAC; o que difere do arquivo é o dano. Prova
aprovada pelo usuário: 3,87% de bits fora, uniforme e com estatística de trocas
independentes; o NAL corrigido fecha no tamanho exato; JM e ffmpeg sem erro;
imagem 16/43 exata; o `mapa` do filme muda só no 11. Uma a uma as linhas não
fecham nada — só as 873 juntas —, por isso o `verify` pula este também.

No mesmo dia entrou o sexto: a **tarja de cima** (`data/patches_cabeca.txt`,
943 linhas em 232 quadros, gerado pelo `tools/anchor/cabeca.py`, censo em
`docs/CENSO_CABECA.md`). No começo do slice o estado do CABAC é exato, e a
sintaxe da tarja de cima foi medida (I16x16 DC nos IDRs, skip nos P/B); o que
difere da recodificação é dano. **A comparação é no NAL, byte a byte**: a
cabeça de P/B é quase só zeros e um terço dos bytes é escape — comparar no
RBSP deu um falso "11" em ~310 quadros B (escape trocado desloca o RBSP).
Condição aprovada pelo usuário: nível A (P/B com distância 1–3, 16+ bits
iguais depois da última troca, trocas a 8+ bits uma da outra) e nível I (IDRs
com dano uniforme, no máximo 12 diferenças por janela de 64 bits); só quadro
que parava na tarja e que o `mapa` faz avançar. O nível B (362 quadros, folga
curta) ficou de fora: no controle, as correções falsas eram todas assim. O
`mapa` muda só nesses 232, todos para a frente; o `serie` fica em 165.

Em 2026-09-29 entrou o sétimo: **os B 10 e 12 recodificados pelos gêmeos**
(`data/patches_gemeos.txt`, 406 linhas, `tools/anchor/gemeos.py`). No fade do
GOP 0 os B não-referência repetem a sintaxe do irmão: o 10 é todo pulado como
o 6; o 12 é a sintaxe do 8 (skip na tarja, B_Direct_16x16 com DC de croma).
Mesmo QP, mesmo modelo e mesma sintaxe dão os mesmos bits, então o que difere
é dano; o 6 recodificado bate em 0 bits. Saíram junto as linhas 1.826
(`147444 0`, que estava em `data/deterministicos.txt`) e 1.828 (`150538 0`):
elas trocavam o escape `03` por `02`, e o 10 e o 12 só pareciam bons porque o
ffmpeg cortava o NAL e os lia como todo pulados (armadilha 62). `mapa`,
`panorama` e `serie` idênticos; o JM decodifica o GOP 0 do 0 ao 12 sem erro.

**Depois de todo lote, rodar `python tools/proibidas.py`**: nenhuma sequência
`00 00 0x` (x < 3) pode ter sido criada por patch, e quadro "inteiro" no `mapa`
com uma delas antes do fim dos dados é o NAL cortado, não conserto. Foi assim
que os "3 inteiros" do lote da cabeça (263, 1638, 2459) e o `00 00 01` que ele
criou no 2971 apareceram; os 7 escapes (com 59, 407 e 1610) entraram no mesmo
dia, aprovados pelo usuário, também em `data/deterministicos.txt`.

A `docs/ARMADILHAS.md` lista **62** maneiras de medir errado que já produziram
conclusões falsas neste projeto. As três que mais enganam:

- **Contagem de frames do ffmpeg não mede nada** — ele emite quadros de
  ocultação cinza e infla o número. Foi assim que "2808 de 3445" virou verdade
  por um tempo. Hoje o filme emite ~3.100 quadros; o critério rigoroso aprova
  209, mas **só 167 são imagem boa** — os outros 42 decodificam sem erro sobre
  referência borrada e herdam as listras. Armadilhas 1, 48 e 56.
- **Toda medida sobre bytes sai do buffer remendado, nunca do MP4 cru.** Script
  em Python que abre o arquivo direto enxerga dano que já foi consertado há
  centenas de commits. Armadilha 49.
- **Métrica agregada esconde falha em subgrupo.** Medir sempre separado por
  tipo (I, P, B). Um "conserto" no `weighted_pred_flag` já matou 100% dos
  frames P sem que a média acusasse.

Não mexer no `weighted_pred_flag`: ele fica em 1.

Depois de gerar patches novos, revalidar com `BASE_N=1338 ... verify`. Espera-se
hoje **`0 válidos, 11 falsos, 3999 determinísticos pulados`** (os 486 de
`data/deterministicos.txt`, os 783 de `data/patches_cabecalho.txt`, os 33 de
`data/patches_cauda.txt`, os 475 de `data/patches_cauda_pb.txt`, os 873 de
`data/patches_f11.txt`, os 943 de `data/patches_cabeca.txt` e os 406 de
`data/patches_gemeos.txt`), e os 11 falsos são
todos explicados — nenhum é patch ruim:

| falsos | frames | leitura |
|---|---|---|
| 6 | 2360, 2362, 2363, 2364, 2365, 2366 | `com=1 sem=1` — insuficientes, não errados: o quadro segue com dano adiante. 2360 e 2363 contavam como "válidos" até 2026-09-18 só porque o critério antigo aceitava slice encerrado cedo (armadilha 59) |
| 1 | 12 | `com=1 sem=1` — o `150530 4`, cabeçalho do 12 (QP 18, o do gêmeo 8). O `verify` usa o critério rigoroso, que recusa todo o fade do GOP 0 (sem `TARJA=1` o `serie 0 13` dá 0 de 14, até o 0–10). O outro falso do 12, o `150538 0`, saiu em 2026-09-29 (armadilha 62) |
| 4 | 3435, 3439 | `com=1 sem=1` — pares **cancelados de propósito**; o `verify` testa cada ocorrência e não entende cancelamento |

**Nenhum reparo de 1 linha hoje faz um quadro fechar sozinho pelo critério
honesto** — o último que fazia, o do 3442, era ele mesmo o defeito e saiu. O
único quadro que um reparo fez fechar é o 11, e com as 873 linhas juntas (e o 10 e o 12,
recodificados pelos gêmeos, com as linhas de cada um juntas). **Qualquer falso
além desses 11 é problema.** Rodar `verify` **sem** `BASE_N`
acusa ~1328 falsos por construção, o que é esperado e não é bug.

## 4. Manter a documentação viva

Ele é a memória do projeto entre sessões. Depois de qualquer corrida que mude
resultado, **atualizar o `docs/RESULTADOS.md`** com números medidos, não estimados:

- frames perfeitos / 3445, e a que tempo isso corresponde
- trechos contínuos e quanto dá para assistir de fato
- quantos IDRs estão limpos (isto governa o que é reparável)
- resultado do `verify`

Distinguir sempre **"consertado por reparo"** de **"já estava intacto"**. Somar
os dois num número só foi uma confusão real que aconteceu aqui.

Quando um parâmetro for resolvido, escrever no `README.md` que está resolvido,
para ninguém reinvestigar. Quando uma hipótese for descartada, registrar que foi
testada e o resultado — o `docs/INVESTIGACOES.md` existe para isso.

### A documentação da função muda junto com a função

**Toda mudança numa função atualiza, no mesmo commit, o bloco de documentação
acima dela** (padrão do `docs/REFATORACAO.md`, seção 4c): o que faz, os
parâmetros, as variáveis de ambiente que mudam o comportamento, a saída e o
valor de retorno com o caso de falha. Parâmetro, variável de ambiente, escala
de nota ou formato de saída novo que não esteja no bloco é mudança incompleta.

Comentário no meio da função continua bem-vindo — é onde mora a justificativa
medida de cada linha —, mas **não substitui o bloco**: quem chama a função lê
o bloco, não o corpo. Se só der tempo de um, é o bloco.

O limite de 8 linhas da seção 4c vale para funções auxiliares; os `modo_*`
podem passar dele, porque o bloco é o manual do modo. E o bloco fica **acima da
função que descreve** — na separação dos modos em funções vários comentários
ficaram no fim da função anterior, onde ninguém os procura.

## 5. Ordem de ataque

Todo reparo ancora no IDR anterior (`ancora_de`). Um GOP cujo IDR está quebrado
é **irreparável** enquanto o IDR não for consertado: os ~27 frames dele estão
bloqueados por dependência, não por dano próprio.

Portanto: **consertar IDR quebrado primeiro.** Rende ~27 frames destravados por
conserto, contra 1 de um frame comum. O modo `idr` do `reparador.c` faz esse
diagnóstico sem gravar nada.

Duas janelas importam, e a segunda foi ignorada por muito tempo:

1. em volta do ponto de corte que o decoder informa;
2. **os primeiros ~256 bytes do NAL** — quando o corte cai antes do byte ~64,
   a janela do corte colapsa e esta é a única busca que existe de verdade.

## 6. Ambiente

Windows com **MSYS2 UCRT64** — não é Linux, não há `apt-get`. Toda sessão:

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
MP4="Caio & Lizandra - Making- Caio-Balu.mp4"
```

UCRT64 e não MINGW64: o reparador lê `%td` das mensagens do decoder, que a
msvcrt antiga não interpreta — a heurística falharia em silêncio. Detalhes na
seção 3 do `README.md`.

Python é o do Windows (`C:\Python314`), chamado como `python`. Dentro de string
passada com `python -c`, usar caminho `C:/...`; o Git Bash só converte `/c/...`
quando é argumento.

## 7. Paralelizar tudo que der, sem abrir mão do determinismo

Trabalho que testa candidatos independentes **deve** ser paralelizado. As
corridas duram dezenas de minutos; deixar isso numa thread só é desperdício.

**`THREADS=12` é o certo, e é o padrão.** A máquina tem 28 núcleos e o teto do
código é `NUMBER_OF_PROCESSORS - 2` = 26, mas **não se satura**: cada worker
carrega um `AVCodecContext` de 1920×1080 com buffers de referência, e 26 deles
viram pressão de cache que come o ganho; o ganho já é sublinear bem antes disso;
e a máquina precisa continuar usável durante corrida de meia hora. A
justificativa completa está na seção 5 do `docs/PARALELIZACAO.md`, junto com o
histórico — o default foi 6, subiu para 12 por decisão explícita, e 12 fica.

Não "otimizar" isto para 26 achando que metade da máquina está parada: ela está
parada de propósito.

A maquinaria de paralelização já existe no `reparador.c` (`varre_par`) — use-a em
vez de escrever laço sequencial novo. Já aconteceu de eu paralelizar um modo e,
logo depois, escrever outro com laços sequenciais próprios ao lado.

**Onde não paralelizar:** o `thread_count` do libavcodec fica em 1. Detalhes e
justificativa na seção 1 do `docs/PARALELIZACAO.md`.

O determinismo vem do desenho, não de sorte de escalonamento:

- candidatos numerados na ordem da varredura sequencial;
- distribuídos por contador atômico, não por blocos fixos — o custo varia muito
  com a posição do bit;
- saída ordenada pelo índice do candidato no final;
- com parada antecipada, vale a **regra do menor índice**: ao achar solução em
  `k`, parar de puxar índices `>= k` mas terminar os já em voo com índice menor.
  Nunca "a primeira que chegar" — há frames com milhares de soluções válidas, e
  a escolha por ordem de chegada pegaria bit errado.

### Validação obrigatória

**Toda paralelização se prova comparando `THREADS=1` com o default.** Não é
opcional e não se pula por parecer óbvio:

```bash
THREADS=1 ./reparador.exe "$MP4" index.txt patches.txt <modo> > seq.txt
              ./reparador.exe "$MP4" index.txt patches.txt <modo> > par.txt
# iguais ignorando só o campo de tempo:
diff <(sed -E 's/ *\[[0-9.]+s\]//' seq.txt) <(sed -E 's/ *\[[0-9.]+s\]//' par.txt)
```

`THREADS=1` cai no caminho sequencial original, então a comparação é honesta:
mesmo binário, mesma máquina, mesmos dados. Se divergir, **o desenho está errado
— corrija o código, nunca ajuste a referência.**

Referência já validada: modo `unico` sobre os IDRs deu saída byte a byte
idêntica (mesmo SHA-256) em 1, 6 e 10 threads, com ganho de 4,8x e 6,5x. (O
filme tem **132** IDRs no `index.txt`; anotações antigas falam em 71 ou 128 —
ver `docs/IDRS.md`.)

## 8. Commits — um por ideia

**Cada commit carrega uma ideia só.** Se a mensagem precisa de "e" para dizer o
que foi feito, provavelmente são dois commits.

Não misturar num mesmo commit: correção de bug, funcionalidade nova, atualização
de documento, ajuste de formatação. Mesmo que tenham nascido do mesmo trabalho,
são ideias distintas e se revisam separado. Em particular, **atualizar o
`README.md` com números medidos é commit próprio**, separado do código que
produziu os números — o código é uma ideia, o resultado é outra.

**Commitar assim que a ideia fecha**, não acumular para o fim da sessão. Se o
próximo passo é disparar uma corrida de 30 minutos, o código vai commitado
antes: aí o que se perde num acidente é o resultado, que se refaz, e não o
código.

Mensagem em português explicando o **porquê**, não o quê — o diff já diz o quê.
E a descoberta que motivou a mudança entra na mensagem: foi assim que ficou
registrado que o IDR 1802 tem duas soluções de 1 bit válidas, informação que se
perderia se o commit dissesse apenas "adiciona modo unico".

## 9. Higiene

- Finais de linha **LF** nos dados. O `.gitattributes` segura isso contra o
  `core.autocrlf`; o `reparador.c` grava com `fopen(...,"ab")` e o
  `ferramentas.py` com `newline="\n"`. Não reverter.
- Corridas longas: rodar em background e redirecionar a saída, que é volumosa.
  Não filtrar a saída com `grep` no redirecionamento — os dados dos outros casos
  se perdem e a corrida tem que ser refeita.
