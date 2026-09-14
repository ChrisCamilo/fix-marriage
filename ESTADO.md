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
> modificando e conferindo `sha256sum -c CHECKSUMS.txt` na dúvida. Não há
> segunda chance se ele for corrompido.

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
| **3319–3444** | **126** | **4,20 s** | 110,74 s | íntegro, **o final do filme inteiro** |
| 2333–2359 | 27 | 0,90 s | 77,84 s | íntegro por conta própria |
| 1138–1141 | 4 | 0,13 s | 37,97 s | — |

**Vídeo real hoje: 5,10 s de 114,95 s.** Confirmado por inspeção visual: o frame
2333 mostra o noivo ajustando a gravata diante do espelho; o 3319 mostra o noivo
calçando o sapato.

**O trecho final está completo, sem nenhum frame corrompido.** São 5 GOPs
seguidos (IDRs 3319, 3348, 3368, 3397, 3426) e os 126 frames decodificam. Os
últimos ~10 frames aparecem como `uniforme`/`propagado` nas classificações
automáticas, mas **são o fade para preto que encerra o filme**, não dano: o
brilho médio cai monotonicamente 54,5 → 44,5 → 36,5 → 32,7 → 25,9 → 22,8 → 16,0
e o número de tons vai de 110 a 1. Não tentar "consertar" esses frames.

**Os 5 reparos do `patches.txt` não produziram nenhum frame com imagem.** Eles
pertencem aos frames **2362–2366**, e os cinco estão classificados como
`propagado` — listra vertical. O trecho real de 0,90 s é o 2333–2359, que não
tem patch algum: estava íntegro por conta própria. A anotação antiga de "1,03 s
a partir de 77,84 s exigiu 4 reparos" confundia as duas coisas, porque só media
pelo critério sintático.

Ou seja: **todo o vídeo real que existe hoje sobreviveu sozinho.** O trabalho de
reparo até aqui rendeu zero segundo de imagem.

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

### Medição de 2026-09-14 (depois da correção do pts)

O casamento do quadro por `pts` e a exigência de imagem mudaram os números. Não
é o arquivo que mudou, é a régua — e a régua anterior lia o quadro errado.

| estado | frames | |
|---|---|---|
| `real` | **186** | decodifica limpo e tem conteúdo |
| `uniforme` | 2 | |
| `propagado` | **0** | agora reprovam no critério, viram `quebrado` |
| `quebrado` | 3257 | |

**188 frames com imagem = 6,27 s** de 114,95 s. Mas isolado não se assiste; em
trechos contínuos:

| trecho | frames | duração | onde |
|---|---|---|---|
| 3319–3427 | 109 | **3,64 s** | t=110,7 s, final do filme |
| 2333–2359 | 27 | **0,90 s** | t=77,8 s |

**4,54 s assistíveis.** Os outros 52 frames são ilhas de 1 a 4 quadros.

### O final do filme é um fade, e isso é medida, não impressão

Medindo só a área útil (linhas 136–943, sem as tarjas), o desvio padrão cai:
41,9 no frame 3420 · 21,2 no 3423 · 6,4 no 3427 · 1,7 no 3429 · **0,00 do 3431
em diante**. Dali para frente `min == max`: são campos de um único valor.

A cadência do GOP 3426 é de **período 2**, e a ordem de exibição não é a de
decodificação. Por `poc`: `3434 3433 3436 [3435] 3438 3437 3440 [3439] …`. Os
valores caem linearmente: 43, 41, 38, 36, **34**, 32, 29, 27.

Isso dá um gabarito aritmético — o valor de um frame quebrado é a média dos
vizinhos de exibição. Foi assim que saiu o reparo do 3435. **Cuidado:** ancorar
nos vizinhos de decodificação dá o frame errado; esse erro foi cometido e
elegeu o candidato errado antes de ser pego.

Assinatura de um frame bom do fade, que é o que um candidato precisa reproduzir:

- topo (linhas 0–129) uniforme 16;
- campo (136–**949**) uniforme, no valor previsto — inclusive as seis últimas
  linhas, que valem o mesmo que o resto;
- borda **seca** na linha 950, sem rampa: a rampa esfumada é o borrão da
  ocultação;
- tarja inferior (951–1079) uniforme 16, **até a última linha**.

### Nenhum frame do fade é reparável com 1 bit

Varredura exaustiva do NAL inteiro dos seis quebrados do trecho final:

| frame | bytes | soluções de 1 bit | melhor candidato |
|---|---|---|---|
| 3435 | 963 | **14** | campo e borda certos, **tarja 1040–1079 suja** |
| 3439 | 988 | 349 | campo 25 certo, tarja suja em todos |
| 3441 | 940 | **1** | campo 16–19 e tarja 19–20: errado |
| 3442 | 2939 | 959 | 15 com campo 23 e tarja limpa, mas linhas 944–949 saem 23–25 |
| 3443 | 261 | **0** | |
| 3444 | 266 | 2 | borda borrada |
| 3428 | 5211 | 25537 | ambíguo demais |
| 3430 | 7223 | 17448 | ambíguo demais |

Todos precisam de **mais de um bit**. Cada solução de 1 bit conserta uma parte
do quadro e deixa defeito em outra — é o sinal de que o dano é múltiplo, e a
peneira por região do quadro é o que revela isso. Sem olhar a tarja inteira, o
3442 passaria por resolvido com 15 candidatos "limpos".

### `verify`: 4 patches ficaram insuficientes, não falsos

Com o critério atual dá `2 válidos, 4 falsos`; com `VISUAL=0`, que é o critério
da época em que entraram, dá `5 válidos, 1 falso`. Os patches dos frames 2362,
2364, 2365 e 2366 continuam satisfazendo a sintaxe — o que mudou foi a régua,
que ganhou a exigência de imagem e a correção do pts. **Não removê-los:** podem
ser bits necessários de um reparo de vários bits. O append-only está certo aqui.

O espelho disso é o patch novo do 3435, que aparece como falso sob `VISUAL=0`
com `com=0 sem=0`: aquele frame sempre passou na sintaxe e só falhava na imagem.
Os dois critérios medem coisas diferentes; nenhum sozinho decide.

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
8. **A busca aprende a burlar qualquer métrica que você inventar.** Cinco
   tentativas foram exploradas em sequência no IDR 3191, cada uma "melhorando"
   o frame enquanto a imagem piorava:

   | métrica | pontuou | imagem real |
   |---|---|---|
   | linhas que diferem da anterior | 950 | ~200 |
   | + linha blocada é ruim | 755 | ~200 |
   | + média ancorada no conteúdo | 723 | ~200 |
   | + diferença para a linha y−30 | 723 | ~200 |
   | + **regularidade** (saltos bruscos) | 241 | ~209 ✓ |

   São bilhões de candidatos: a busca sempre acha o que maximiza o número, não
   o que conserta o vídeo. **Nunca confie num ganho reportado sem despejar a
   imagem e olhar.**
   - O que finalmente funcionou não foi limiar de **nível**, e sim de
     **regularidade**. Medindo linhas consecutivas: real dá `8 9 8 7 9 7 7 6`,
     borrão dá `15 4 3 3 10 3 3 3` — período 4, um degrau e três cópias, que é
     o preenchimento copiando a última linha boa para baixo. Saltos bruscos em
     60 linhas: 0 no real, 8 no borrão. Limiar de nível não separa, porque as
     linhas borradas ficam em 1,3–2,2 e passam raspando por qualquer corte.
   - Calibre sempre contra **falsas recuperações guardadas**, não só contra
     frames bons e ruins. Elas são o conjunto de teste que importa.
9. **O bit corrompido fica ~2600 bytes ANTES de onde o decoder para.** Medido
   com erro injetado em posição conhecida: bit em `rel=20000`, fronteira de
   consumo em `22623`. Uma janela de ±2048 centrada no consumo **não alcança o
   bit verdadeiro** — a busca então escolhe o melhor bit falso disponível e
   reporta ganho pequeno. Use janela de 8192 ou mais, e desconfie de ganho
   pequeno: pode ser sinal de que a janela não cobriu o alvo.

10. **O quadro capturado não é necessariamente o que você pediu.** Com
    reordenação de B, a ordem de saída não é a de decodificação: guardar "o
    último quadro recebido" entrega outro frame. O sintoma foi vizinhos
    *bons* consecutivos acusando diferença 0,00 — eu comparava uma imagem com
    ela mesma. Corrigido casando `fr->pts` com o índice do alvo; depois disso
    vizinhos bons passaram a diferir de **0,63 a 1,51**, que é o valor natural.
    Toda medição feita sobre cadeia de frames antes disso está inválida.

11. **Parecido demais com o vizinho é sinal de falha, não de acerto.** Quando o
    frame não decodifica, o ffmpeg emite uma cópia do anterior como ocultação —
    que tira nota máxima num critério de "seja parecido com o vizinho". O piso
    de 0,63 da armadilha 10 é o que separa um decode de verdade de uma cópia:
    **abaixo dele, desconfie.** É por isso que o resultado do `vizinho` sobre o
    frame 2360 (0,46–0,47) não conta como reparo.

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

3. **Cache do estado do decoder na âncora** — impossível como descrito, e a
   versão possível foi medida e descartada. Ver item 6 abaixo. Não tente.
4. **Paralelizar a busca com parada antecipada** (`busca1`) — ainda sequencial.
   Exige a regra do menor índice descrita em `PARALELIZACAO.md`, porque com
   múltiplas soluções "a primeira que chegar" escolheria bit errado.

### Descartada

5. ~~**Busca de 2 bits**~~ — implementada e testada: 0 soluções em todas as
   corridas. E a seção 6 mostra que a premissa estava errada de qualquer forma.

6. ~~**Contexto aquecido entre candidatos**~~ — implementado atrás de `WARM=1`,
   medido em 2026-09-14, **descartado**. Era a versão possível da melhoria 3.

   A premissa: decodificar um frame **não-referência** (`nal_ref_idc == 0`) não
   altera o buffer de referências, então um contexto que já decodificou
   `âncora..alvo-1` continuaria válido de um candidato para o seguinte — 28
   decodificações viram 1, em 70% dos frames (2395 de 3445).

   O ganho era real: `vizinho 2360 2359 256 3` caiu de **93 s para 3 s** (31x).
   Mas reprovou nas duas validações da seção 7 do `AGENTS.md`:

   | | passo 1, candidato `off 8 bit 2` | reprodutível |
   |---|---|---|
   | `WARM=0`, 1 thread | 0,47 | sim |
   | `WARM=0`, 12 threads | 0,47 | sim |
   | `WARM=1`, 1 thread | 0,46 | sim, mas ≠ referência |
   | `WARM=1`, 12 threads | 0,46 / 0,46 / 0,47 | **não** |

   O dado que fecha o diagnóstico é a última linha: **o mesmo candidato recebe
   nota diferente a cada corrida.** Não é "escolheu outro candidato igualmente
   válido" — a métrica é que tem ruído, e aí o mínimo vira sorteio. O `melhor_ref`
   varre tudo e desempata pelo menor índice, então ordem de varredura não
   explicaria divergência nenhuma.

   Causa: a premissa vale para um frame não-referência **íntegro**. Quase todo
   candidato de uma varredura é lixo, e um decode que falha deixa estado para
   trás (ocultação, buraco de `frame_num`, POC). O resultado do candidato *k*
   passa a depender de quais candidatos aquela thread viu antes — dependência de
   história, que a regra do menor índice não conserta. Com 1 thread o efeito é
   determinístico, mas continua contaminado: por isso ele reproduz a si mesmo e
   não reproduz o `WARM=0`.

   `AV_CODEC_FLAG_LOW_DELAY` foi suspeito e **está inocente**: separando o flag
   do modo (`LOWDELAY=1 WARM=0`), a saída ficou idêntica à de `LOWDELAY=0` nos
   alvos 2358 e 2360. O flag não muda nada.

   Não há limpeza barata: `avcodec_flush_buffers` zera o estado mas leva junto os
   frames de referência, forçando a redecodificação da cadeia que a otimização
   existia para evitar. **A ideia morre no desenho, não na implementação.**

   Sinal de alerta a guardar: com 12 threads o resultado contaminado sai
   *melhor* (0,42) que o limpo (0,46–0,47). Ruído que melhora a nota significa
   que o contexto sujo empurra o decoder para a ocultação — que copia o frame
   anterior, que é justamente a referência contra a qual se mede.
