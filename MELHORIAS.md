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

### Pendente e estrutural: o modelo de cadeia não serve para todo GOP

**Não implementar sem decidir explicitamente — muda o coração da ferramenta e
todo resultado histórico do projeto foi medido com o modelo atual.**

O `decodifica(ancora, alvo)` alimenta o decoder com os pacotes de `ancora` até
`alvo` e exige `quadros == esperado`. No GOP 0 isso trava no frame 2, com o
decoder dizendo `co located POCs unavailable`: aquele quadro precisa de
contexto que só existe quando o GOP inteiro é decodificado.

| | frames emitidos em 0–28 |
|---|---|
| `panorama` (GOP inteiro numa passada) | 0–9, 11, 12, 13, 16, 18, 20 |
| cadeia `0..alvo` | **só 0 e 1** |

**Consequência:** varredura em qualquer frame do GOP 0 depois do 1 devolve zero
por construção, e esse zero não significa "sem solução". Três corridas minhas
nos frames 10, 11 e 13 foram invalidadas assim.

E separa dano de artefato: dos frames que o critério reprova no GOP 0, só
**10, 14, 15, 17, 19 e 21–28** não são emitidos nem na passada completa. Os
demais estão bons e falham pelo modelo.

O conserto seria um critério que decodifica o GOP inteiro e confere se o quadro
alvo saiu e está correto — que é o que o `panorama` já faz. Custa uma passada
de GOP por candidato em vez de uma cadeia, o que para GOP de 29 frames é mais
caro, não menos.

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
