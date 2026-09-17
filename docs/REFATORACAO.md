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

### `blocagem` x `blocagem_faixa`

Mesma ideia — degrau na grade 16x16 contra o interior — implementações
diferentes:

| | `blocagem` | `blocagem_faixa` |
|---|---|---|
| amostragem | todo pixel, eixos X e Y | 1 em 4 pixels, só X, linhas alternadas |
| interior | só `x % 16 == 8` | tudo que não é borda |
| valor em caso de falha | **0** | **99,0** |

A última linha é a armadilha: **0 e 99 são veredictos opostos**. Quem ler
"blocagem 0" de uma pensando na outra conclui o contrário da medida. Unificar
numa função com faixa opcional e **uma** convenção de falha.

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

**Por que não dá hoje:** o libavcodec não expõe checkpoint de estado do
decodificador. Não há como salvar o estado depois do quadro 10 e restaurá-lo
893.200 vezes — `avcodec_flush_buffers` não devolve as referências. Fazer isso
exigiria um analisador CABAC próprio, e aí o libavcodec deixaria de ser o
oráculo independente, que é o alicerce de confiança deste projeto. Não vale o
risco.

### O que dá para fazer, e é medível

1. **Tirar a tabela de combinações da memória.** `gera_combos` materializa
   todas: hoje 893.200 x 3 ints = 10,7 MB, tolerável. Mas 4 bits numa janela de
   240 bits dá **141 milhões de combinações = 2,2 GB de tabela mais 565 MB de
   placar**. A k-ésima combinação se calcula do índice pelo sistema numérico
   combinatório, sem tabela. Remove o teto e melhora o cache.

2. **Medir onde o tempo vai antes de otimizar.** O custo por candidato é a
   decodificação de `ancora..alvo`. Quanto disso é prefixo repetido ainda não
   foi medido, e medir vem antes de mexer.

3. **`THREADS` fica em 12** — decisão medida, seção 5 do `PARALELIZACAO.md`.
   Não é aqui que se ganha.

## 5. Ordem sugerida

Da menor para a maior chance de quebrar coisa:

| # | item | risco | ganho |
|---|---|---|---|
| 1 | tabela de modos e validação de `argc` | baixo | mata o `report` silencioso e o `argv[5]` nulo |
| 2 | unificar `blocagem` numa convenção só | baixo | tira a armadilha do 0 x 99 |
| 3 | renomear as famílias do croma e da tarja | baixo | o nome passa a dizer o que mede |
| 4 | motor único de varredura em `src/workers.c` | **alto** | menos ~400 linhas, e worker novo deixa de ser copiar-e-colar |
| 5 | combinações sem tabela | médio | remove o teto de memória para 4 bits |

O item 4 é o que mais encolhe o arquivo e o que mais pode quebrar. Só entra com
a prova de saída idêntica, modo a modo.

## O que não mexer

- `ctx->thread_count = 1` — determinismo, seção 1 do `PARALELIZACAO.md`.
- A regra do menor índice na parada antecipada — seção 7 do `AGENTS.md`.
- `weighted_pred_flag` fica em 1.
