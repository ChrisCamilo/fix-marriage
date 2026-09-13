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

Tudo mora na raiz do projeto — **não existe subdiretório `rep/`**. O nome do
vídeo tem espaços e `&`, então precisa vir sempre entre aspas na linha de comando.

| arquivo | papel |
|---|---|
| `Caio & Lizandra - Making- Caio-Balu.mp4` | original intocado — **fonte de verdade, nunca modificar** |
| `patches.txt` | lista `offset bit`, append-only — a outra fonte de verdade |
| `index.txt` | índice `i offset size idr` das 3445 amostras |
| `reparador.c` | reparador em C com libavcodec |
| `ferramentas.py` | gera índice, patches base, e remonta o MP4 |
| `CHECKSUMS.txt` | SHA-256 do original, para detectar novo bit-rot nele |

O projeto é um repositório git **local e privado, sem remoto**. Isto é material
pessoal de família: não publicar em lugar nenhum. O `.mp4` e o binário compilado
ficam fora do versionamento (ver `.gitignore`); a integridade do original é
conferida com `sha256sum -c CHECKSUMS.txt`.

Os 1338 primeiros patches são determinísticos (prefixos de NAL e cabeçalhos), e
isto foi reconferido: regerar com `base` reproduz exatamente os mesmos 1338, byte
a byte. Os seguintes são reparos reais — hoje são apenas **5**, todos no trecho
dos 77,5–77,8 s. Use `BASE_N=1338` no modo `verify`.

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
gcc -O2 -o reparador.exe reparador.c $(pkg-config --cflags --libs libavcodec libavutil)

python ferramentas.py base  "$MP4" patches.txt   # só na 1a vez; hoje aborta (ver abaixo)
python ferramentas.py index "$MP4" index.txt patches.txt

./reparador.exe "$MP4" index.txt patches.txt repair 0 500 4096
BASE_N=1338 ./reparador.exe "$MP4" index.txt patches.txt verify
python ferramentas.py build "$MP4" patches.txt reparado.mp4
```

`patches.txt` é carregado inteiro na inicialização: o processo é retomável,
idempotente (pula frames já bons) e incremental por faixa.

**O modo `base` recusa sobrescrever um `patches.txt` existente.** Ele abria a
saída com `"w"` e truncava, então rodar a linha acima apagaria os reparos reais.
Para regerar do zero, mova o arquivo atual para outro nome antes.

**Por que UCRT64 e não MINGW64:** o `reparador.c` localiza o byte corrompido
lendo as mensagens de erro do decoder, e o ffmpeg emite o resto do bytestream
com `%td`. A UCRT interpreta `%td`; a msvcrt antiga do MINGW64 não, e a
heurística falharia **em silêncio**, caindo na busca binária — muito mais lenta,
sem nenhum aviso. Conferido no `avcodec-62.dll`: as duas strings que o reparador
consome (`error while decoding MB %d %d, bytestream %td` e `concealing %d DC`)
continuam presentes no ffmpeg 8.1.1.

## 4. Resultados já obtidos

- **4,17 s** contínuos a partir de 110,74 s — região intacta, zero reparos
- **1,03 s** a partir de 77,84 s — exigiu 4 reparos de bit válidos
- **7 keyframes** perfeitos em 1920×1080 (0,0 / 77,8 / 110,7 / 111,7 / 112,4 / 113,3 / 114,3 s)
- Arquivo remontado decodifica 2808 dos 3445 frames (muitos com ocultação)

## 5. Armadilhas de método — ler antes de medir qualquer coisa

Cada uma delas me custou horas e produziu uma conclusão errada:

1. **Contar frames emitidos pelo ffmpeg não mede nada.** Ele emite quadros de
   ocultação cinza. Cheguei a 1043/3445 e comemorei; eram todos cinza.
2. **"Mais macroblocos decodificados" aceita reparos errados.** Um bit errado mas
   sintaticamente válido faz o CABAC andar mais um pouco.
3. **`frames_emitidos > base` é furado** com reordenação de B: o decoder libera
   quadros atrasados do buffer. Dois dos meus seis "reparos" eram falsos por isso.
   O critério correto é o do `reparador.c`: **flush do decoder e exigir
   quadros == pacotes, com zero linhas de log**.
4. **Métrica agregada esconde falha sistemática em subgrupo.** Meça sempre
   separado por tipo (I, P, B).
5. **Atribuir linhas `concealing` a frames pelo log não funciona** — o ffmpeg
   emite em ordem de saída, não de decodificação. Use extensão incremental.
6. **Destruir o cabeçalho do NAL zera a contagem de erros** porque o ffmpeg
   descarta o pacote em silêncio. Exija sempre que o quadro seja produzido.

## 6. Questões abertas

- **Anomalia do início do NAL:** 20% dos prefixos e 17% dos slice headers
  corrompidos, contra interior quase limpo. Fator ~400x que nenhum modelo
  uniforme explica. Testei a hipótese de "janela de 16 bytes" em 14 keyframes:
  **0 de 14**. A anomalia é real mas não é uma regra simples de reparo.
- **Unidade de dano:** nunca testei busca de 2 bits. Se o dano for de 2 bits,
  toda busca de 1 bit está condenada nos frames duros.
- **Densidade real:** todas as estimativas que circulei (27.000 reparos, depois
  2.100) foram medidas com PPS errado ou métrica furada. Refazer do zero.

## 7. Melhorias previstas no reparador

1. **Janela dupla** — buscar também nos primeiros ~64 bytes. Hoje ele marcou
   os frames 2360 e 2361 como duros, mas o bit deles estava nos bytes 4 e 1.
2. **Cache do estado do decoder na âncora** — hoje redecodifica o GOP inteiro a
   cada tentativa. Maior ganho de velocidade disponível.
3. **Busca de 2 bits** em janela estreita, para os duros.
4. **Paralelizar por GOP** — são independentes.
