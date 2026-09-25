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

17. **Detector bom não vira objetivo bom.** "Linhas idênticas" separa listra de
    cena suave de forma binária e perfeita — é o melhor detector de propagação
    que este projeto tem (armadilha 16). Usado como **alvo de otimização**, foi
    burlado na primeira corrida.

    Medido no IDR 1683: o melhor candidato baixou a nota de 277 para 153 linhas
    idênticas, e a imagem **piorou** — a propagação limpa virou lixo colorido em
    magenta, vermelho e verde. Ruído não tem linha idêntica, então a nota cai
    justamente porque o quadro ficou pior.

    | | croma U | croma V |
    |---|---|---|
    | quadro bom | 101–135 | 126–155 |
    | IDR 1683 antes | 111–134 | 127–154 |
    | o "melhor" candidato | **56–171** | **114–187** |

    É a armadilha 8 numa forma nova: ali a busca burlava proxy visual inventado;
    aqui burlou uma métrica **bem calibrada**, porque calibração serve para
    detectar, não para otimizar.

    Tentei salvar com guardas — croma perto de 128 e blocagem na faixa dos
    genuínos. **Foi burlado de novo**: o melhor candidato baixou 277 para 227 e a
    região ficou com listras **pastel** (roxo, verde, rosa) em vez de saturadas,
    croma `U 105-144`, dentro da guarda. Apertar mais só empurra a busca para um
    lixo mais parecido com cena.

    **A razão é estrutural, e vale além deste caso:** a região danificada do IDR
    1683 não tem gabarito. Não há vizinho temporal — o GOP inteiro está quebrado
    e o anterior também — e a aritmética não diz nada sobre conteúdo de
    macrobloco. **Sem verdade conhecida, todo objetivo é proxy, e proxy contra
    654 mil candidatos sempre encontra o caso patológico.**

    Onde houve reparo hoje, havia gabarito: o frame 2360 tinha vizinhos íntegros
    dos dois lados, o 3435 tinha o valor do fade previsto por aritmética. O IDR
    1683 não tem nenhum dos dois, e por isso resiste.

18. **Blocagem acima de 0,05 não prova que o corte é real — listra também
    bloca.** A armadilha 14 mandou conferir se o frame "produz imagem com
    estrutura" antes de confiar no ponto de corte, e a medida escolhida foi a
    blocagem. Ela separa quadro chapado de quadro com conteúdo, mas **não separa
    conteúdo de propagação vertical**: listra tem borda de macrobloco tanto
    quanto cena tem.

    Medido no IDR 1712, classificado como "corte confiável" com blocagem
    **1,040** — valor dentro da faixa dos IDRs genuínos (1,0 a 1,2):

    | | linhas idênticas | primeira propagada |
    |---|---|---|
    | IDR 1712 original | **71,2%** | linha 181 de 949 |
    | IDR 1683 (o listrado conhecido) | 34,3% | 581 |
    | os 6 IDRs utilizáveis | 0,0% | — |

    Ou seja: o 1712 decodifica 4% do slice, morre na linha 181 e o resto é
    listra — e mesmo assim passou no filtro. A blocagem dele vem das bordas
    verticais da própria listra.

    **Consequência prática:** a triagem dos 121 IDRs quebrados em "84 com corte
    confiável" foi feita com a métrica errada. O número real é desconhecido e
    provavelmente bem menor. Refazer a triagem com **linhas idênticas**, que é o
    detector que funciona (armadilha 16) — mas nunca como objetivo de
    otimização (armadilha 17).

19. **Ordenar por estatística do gabarito, quando nada chega perto do gabarito,
    é ordenar ruído.** A tarja é gabarito legítimo — média 16,00 é conteúdo
    sabido a priori, não proxy inventado (é a diferença entre o IDR 1773 e o
    1683). Mas isso vale para **aprovar ou reprovar**, não para **rankear**.

    No IDR 1773, nenhuma das 26.865 soluções chega perto de 16. Peguei então "a
    melhor", por menor desvio de tarja, e ela é catastrófica:

    | | linhas idênticas | 1ª propagada | tarja média / desvio |
    |---|---|---|---|
    | original quebrado | **11 (1,4%)** | **933** | 190,7 / 38,5 |
    | "melhor" por desvio | 285 (35,1%) | 565 | 220,3 / **13,3** |
    | "melhor" por média | 97 (11,9%) | 821 | **126,3** / 74,8 |

    Ela venceu porque **listra pastel uniforme é mais plana que conteúdo
    vazado**. O desvio menor não é tarja reaparecendo, é destruição mais
    homogênea — e custou 368 linhas de imagem boa.

    **A regra:** um gabarito com valor conhecido responde sim ou não. Se a
    resposta é não para todos, a busca acabou — não existe "o menos não".
    Transformar a distância até o gabarito em ranking devolve o problema da
    armadilha 17, agora disfarçado de medida legítima.

    Corolário que fecha o 1773: a primeira linha propagada do original é 933 e a
    última linha da imagem é 949. **O original já está no teto** — nenhum
    candidato tem para onde melhorar a imagem, e nenhum conserta a tarja. Entre
    as 26.866 opções (26.865 candidatos mais não fazer nada), **não fazer nada
    é a melhor**.

20. **O ponto de corte não prevê quantos bits estão errados.** É tentador tratar
    o corte como amostra de densidade — se o primeiro erro aparece no byte
    18.887 de 50.635, a densidade seria 1/18.887 e o NAL teria ~2,7 erros. O
    raciocínio é limpo, e os dados o destroem:

    | frame reparado | bytes | posição do bit errado | resto do NAL |
    |---|---|---|---|
    | 2360 | 32.501 | **rel 8** | 32.493 bytes íntegros |
    | 3435 | 963 | rel 757 | íntegro |
    | 3439 | 988 | rel 818 | íntegro |
    | 3442 | 2.939 | rel 1.122 | íntegro |

    Sob densidade uniforme, o 2360 teria ~4.000 erros. Teve **um**.

    **O dano deste arquivo é em rajada, não espalhado.** Medido nos 17 IDRs com
    prefixo fora do canônico: 35,3% têm ≥2 bits errados dentro de uma janela de
    **24 bits**, contra 6,6% que a independência preveria — excesso de 5,3x. Um
    deles tem 6 bits errados em 3 bytes, 25% de densidade local. E os 17 vão de
    3,7% a 73,7% do arquivo e param aí; os últimos 26% não têm nenhum, que é
    onde estão 5 dos 7 IDRs bons.

    **Consequência para estimar esforço:** há dois regimes, não um número.
    Na borda da rajada, 1 ou 2 bits — é onde estão os 5 reparos que deram certo.
    Dentro dela, com densidade de 4% a 25%, um NAL de 100 KB tem milhares de
    bits errados e nenhum método alcança.

    O preditor do regime **não é o corte, é a vizinhança**: os 5 reparos bem
    sucedidos estão em GOPs íntegros no resto, e o GOP 1773 — 26 de 29 frames
    sem imagem — é assinatura de estar dentro da rajada.

    Isto explica por que gabarito e reparabilidade sempre aparecem juntos neste
    projeto: estar na borda da rajada é o que produz vizinho íntegro **e** poucos
    bits errados. Não são duas condições, são a mesma.

21. **Linhas idênticas não detecta borrão de compensação de movimento.** A
    armadilha 16 estabeleceu que linha idêntica separa listra de cena, e isso
    vale para propagação **intra**, que copia a linha exata. Frame P ou B que
    prediz de uma referência listrada produz linhas **quase** iguais, não
    iguais, e o detector lê 0,0%.

    Medido em frames dos GOPs 1802 e 2801, cujos IDRs são 43% e 49% listra:

    | frame | listra | razão V/H | o que é |
    |---|---|---|---|
    | 2340, 2350, 3390 (genuínos) | 0,0% | **0,78 a 0,99** | cena |
    | 2805, 2810 | 0,0% | **0,49 / 0,54** | borrão vertical |
    | 1810, 1820, 1830 | 0,0% | **0,41 / 0,52 / 0,59** | borrão vertical |

    Os frames de 0,0% do GOP 1802 parecem limpos por toda métrica de linha, e
    em resolução cheia são a cabeça do noivo com o corpo esticado em colunas.

    **O detector que funciona é a razão entre gradiente vertical e horizontal.**
    Borrão vertical achata o gradiente vertical e deixa o horizontal intacto:
    genuínos ficam em 0,78 ou acima, borrados abaixo de 0,6.

    Isto quase me fez concluir que havia 28 frames limpos dentro de um GOP com
    IDR quebrado, o que contradiria a armadilha 12. Não contradizia — o detector
    é que era cego.

22. **O critério rejeita quadro de fade por construção.** O `decodifica` aprova
    com `log_erros == 0 && quadros == esperado` e, com `exigir_imagem`, também
    `propagacao(...) < 0.995`. Essa terceira parte existe contra listra
    vertical — e **campo uniforme tem propagação 1,0**, porque toda linha é
    igual à anterior.

    Um fade é campo uniforme por definição. Medido no GOP 0, os frames 1, 2, 4,
    5, 7, 8, 9 e 12 **não emitem uma linha de erro no decoder** e mesmo assim
    são reprovados: a imagem deles está certa, é lisa, e lisa demais para o
    critério.

    | frame | campo | tarja | log de erro | veredito do critério |
    |---|---|---|---|---|
    | 1 | 21,00 ± 0,00 | 16,000 / 0,000 | **nenhum** | reprovado |
    | 2 | 18,00 ± 0,00 | 16,000 / 0,000 | **nenhum** | reprovado |
    | 4 | 23,00 ± 0,00 | 16,000 / 0,000 | **nenhum** | reprovado |

    **Consequências práticas, e a segunda é a que custou tempo:**

    - Quadro de fade reprovado **não é dano** e não é alvo de varredura. Três
      frames da cauda (3441, 3443, 3444) receberam varredura e julgamento de
      candidatos quando já produziam o valor exato previsto pela reta do fade.
    - Mas o reprovado **não polui a cadeia**: `log_erros` é zero. Quem bloqueia
      o GOP é quem realmente erra — no GOP 0 é o próprio IDR.

    O desempate certo é o **gabarito da tarja**: 16,000 com desvio 0,000. Listra
    de verdade não o produz, e é isso que o `TARJA=1` do modo `serie` usa para
    aceitar esses quadros na remontagem sem afrouxar o critério do `patches.txt`.

23. **Varredura em quadro uniforme é inválida pelo próprio critério — e depois
    indecidível.** Duas falhas em série, e eu caí nas duas seguidas no IDR 0,
    gastando 4,4 h de varredura de 2 bits.

    **Primeira: o critério rejeita o acerto.** A segunda parte do `decodifica` é
    `propagacao(...) < 0.995`, e quadro uniforme tem propagação **1,0000**
    (armadilha 22). O quadro correto do IDR 0 é preto liso. Medido:

    | varredura de 1 bit, NAL inteiro | soluções |
    |---|---|
    | com critério de imagem | **1** — e ela *piora* o quadro |
    | só sintático (`VISUAL=0`) | **16.786** |

    A busca estava estruturalmente impedida de aprovar a resposta certa, e a
    única que ela aprovou foi justamente uma que estraga a tarja.

    **Segunda: com o critério certo, nada decide.** Das 16.786, **15.537**
    produzem quadro inteiro em 16 com tarja 16,000 e desvio 0,000 — e as
    testadas saem **byte a byte idênticas ao original sem patch**. Não é que o
    juiz esteja fraco: não existe diferença para julgar.

    **A razão é o que dá força à tarja se voltando contra ela.** Ela vale como
    gabarito porque são 227 mil pixels com valor prescrito **diferente do
    conteúdo** (CRITERIOS seção 0). Num quadro preto o conteúdo *é* a cor da
    tarja, e o teste passa a carregar zero informação.

    **Regra:** antes de varrer, conferir se o quadro alvo tem conteúdo. Alvo de
    campo uniforme — fade, corte para preto — não é reparável por busca, porque
    nenhuma medida de imagem separa candidatos. Se precisar destravar a cadeia
    para investigar o resto do GOP, dá para usar qualquer um deles **sem
    escrever no `patches.txt`**: a imagem é idêntica, então o decode a jusante é
    idêntico, mas afirmar qual bit estava corrompido seria invenção.

24. **Imagem idêntica não quer dizer bitstream equivalente.** Achei 15.537
    inversões de 1 bit no IDR 0 que produzem quadro **byte a byte idêntico** ao
    original — conferido por SHA-256 do plano Y. Concluí que usar qualquer uma
    delas para destravar a cadeia era inofensivo, já que "o decode a jusante é
    idêntico".

    **Não é.** O bit muda o bitstream, e o estado que ele deixa no decoder
    impede os quadros seguintes: com ele, o frame 2 do GOP 0 para de sair e o
    decoder diz `co located POCs unavailable`. Sem ele, o frame 2 decodifica
    limpo.

    | | frames de 0–12 que decodificam limpo (`VISUAL=0`) |
    |---|---|
    | `patches.txt` puro | **0 a 9** |
    | com o bit "inofensivo" no IDR 0 | **só 0 e 1** |

    O dano custou três varreduras invalidadas, e pior: eu li o zero delas como
    defeito do modelo de cadeia e cheguei a registrar uma reforma do critério
    que não era necessária.

    **A regra:** um quadro carrega mais estado do que os pixels dele — ordem de
    exibição, marcação de referência, POC. Comparar imagens não prova
    equivalência. **Antes de tocar num quadro de que outros dependem, conferir
    os quadros seguintes**, não o próprio.

25. **Conferir se o alvo está mesmo quebrado — antes de varrer.** Gastei quase
    três horas procurando conserto para o IDR 0, que está **intacto**.

    Ele decodifica limpo pelo critério sintático, produz campo 16,00 com desvio
    0,00 e tarja 16,000 / 0,000. Três sinais me enganaram, e os três são
    reconhecíveis:

    | o que parecia | o que era |
    |---|---|
    | `error while decoding MB 119 64` | artefato do `acha_consumo`, que **trunca o NAL de propósito** para achar o ponto de consumo. Não é o decode natural. |
    | estar na lista de IDRs quebrados | o juiz de imagem reprovando campo uniforme (armadilha 22) |
    | 16.786 soluções de 1 bit | a partir do byte 500 **todo** bit "resolve", porque o original já resolvia |

    O terceiro é o diagnóstico mais claro que existe de varredura inútil:
    **se a densidade de soluções chega a 100% numa faixa, a varredura não está
    medindo conserto**, está medindo "continua decodificando". Bits nos bytes
    1.500, 2.200 e 2.290 — que o decoder nem consome — "resolviam" o frame.

    **A conferência que evita tudo isso custa um segundo:**

    ```bash
    VISUAL=0 ./reparador.exe "$MP4" index.txt patches.txt dumpyuv <alvo> /tmp/x.yuv
    ```

    Se sair `decode limpo`, o alvo não tem defeito de bitstream — o que sobra é
    o juiz de imagem, e aí a pergunta é se a imagem está mesmo errada, não qual
    bit trocar.

    Medido nos 125 IDRs ditos quebrados: **50 decodificam limpo sem o juiz de
    imagem**, e desses **49 têm tarja errada** — ali o juiz está certo. Só o
    IDR 0 é barrado indevidamente. O critério está bem calibrado; o erro foi meu
    em não separar os dois casos antes de gastar CPU.

26. **A tarja prova a fonte, não o quadro — cópia passa no gabarito.** Os 274
    bits do `molde_slice.py` fizeram os frames 10 e 11 aparecerem com tarja
    16,000 / desvio 0,000, e eu contei como dois quadros recuperados. **São
    cópias.**

    | grupo de quadros byte a byte idênticos | leitura |
    |---|---|
    | [0, 3443] | legítimo — o filme abre e fecha em preto puro |
    | [5, 11, 12] | 11 e 12 reproduzem o 5 |
    | [9, 10] | 10 reproduz o 9 |

    O frame 12 já era contado como bom **antes** de qualquer mudança de hoje, e
    também é cópia. Dos 162 quadros com tarja correta, **3 não têm conteúdo
    próprio**.

    Num fade cada quadro difere do anterior por ~2 níveis. Três quadros em
    posições de exibição diferentes com a mesma imagem não é decodificação: é o
    decoder reproduzindo a referência.

    **A cópia herda a tarja boa**, então o gabarito prova que a *fonte* estava
    correta, não que *este* quadro decodificou. É a armadilha 23 numa forma
    nova: lá o conteúdo era igual à cor da tarja, aqui vem inteiro de um quadro
    que já tinha a tarja certa.

    **O detector é trivial e custa nada:** comparar o hash do plano Y com o dos
    vizinhos de exibição. Quadro genuíno num fade nunca repete; quadro em cena
    parada pode repetir, e aí vale olhar. Rodar isso sobre qualquer conjunto de
    "quadros bons" antes de contá-los.

27. **Regra de formato também precisa ser validada contra quadro bom.** Depois
    que o `00 00 01` ilegal rendeu 84 bits provados, fui atrás de outras
    violações da norma de escape. Achei três, todas plausíveis:

    | regra candidata | ocorrências | veredito |
    |---|---|---|
    | `00 00 03` seguido de byte > `03` | 435 em 376 frames | **acusa 3 quadros bons** |
    | `00 00 02` no payload | 82 em 80 frames | **acusa o frame 2360** |
    | `00 00 00` no payload | 11 em 11 frames | **acusa o frame 3444** |

    As três seriam 528 bits num arquivo append-only, e as três estão erradas.

    **O motivo:** o `cabac_zero_word`. Para CABAC o codificador pode anexar
    palavras `0x0000` depois do RBSP, e escapadas elas produzem exatamente esses
    padrões. O frame 3444 tem `00 00 00` a cinco bytes do fim; o 2360 tem
    `00 00 02` logo no começo. A regra de escape vale para o RBSP **antes** do
    escape, e o fluxo escapado não obedece ao padrão simples que procurei.

    **O que salva é a mesma conferência de sempre**, e ela é barata: rodar a
    regra sobre os quadros cuja tarja prova que decodificam certo. Se acusar um
    deles, a regra está errada — e não se ajusta a referência, corrige-se a
    regra. Aqui ela pegou três de quatro.

    Sobrevive só o `00 00 01`, e por um motivo mais forte: start code dentro de
    payload não tem leitura alternativa. Nenhum quadro íntegro do filme tem um.

    **O `00 00 02` foi depois provado legítimo por experimento**, não só por
    correlação: ele está no arquivo cru do frame 2360, em posição diferente do
    patch dele, e **trocá-lo por `03` quebra o quadro**, que hoje decodifica
    perfeito. O codificador deste filme não escapa esse caso, e a norma que eu
    citava era leitura minha, não conferência.

    > **REVISTO em 2026-09-18 — a prova acima era falsa, e a classe está
    > ENCERRADA.** O 2360 não "decodificava perfeito": o `00 00 02` no byte 10
    > faz o leitor de NAL do ffmpeg **encerrar o NAL ali**, o slice acaba no MB
    > 510 e o ffmpeg oculta 7.650 macroblocos **sem erro nenhum** — o que o
    > reparador não enxergava até a armadilha 59. Trocar por `03` não quebra o
    > quadro: expõe o dado real, que vai até o MB 959. E `cabac_zero_word` vem
    > DEPOIS do fim do slice, nunca no byte 10 de um quadro de 32 KB.
    >
    > Medido no filme inteiro: **93 NALs** têm `00 00 02` (82) ou `00 00 00`
    > (11) antes do fim do dado. Restaurar o `03` em todos de uma vez: o parse
    > sobe em 15 quadros (~550 → ~1.000, até o próximo dano), cai em 14 — cinco
    > deles estavam em 8.160 só porque o corte do NAL escondia o resto (10, 12,
    > 59, 407, 1610). Nenhum completa só com isso. No frame 10 o corte
    > provavelmente mostra a imagem VERDADEIRA: é um B "tudo skip" cujos zeros
    > cortados têm bits trocados.
    >
    > **Regra final, para não reinvestigar:** a sequência é dano quase sempre (um
    > `03` com o bit 0 trocado), mas **não se aplica em lote**. Decide-se caso a
    > caso, e só com o critério de ocultação (`reparador.c` desde `3e25fec`) e a
    > curva de truncamento do `corta` — reparo real usa todo o dado do quadro.
    > Aplicado assim no 2360 (`77496469 0`), com aprovação do usuário. Detalhe em
    > `dados/alvos_ocultos.txt`.


28. **`cabac_zero_word` vem em pares — e o invariante detecta sem consertar.**
    Depois do RBSP, o codificador pode anexar palavras `0x0000`. Medido nos 161
    quadros de tarja comprovadamente correta: o número de bytes zerados no fim é
    **par em 100% deles**, sem exceção — 145 têm zero, os demais têm de 78 a
    1.320, sempre par.

    No filme inteiro, **9 frames têm exatamente 1 zero final**: 560, 894, 945,
    1119, 1444, 1586, 2023, 2769 e 3141. O último byte do NAL carrega o
    `rbsp_stop_one_bit` e não pode ser zero.

    A busca que isso abre é minúscula e bem fundada — se bit-rot zerou o byte, o
    original está a pelo menos 1 bit, e são só **8 candidatos por frame**.
    Testados os 72: **nenhum resolve**. O único que "aceita" os oito é o 1586,
    que já decodificava limpo antes.

    **O invariante detecta e não conserta.** O último byte zerado é sintoma de
    dano que continua em outro lugar do NAL. Vale como sinal — um frame nessa
    lista tem corrupção provada —, não como reparo.

29. **Anticorrelação entre campo e tarja denuncia dano global, não local.**
    Varridos 2 bits na janela `[5,60)` do frame 11 — 96.580 pares, 6 min —,
    **4.957 fazem o quadro decodificar limpo**. Nenhum presta, e o padrão diz
    por quê:

    | candidatos | campo | tarja |
    |---|---|---|
    | **3.597** | **43** (o que a rampa prevê) | **15,000** |
    | 80 | 38, 44 ou 45 | **16,000** |
    | **0** | 43 | 16,000 |

    Os dois grupos diferem por **exatamente 1 nível em tudo**. Nenhum candidato
    acerta campo e tarja ao mesmo tempo, e isso não é coincidência de busca:
    é assinatura de **deslocamento global de nível** — um DC ou um QP que move o
    quadro inteiro — em vez de dano local de macrobloco.

    **A lição de método:** quando dois gabaritos independentes discordam de forma
    sistemática ao longo de milhares de candidatos, o defeito não está no lugar
    onde se procura. Aceitar o grupo de 3.597 porque "o campo bate" seria pegar
    3.597 quadros com a tarja errada — e a tarja é gabarito conhecido a priori,
    não negociável.

    **Ter dois juízes independentes foi o que salvou.** Com só a rampa, 3.597
    candidatos passariam; com só a tarja, 80 passariam. Exigindo os dois, zero —
    que é a resposta certa.

30. **Varredura insatisfazível devolve zero, e zero parece resposta.** A
    primeira varredura de 1 bit do frame 12 devolveu **0 soluções em 9.120
    candidatos**. Não testou nada: o critério não podia ser satisfeito por
    construção nenhuma, e por **duas** razões somadas.

    | razão | por quê |
    |---|---|
    | `log_erros == 0` conta a **cadeia inteira** | o frame 11 emite duas linhas de erro em toda decodificação do GOP 0, então nenhum flip no 12 chegaria a zero |
    | `propagacao < 0.995` | o frame 12 é campo liso do fade — propagação 1,0 sempre, é a armadilha 22 |

    Com `VISUAL=0` e `ERROS_BASE=2` (os dois erros que o frame 11 já tem), a
    mesma faixa devolve 21 candidatos. O zero anterior era um zero falso.

    **O teste que separa os dois zeros custa 2 segundos:** rodar a varredura com
    o critério afrouxado num pedaço pequeno da janela. Se com folga ele aceita
    quase tudo (aqui: 257 de 280) e com o critério real aceita alguns, o
    mecanismo funciona. Se aceita zero nos dois, o critério é insatisfazível e o
    resultado não é resposta nenhuma.

    É a armadilha 25 numa forma nova: lá eu varri três horas um IDR que estava
    intacto; aqui eu quase registrei "1 bit está fechado para o frame 12" sem ter
    testado um bit sequer.

31. **Critério binário descarta a melhor pista.** A varredura de 1 bit do frame
    12 devolveu 21 "soluções", todas cópia do frame 9. O candidato mais
    informativo do arquivo inteiro — `150530 bit 4`, que decodifica os **960
    macroblocos da tarja** e só morre no primeiro macrobloco da imagem — **não
    estava entre elas**, porque ele ainda erra.

    | candidato | macrobloco alcançado | critério binário |
    |---|---|---|
    | base, sem flip | 35 | reprovado |
    | `150530 4` | **961** | reprovado |
    | as 21 "soluções" | 8160 | aprovado, e todas são cópia |

    Num slice CABAC tudo que vem antes do primeiro erro está correto, então
    "até onde chegou" é progresso real e monotônico. Sim-ou-não joga isso fora e
    ainda inverte a ordem: premia o degenerado que vira `all-skip` e descarta o
    que decodificou 960 macroblocos de verdade.

    **Onde não existe gabarito que feche o quadro, medir progresso é melhor que
    medir sucesso.** Ver `CABAC.md` e o modo `avanco`.

32. **Nota máxima para quadro que não existe.** O modo `avanco` pontuava pelo
    macrobloco do último erro, e quando o decodificador **rejeita o pacote antes
    de decodificar macrobloco nenhum** não sai linha `error while decoding MB`.
    O `log_mbx` ficava em −1 e o candidato tirava **8160** — a nota de quadro
    perfeito.

    Medido nos três bits do `slice_type` do frame 12: `serie` diz `0 de 1`, o
    quadro não sai, e o `avanco` dava 8160 nos três.

    O juiz de imagem barrava esses candidatos depois, então nenhum resultado
    saiu errado — mas a contagem de "fecham o quadro" estava inflada e a busca
    gastava etapa com eles. Corrigido exigindo `cap_w > 0`: **sem quadro na
    saída não há pontuação, e a nota vira −1.**

    A regra geral: toda métrica de progresso precisa de um piso que distinga
    "não errou" de "não chegou a tentar". Ausência de erro não é sucesso.

33. **A métrica premiava o desastre completo.** Irmã da 32, e pior: ali o quadro
    não existia; aqui ele existe e é lixo. No frame 13, **51,6% de todos os
    flips de 1 bit — 150.794 de 292.216 — tiravam 8160** e "passavam da base".

    O motivo é que o decodificador **desiste em silêncio** e devolve ocultação:
    quadro liso, sem uma única linha de erro no log. A base, que erra
    honestamente no macrobloco 6600, pontuava **menos que o desastre completo**.

    Exigir `cap_w > 0` (armadilha 32) não pega isso, porque o quadro de
    ocultação existe e tem 1920×1080.

    O piso que separa é a **tarja**, conhecida a priori em todo quadro do filme:
    ocultação não a reproduz. Com `PISO_TARJA`, os 150.794 viraram **zero** — e
    o zero é resposta, não artefato, porque o mesmo piso é satisfazível no frame
    12 já reparado, onde 9.032 candidatos passam por ele.

    **Sempre conferir que o piso é satisfazível em algum quadro conhecido antes
    de ler um zero como resposta.**

34. **Arquivo de saída compartilhado num laço faz quadro inexistente parecer
    existir.** Medi "quais quadros do GOP 0 produzem imagem" com um laço que
    escrevia todos no **mesmo** `$SB/h.yuv`. Quando o quadro não saía, o arquivo
    guardava o do quadro anterior, o teste de tamanho passava, e eu concluí que
    os 16 produziam imagem — e **retratei uma medição anterior que estava
    certa**.

    Refeito com um arquivo por quadro em diretório limpo: só **13, 16, 18 e 20**
    produzem imagem; os outros doze não. Confirmado de forma independente pelo
    `avanco`, cuja base dá **−1** para 19, 21 e 22.

    Custou duas retratações em cadeia, a segunda desfazendo a primeira. **Laço
    que mede N coisas escreve em N arquivos, e apaga antes de escrever.**

35. **`quadros == esperado` torna o `campo` insatisfazível em cadeia furada.**
    O `decodifica` só devolve 0 quando o número de quadros emitidos bate com o
    número de pacotes enviados. Na cadeia do frame 19 há quatro quadros que não
    emitem (14, 15, 17 e o próprio 19), então a condição **não pode** valer, e o
    modo `campo` descarta todos os candidatos em silêncio — devolve tabela
    vazia, que parece "nenhum candidato bom".

    É a armadilha 30 numa terceira forma. Julgar ali exige medir o YUV por fora,
    e foi o que se fez.

36. **"Completa cedo" não prova atalho — refutei o juiz de consumo, e a
    refutação estava generalizada demais.** *(corrigida no fim do item)* O frame
    13 com o candidato `184311 bit 6` fecha o quadro no byte **31.900** de
    **36.528**, e o payload dele não tem `cabac_zero_word` nenhum. Parecia prova
    de que o candidato atravessava a cauda como skip em vez de decodificá-la, e
    eu implementei o juiz inteiro em cima disso.

    **O controle obrigatório derrubou na primeira medição:**

    | quadro | completa no corte | dados reais | sem ler |
    |---|---|---|---|
    | frame 12 — **reparo verificado** | **100** | 1.135 | **91,2%** |
    | frame 13 — candidato 7 | 31.900 | 36.528 | 12,7% |

    O ffmpeg termina a slice com muito menos dado do que ela tem. O quadro que
    eu sabia estar certo é 7× mais "atalho" que o candidato que eu queria
    reprovar por atalho.

    **A regra que eu violei é a do próprio projeto:** todo juiz novo tem que ser
    rodado contra um caso comprovadamente bom antes de julgar qualquer coisa.
    Gastei a implementação inteira antes do controle — se tivesse rodado o
    controle primeiro, teria custado uma linha de comando.

    E a consequência retroativa: **a falsificação do candidato 7 por consumo não
    vale.** O que ainda pesa contra ele é outra coisa — a tarja em 14 onde devia
    ser 16, e as fileiras 62 a 67 saindo como `i` × 120, padrão que nenhum quadro
    verificado do filme mostra na tarja.

37. **Erro no log do vizinho não é dependência.** Os frames 20, 21 e 22 mostravam
    `MB 1 0, bytestream 143806` nos seus logs, e `143806` de `143870` é o tamanho
    do frame **19**. Concluí que o 19 era raiz deles e recomendei o alvo por isso.

    **Era só a cadeia:** decodificar até o frame 22 passa pelo 19, e o erro dele
    aparece no caminho. Medido com o `mapa` antes e depois de consertar o parse
    do 19: **muda um quadro só, o próprio 19.**

    O teste que separa custa duas corridas de 11 s — rodar o `mapa` com e sem o
    conserto e comparar a coluna inteira. Fiz depois de já ter escolhido o alvo
    e escrito a justificativa.

    A regra: **dependência se mede mudando a causa e olhando o efeito**, não
    lendo quem aparece junto no log.

### Correção da armadilha 36

A refutação acima vale para **quadro de campo liso**, e eu a escrevi como se
valesse para todos. Medindo a razão de consumo em IDRs de conteúdo real:

| IDR | bytes | completa em | % usado | listra |
|---|---|---|---|---|
| 2333 (reparado) | 175.857 | 175.860 | **100,0%** | 0,0% |
| 3319 (reparado) | 247.520 | 247.523 | **100,0%** | 0,0% |
| 3426 (reparado) | 58.412 | 58.415 | **100,0%** | 0,0% |
| 0 (fade preto) | 2.298 | 420 | 18,3% | 100% |
| **29 (borrado)** | 289.388 | **4.719** | **1,6%** | 71,6% |

**Quadro de conteúdo real consome 100% do payload.** O frame 12, que derrubou o
juiz, é campo chapado de fade — legitimamente barato, como o IDR 0. O erro foi
testar a regra num caso que não a representa e concluir que ela não vale em
lugar nenhum.

O `CONSUMO` do `reparador.c` é **válido para quadro de conteúdo real** e o
controle confirma: no IDR 3426, bom, 35 candidatos passam; no IDR 29, zero.

38. **Dessincronização silenciosa: analisa limpo, não é cópia, e está borrado.**
    O IDR 29 percorre os 8.160 macroblocos **sem emitir um erro sequer**, não é
    cópia de nenhum vizinho, e tem **71,6% das linhas repetindo a de cima**.

    O mecanismo: num IDR não há referência, todo macrobloco é intra, e **intra
    vertical sem resíduo copia a linha de cima** — sintaxe legal e baratíssima
    em símbolos. Ele decodifica ~1.101 macroblocos direito e depois dispara,
    atravessando os 7.000 restantes com 2 KB.

    **Escapa de todos os detectores anteriores:** não para cedo, não duplica
    vizinho, não emite erro. Só a listra com conteúdo real e a razão de consumo
    pegam. E IDR assim envenena o GOP inteiro.

39. **A listra premia ruído — "sem listra" não é "com imagem".** Encadeando bits
    no IDR 3047 eu levei a listra de **69,4% para 40,0%** e apresentei como
    progresso. A faixa de linhas 160 a 639 saiu de ~120 linhas repetidas por
    faixa para **zero**.

    **O usuário olhou e viu o que a métrica não via:** a faixa liberada está
    cheia de artefato colorido. Linha diferente da de cima satisfaz o critério
    da listra, e **lixo decodificado satisfaz trivialmente**. Troquei borrão
    limpo — do qual dá para distinguir o que é real — por ruído.

    O croma separa os três estados, e a faixa é estreita. Medido na região
    160–639 de 12 quadros verificados, de dois trechos diferentes do filme:

    | estado | desvio U | desvio V |
    |---|---|---|
    | **imagem real** | **5,99 a 8,52** | **3,40 a 7,42** |
    | borrão | 2,78 | 2,25 |
    | lixo (meus 4 bits) | **21,43** | **15,49** |

    Repetir linha **achata** a cor; lixo a **estoura**. Por isso o juiz tem que
    ser **faixa e não limiar** — e nenhuma métrica monotônica serve, porque o
    alvo está no meio e não num extremo.

    **Estado da implementação: o `PISO_CROMA` do `reparador.c` NÃO está
    filtrando.** Encadeando com ele ligado, o estado resultante mede U 23,83 —
    fora da faixa que o próprio piso exige. A calibração acima é confiável; o
    código que a aplica ainda não. Não usar até depurar.

40. **Igualdade exata não mede o borrão — mede uma coincidência frágil.** O
    `fronteira_borrao` procurava 8 linhas seguidas **byte a byte iguais** à de
    cima. Medido no IDR 3047, linhas 400 a 1070: em **100%** delas a diferença
    para a linha de cima é no máximo 1, mas só ~75% são exatamente iguais, e
    espalhadas. Uma corrida de 8 exatas é sorte; um bit de dither a quebra.

    | quadro | fronteira tol 0 | fronteira tol 1 |
    |---|---|---|
    | original | 292 | **264** |
    | candidato aprovado | 328 | **296** |
    | candidato reprovado | 692 | **360** |

    Dois quadros com o **mesmo** borrão mediam 328 e 1080. Foi assim que a
    etapa 2 devolveu **591 candidatos "sem borrão nenhum"** que, na tela, são o
    mesmo quadro listrado da base. Corrigido com `tol_copia = 1`; `TOL_COPIA=0`
    volta ao antigo para reproduzir medidas velhas.

    **E derrubou junto o critério 3 inteiro.** Com a tolerância certa o borrão é
    **um trecho único** cobrindo 99% da área abaixo da fronteira, nos três
    quadros de calibração. Os "181 trechos de maior 19" e a "fragmentação" que
    eu usava para reprovar (92 trechos, maior 11) eram artefato da igualdade
    exata. Quem reprovava aquele candidato era a blocagem, sozinha — eu tinha
    creditado a dois critérios o trabalho de um.

41. **A nota do `avanco` não vale neste alvo: 82 de 119 são dessincronização
    silenciosa.** Na etapa 2 do IDR 3047, 82 candidatos alcançaram o macrobloco
    8160 — nota máxima, "sem erro nenhum". Medida a fronteira do borrão de cada
    um: **367,5 de média**, estatisticamente idêntica à dos que só chegam ao
    2.280 (366,1). Quadro consertado não tem borrão; estes analisam os 8.160
    macroblocos sem erro **e continuam borrados**. Se o feixe escolhesse pela
    nota, a cadeia subiria num ramo falso na etapa 3 — a mesma armadilha 38, uma
    etapa antes. Neste alvo quem pontua é a fronteira, e o `BASE=-1` existe para
    tirar o macrobloco do filtro.

42. **Contagem proporcional à área não pode ter piso absoluto.** O critério 3
    exigia `trechos >= 80% dos da base`. Mas o número de trechos é proporcional
    à área ainda borrada, e essa área encolhe **exatamente quando o conserto
    avança**: densidade 0,2354 na base contra 0,2361 de média em 119
    candidatos. Um candidato que liberasse até a linha 630 teria ~106 trechos e
    seria reprovado **por ser bom demais**. Medido: com o piso absoluto, 119
    sobreviventes; sem ele, **920**. Oitocentos e um candidatos estavam sendo
    descartados por liberar imagem demais.

43. **A blocagem não pega o artefato que o olho pega — o croma da faixa pega.**
    Dos sete sobreviventes da etapa 1, os dois de **melhor** blocagem (1,194 e
    1,254 contra 1,375 do aprovado) têm respingo colorido bem na linha da
    fronteira. A blocagem é média de luma e dilui um artefato pequeno em área.
    O p99 da diferença entre pixels vizinhos de croma, medido **só na faixa
    liberada**, ordena igual ao olho:

    | | blocagem | respingo de croma |
    |---|---|---|
    | original (borrão liso) | — | 2 |
    | candidato do olho | 1,375 (pior) | **7** (melhor) |
    | os dois da blocagem | 1,194 / 1,254 | 9 / 19 |
    | descartado pelo olho | 2,306 | 14 |

    E a janela tem que ser **fixa**, não "a faixa que cada um liberou": faixa
    estreita concentra o respingo, faixa larga o dilui. Com janela própria dois
    candidatos trocam de lugar; com 64 linhas fixas a partir da base, não.

44. **O byte onde o decodificador PARA não é o byte onde o fluxo quebrou.** O
    modo `corta` cravou a parada do frame 13 entre os bytes 31.478 e 31.480, e
    eu varri aqueles 16 bytes exaustivamente até 3 bits — 341.376 combinações,
    **zero**. A resposta está no byte **30.851**, 630 bytes antes, no meio da
    fileira 54. O `corta` localiza o fim do que dá para consumir; a origem do
    dano pode estar em qualquer lugar antes dele, porque um erro de CABAC é
    detectado quando o estado fica inconsistente, não quando o bit errado é
    lido. Janela de varredura tem que cobrir a fileira inteira, não o ponto de
    parada.

45. **Critério calibrado num tipo de conteúdo reprova o alvo certo em outro.**
    A `croma_real` exige desvio entre 0,70 e 1,60, medido em quadros claros. O
    frame 13 é escuro e quase monocromático: p99 de croma **= 1 na imagem boa e
    no borrão igualmente**, desvio de 0,4 a 0,8 do topo à base. A parte
    verificadamente BOA do quadro mede 0,10 — o piso reprovaria o próprio
    conserto. Deu 0 de 8.008, e o zero não valia nada. Antes de usar um piso num
    alvo novo, medir a **parte boa daquele alvo** e ver se ela passa.

46. **"Liberar tudo" pode ser destruir.** No frame 13, os candidatos que levam a
    fronteira do borrão a 1080 — *nenhum* trecho de cópia no quadro inteiro —
    têm desvio de tarja entre **13 e 26**, contra ~2,5 dos que avançam uma
    fileira. Eles não decodificaram a tarja: transformaram-na em ruído, e ruído
    não é cópia. Em alvo que **tem** tarja, o guia do encadeamento é o desvio da
    tarja, cujo estado de chegada é zero; a fronteira do borrão leva ao ramo
    errado.

47. **A decodificação do ffmpeg NÃO é determinística com as threads padrão, e
    isso invalida comparação de pixel.** Decodificando o **mesmo arquivo duas
    vezes**, com `-threads` no padrão:

    | | |
    |---|---|
    | quadros em comum | 3.095 |
    | com pixel idêntico | **371 (12,0%)** |

    Com `-threads 1`, o mesmo controle dá **3.095 de 3.095, 100%**. A ocultação
    de erro depende do escalonamento das threads, e num arquivo com 3.236
    quadros quebrados quase toda saída passa por ocultação.

    Eu comparei duas remontagens e concluí "só 1,1% dos quadros ficam iguais"
    antes de rodar o controle. O número estava certo e não queria dizer nada.
    **Todo `framemd5`, todo `cmp` entre YUVs e toda contagem de quadro tem que
    passar por `-threads 1`**, e o controle arquivo-contra-ele-mesmo tem que
    rodar antes de qualquer comparação ser lida.

    O `reparador.c` já faz certo desde sempre — `ctx->thread_count = 1`, com o
    comentário "determinismo acima de velocidade". Então `mapa`, `serie` e
    `panorama` nunca tiveram esse problema. Quem introduziu a não-determinismo
    fui eu, medindo por fora com a linha de comando do ffmpeg em vez de usar a
    ferramenta do projeto.

48. **Contar quadros emitidos continua não medindo nada — inclusive quando eu
    mesmo escrevo isso na armadilha 1.** Medindo a remontagem com e sem os 702
    prefixos corrigidos, li "3.107 contra 2.308 quadros" como piora de 799
    quadros. Mas o critério rigoroso diz que o estado atual tem **209 quadros
    bons de 3.445**: os outros 3.236 que o ffmpeg emite são ocultação. Uma
    diferença de 800 quadros emitidos pode ser inteiramente diferença de quanta
    ocultação o decodificador resolveu fabricar, e não diz nada sobre imagem
    recuperada. A pergunta certa é sempre "quantos passam no critério", nunca
    "quantos saem".

49. **Medir no arquivo cru em vez do buffer remendado — e "descobrir" o reparo
    que já existe.** Comparando o início do frame 11 com os irmãos do fade, achei
    o prefixo AVCC errado: 133.900 para uma amostra de 2.832 bytes. Varri o
    arquivo: **702 dos 3.445 quadros assim**, com assinatura limpa de bit rot
    (1,6 bits por quadro, mediana 1). Numa região que o `avanco` nunca toca,
    porque tem `ini_k >= 5` cravado.

    Tudo verdade, e tudo já sabido. O `ESTADO.md` diz na linha 85 que os **1.338
    primeiros patches são determinísticos, "prefixos de NAL e cabeçalhos"**. Com
    o `patches.txt` aplicado, **os 3.445 prefixos estão corretos, zero divergem.**

    Eu gerei os 1.107 bits que faltavam e **anexei**. Já estavam lá. XOR duas
    vezes se cancela: apliquei o desfazimento do reparo. Daí todos os resultados
    estranhos — "274 quadros passam a analisar 100%" era lixo sendo analisado
    mais longe com o comprimento de NAL recorrompido, e "corrigir o prefixo do
    frame 10 o quebra" era desfazer o reparo dele.

    Pior: inventei uma explicação para o paradoxo — enchimento `cabac_zero_word`
    fora do NAL — e ela é **falsa**. Não existe enchimento fora do NAL em quadro
    nenhum deste arquivo. Quando a medida não fecha, a hipótese seguinte tem que
    ser testada, não narrada.

    **Toda medida sobre bytes sai do buffer remendado, nunca do MP4 cru.** O
    `reparador` faz isso sozinho; scripts em Python que abrem o arquivo direto,
    não. E o sintoma é reconhecível: um achado grande, com assinatura limpa,
    numa região que o projeto inteiro nunca varreu. Se nunca varreu e o dano é
    óbvio, a primeira hipótese não é "ninguém tinha visto" — é "já está
    consertado e eu estou olhando o lugar errado".

50. **Documento com número antigo apresentado como "hoje".** Auditando os
    documentos, seis afirmações numéricas estavam desatualizadas e todas se
    liam como estado atual:

    | onde | dizia | é |
    |---|---|---|
    | `AGENTS.md` | "treze maneiras de medir errado" / "seis maneiras" | 51 |
    | `AGENTS.md` | `verify`: 7 válidos, 4 falsos, 74 pulados | 5, 10, 479 |
    | `AGENTS.md` | modo `unico` sobre "os 71 IDRs" | 132 |
    | `ESTADO.md` | "os seguintes são reparos reais — hoje são apenas 5" | 15 |
    | `ESTADO.md` | "os nove documentos" em `docs/` | 11 |
    | `docs/IDRS.md` | `patches.txt` 1467 linhas, `deterministicos.txt` 118 | 1.832 e 479 |
    | `docs/RESULTADOS.md` | medida com o modo `report` | o modo não existe mais |

    O risco concreto não é o número errado: é **refazer reparo já feito**.
    "Hoje são apenas 5 reparos reais" convida a procurar o sexto, quando já são
    quinze — e foi assim que eu reencontrei, e desfiz, o reparo dos prefixos de
    NAL (armadilha 49).

    **Toda seção chamada "números de hoje" leva data.** E número que descreve
    estado se remede antes de ser citado, porque medir custa segundos e a
    conclusão errada custa uma sessão.

51. **"Corrigir" uma decisão explícita sem ler a justificativa dela.** Auditando
    os documentos, vi que a máquina tem 28 núcleos e que `quantas_threads()` usa
    12, e escrevi no `AGENTS.md` que corrida longa "merece `THREADS=26`
    explícito, senão metade da máquina fica parada".

    A seção 5 do `docs/PARALELIZACAO.md` já explicava que **12 é decisão**: cada
    worker carrega um `AVCodecContext` de 1920×1080 com buffers de referência, e
    saturar vira pressão de cache que come o ganho; o ganho é sublinear bem
    antes de 26; e a máquina precisa continuar usável durante corrida de meia
    hora. O default inclusive já subiu de 6 para 12 por decisão explícita.

    Aconteceu **dentro de uma auditoria cujo objetivo era remover
    inconsistências** — e o que fiz foi criar uma, contradizendo um documento do
    próprio projeto. Número que parece subutilizado geralmente é escolha de
    alguém que mediu. Antes de recomendar mudar um parâmetro, procurar a seção
    que o justifica; se não houver, aí sim é lacuna.


52. **Piso que filtra a aparência de um quadro que não está sendo mostrado.**
    Encadeando o frame 11 com `PONTUA_CONSUMO=1`, o consumo puro destruiu o
    quadro — 227 valores distintos onde deveria haver 2. Liguei o `PISO_TOPO=1`,
    que exige tarja uniforme, e a cadeia passou a manter o quadro impecável em
    todas as etapas: 2 valores, tarja 15,000 / 0,000, campo 43,00.

    **E continuava errado.** O frame 11 está 100% ocultado, então a tarja de
    saída é uniforme *por construção* — o piso aprovava tudo. Ele media a
    aparência de um quadro que o decodificador nem chegou a produzir.

    O que denunciou foi a razão **bytes por macrobloco**:

    | | macroblocos | bytes | B/mb |
    |---|---|---|---|
    | frame 8, legítimo | 8.160 | 1.220 | **0,15** |
    | frame 9, legítimo | 8.160 | 2.820 | **0,35** |
    | frame 11, base | 15 | 54 | 3,6 |
    | a cadeia de 8 bits | 62 | 1.670 | **27** |

    Consumiu 30x mais e andou 47 macroblocos, todos na fileira 0 — tarja
    chapada, que deveria custar quase nada. Estava fabricando resíduo.

    **Antes de confiar num piso, verificar se a grandeza que ele mede responde
    ao que se quer filtrar.** Aqui não respondia: a saída era ocultação em todos
    os casos, e ocultação é sempre uniforme.

53. **A métrica que se otimiza diretamente deixa de medir.** O consumo era um
    diagnóstico honesto enquanto era só lido — foi ele que mostrou que os 14
    bits "que completam" o frame 11 leem 216 de 2.832 bytes. Virou pontuação e
    passou a ser fabricado: a busca encontra bits que fazem o decodificador
    engolir bytes, que é literalmente o que foi pedido.

    A forma correta é a inversa: **maximizar macroblocos alcançados e usar o
    consumo como sanidade** — ele barra a falsa conclusão (8.160 macroblocos
    lendo 216 bytes) de um lado e a fabricação de resíduo (1.670 bytes para 62
    macroblocos) do outro. Critério de progresso e critério de sanidade não são
    intercambiáveis, e trocar um pelo outro produz exatamente o artefato que o
    outro existia para barrar.

54. **`\b` protege identificador de identificador, não código de string.**
    Renomeando as famílias do croma e da tarja (item 3 do `REFATORACAO.md`),
    usei `\btarja_des\b` — que é o cuidado certo contra casar `base_tarja_des`
    por acidente, e funcionou para isso. Mas a mesma expressão trocou o nome
    dentro de **duas strings de cabeçalho de coluna**, no `panorama` e no
    `trinca`:

    ```
    printf("frame campo_med campo_des tarja_med tarja_des listra vh\n")
    ```

    A coluna passou a se chamar `tarja_baixo_desvio` na saída. Qualquer script
    que leia a coluna pelo nome quebraria em silêncio — e eu tinha acabado de
    classificar a renomeação como "risco baixo, não mexe em lógica".

    **O arnês de regressão pegou:** 2 de 27 modos mudaram. Sem ele a mudança
    teria entrado com a justificativa de que renomear não altera comportamento.

    Renomeação por expressão regular exige uma passada separada pelas strings
    literais antes de ser considerada segura. E a lição maior: **"não mexe em
    lógica" não é o mesmo que "não muda a saída"** — o nome de uma coluna é
    contrato com quem lê.

55. **"Nenhum erro de macrobloco" não é "quadro inteiro".** O `mapa` calcula
    `mb = (log_mbx < 0) ? 8160 : ...` — se o log não trouxe erro de macrobloco,
    o quadro conta como 8160 de 8160. Só que quando o ffmpeg **rejeita o
    cabeçalho do slice** (`illegal modification_of_pic_nums_idc`,
    `cabac_init_idc overflow`, `deblocking_filter_idc out of range`…), ele nunca
    chega a decodificar macrobloco nenhum: imprime `no frame!` e não emite
    imagem. Para o `mapa`, silêncio de macrobloco = perfeito.

    Medido em 2026-09-18: dos 2.376 que o `mapa` dava como inteiros, **324 não
    emitem imagem nenhuma** e 46 emitem sobre cabeçalho inválido (o ffmpeg não
    confere o bit de alinhamento CABAC). Inteiros de verdade: 2.006. O número
    inflado estava no `RESULTADOS.md` como estado do projeto.

    Conferido em cada uso da expressão no `reparador.c`: o `worker_avanco`, a
    base do `avanco` e o `trinca` exigem `cap_w > 0` antes; o `corta` registra
    numa coluna própria se saiu quadro. O `mapa` era o único sem a guarda.

    **E a guarda óbvia estava errada da primeira vez.** Marcar "emitiu imagem"
    pelo `pts` de cada quadro recebido deu 676 sem imagem — o dobro do ffprobe.
    O `mapa` reabria o decodificador a cada GOP, e o SPS deste filme não declara
    a profundidade de reordenação (`bitstream_restriction_flag` = 0): o h264
    reaprende o atraso dos B em todo GOP e **descarta quadros no caminho**
    (2.769 imagens contra 3.107). Com um decodificador só para o filme inteiro,
    como o ffprobe faz, o conjunto sem imagem fica idêntico ao do ffprobe, 338
    de 338, e nenhum `mb_parada` de quadro com imagem muda. Quem abre o
    decodificador por GOP e conta imagens está contando o descarte da
    reordenação junto. A régua certa
    tem três partes: MB final, imagem emitida (ffprobe `frame=pkt_pos`) e
    cabeçalho válido pela norma (`-bsf:v trace_headers`).

56. **Quadro sem erro sobre referência borrada é borrão.** O critério rigoroso
    (`serie`) julga cada quadro sozinho: decodificou sem erro e a imagem não é
    propagação vertical, passa. Mas um P ou B que decodifica perfeitamente em
    cima de um IDR borrado **herda o borrão** — as listras verticais continuam
    lá, e o movimento desenha figuras por cima. Com as figuras, nenhuma linha é
    cópia exata da de cima, e o teste de propagação aprova.

    Medido em 2026-09-18: 43 dos 209 "bons" vêm depois de alguma referência
    quebrada no próprio GOP. Vistos um a um (`saidas/suspeitos_43.png`), **42
    estão listrados**; só o frame 12 é bom, porque num fade escuro a ocultação
    da referência quase não erra. Os quadros bons de verdade são 167: as três
    ilhas e o frame 12. Todos os "trechos de 3 quadros" que o `RESULTADOS.md`
    listava eram borrão.

    Apareceu consertando cabeçalhos: o frame 958 passou a decodificar e os
    frames 960 e 961 entraram no critério rigoroso — 209 → 211. A imagem dos
    dois é listrada. **Ganho no critério não é ganho de imagem enquanto o
    critério não olha a cadeia de referência.** Olhar o quadro é o que decide.

57. **Proteger os quadros bons não valida o resto do lote.** O lote de
    cabeçalhos entrou com uma condição: as imagens dos 167 quadros bons byte a
    byte iguais. Passou — e 6 correções dele estavam erradas, porque o trecho
    fica fora das ilhas. A ferramenta reconhecia IDR pelo byte NAL, o byte do
    1595 diz IDR sem ser, e os `frame_num`/POC esperados de 1596–1610 foram
    contados a partir dele. As 6 "correções" trocaram valores certos por errados,
    e os 6 quadros **perderam a imagem**.

    O sinal estava na minha própria tabela de avaliação do grupo B:
    "inteiro → sem imagem: 4" e "quebrado → sem imagem: 2". Vi e não investiguei.
    Essa é a verificação que faltava — e a primeira versão dela também estava
    errada. "Nenhum quadro pode perder a imagem no `mapa`" acusou mais 6 depois
    da remoção (48, 1516, 1690, 2412, 2466, 2717), e **nenhum tinha correção no
    próprio quadro**. O parse deles é idêntico antes e depois; o que mudou foi a
    EMISSÃO: com um decodificador para o filme inteiro e o SPS sem profundidade
    de reordenação declarada, corrigir POCs em qualquer lugar muda quais quadros
    o h264 descarta na saída (3.107 emitidos antes, 3.286 depois).

    A regra certa tem duas partes: **o parse (macrobloco alcançado) não pode
    piorar**, e quando piora por correção no próprio quadro, **olhar a imagem de
    antes**. Se era imagem real, a correção está errada. Se era borrão — como nos
    8 do alinhamento CABAC e nos 3 do grupo C que passaram de 8.160 a poucos
    macroblocos —, o 8.160 era falsa completude e a correção só tornou o ponto
    de quebra honesto.

    E a lição de fundo, a mesma da armadilha 49 em outra roupa: o byte NAL também
    sofre bit-rot. Quem é IDR se decide pelo que os quadros seguintes fazem —
    reiniciam a sequência ou continuam a anterior —, não pelo byte.

58. **O arnes de regressão escrevia na fonte de verdade — e media saída
    cortada.** Dois defeitos no `ferramentas/regressao.sh`, achados no mesmo dia:

    - O caso `repair 2360 2361 256` recebia o `patches.txt` VERDADEIRO, e o modo
      `repair` **grava** o que acha. Enquanto o 2361 decodificava "limpo", nunca
      achava nada. Quando o 2361 passou a quebrar (cabeçalho corrigido), uma
      regravação achou `77529030 2` e o escreveu no `patches.txt` — e a linha
      entrou num commit sem aprovação, porque o commit veio logo depois. Removida
      no mesmo dia. Agora cada caso recebe uma cópia, e o arnes aborta se o
      `patches.txt` mudar durante a corrida.
    - Quatro casos (`varrek`, `vizinho`, `varre2`, `report`) passavam dos 300 s
      do tempo-limite. O `timeout` cortava a saída e o hash era do pedaço que
      tinha saído — estável enquanto o corte caía no mesmo lugar, instável com a
      máquina carregada. Três deles **nunca** foram testados de verdade. Agora
      estouro de tempo é falha explícita; `varrek` e `vizinho` ganharam casos
      pequenos, `varre2` e `report` saíram (não há caso pequeno que os exercite).

    Lição: ferramenta de verificação também precisa ser verificada — conferir o
    código de retorno, e nunca dar a ela escrita sobre o que ela verifica.

59. **Sem erro de macrobloco e com os 8.160 "decodificados" — e metade é
    ocultação.** O CABAC pode ler `end_of_slice_flag` = 1 antes do último
    macrobloco. A norma permite vários slices por quadro, então o ffmpeg trata
    como "este slice acabou", **não reporta erro** e oculta o resto no fim do
    quadro. Só a mensagem `concealing N DC` revela — e o `mapa`, o `repair` e o
    critério rigoroso não a olham.

    Apareceu avaliando o bit que o `repair` achou para o 2361 (`77529030 2`):
    com ele o quadro "decodifica limpo", mas o ffmpeg oculta 7.108 macroblocos —
    o bit fabrica um fim de slice alguns MBs depois do ponto onde hoje há erro
    (989). Rejeitado. E a mesma medida achou o defeito em quadros contados como
    bons: **2359 (3.339 MBs ocultados), 2360 (7.650) e 3442 (1.037)**, além do
    2361. Parecem perfeitos porque a ocultação copia de vizinhos bons (ou de um
    quadro quase preto, no fade final do 3442).

    Medir com `python ferramentas/ocultacao.py <scratch> patches.txt <gop>...`:
    cada GOP vira um stream Annex B e o `-debug pict` do ffmpeg dá uma linha por
    slice, com a ocultação logo depois. Cuidado: quadro com cabeçalho rejeitado
    não tem linha, e a atribuição por ordem desalinha depois dele — o script
    avisa comparando o número de linhas com o de quadros.

    Busca de reparo que aceita "decodifica limpo" vai achar esse atalho — foi o
    que o reparo antigo do 2361 achou, e o `repair` achou de novo. O critério
    certo é **zero macroblocos ocultados**.

    **Implementado no `reparador.c` no mesmo dia.** A raiz: o `meu_log`
    descartava tudo acima do nível ERROR, e o `concealing N DC` sai em INFO — o
    reparador nunca viu ocultação (o `log_ocultados` do `acha_consumo` sempre
    valeu −1, inclusive com `TRACO=1`). Agora o `decodifica` guarda os MBs
    ocultados do alvo e exige zero; `ACEITA_OCULTO=1` volta ao critério antigo.
    As exceções do `serie` (tarja 16, repintura) também exigem zero. E o
    "macrobloco alcançado" das buscas (`avanco`, `trinca`, `corta`) passou a ser
    `8160 − ocultados` quando o slice acaba cedo sem erro — antes era 8.160, nota
    máxima para o atalho.

    Efeito medido: o `serie` passa a dar **162**, subconjunto exato dos 167
    visualmente bons (saem 12, 2359, 2360, 2361, 3442 — e o **1683**, o IDR
    listrado que era o último falso positivo). No modo `idr`, as "soluções" de
    1 bit dos IDRs 0 e 29 somem: eram o mesmo atalho, e a do IDR 0 o RASTREIO já
    dava como reprovada pela tarja. A regressão com `ACEITA_OCULTO=1` bate com a
    referência antiga em todos os casos que não usam a contagem nova.

60. **Imagem com a ocultação do ffmpeg ligada mente sobre ONDE o dano
    começa.** No IDR 1773 eu comparei o quadro com e sem o bit `59363305 b7`
    decodificado do jeito normal e concluí que o bit "só recupera em parte" a
    fileira 57. Errado: o ffmpeg reconhece o erro muito depois do bit e a
    ocultação repinta também macroblocos **antes** do ponto do erro — e o erro
    cai em lugares diferentes com e sem o bit, então as duas imagens têm trechos
    repintados diferentes. O deblocking ainda espalha a diferença 3 pixels para
    os vizinhos de cima e da esquerda (a comparação direta apontava o MB 6823,
    uma fileira acima do bit).

    **Para localizar por imagem, decodificar o quadro sozinho com
    `-ec 0 -skip_loop_filter all`**: sem ocultação e sem deblocking, o primeiro
    MB que muda entre duas versões é exatamente onde o bit age. Medido: o bit
    acima age no **MB 6.939** e o segundo do par (`59363485 b0`) no **7.046** —
    e a curva do `corta` com o 1º bit aplicado dá o byte 32.900 no MB 7.046,
    batendo com o byte 32.897 do 2º bit. As duas réguas concordam.

61. **Tarja com pixel certo não tem sintaxe certa.** O modelo da tarja do
    plano 2 assume que todo MB dela é `I16x16, DC, croma DC, cbp 0`, e a imagem
    parece confirmar: 16,00 em todos os pixels. No IDR íntegro 3348 a coluna 1
    de todas as fileiras de tarja usa croma **modo 2** — mesmo pixel, outra
    sintaxe, escolhida pelo encoder por empate e herdada pela coluna. O modelo
    fica a 23 bits dele desde a fileira 63, e desde a 66 acha **1 bit
    "errado" num quadro sem dano**. Eu li as 26–28 divergências do 1773 como
    dano denso na cauda; eram, com alta probabilidade, isto. Pixel não
    valida sintaxe: validar o modelo contra o JM, e desconfiar de encaixe curto
    (só as últimas fileiras) em quadro que não encaixa desde a fileira 63.
