# Recuperação de Caio___Lizandra_-_Making-_Caio-Balu.mp4 — estado

Arquivo: 114.275.615 bytes. Falha: bit-rot. `moov` corrompido, `mdat` recuperável.

## 1. Parâmetros resolvidos (não reinvestigar)

**Layout real do arquivo** — a aritmética fecha exata:
```
ftyp 0..24 | moov 24..61188 (size 61164) | uuid XMP 61188..110168 | mdat 110168..fim
```
O `moov.size` correto é **61164** (`0xEEFC`→`0xEEEC`, 1 bit). Se alguma anotação antiga
disser 110144, está errada: aquilo engolia o átomo `uuid` do XMP dentro do `moov`.

**SPS — 4 bits corrigidos:**
```
674d4029965200f0044fcb29010101400000fa40003a9821
```
byte 2 `0x41`→`0x40` · byte 4 `0x92`→`0x96` (log2_max_frame_num 7→8) ·
byte 19 `0x44`→`0x40` · byte 23 `0x29`→`0x21` (nal_hrd fantasma)

**PPS — 2 bits corrigidos:**
```
68eb7352
```
cabeçalho NAL `0xe8`→`0x68` · `redundant_pic_cnt_present_flag` 1→0.
**`weighted_pred_flag` fica em 1.** Eu já o "corrigi" para 0 uma vez e isso matou
todos os frames P (25% do filme) — a métrica agregada escondeu porque os B dominam 3:1.

**Estrutura:** 3445 frames, 1 sample = 1 NAL, 1 slice por frame, CABAC, 4:2:0,
Main 4.1, 1920×1080, 29,97 fps, 114,95 s. Áudio AAC-LC 48 kHz estéreo, 5390 frames.
`stsz`, `stco`, `stsc` e `ctts` de vídeo estão **100% íntegros**.
Ordem de exibição confirmada por duas fontes independentes (ctts × POC).

**Parâmetros globais:** mvhd.timescale 90000 · duration 10348816 · mdhd.timescale 30000

## 2. Arquivos que importam

> ### NÃO EXISTE OUTRA CÓPIA DESTE ARQUIVO
> Não há backup, não há mídia original de câmera, não há segunda via com quem
> produziu o vídeo. Este MP4 é tudo o que restou deste registro.
>
> Duas consequências práticas: **não sugerir "procurar outra cópia"** — já está
> descartado; e tratar o original com o cuidado que isso exige, nunca
> modificando e conferindo `sha256sum -c dados/CHECKSUMS.txt` na dúvida. Não há
> segunda chance se ele for corrompido.

O nome do vídeo tem espaços e `&`, então precisa vir sempre entre aspas na linha
de comando.

**Todo comando se roda a partir da raiz do projeto.** Os caminhos relativos dos
programas contam com isso.

```
raiz/                   o que todo comando cita, e o que nunca se move
  Caio & ... .mp4       original intocado — fonte de verdade, nunca modificar
  patches.txt           lista `offset bit` — a outra fonte de verdade; toda
                        mudança nele passa pelo usuário antes
  index.txt             índice `i offset size idr` das 3445 amostras
  reparador.exe         binário compilado (fora do versionamento)
  ESTADO.md AGENTS.md CLAUDE.md

  src/                  programa em C
    reparador.c         o reparador, com libavcodec -- decodificacao e politica
    juizes.c juizes.h   a camada de MEDIDAS: so depende dos argumentos, nao le
                        estado do decodificador. E o que da para testar sozinho
    testes_juizes.c     testa as medidas com quadros SINTETICOS, sem decodificar
                        nada:  gcc -O2 -Isrc -o testes.exe src/testes_juizes.c                                    src/juizes.c -lm && ./testes.exe
  ferramentas/          programas em Python
    ferramentas.py      gera índice, patches base, e remonta o MP4
    molde_idr.py molde_slice.py escapes.py cabecalhos.py
    encadeia.py         busca por etapas com o modo `avanco`
    remontar.py conferir_dump.py
    cabecalho_slice.py  acha bits trocados no cabeçalho do slice pelo que ele
                        deveria dizer (gera dados/cabecalho_slice.txt)
    mapa_dano.py        onde o arquivo está danificado, medido em conteúdo
                        conhecido (gera dados/mapa_dano.txt)
    ocultacao.py        quantos macroblocos o ffmpeg oculta em cada quadro
    regressao.sh        prova que uma refatoração não mudou nada, modo a modo
                        (referência em dados/regressao/)
    confere_doc.py      confere o padrão de documentação das funções
                        (docs/REFATORACAO.md, seção 4c)
  docs/                 os doze documentos que crescem
  dados/                registros derivados, versionados
    CHECKSUMS.txt       SHA-256 do original, para detectar novo bit-rot nele
    deterministicos.txt remontados.txt
    patches_cabecalho.txt  o lote de cabeçalhos que entrou no patches.txt
    mapa_dano.txt alvos.txt alvos_ocultos.txt cabecalho_slice.txt
                        medidas; cada um traz no topo como foi gerado
    regressao/          hash de referência da saída de cada modo (SHA-256
                        cortado em 16 dígitos), para o regressao.sh
    candidatos_f13.txt candidatos_f19.txt candidatos_idr3047.txt
    janela_f11.txt      candidatos e janelas de busca -- NAO sao patches,
                        cada um traz sua condicao de promocao escrita
  saidas/               produtos: novos_*.txt, vídeo remontado, ver_final.html
  logs/                 saída de corrida — fora do versionamento
```

**Por que as duas fontes de verdade ficam na raiz:** elas são citadas em toda
linha de comando e em todo documento, e o MP4 tem 114 MB. Movê-las não arruma
nada e multiplica a chance de um comando errado tocar no original.

O projeto é um repositório git **privado, com um remoto só**, também privado,
criado pelo usuário em 2026-09-18: `git@github.com:ChrisCamilo/fix-marriage.git`.
Isto é material pessoal de família: nunca criar outro remoto, nunca torná-lo
público, não publicar em lugar nenhum. **O push é sempre do usuário** — o
agente faz os commits e nunca roda `git push`. O `.mp4` e o binário compilado
ficam fora do versionamento (ver `.gitignore`); a integridade do original é
conferida com `sha256sum -c dados/CHECKSUMS.txt`.

**Composição do `patches.txt` — 2.613 linhas:**

| faixa | quantas | o que é |
|---|---|---|
| 1–1338 | 1.338 | base determinística (prefixos de NAL e cabeçalhos). Reconferido: regerar com `base` reproduz os mesmos 1338 byte a byte |
| 1339–1830 | 492 | destas, **479** também estão em `dados/deterministicos.txt`. Saíram em 2026-09-18 a `77528965 2` (reparo antigo do 2361) e a `114260576 3` (reparo antigo do 3442, que era o próprio defeito) |
| | **12** | os reparos reais, que é o que o `verify` testa com `BASE_N=1338` — hoje todos "insuficientes" pelo critério de ocultação |
| | 1 | a linha 1378, `77528961 0` — o `frame_num` do reparo antigo do 2361, que ficou. Reclassificada como cabeçalho: está em `dados/patches_cabecalho.txt` (por isso ele tem 783 linhas, não 782) e o `verify` a pula |
| 1831–2612 | 782 | **cabeçalhos de slice** consertados por coerência (2026-09-18), 570 quadros — linha a linha em `dados/patches_cabecalho.txt`, que o `verify` também pula. Provam o cabeçalho; não trazem imagem nova (o dano segue no corpo). As imagens dos 167 quadros bons foram conferidas byte a byte antes de entrar |
| 2613 | 1 | `77496469 0` — o `00 00 02` proibido do 2360 restaurado para `03` (armadilha 27, revista); também em `dados/deterministicos.txt` |

**18 pares de linhas duplicadas** se cancelam de propósito (XOR duas vezes é
identidade), desfazendo reparos que foram aceitos por engano. Conferido par a
par: remover qualquer um deles piora o filme. Não "limpar" duplicata sem medir.

O `patches.txt` **não** é append-only, mas **toda mudança nele passa pelo
usuário antes**.

## 3. Comandos

Ambiente: **Windows com MSYS2 UCRT64** (não é Linux, não há `apt-get`). Toda
sessão começa exportando o PATH, senão o `gcc`, o `pkg-config` e as DLLs do
ffmpeg não são encontrados:

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
MP4="Caio & Lizandra - Making- Caio-Balu.mp4"
```

Toolchain já instalado e validado: **gcc 16.1.0**, **ffmpeg 8.1.1 /
libavcodec 62**. Se precisar reinstalar, no shell UCRT64:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-pkgconf
```

O Python é o do Windows (3.14, em `C:\Python314`), invocado como `python` — não
existe `python3` no UCRT64 a menos que instalado à parte.

```bash
gcc -O2 -o reparador.exe src/reparador.c src/juizes.c $(pkg-config --cflags --libs libavcodec libavutil)

python ferramentas/ferramentas.py base  "$MP4" patches.txt   # só na 1a vez; hoje aborta (ver abaixo)
python ferramentas/ferramentas.py index "$MP4" index.txt patches.txt

./reparador.exe "$MP4" index.txt patches.txt repair 0 500 4096
BASE_N=1338 ./reparador.exe "$MP4" index.txt patches.txt verify
python ferramentas/ferramentas.py build "$MP4" patches.txt saidas/reparado.mp4
```

`patches.txt` é carregado inteiro na inicialização: o processo é retomável,
idempotente (pula frames já bons) e incremental por faixa.

**O modo `base` recusa sobrescrever um `patches.txt` existente.** Ele abria a
saída com `"w"` e truncava, então rodar a linha acima apagaria os reparos reais.
Para regerar do zero, mova o arquivo atual para outro nome antes.

**Por que UCRT64 e não MINGW64:** o `src/reparador.c` localiza o byte corrompido
lendo as mensagens de erro do decoder, e o ffmpeg emite o resto do bytestream
com `%td`. A UCRT interpreta `%td`; a msvcrt antiga do MINGW64 não, e a
heurística falharia **em silêncio**, caindo na busca binária — muito mais lenta,
sem nenhum aviso. Conferido no `avcodec-62.dll`: as duas strings que o reparador
consome (`error while decoding MB %d %d, bytestream %td` e `concealing %d DC`)
continuam presentes no ffmpeg 8.1.1.

## 4. Onde está o resto

Este arquivo guarda só o que se lê no começo de toda sessão: parâmetros
resolvidos, arquivos e comandos. O que cresce fica separado:

| arquivo | o que tem |
|---|---|
| [`RESULTADOS.md`](docs/RESULTADOS.md) | números medidos: quantos frames, quantos segundos, o que os reparos renderam |
| [`CRITERIOS.md`](docs/CRITERIOS.md) | **como julgar um candidato** — a imagem manda, a tarja é subordinada |
| [`ARMADILHAS.md`](docs/ARMADILHAS.md) | **60 maneiras de medir errado** que já produziram conclusão falsa aqui |
| [`CABAC.md`](docs/CABAC.md) | **por que não existe ressincronização dentro do slice**, e o que dá para explorar |
| [`INVESTIGACOES.md`](docs/INVESTIGACOES.md) | hipóteses testadas, o que foi resolvido e o que segue aberto |
| [`IDRS.md`](docs/IDRS.md) | **tudo sobre os quadros-chave** — censo, molde do cabecalho, o que ja foi tentado |
| [`RASTREIO.md`](docs/RASTREIO.md) | **toda varredura já feita**, por alvo — consultar antes de disparar qualquer corrida |
| [`ALVOS.md`](docs/ALVOS.md) | mapa dos alvos: em que macrobloco cada quadro para, medido pelo modo `mapa` |
| [`PREFIXOS.md`](docs/PREFIXOS.md) | registro de um erro: os 702 prefixos AVCC "corrompidos" já estavam consertados no `patches.txt` |
| [`MELHORIAS.md`](docs/MELHORIAS.md) | o que foi feito, o que falta e o que foi descartado no `reparador.c` |
| [`PARALELIZACAO.md`](docs/PARALELIZACAO.md) | por que `thread_count` fica em 1 e como a varredura paralela preserva determinismo |
| [`REFATORACAO.md`](docs/REFATORACAO.md) | **plano de refatoração do `reparador.c`** — o que separar, o que unificar e por que quicksort não se aplica |

**Se for medir qualquer coisa, leia o `ARMADILHAS.md` primeiro.** É o arquivo
que mais economiza tempo: quase toda métrica óbvia deste problema já foi tentada
e já enganou alguém.

## 5. Cadeia aberta no IDR 3047

Alvo mais extremo fora das três ilhas: decodifica 6,7% do payload, o resto é
borrão. Não tem tarja, então quem julga é a estrutura do borrão — é o único
tipo de alvo com juiz utilizável fora das ilhas.

| etapa | bit | fronteira do borrão | respingo | desvio U / V da faixa liberada |
|---|---|---|---|---|
| base | — | 264 | 2 | 2,79 / 2,21 (borrão puro) |
| 1 | `103419946 b5` | 296 | 7 | 4,19 / 9,56 — **fora** |
| 2 | `103420786 b6` | **316** | **2** | **7,54 / 6,59 — dentro** |

Alvo dos quadros intactos: U 5,99–8,52, V 3,40–7,42. A etapa 2 é a primeira
vez na sessão que a faixa liberada entra nessa faixa nos dois planos — 30 dos
734 sobreviventes conseguem.

**Nada disso está no `patches.txt` e não deve entrar.** Ponto de partida de
busca não é conserto: o quadro segue borrado da linha 316 para baixo. A cadeia
está em [`dados/candidatos_idr3047.txt`](dados/candidatos_idr3047.txt) com a
condição de promoção escrita.

Como retomar:

```bash
cat patches.txt dados/candidatos_idr3047.txt | grep -E '^[0-9]+ [0-9]+$' > $SB/cad.txt
VISUAL=1 PISO_TRINCA=1 PISO_CROMA=1 BASE=-1 ./reparador.exe "$MP4" index.txt $SB/cad.txt avanco 3047 1 4450 20000 $SB/e3.txt
VISUAL=1 ./reparador.exe "$MP4" index.txt $SB/cad.txt trinca 3047 <lista>   # mede os sobreviventes
```

## 6. Mapa de dano (2026-09-18)

O bit-rot vem em **zonas** de 2–5% dos bits trocados; fora delas o arquivo está
praticamente limpo. Medido em conteúdo conhecido, no MP4 original
(`python ferramentas/mapa_dano.py`, detalhe em
[`dados/mapa_dano.txt`](dados/mapa_dano.txt)):

| régua | limpo | danificado |
|---|---|---|
| enchimento `00 00 03` | 0,11–0,13 MB · 48–50 MB · 104 MB · 113–114 MB | **0,147–0,150 MB (frames 10–11: 4–4,5%)** · **59,8–60,4 MB (~2%)** |
| 5 primeiros bytes de cada quadro | 2.669 quadros sem troca | 776 com 1–5; distribuição **não binomial** (39 quadros com 4 trocas; uniforme daria 2) |

Consequência: quadro com payload em zona de ~4% tem centenas de bits trocados —
o frame 11 tem ~600 — e nenhuma busca de 1–3 bits o conserta. **Consultar o mapa
antes de escolher alvo de varredura.** Aberto: tamanho e alinhamento dos blocos
de dano.

**Os 1.439 quadros não inteiros, por onde está o dano** ([`dados/alvos.txt`](dados/alvos.txt)):
435 com **cabeçalho do slice inválido** pela norma (o caso do frame 11; 151 P,
279 B), 407 que param antes de 512 bytes, 57 entre 512 B e 2 KB, 517 depois de
2 KB, 23 sem imagem sem causa visível. "Inteiro" = MB final **e** imagem emitida
**e** cabeçalho válido: 2.006 quadros, não os 2.376 do `mapa` (armadilha 55).
O dano se concentra no começo dos quadros: 492 param antes de 512 bytes, contra
~27 esperados se o dano fosse uniforme no payload.
