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

Medido em 2026-09-13 com `report` (ffmpeg 8.1.1). O critério agora tem **duas
partes**, e a segunda é indispensável: além do sintático (flush, quadros ==
pacotes, zero logs), exige-se que a imagem não seja propagação vertical — ver
armadilha 7 da seção 5. Sem ela o número fica ~2x inflado por lixo.

| classificação | frames | o que é |
|---|---|---|
| **real** | **149** | decodifica limpo **e** tem imagem de verdade |
| propagado | 137 | "limpo", mas slice terminou cedo: listra vertical |
| uniforme | 16 | imagem chapada (preto/fade); pode ser legítima |
| quebrado | 3143 | falha no critério sintático |

Os 149 reais estão em **apenas 4 trechos contínuos** — não em 53, como a medida
antiga sugeria:

| trecho | frames | duração | início | origem |
|---|---|---|---|---|
| 3319–3434 | 116 | 3,87 s | 110,74 s | região intacta |
| 2333–2359 | 27 | 0,90 s | 77,84 s | **reparado** (os 5 patches) |
| 1138–1141 | 4 | 0,13 s | 37,97 s | — |
| 3437–3438 | 2 | 0,07 s | 114,68 s | — |

**Vídeo real hoje: 4,77 s de 114,95 s** (trechos ≥ 15 frames). Desses, **0,90 s
foram consertados pelo trabalho de reparo** e 3,87 s nunca estiveram quebrados.

Confirmado por inspeção visual: o frame 2333 mostra o noivo ajustando a gravata
diante do espelho, imagem íntegra. **Os 5 reparos produziram vídeo verdadeiro** —
o que estava errado antes era a régua, não os reparos.

A medida anterior dizia "306 perfeitos, 5,61 s assistíveis". Dos 306, **137 eram
listra vertical** e 16 chapados. E a região intacta encolheu de 4,20 s para
3,87 s: os frames 3435–3444 também são propagados.

**Atenção:** a contagem de keyframes abaixo usa só o critério sintático, de
antes da armadilha 7 ser conhecida. Os IDRs 29, 128, 215 e 323 foram inspecionados
visualmente e são **listra vertical**, apesar de constarem como "perfeitos". O
número real de IDRs com imagem é muito menor que 57 e precisa ser remedido.

**Keyframes: 57 dos 128 IDRs decodificam perfeitos**, espalhados por todo o
filme (0,0 / 1,0 / 4,3 / 6,2 / 7,2 / 10,8 / 12,7 / 13,7 / 14,6 / 15,2 / 17,2 /
24,5 / 25,5 / 27,2 / 31,8 / 36,9 / 37,9 / 40,1 / 41,0 / 42,3 / 43,3 / 44,1 /
47,0 / 49,9 / 52,8 / 56,2 / 59,1 / 61,2 / 62,1 / 65,0 / 66,0 / 67,0 / 67,9 /
69,1 / 72,0 / 74,0 / 77,8 / 78,8 / 80,7 / 86,2 / 89,1 / 90,1 / 91,0 / 93,5 /
95,4 / 96,2 / 98,2 / 100,0 / 101,7 / 104,3 / 106,5 / 107,4 / 110,7 / 111,7 /
112,4 / 113,3 / 114,3 s). A anotação antiga de "7 keyframes" é de antes da
correção do SPS/PPS.

**Isto tem consequência estratégica.** Todo reparo ancora no IDR anterior
(`ancora_de`), então um GOP cujo IDR está quebrado é irreparável enquanto o IDR
não for consertado — os ~27 frames dele estão bloqueados. Há 57 GOPs com âncora
limpa, prontos para reparo, e **71 GOPs bloqueados pelo próprio IDR**. Consertar
IDR quebrado rende muito mais que consertar frame comum: destrava o GOP inteiro.

Os 5 reparos reais foram revalidados sob o ffmpeg 8.1.1: `verify` com
`BASE_N=1338` dá **5 válidos, 0 falsos**.

Rodar `verify` sem `BASE_N` julga também os 1338 base e reporta 1328 "falsos".
Isso é esperado e não indica problema: o teste pergunta "sem este bit o frame
quebra e com ele fecha perfeito?", e um frame com outra corrupção no corpo nunca
fecha, por mais correto que esteja o cabeçalho. Dado útil desse run: **10 dos
1338 base validam sozinhos** — frames cuja corrupção era só de cabeçalho.
Reforça a anomalia do início do NAL da seção 6.

**A anotação antiga de "2808 dos 3445 frames" era a contagem do ffmpeg** — a
métrica que a armadilha 1 da seção 5 desmascara. O número rigoroso é 306.

Que 47 dos 53 trechos tenham apenas 2 a 5 frames diz que o dano é **denso e bem
distribuído**, não concentrado: o decoder trava, recupera por poucos frames e
trava de novo, ao longo do filme inteiro.

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
7. **"Decodifica sem erro" não quer dizer que a imagem existe.** Esta é a mais
   cara de todas: contaminou todas as medições anteriores. Uma slice pode
   terminar cedo, o decoder decodifica as primeiras fileiras de macrobloco,
   propaga o resto verticalmente e **não emite log nenhum**. O quadro é
   produzido, `quadros == pacotes` bate, e o frame passa como perfeito sendo
   listra. Medido: em amostras de 200–290 KB, corromper qualquer byte além de
   ~8–16 KB não muda nada — o decoder consome ~5% do dado e para.
   - Ligar `err_recognition` **não** resolve; testado com `AV_EF_EXPLODE |
     AV_EF_BITSTREAM` e o ffmpeg aceita a slice truncada do mesmo jeito.
   - O que funciona é a assinatura visual: na propagação cada linha é cópia da
     anterior. Frames bons dão ~24% de linhas repetidas na metade de baixo, os
     truncados dão 100%. É o que `propagacao()` mede.
   - **Calibre nos dois sentidos.** Só com exemplos ruins eu teria condenado o
     frame 2333, que dá 67,8% por ser uma cena de paredes claras e chapadas —
     e é vídeo real e bom. Sem um exemplo bom conhecido, o limiar sai errado.
   - Na dúvida, **olhe a imagem**. Despejar o frame com o modo `dump` e abrir
     custa segundos e responde o que métrica nenhuma respondeu aqui.

## 6. Questões abertas

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
era o bit corrompido.** É a armadilha 2 da seção 5 numa forma que ela não
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
- **Densidade real:** todas as estimativas que circulei (27.000 reparos, depois
  2.100) foram medidas com PPS errado ou métrica furada. Refazer do zero.

## 7. Melhorias no reparador

### Feitas

1. **Janela dupla** — buscar também no início do NAL. Feito, e não é opcional:
   quando o corte cai antes do byte ~64 a janela `[corte-j, corte+3]` colapsa e
   essa é a única busca que de fato acontece. Resolveu 7 dos 45 IDRs.
2. **Paralelização da enumeração exaustiva** — feita, com saída byte a byte
   idêntica à sequencial (mesmo SHA-256), validada em 6 e 10 threads:

   | threads | tempo (71 IDRs) | ganho | eficiência |
   |---|---|---|---|
   | 1 (sequencial) | 2064 s | 1,0x | 100% |
   | 6 | 430 s | 4,8x | 80% |
   | 10 | 319 s | 6,5x | 65% |
   | **12 (default)** | **298 s** | **6,9x** | 58% |

   Controlada por `THREADS`, teto em `núcleos-2`. O ganho satura perto de 7x:
   de 10 para 12 threads são só 6,6% a mais de velocidade por 20% mais threads.
   O default 12 é escolha deliberada de priorizar o relógio sobre a eficiência.
   A saturação indica gargalo serial por IDR — `acha_corte`, a decodificação de
   checagem e a ordenação final — que é o que a melhoria 3 atacaria. Desenho e
   justificativa em `PARALELIZACAO.md`. **Não** mexer no `thread_count` do
   libavcodec — ver seção 1 daquele documento.

### Pendentes

3. **Cache do estado do decoder na âncora** — hoje cada candidato de um frame
   comum redecodifica o GOP inteiro desde o IDR. Continua sendo o maior ganho
   disponível para frames não-IDR; para IDR a cadeia tem tamanho 1 e só o
   threading ajuda. Soma-se à paralelização, não a substitui.
4. **Paralelizar a busca com parada antecipada** (`busca1`) — ainda sequencial.
   Exige a regra do menor índice descrita em `PARALELIZACAO.md`, porque com
   múltiplas soluções "a primeira que chegar" escolheria bit errado.

### Descartada

5. ~~**Busca de 2 bits**~~ — implementada e testada: 0 soluções em todas as
   corridas. E a seção 6 mostra que a premissa estava errada de qualquer forma.
