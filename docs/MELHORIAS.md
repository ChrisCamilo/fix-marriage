# Melhorias no reparador

O que foi feito no `reparador.c`, o que está pendente e o que foi descartado
depois de medido.

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

### RETRATADA: "o modelo de cadeia não serve para todo GOP"

**Esta seção afirmava um defeito que não existe. Fica registrada em vez de
apagada, porque o erro é instrutivo.**

Eu havia concluído que o `decodifica(ancora, alvo)` não conseguia validar o GOP
0: o frame 2 morria com `co located POCs unavailable` e o `panorama`, que
decodifica o GOP inteiro, o emitia sem problema. Propus reformar o critério.

**Estava errado.** Com o `patches.txt` puro o frame 2 decodifica **limpo** em
`FOLGA=0`, e os frames 0 a 9 também. O modelo de cadeia funciona.

Quem quebrava a cadeia era **um bit que eu mesmo tinha adicionado** para
"destravá-la" — um dos 15.537 do IDR 0 que produzem imagem byte a byte
idêntica ao original. A imagem é idêntica; o **bitstream não é**, e o estado que
ele deixa no decoder impede os quadros seguintes.

Três varreduras nos frames 10, 11 e 13 foram invalidadas por isso, e eu
atribuí o zero delas ao modelo em vez de ao meu próprio patch.

O `FOLGA=n` foi implementado antes da retratação e **fica no código**, em 0 por
padrão: é inofensivo, custou pouco, e serve se algum dia aparecer um GOP que
precise mesmo. Testado de 1 a 26 no GOP 0, não muda nada — o que confirma que
não havia problema de lookahead.

### Estudo de velocidade da varredura — medido, não estimado

Com o `panorama` dando o custo puro de decodificação (**4,45 ms por quadro,
1 thread**), a decomposição fecha:

| alvo | cadeia | ms/candidato | composição |
|---|---|---|---|
| IDR 0 | 1 | 5,25 | 4,45 decode + **0,80 montar o decoder** |
| frame 3443 | 17 | 73,41 | 17 × 4,45 = 75,6 — **a cadeia é tudo** |

**O que dá ganho, em ordem de tamanho:**

1. **Escolher alvo de cadeia curta.** Medido: 2.284 cand/s num IDR contra
   **163 cand/s** num frame de cadeia 17. **14x, e é de graça** — só depende de
   qual alvo se escolhe.

2. **Não redecodificar o prefixo da cadeia** (a melhoria 2 deste arquivo).
   Valeria até 94% num alvo de cadeia 17. **Mas provavelmente não é
   implementável:** a libavcodec não expõe snapshot nem clone do estado do
   decoder, e o prefixo termina com o DPB carregado de quadros de referência que
   não há como reinjetar. Antes de tentar, confirmar se existe API para isso —
   se não existir, a melhoria 2 deve ser marcada como descartada em vez de
   pendente.

3. **Reusar o `AVCodecContext`** com `flush_buffers` em vez de
   `free_context` + `alloc` + `open2` por candidato. Vale os **0,80 ms**, ou
   seja **15% num IDR e ~1% num alvo de cadeia longa**. Exige revalidar
   `THREADS=1` contra o default, porque o motivo de estar assim é determinismo.

4. **Reduzir o número de candidatos em vez do custo de cada um.** É o que o
   `molde_idr.py` faz: trocou busca por aritmética e fechou 130 de 131
   cabeçalhos sem decodificar nada.

**Um efeito colateral que ninguém esperaria:** consertar cabeçalho deixa a
varredura **mais lenta**. O IDR 3290 rodava a 34.000 cand/s quando morria no
byte 10; depois da correção de cabeçalho os candidatos passaram a decodificar
de verdade e a taxa caiu para **1.747 cand/s**, 20x. Qualquer estimativa de
tempo medida antes de uma correção de cabeçalho está otimista demais.

### Descartada

5. ~~**Busca de 2 bits**~~ — implementada e testada: 0 soluções em todas as
   corridas. E o `INVESTIGACOES.md` mostra que a premissa estava errada de qualquer forma.

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

# Planos de ação vindos do CABAC.md

Seis lacunas entre o que a norma permite explorar e o que o `reparador.c` faz
hoje. A 1 já está implementada; as outras estão registradas e **não**
implementadas.

## 1. Busca por etapas, pontuando por macrobloco — FEITO, falta rodar

Modo `avanco <alvo> <k> [ini] [fim] [saida]`. Pontua cada candidato pelo
endereço linear do macrobloco onde o alvo parou (`mb_y * 120 + mb_x`, 8160 =
não errou), em vez de sim-ou-não.

Medido no frame 12, 1 bit, `[5,1145)`, 37 s:

| etapa | base | combinações que avançam | melhor |
|---|---|---|---|
| inicial | mb 35 | 62 | 8160 |
| com `150530 4` fixo | **mb 961** | **318** | 8160 |

O `150530 4` decodifica a tarja inteira e morre no primeiro macrobloco da
imagem. Ele não aparecia no critério binário.

**Custo: 37 s por etapa.** Contra 45h28 da varredura exaustiva de 2 bits na
mesma faixa. Falta: rodar as etapas encadeadas e julgar cada patamar pela rampa
(campo 40) e pelo hash, para não subir num degenerado `all-skip`.

## 2. A tarja é gabarito de SÍMBOLO, não só de pixel — não implementado

A imagem começa na linha 130, então as fileiras de macrobloco **0 a 7 são só
tarja**, 960 macroblocos cujo conteúdo é conhecido a priori.

**Correção sobre a justificativa.** Escrevi antes que os 960 "têm que sair como
skip com resíduo zero". Isso é forte demais: o comentário do `reparador.c` já
registrava desvio de até 0,40 no fundo da tarja em 144 quadros genuínos, ou
seja, parte dos macroblocos da tarja carrega resíduo e não é skip puro. Medido
nos 161 quadros de tarja 16,000 do panorama, o desvio dá 0,000 em todos — então
a tarja é chapada na esmagadora maioria, mas "todos skip" não está provado e não
pode ser usado como regra.

O que **está** provado e basta: quem para antes do macrobloco 960 parou dentro
de uma região cujo conteúdo é conhecido, e portanto avançou menos que quem
chega na imagem. A porta é filtro de **progresso**, não prova de sintaxe.

Uso imediato e de graça: **porta dura de `macrobloco >= 960`**. Candidato que
para antes disso errou dentro da tarja, e a tarja não tem o que errar. Descarta
sem olhar imagem nenhuma.

## 3. Traço de macrobloco contra o gêmeo — não implementado

Os frames 8 e 12 emitem a mesma sequência de símbolos (90% dos bytes iguais,
nenhum byte discordante com 4+ bits). Decodificar os dois com
`ctx->debug = FF_DEBUG_MB_TYPE` e comparar macrobloco a macrobloco: o primeiro
em que divergem limita o dano a uma janela de bytes.

Custo: ~30 linhas e duas decodificações. Ressalva: os cabeçalhos diferem
legitimamente (poc, referências), então parte da divergência é esperada e o
juiz tem que ser o *tipo* do macrobloco, não o vetor.

## 4. Prefixo válido: quadro parcial não vale zero — não implementado

Se não há ressincronização, tudo antes do primeiro erro está certo. Um quadro
que decodifica 7.000 dos 8.160 macroblocos está **86% recuperado**, e o
`reparador` pontua ele igual a um quadro totalmente perdido.

Para os 168 quadros quebrados do filme isso pode valer mais que qualquer
varredura: em vez de exigir o quadro inteiro, aproveitar o prefixo correto e
ocultar só a cauda. Entra no `remontar.py` como ocultação parcial, registrada.

## 5. `codIOffset` inicial e o fim do cabeçalho — não implementado

A norma proíbe o `codIOffset` inicial valer 510 ou 511. São os 9 bits logo
depois do alinhamento de byte que fecha o cabeçalho do slice.

Vale pouco como filtro (9 bits), mas vale muito como **medida do comprimento do
cabeçalho**: varrer as posições de alinhamento possíveis e ver qual produz slice
decodificável localiza o fim do cabeçalho sem precisar parsear a
`ref_pic_list_modification`, que é onde o parser em python trava nos frames 7, 9
e 11.

## 6. O `patches.txt` não expressa mudança de comprimento — limitação estrutural

O formato é `offset bit`, um XOR. Ele **não consegue representar inserção nem
remoção de bit**. Todo campo de comprimento variável cujo conserto mude o número
de bits — `slice_qp_delta`, `num_ref_idx_override`, entradas da tabela de pesos —
está fora de alcance por construção, e as tentativas registradas no `RASTREIO.md`
falharam por isso, não por o valor estar errado.

Não é para mudar agora: o `patches.txt` é fonte de verdade e append-only. Mas
fica registrado que **a ausência de solução nesses campos é da representação, e
não do arquivo**.
