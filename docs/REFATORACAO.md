# Plano de refatoração do `reparador.c`

Medido em 2026-09-17 sobre 3.004 linhas. **Nada aqui foi executado** — é plano,
e cada item traz o critério de aceitação, porque refatoração sem critério é como
varredura sem juiz.

## O retrato

| | |
|---|---|
| linhas do arquivo | 3.004 |
| linhas de `main()` | **1.212** |
| linhas nos 24 blocos de modo dentro do `main` | **1.152** |
| funções `worker_*` | **9**, espalhadas entre as linhas 574 e 1668 |

Ou seja: **40% do arquivo é uma função só**, e ela é uma cadeia de 24 `else if`.

## 1. Separar os workers do código principal

Os 9 workers repetem o mesmo esqueleto. Medido contando o padrão dentro dos
blocos `worker_*`:

| padrão | em quantos workers |
|---|---|
| `cap_buf = malloc(...)` | **9 de 9** |
| `memcpy(copia, arq ...)` | **9 de 9** |
| `free(copia)` | **9 de 9** |
| contador atômico para puxar índice | 9 de 9 |

O ciclo é sempre o mesmo: aloca buffers próprios, puxa índice do contador
atômico, aplica flip na cópia do NAL, decodifica, pontua, devolve a cópia ao
estado anterior.

**Proposta.** Um `src/workers.c` com um motor único:

    typedef struct {
        int alvo, ancora;
        long n_tarefas;
        int (*avalia)(long tarefa, uint8_t *nal, int len);
        int *placar;
    } Varredura;

    void varre(Varredura *v, int nthreads);

Cada modo passa só o `avalia`. O que hoje é um worker de 60 linhas vira uma
função de 10 que não sabe o que é pthread.

**Critério de aceitação, e não é opcional.** A seção 7 do `AGENTS.md` já manda
comparar `THREADS=1` com o default. Aqui vale a versão forte: antes e depois da
refatoração, a saída de cada modo tem que ser **byte a byte idêntica**, mesmo
SHA-256, em pelo menos um caso por modo. Sem isso não entra.

## 2. Duplicatas e quase-duplicatas

Não há função repetida literalmente. Há algo pior: **famílias parecidas com
convenções incompatíveis**.

### `blocagem` x `blocagem_faixa` — as duas ficam

A primeira versão deste plano propunha unificá-las. **Está errado**, e os usos
mostram por quê:

| | `blocagem` | `blocagem_faixa` |
|---|---|---|
| usos | **6** — métrica do `varre_par`, `cresce`, `oraculo`, `dump`, `report` | **2** — só o juiz da trinca |
| região | quadro inteiro | faixa entre duas linhas |
| amostragem | todo pixel, eixos X e Y | 1 em 4 pixels, só X, linhas alternadas |
| interior | só `x % 16 == 8` | tudo que não é borda |
| calibração documentada | valores 0,80 / 1,04 / 1,429 no `CRITERIOS.md` | limiar **1,45** na faixa liberada |
| valor em caso de falha | **0** | **99,0** |

São medidas diferentes com calibrações próprias, e cada calibração foi obtida
com aquela amostragem. **Unificar invalidaria as duas de uma vez** — e o juiz da
trinca já foi reescrito três vezes por causa de medida trocada de lugar
(armadilhas 40 a 43). Não se mexe em função calibrada sem recalibrar, e
recalibrar aqui não compra nada.

**O que fazer em vez disso:** só documentar o contrato nas duas, porque a
diferença que engana é a última linha — **0 e 99 são veredictos opostos**. Quem
ler "blocagem 0" de uma pensando na outra conclui o contrário da medida. Um
comentário em cada assinatura dizendo região, amostragem e o que significa o
valor de falha. Zero risco, e resolve o que era o problema de verdade.

### A família do croma — cinco funções

`croma_real`, `croma_respingo`, `croma_dp`, `croma_stat`, `croma_sao`. Medem
coisas diferentes e legítimas — variação dentro do macrobloco, salto entre
pixels vizinhos, desvio do plano — mas o nome não diz qual, e eu já me confundi
entre `croma_real` e `croma_dp` nesta sessão. Renomear pelo que medem:
`croma_var_mb`, `croma_salto_p99`, `croma_desvio_plano`.

### A família da tarja — quatro funções

`tarja_perfeita` (uniforme **e** 16), `base_uniforme` (uniforme, valor livre),
`topo_uniforme` (com `piso_topo >= 2` exigindo 16) e `tarja_des` (desvio). Três
percorrem a mesma região com o mesmo laço. Uma `tarja(regiao, &media, &desvio)`
mais predicados finos resolve.

## 3. Tabela de modos — e um bug que ela mata

A cadeia de 24 `else if (!strcmp(modo, ...))` termina num `else` **sem
comparação**, na linha 2977. Esse `else` é o modo `report`.

**Consequência: erro de digitação no nome do modo roda um relatório do filme
inteiro em silêncio** — 3.445 decodificações — em vez de dizer "modo
desconhecido". `report` nem aparece num `strcmp`.

E cada bloco lê `argv[5]`, `argv[6]`, `argv[7]` por conta própria: são **69 usos
de `argv`** e a única validação global é `argc < 4`. O modo `repair` chamado sem
argumentos faz `atoi(argv[5])` com `argv[5]` nulo.

**Proposta.** Tabela, não cadeia:

    typedef struct {
        const char *nome;
        int min_args;
        const char *uso;
        int (*executa)(int argc, char **argv);
    } Modo;
    static const Modo MODOS[] = { ... };

O despacho vira busca na tabela, e nome desconhecido imprime o uso e devolve
erro. C não faz `switch` sobre string, então a tabela é a forma correta — e é
melhor que `switch`, porque carrega o `min_args` e o texto de uso junto.

**Ganho colateral:** o texto de uso hoje é uma string gigante no começo do
`main`, longe dos modos, e já cita `report` como se fosse modo próprio. Na
tabela, uso e implementação ficam lado a lado e não dessincronizam.

## 4. Quicksort nas varreduras — a resposta honesta

**Quicksort não se aplica.** Varredura exaustiva não tem o que ordenar: todo
candidato precisa ser testado, a ordem não muda o total, e não existe comparação
entre candidatos antes de medir. Ordenar 893.200 combinações antes de testá-las
só acrescenta trabalho.

**Mas a intuição por trás da pergunta está certa**, e existe uma versão real
dela — que é, aliás, uma ordenação:

> Num slice CABAC tudo antes do primeiro bit alterado decodifica **idêntico**.
> Dois candidatos que só diferem depois do byte B compartilham todo o trabalho
> até B. Ordenando os candidatos pelo **menor offset alterado**, dava para
> decodificar o prefixo uma vez e ramificar — uma busca em profundidade sobre a
> árvore de prefixos, em vez de uma varredura plana.

O ganho seria grande: na varredura de 3 bits do frame 11, **cada candidato
redecodifica os 12 quadros da cadeia desde a âncora**, e os 11 primeiros são
idênticos nos 893.200 testes.

**E está DESCARTADO.** O libavcodec não expõe checkpoint de estado do
decodificador — não há como salvar o estado depois do quadro 10 e restaurá-lo
893.200 vezes, e `avcodec_flush_buffers` não devolve as referências. Fazer isso
exigiria um analisador CABAC próprio, e aí o libavcodec deixaria de ser o
oráculo independente, que é o alicerce de confiança deste projeto. **Decidido
não fazer.** Fica escrito aqui para ninguém reabrir a ideia achando que é ganho
fácil: é ganho grande, com preço que o projeto não pode pagar.

### O que sobra

**`gera_combos` fica como está.** Cogitei calcular a k-ésima combinação pelo
índice para tirar a tabela da memória — 893.200 x 3 ints são 10,7 MB hoje, e 4
bits numa janela de 240 dariam 2,2 GB. **Decidido não fazer:** a profundidade
máxima é 4 por construção, as janelas que importam são estreitas, e trocar um
`malloc` que funciona por aritmética combinatória é risco de erro de índice sem
ganho no relógio.

**`THREADS` fica em 12** — decisão medida, seção 5 do `PARALELIZACAO.md`. Não é
aqui que se ganha.

Ou seja: **nesta frente não há nada a fazer.** As varreduras ficam como estão, e
o ganho de tempo, se vier, vem de reduzir o número de candidatos com juiz melhor
— não de reescrever o motor.

## 4b. Separar os juízes num arquivo — vale, e por um motivo melhor que arrumação

Medido o acoplamento das 19 funções de medida e juízo:

| grupo | quantas | do que dependem |
|---|---|---|
| **puras** — só os argumentos | **11** | nada. `blocagem`, `propagacao`, `linhas_reais`, `linhas_identicas`, `sem_imagem`, `fronteira_borrao`, `estrutura_borrao`, `blocagem_faixa`, `tarja_des`, `base_uniforme`, `tarja_perfeita` |
| leem a captura | 5 | `cap_u`, `cap_v`, `cap_w`, `cap_h` — toda a família do croma |
| leem uma flag | 2 | `linha_copia` lê `tol_copia`; `topo_uniforme` lê `piso_topo` |
| lê tudo | 1 | `trinca_ok`: captura, `base_*`, flags e `img_base` |

**Onze das dezenove já são puras.** Movem de arquivo sem uma linha de mudança.
As cinco do croma leem a captura por variável global em vez de receber por
argumento — virar puras é acrescentar parâmetros, mudança mecânica.

### O motivo que justifica de verdade

Não é o tamanho do arquivo. É que **medida pura se testa sem decodificador**.

Hoje, para conferir o que `fronteira_borrao` realmente faz, é preciso
decodificar um quadro real e olhar. Foi exatamente essa fricção que deixou
passar as armadilhas 40 a 43 — todas da forma "a medida mede outra coisa, e eu
só descobri depois de horas". A armadilha 40 (igualdade exata é frágil) morreria
em segundos com um buffer sintético de listras que derivam de 1 por linha.

Com `src/medidas.c` puro, um `src/testes_medidas.c` monta quadros sintéticos com
estrutura conhecida — borrão de N linhas a partir da linha L, faixa com salto de
croma de valor V — e afirma o resultado. **É a única parte deste projeto que dá
para testar sem oráculo**, porque a resposta é conhecida por construção.

### O que fica de fora

`trinca_ok` **não é medida, é política**: 87 linhas que compõem cinco medidas
com limiares, estado da base e flags. Vai junto com os `piso_*`, o `base_*` e a
linha de pontuação — não com as medidas puras. Misturar as duas camadas é o que
torna o juiz difícil de auditar hoje.

E a linha de pontuação merece virar função. Hoje ela mora dentro do
`worker_avanco`, e **por isso os pisos só funcionam no modo `avanco`** — quem
passar `PISO_TARJA` para o `varrek` ou o `cresce` não recebe erro, recebe
silêncio. Extraída como `nota_do_quadro()`, passa a valer em qualquer modo, e o
fato de hoje não valer fica visível.

### O custo

O programa é **um arquivo só** hoje, compilado por uma linha de `gcc` no
`ESTADO.md`. Separar exige cabeçalho e mudar a linha de compilação — ou um
`Makefile`, que o projeto não tem. É custo real, pequeno, e paga-se na primeira
vez que uma medida for testada em vez de conferida no olho.

**Critério de aceitação:** o mesmo dos workers — saída byte a byte idêntica,
mesmo SHA-256, modo a modo. E a mudança de assinatura do croma tem que ser
**mecânica**: passar o que hoje é lido de global, sem tocar em amostragem nem em
limiar. Qualquer alteração de amostragem invalida calibração.

## 5. Ordem sugerida

Da menor para a maior chance de quebrar coisa:

| # | item | risco | ganho |
|---|---|---|---|
| 1 | tabela de modos e validação de `argc` | baixo | mata o `report` silencioso e o `argv[5]` nulo |
| 2 | documentar o contrato das duas `blocagem` | **nenhum** | tira a armadilha do 0 x 99 sem tocar em calibração |
| 3 | renomear as famílias do croma e da tarja | baixo | o nome passa a dizer o que mede |
| 4 | `src/medidas.c` com as 11 puras + as 5 do croma | **baixo** | abre teste sem decodificador — é o item de maior retorno por risco |
| 5 | `nota_do_quadro()` fora do `worker_avanco` | médio | os pisos passam a valer em todo modo, não só no `avanco` |
| 6 | motor único de varredura em `src/workers.c` | **alto** | menos ~400 linhas, e worker novo deixa de ser copiar-e-colar |

O item 6 é o que mais encolhe o arquivo e o que mais pode quebrar. Só entra com
a prova de saída idêntica, modo a modo.

**Descartados, e o registro fica para ninguém reabrir:** unificar as duas
`blocagem` (invalidaria duas calibrações), analisador CABAC próprio (tiraria do
libavcodec o papel de oráculo) e tirar `gera_combos` da memória (risco de erro
de índice sem ganho no relógio).

## O que não mexer

- `ctx->thread_count = 1` — determinismo, seção 1 do `PARALELIZACAO.md`.
- A regra do menor índice na parada antecipada — seção 7 do `AGENTS.md`.
- `weighted_pred_flag` fica em 1.
