# fix-marriage — recuperação de Caio___Lizandra_-_Making-_Caio-Balu.mp4

Recuperação bit a bit de um vídeo de casamento de 2017 danificado por bit-rot.
O MP4 original nunca é modificado: a correção vive no `patches.txt` (uma linha
`offset bit` por bit trocado), e o vídeo reparado é remontado a partir dos dois.
Repositório privado, material de família.

- **Convenções de trabalho:** [AGENTS.md](AGENTS.md) — leia antes de começar.
- **Estado do reparo e contexto técnico:** este arquivo, a partir daqui.
- **Números medidos:** [docs/RESULTADOS.md](docs/RESULTADOS.md); planos e
  investigações em `docs/`.

Estrutura: `src/` (o reparador em C), `tools/` (Python e o arnês de
regressão), `data/` (registros derivados, versionados), `docs/`, `output/`
(produtos, fora do versionamento em boa parte) e `logs/` (fora do
versionamento). Detalhe na seção de arquivos abaixo.

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
> modificando e conferindo `sha256sum -c data/CHECKSUMS.txt` na dúvida. Não há
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
  README.md AGENTS.md CLAUDE.md

  src/                  programa em C
    reparador.c         o reparador, com libavcodec -- decodificacao e politica
    juizes.c juizes.h   a camada de MEDIDAS: so depende dos argumentos, nao le
                        estado do decodificador. E o que da para testar sozinho
    testes_juizes.c     testa as medidas com quadros SINTETICOS, sem decodificar
                        nada:  gcc -O2 -Isrc -o testes.exe src/testes_juizes.c                                    src/juizes.c -lm && ./testes.exe
  tools/                programas em Python
    ferramentas.py      gera índice, patches base, e remonta o MP4
    molde_idr.py molde_slice.py escapes.py cabecalhos.py
    encadeia.py         busca por etapas com o modo `avanco`
    remontar.py conferir_dump.py
    cabecalho_slice.py  acha bits trocados no cabeçalho do slice pelo que ele
                        deveria dizer (gera data/cabecalho_slice.txt)
    mapa_dano.py        onde o arquivo está danificado, medido em conteúdo
                        conhecido (gera data/mapa_dano.txt)
    ocultacao.py        quantos macroblocos o ffmpeg oculta em cada quadro
    proibidas.py        as sequencias 00 00 0x que cortam o NAL (guarda de
                        todo lote: nenhuma criada por patch)
    regressao.sh        prova que uma refatoração não mudou nada, modo a modo
                        (referência em data/regression/)
    confere_doc.py      confere o padrão de documentação das funções
                        (docs/REFATORACAO.md, seção 4c)
    anchor/             recodificação CABAC (planos 2 e 5): ancora*.c,
                        cabac_enc*.py, cauda_lote.py, cauda_pb.py (a tarja);
                        cabac_p.py (CABAC de slice P nos dois sentidos, com
                        ctx_init.json), jm_trace.py (lê o trace do JM) e
                        f11.py (encaixe, leitura, feixe e o NAL
                        corrigido do frame 11: gera o patches_f11.txt);
                        cabeca.py (censo da tarja de cima: gera o
                        censo_cabeca.txt e o patches_cabeca.txt);
                        gemeos.py (os B 10 e 12 pelos gemeos 6 e 8);
                        prefixo.py (quadros sinteticos para o PREFIXO=
                        do avanco: ~5x mais rapido no fim do GOP)
  docs/                 os doze documentos que crescem
  data/                 registros derivados, versionados
    CHECKSUMS.txt       SHA-256 do original, para detectar novo bit-rot nele
    deterministicos.txt remontados.txt
    patches_cabecalho.txt  o lote de cabeçalhos que entrou no patches.txt
    patches_cauda.txt patches_cauda_pb.txt patches_f11.txt patches_cabeca.txt
    patches_gemeos.txt patches_cabeca_b.txt patches_cabeca_f15.txt
    patches_cabeca_fis.txt patches_cabeca_gop29.txt patches_cabeca_gop58.txt
                        os lotes de cauda e o frame 11 (idem; o verify pula)
    mapa_dano.txt alvos.txt alvos_ocultos.txt cabecalho_slice.txt
                        medidas; cada um traz no topo como foi gerado
    regression/         hash de referência da saída de cada modo (SHA-256
                        cortado em 16 dígitos), para o regressao.sh
    candidatos_f13.txt candidatos_f19.txt candidatos_idr3047.txt
    janela_f11.txt      candidatos e janelas de busca -- NAO sao patches,
                        cada um traz sua condicao de promocao escrita
    f11/                sintaxe dos quadros P do fade (trace do JM) e as
                        hipóteses do frame 11 -- a sintaxe de onde sai o
                        patches_f11.txt (python tools/anchor/f11.py nal)
  output/               produtos: novos_*.txt, vídeo remontado, ver_final.html
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
conferida com `sha256sum -c data/CHECKSUMS.txt`.

**Composição do `patches.txt` — 5.955 linhas:**

| faixa | quantas | o que é |
|---|---|---|
| 1–1338 | 1.338 | base determinística (prefixos de NAL e cabeçalhos). Reconferido: regerar com `base` reproduz os mesmos 1338 byte a byte |
| 1339–1827 | 489 | destas, **477** também estão em `data/deterministicos.txt`. Saíram em 2026-09-18 a `77528965 2` (reparo antigo do 2361) e a `114260576 3` (reparo antigo do 3442, que era o próprio defeito); em 2026-09-30 a `2993169 2` (determinística do 63: o `molde_slice` fez o `slice_type` B para casar com o `nal_ref_idc` corrompido); em 2026-09-29 a `147444 0` (determinística, frame 10) e a `150538 0` (reparo do 12) — as duas trocavam o escape `03` do byte 13 do NAL por `02`, o ffmpeg cortava o NAL e o B virava interpolação (armadilha 62; `data/patches_gemeos.txt`) |
| | **11** | os reparos reais, que é o que o `verify` testa com `BASE_N=1338` — hoje todos "insuficientes" pelo critério de ocultação |
| | 1 | a linha 1378, `77528961 0` — o `frame_num` do reparo antigo do 2361, que ficou. Reclassificada como cabeçalho: está em `data/patches_cabecalho.txt` (por isso ele tem 783 linhas, não 782; e mais 8 desde 2026-09-29, os cabeçalhos do 14 e do 15 e o QP do 17 e do 19: 791; e mais 9 em 2026-09-30, os do GOP 29, e 12 do GOP 58: 812) e o `verify` a pula |
| 1828–2609 | 782 | **cabeçalhos de slice** consertados por coerência (2026-09-18), 570 quadros — linha a linha em `data/patches_cabecalho.txt`, que o `verify` também pula. Provam o cabeçalho; não trazem imagem nova (o dano segue no corpo). As imagens dos 167 quadros bons foram conferidas byte a byte antes de entrar |
| 2610 | 1 | `77496469 0` — o `00 00 02` proibido do 2360 restaurado para `03` (armadilha 27, revista); também em `data/deterministicos.txt` |
| 2611–2643 | 33 | **caudas de IDR** provadas pela tarja recodificada com CABAC (2026-09-24), 13 IDRs, todas com encaixe desde as fileiras 63–65 — linha a linha em `data/patches_cauda.txt`, que o `verify` também pula. Provam os bits do fim do NAL; o dano do meio continua. Entraram 65; as 32 de encaixe nas fileiras 66–67 saíram em 2026-09-25 (sintaxe variante na tarja, armadilha 61; `data/cauda_auditoria.txt`). `mapa` idêntico nas duas mudanças. Ver `docs/PLANO_ANCORA_CAUDA.md` |
| 2644–3118 | 475 | **caudas de P/B** provadas pela tarja de skips recodificada (2026-09-24), 325 quadros — linha a linha em `data/patches_cauda_pb.txt`, que o `verify` também pula. Nível A (dist 0–1): 189 quadros, 196 bits; nível B (dist 2): 136 quadros, 279 bits. `mapa` muda só no 2189 (erro do MB 7356 para o 7358, quadro que já era lixo antes da tarja) |
| 3119–3991 | 873 | **o frame 11 recodificado inteiro** (plano 5, 2026-09-28): a sintaxe do quadro reconstruída pela física do fade e recodificada com CABAC; 818 bits no slice, 55 no enchimento, 3,87% uniforme; o NAL corrigido fecha no tamanho exato — linha a linha em `data/patches_f11.txt`, que o `verify` também pula (só as 873 juntas fecham o quadro). `mapa` muda só no 11 (MB 1 → 8.160 inteiro); `serie` do filme de 163 para **165** (o 11 e o 12). Ver `docs/PLANO_RECODIFICA_F11.md` |
| 3992–4934 | 943 | **a tarja de cima recodificada** (censo da cabeça, 2026-09-28): os 960 MBs das fileiras 0–7 recodificados com CABAC a partir do estado exato do começo do slice e comparados com o arquivo byte a byte no NAL; 232 quadros — nível A, 197 P/B com 243 bits (distância 1–3, 16+ bits iguais depois da última troca), e nível I, 35 IDRs com 700 bits (dano uniforme de 1,5–6,6%). Linha a linha em `data/patches_cabeca.txt`, que o `verify` também pula. `mapa` muda só nesses 232, todos para a frente (184 passam da tarja, 45 avançam; parados na tarja de cima: 789 → 602; os 3 que pareciam fechar — 263, 1638, 2459 — eram o NAL cortado num `00 00 02` logo depois da janela, corrigido em 2026-09-29, linhas 5342–5348); `serie` igual (165). O nível B (362 quadros) ficou de fora. Ver `docs/CENSO_CABECA.md` |
| 4935–5340 | 406 | **os B 10 e 12 recodificados pelos gêmeos** (2026-09-29): o 10 todo pulado como o 6 (81 bits, 3,9%), o 12 com a sintaxe do 8 (325 bits nos 1.135 bytes de dados, 3,6%; enchimento limpo); os dois NALs fecham no tamanho exato e o JM decodifica o GOP 0 do 0 ao 12 sem erro nem aviso. Linha a linha em `data/patches_gemeos.txt`, que o `verify` também pula. `mapa`, `panorama` e `serie` idênticos — a imagem já era a interpolação, agora vem dos dados. Ver `tools/anchor/gemeos.py` |
| 5341–5347 | 7 | **escapes restaurados** (2026-09-29): o `03` onde um `00 00 02` (59, 263, 407, 1610, 1638, 2459) ou `00 00 01` (2971, criado pela linha 4.834 do lote da cabeça) cortava o NAL — o quadro parecia inteiro no `mapa` sem ler os dados. Com o escape, os 6 falsos inteiros param no dano real (MB 805–1.278) e o 2971 vai de 960 a 998; `serie` igual. Também em `data/deterministicos.txt`. Guarda: `tools/proibidas.py` (armadilha 62) |
| 5348–5672 | 325 | **nível B da tarja de cima** (2026-09-29): 163 quadros, distância 1–3 com folga ≥ 10 bits depois da última troca — calibrado nos 26 quadros de controle com evento real na cabeça, onde nenhuma rajada verdadeira passa como troca com essa folga. Linha a linha em `data/patches_cabeca_b.txt`, que o `verify` também pula. `mapa`: os 163 andam (121 passam da tarja); `serie` igual |
| 5673–5723 | 51 | **escapes `00 00 02` → `00 00 03`** em 49 quadros (2026-09-29, `tools/proibidas.py`): nos quadros bons há 705 escapes contra 2 casos da alternativa de 1 bit (byte de 1 bit zerado antes de um `02`), ~350:1. Ficaram fora os 10 `00 00 00` (voltar custa 2 bits; a alternativa de 1 bit é ~3× mais provável) e o 3444 (legítimo). O `mapa` não julga: o corte dava avanço falso, e 5 quadros recuam para o dano real. Também em `data/deterministicos.txt` |
| 5724–5729 | 6 | **cabeçalhos do 14 e do 15** (2026-09-29): o 14 com 4 bits pelo molde dos B do GOP 0 (4, 6, 8, 10, 12 e 16 idênticos fora `frame_num`/POC; fica QP 18 como os irmãos, tarja de cima com distância 0); o 15 com a solução A das duas de 2 bits (pesos na progressão do fade; a B dava deslocamento +125). Também em `data/patches_cabecalho.txt`. `mapa`: o 14 de sem imagem ao MB 896, o 15 ao MB 1 — os corpos estão na zona densa |
| 5730–5777 | 48 | **a tarja de cima do frame 15 com o MB 0 pela física** (2026-09-29): pelos pesos do cabeçalho, o MB 0 é inter da ref0 (o 13, tarja 16 → 17) com resíduo −1 na luma e −1 no Cr; os MBs 1–959, I16x16 DC. Das 45 variantes testadas, a prevista bate em 1,8% (48 de 2.696 bits, uniforme) e a segunda fica em 18%. Linha a linha em `data/patches_cabeca_f15.txt`, que o `verify` também pula. `mapa`: o 15 vai do MB 1 ao 965; `serie` igual |
| 5778–5906 | 129 | **o QP e a tarja de cima do 17 e do 19** (2026-09-29): o QP do cabeçalho estava errado (17: bit 164, 15 → 11; 19: bit 159, 14 → 12), achado pela física da tarja depois de corrigir o critério do censo; com ele, a tarja bate em 59 de 2.760 bits (17) e 68 de 2.704 (19). O QP em `data/patches_cabecalho.txt`, as 127 trocas da cabeça em `data/patches_cabeca_fis.txt` (o `verify` pula as duas). O peso da ref0 do 19 tem 1 bit errado, ambíguo entre três — não entrou. `mapa`: 17 de 9 a 1.151, 19 de 1 a 1.080; `serie` igual |
| 5907–5928 | 22 | **cabeçalhos e tarja de cima do GOP 29** (2026-09-30; o IDR 29 segue quebrado, o GOP inteiro depende dele): o QP do 30 (+8 → −8, como os P irmãos; a tarja pulada bate em 56 de 56 bits) e 1 bit da tarja dele; 3 bits da tarja do 54 que o separam dos P irmãos 38/42/46/50 (bytes idênticos); o cabeçalho do 47 (POC 48 → 34, que duplicava o do P 50, e `n1`) e o do 49 (inválido; QP −5 ou −6 ambíguo, ficou −5, 2 trocas a menos) pelo molde dos B irmãos; e 1–2 bits da tarja dos B 35, 37, 48, 49, 55 e 57, que paravam **dentro** da tarja pulada (nível C do censo). O cabeçalho em `data/patches_cabecalho.txt`, a tarja em `data/patches_cabeca_gop29.txt` (o `verify` pula as duas). `mapa`: 30 1 → 1.184, 35 276 → 1.003, 37 542 → 891, 48 e 49 sem imagem → 862 e 985, 54 1 → 990, 55 792 → 997, 57 600 → 987; o 437 e o 438 (quebrados, 159 e 980) deixam de sair — o ffmpeg aprende a profundidade de reordenação dos B mais tarde sem o POC errado do 47; `serie` igual. Fica de fora o QP do B 39 (+6 contra −6 dos irmãos, 1 bit): com ele o quadro para antes |
| 5929–5955 | 27 | **cabeçalhos e tarja de cima do GOP 58** (2026-09-30; o IDR 58 segue quebrado): o 63 era o P de referência com o `nal_ref_idc` corrompido — saiu a linha 1.474 (`2993169 2`, que o fazia B), entrou o `nal_ref_idc` e o QP −6, o único que casa com a tarja, + 2 bits dela; o 79, o 80 e o 83 pelo molde dos irmãos (o 83 tinha `frame_num` 68); a tarja pulada dos B 62, 76 e 84; o escape `03` antes da cena no 72 (`07`), 76 (`41`) e 84 (`43`); e o 1º byte depois do escape no 74 (`41` → `01`, como os irmãos 68–73). O cabeçalho em `data/patches_cabecalho.txt`, a tarja em `data/patches_cabeca_gop58.txt`. `mapa`: 62 731 → 996, 63 sem imagem → 991, 72 935 → 988, 74 848 → 1.107, 76 82 → 1.102, 79 17 → 992, 80 sem imagem → 1.024, 84 79 → 990; nada muda fora do GOP; `serie` igual |

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

python tools/ferramentas.py base  "$MP4" patches.txt   # só na 1a vez; hoje aborta (ver abaixo)
python tools/ferramentas.py index "$MP4" index.txt patches.txt

./reparador.exe "$MP4" index.txt patches.txt repair 0 500 4096
BASE_N=1338 ./reparador.exe "$MP4" index.txt patches.txt verify
python tools/ferramentas.py build "$MP4" patches.txt output/reparado.mp4
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
| [`ARMADILHAS.md`](docs/ARMADILHAS.md) | **62 maneiras de medir errado** que já produziram conclusão falsa aqui |
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
está em [`data/candidatos_idr3047.txt`](data/candidatos_idr3047.txt) com a
condição de promoção escrita.

Como retomar:

```bash
cat patches.txt data/candidatos_idr3047.txt | grep -E '^[0-9]+ [0-9]+$' > $SB/cad.txt
VISUAL=1 PISO_TRINCA=1 PISO_CROMA=1 BASE=-1 ./reparador.exe "$MP4" index.txt $SB/cad.txt avanco 3047 1 4450 20000 $SB/e3.txt
VISUAL=1 ./reparador.exe "$MP4" index.txt $SB/cad.txt trinca 3047 <lista>   # mede os sobreviventes
```

## 6. Mapa de dano (2026-09-18)

O bit-rot vem em **zonas** de 2–5% dos bits trocados; fora delas o arquivo está
praticamente limpo. Medido em conteúdo conhecido, no MP4 original
(`python tools/mapa_dano.py`, detalhe em
[`data/mapa_dano.txt`](data/mapa_dano.txt)):

| régua | limpo | danificado |
|---|---|---|
| enchimento `00 00 03` | 0,11–0,13 MB · 48–50 MB · 104 MB · 113–114 MB | **0,147–0,150 MB (frames 10–11: 4–4,5%)** · **59,8–60,4 MB (~2%)** |
| 5 primeiros bytes de cada quadro | 2.669 quadros sem troca | 776 com 1–5; distribuição **não binomial** (39 quadros com 4 trocas; uniforme daria 2) |

Consequência: quadro com payload em zona de ~4% tem centenas de bits trocados —
o frame 11 tinha 873 — e nenhuma busca de 1–3 bits o consertava (entrou
inteiro em 2026-09-28, recodificado a partir do conteúdo conhecido: plano 5). **Consultar o mapa
antes de escolher alvo de varredura.** Tamanho e alinhamento dos blocos de dano,
medidos em 2026-09-29 nas 1.279 trocas provadas em zona densa: não há bloco nem
alinhamento (bit no byte e posição uniformes), mas as trocas se repelem a menos
de 8 bits (1/3 do esperado) e raramente caem duas no mesmo byte (RASTREIO,
"Frame 13 — o dano denso").

**Os 1.439 quadros não inteiros, por onde está o dano** ([`data/alvos.txt`](data/alvos.txt)):
435 com **cabeçalho do slice inválido** pela norma (o caso do frame 11; 151 P,
279 B), 407 que param antes de 512 bytes, 57 entre 512 B e 2 KB, 517 depois de
2 KB, 23 sem imagem sem causa visível. "Inteiro" = MB final **e** imagem emitida
**e** cabeçalho válido: 2.006 quadros, não os 2.376 do `mapa` (armadilha 55).
O dano se concentra no começo dos quadros: 492 param antes de 512 bytes, contra
~27 esperados se o dano fosse uniforme no payload.
