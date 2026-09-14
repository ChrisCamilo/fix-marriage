# Armadilhas de método — ler antes de medir qualquer coisa

Cada item aqui custou horas e produziu ao menos uma conclusão falsa neste
projeto. Ler antes de inventar métrica nova.

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

12. **Frame limpo dentro de GOP com IDR quebrado é lixo, e nenhuma métrica
    acusa.** Ele passa no critério rigoroso porque a *sintaxe dele* está boa —
    mas prediz a partir de referências que são lixo, e sai em listras verticais.
    Medido em 2026-09-14: dos 191 frames classificados com imagem, **40 estavam
    nessa situação**; amostrei 10 e **todos** estavam visualmente destruídos.

    Por que as métricas falham: o frame 2363, destruído, tem blocagem 2,11 e
    propagação 0,861 — dentro da faixa dos frames bons. A blocagem dos 40
    suspeitos tem mediana 1,69 contra 1,03 dos confiáveis: **não separa**.

    O teste que funciona é barato e não precisa olhar a imagem: **o IDR do
    próprio GOP decodifica?** Se não, o frame não conta, por mais limpo que
    pareça. Toda contagem de "frames recuperados" tem que aplicar esse filtro.

13. **Só se repara na ordem da cadeia.** O critério exige a cadeia INTEIRA
    limpa — `log_erros == 0 && quadros == alvo - ancora + 1`. Se qualquer frame
    entre a âncora e o alvo estiver quebrado, ele emite quadro de ocultação, a
    ocultação gera linha de log, e **nenhum candidato do alvo pode passar**,
    qualquer que seja o bit.

    Custou uma hora de máquina: a varredura do frame 2361 devolveu **0 soluções
    em 243.552 candidatos**, e o resultado é inválido — ela rodou enquanto o
    2360, que está na cadeia dele, ainda estava quebrado. O 2360 foi reparado
    depois, na mesma corrida.

    **Regra: varrer sempre do menor índice para o maior dentro do GOP**, e
    refazer qualquer varredura que tenha rodado com um predecessor quebrado. Um
    "0 soluções" só significa alguma coisa se a cadeia até o alvo estiver limpa.

14. **O ponto de corte só vale se o frame produzir imagem com estrutura.** O
    `acha_consumo` faz busca binária truncando o NAL e comparando o hash da
    imagem. Se o frame não produz imagem nenhuma, ou produz campo chapado, o
    hash não varia com a truncagem e a busca **colapsa para o limite inferior** —
    devolvendo 5, 7, 10, 12 como se fossem medições.

    Medido nos 121 IDRs quebrados: **22 não produzem imagem** e **15 produzem
    campo chapado** (blocagem ≤ 0,05). Nesses 37, o corte é artefato.

    Custou uma varredura inteira: os "39 IDRs de corte precoce" que varri em
    `[5, corte+1024]` eram, em 37 dos 39 casos, uma faixa escolhida por um número
    sem significado. O resultado "38 de 39 sem solução de 1 bit" **não mede
    nada** e foi retirado.

    **Antes de usar o corte, conferir que o frame produz imagem com blocagem
    acima de ~0,05.** Os 84 IDRs que passam nesse teste têm corte de 792 a
    60.787, mediana 17.698 — nada parecido com os 5 a 12 do grupo colapsado.

15. **Limiar calibrado numa região, aplicado em outra, condena quadro genuíno.**
    A tarja tem duas regiões com tolerâncias muito diferentes, porque a fronteira
    imagem/tarja cai **dentro** da fileira de macroblocos 59 (linhas 944–959).
    Medido em 144 quadros genuínos:

    | região | desvio | pior pixel | pixels fora de ±10 |
    |---|---|---|---|
    | transição 950–956 | até **1,36** | até **14** | até **0,238%** |
    | fundo 957–1079 | até 0,40 | até 7 | **zero** |

    O limiar do fundo é **3x mais apertado**. Aplicá-lo à faixa inteira reprova
    quadro perfeitamente normal — erro cometido duas vezes: no frame 3435, onde
    inventei um "defeito de tarja" que não existia, e no 2361, onde a rejeição
    estava certa mas pelo número errado.

    **Calibrar sempre na mesma região onde se vai julgar**, e contra quadros
    genuínos, nunca contra intuição. O `campo` agora reporta as duas separadas.

16. **A propagação não distingue listra de cena suave — linha idêntica sim.**
    O `propagacao` conta linhas *parecidas* com a de cima, e cena desfocada com
    grandes áreas uniformes acerta valores altos legitimamente.

    Medido: o GOP 3368 tem 16 quadros com propagação de 0,93 a 0,98 e eles estão
    **perfeitos** — mãos, calça, tapete, tênis, tudo nítido. Já o GOP 1683 tem
    quadros na mesma faixa (0,941–0,950) e os dois terços de baixo são **listra
    vertical**. A métrica não separa os dois casos.

    O que separa é **linha exatamente idêntica à anterior**:

    | | linhas idênticas |
    |---|---|
    | 137 quadros bons (GOP 2333 e trecho 3319–3426) | **0,0%**, todos |
    | GOP 3368, os "suspeitos" | **0,0%** — estão bons |
    | GOP 1683, frames 1683 e 1685 | **34,1%** — listra |

    Separação binária, sem calibração: quadro genuíno tem **zero** linhas
    idênticas. Propagação alta com zero linhas idênticas é cena suave; com
    dezenas de por cento é propagação vertical de verdade.

