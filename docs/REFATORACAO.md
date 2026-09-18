# Plano de refatoração do `reparador.c`

Medido em 2026-09-17 sobre 3.004 linhas. Cada item traz o critério de aceitação,
porque refatoração sem critério é como varredura sem juiz.

**Feito:** o arnês de regressão (`ferramentas/regressao.sh`) e o item 1, a tabela
de modos. O resto é plano.

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

Uma duplicação literal (seção 4b) e, pior que ela, **famílias parecidas com
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

### A família da tarja — quatro funções, e duas são a mesma

`tarja_perfeita` e `base_uniforme` são idênticas exceto pelo valor de
referência. Esta é a única duplicação literal do arquivo, e o conserto está na
seção 4b — a primitiva `regiao_uniforme`.

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

## 4b. `src/juizes.c` — todos os juízes num arquivo só

Mesma forma do `src/workers.c`: um arquivo com **as 19 funções de medida e
juízo**, da `blocagem` à `trinca_ok`, mais os `piso_*`, o estado da base e a
linha de pontuação. Tudo que decide se um candidato presta sai do
`reparador.c`.

### O que há de comum — e aqui há duplicação de verdade

Duas primitivas absorvem **6 das 19**.

**Primitiva 1 — região uniforme.** `base_uniforme` e `tarja_perfeita` são a
mesma função, caractere por caractere, exceto pelo valor de referência:

| | região | passo | referência |
|---|---|---|---|
| `base_uniforme` | 962–1080 | `x += 8` | `Y[962*w]`, o primeiro pixel |
| `tarja_perfeita` | 962–1080 | `x += 8` | literal `16` |
| `topo_uniforme` | 0–124 | `x += 8` | `Y[0]`, ou `16` se `piso_topo >= 2` |

Mesmo laço, mesma guarda `w < 1920 || h < 1080`, mesmo retorno. Colapsam em:

    static int regiao_uniforme(const uint8_t *Y, int w, int h,
                               int y0, int y1, int valor);   /* valor < 0 = livre */

E as três viram uma linha cada. Ganho extra: o `piso_topo >= 2` sai de dentro da
medida e vai para o chamador, que é onde política pertence — hoje uma função
chamada `topo_uniforme` consulta uma variável de ambiente por dentro.

**Primitiva 2 — média e desvio de uma região.** `tarja_des`, `croma_dp` e
`croma_stat` acumulam `s` e `s2` no mesmo formato e tiram `sqrt(s2/n - m²)`. A
única diferença é o plano (Y, U ou V) e o passo:

    static void estatistica(const uint8_t *P, int w, int y0, int y1, int passo,
                            double *media, double *desvio);

Três funções, uma primitiva. E `croma_dp` e `croma_stat` passam a diferir só na
faixa e no passo, que é o que elas realmente são.

### O acoplamento, medido

Das 19, o que cada uma lê além dos argumentos:

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

Com a camada de medidas isolada, um `src/testes_medidas.c` monta quadros sintéticos com
estrutura conhecida — borrão de N linhas a partir da linha L, faixa com salto de
croma de valor V — e afirma o resultado. **É a única parte deste projeto que dá
para testar sem oráculo**, porque a resposta é conhecida por construção.

### Duas camadas dentro do mesmo arquivo

Tudo vai para `juizes.c`, mas em duas camadas separadas por um comentário, e a
de cima não pode chamar a de baixo:

| camada | o que tem | pode ler |
|---|---|---|
| **medidas** | as 16 que viram puras, mais as duas primitivas | só os argumentos |
| **política** | `trinca_ok`, os `piso_*`, `base_*`, `img_base`, `nota_do_quadro()` | o que quiser |

`trinca_ok` tem 87 linhas e não é medida: é a composição de cinco medidas com
limiares e estado. Misturar as duas camadas é o que torna o juiz difícil de
auditar hoje — e foi o que me fez reescrevê-lo três vezes nesta sessão sem notar
que dois dos três critérios não mediam nada.

A regra "a camada de cima não chama a de baixo" é o que garante que as medidas
continuem testáveis sem decodificador.

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

## 4c. Documentar as funções — padrão fixo, e sem apagar o que já existe

**Estado medido:** 46 funções, **20 com comentário e 26 sem nenhum**. Entre as
sem estão **os 9 workers**, o `main`, o `carrega_patches`, o `grava_patch`, o
`abre_decoder`, o `captura_frame`, o `blocagem_faixa`, o `base_uniforme` e o
`tarja_perfeita` — ou seja, quase tudo que grava em fonte de verdade ou decide
veredito.

### O padrão

Um bloco acima de cada função, nesta ordem:

    /* Descricao: o que a funcao faz e por que existe. Ate 8 linhas.
     *
     *   Y       o plano de luma, empacotado com passo w
     *   w, h    largura e altura em pixels; abaixo de 1920x1080 devolve falha
     *   y0, y1  faixa a medir, [y0, y1), em linhas do quadro
     *
     * Devolve: a razao borda/interior. 99,0 quando nao da para medir --
     *          valor ALTO, que reprova; a `blocagem` devolve 0 no mesmo caso.
     */

Três regras:

1. **Um parâmetro por linha, no máximo duas.** Parâmetros que só fazem sentido
   juntos (`w, h`, `y0, y1`) compartilham a linha.
2. **Descrição de até 8 linhas.** Se não couber, a função faz coisa demais — é
   sinal de refatoração, não de comentário maior.
3. **O valor de retorno é obrigatório**, e com o significado do caso de falha.
   Foi exatamente a falta disso que deixou `blocagem` devolver `0` e
   `blocagem_faixa` devolver `99,0` para "não deu para medir", dois veredictos
   opostos com o mesmo nome de raiz (seção 2).

### O que NÃO fazer: comprimir o que já está escrito

As 20 funções documentadas têm **6 linhas de mediana e até 23**, e o conteúdo
não é interface — é a justificativa medida de por que aquele critério existe. O
comentário do `topo_uniforme` carrega os oito quadros verificados que fixaram a
tarja em 16,000; o do `linha_copia` carrega a armadilha 40 inteira, com os
números que a provaram.

**Isso é a memória do projeto e não cabe em 8 linhas.** O padrão acrescenta o
bloco de interface; a justificativa fica, abaixo dele, separada por uma linha em
branco e um `/* Por que: ... */`. Comprimir seria trocar memória por formatação
— e este projeto já perdeu a armadilha 33 numa reescrita.

### Onde começa

Pelas 26 sem nada, e dentro delas pela ordem do risco:

| primeiro | por quê |
|---|---|
| `grava_patch`, `carrega_patches` | escrevem e leem a fonte de verdade |
| os 9 `worker_*` | decidem veredito, e vão ser reescritos no item 7 — documentar antes é o que torna a reescrita conferível |
| `blocagem_faixa`, `base_uniforme`, `tarja_perfeita` | são juízes sem contrato escrito |

Os workers ganham a documentação **antes** da refatoração do item 7, não depois:
é o bloco de interface que diz o que a saída de cada um tem que continuar sendo.

## 5. Ordem sugerida

Da menor para a maior chance de quebrar coisa:

| # | item | risco | ganho |
|---|---|---|---|
| ~~1~~ | ~~tabela de modos e validação de `argc`~~ | **FEITO** | `main` de 1.212 para 117 linhas; 27/27 na regressão |
| ~~2~~ | ~~documentar o contrato das duas `blocagem`~~ | **FEITO** | pré-processado idêntico ao `HEAD` |
| ~~3~~ | ~~renomear as famílias do croma e da tarja~~ | **FEITO** | 37 ocorrências; o arnês pegou 2 strings trocadas por engano |
| ~~4~~ | ~~`src/juizes.c`~~ | **FEITO** | 13 medidas puras + `testes_juizes.c`; 28/28 |
| ~~5~~ | ~~documentar as funções no padrão~~ | **FEITO** | 46 de 46; conferido por `ferramentas/confere_doc.py` |
| 6 | `nota_do_quadro()` fora do `worker_avanco` | **PARADO** | não é refatoração: fazer os pisos valerem em `varrek` e `cresce` **muda o que esses modos fazem** |
| 7 | motor único de varredura em `src/workers.c` | **PARADO** | a premissa estava errada — ver abaixo |

Os itens 6 e 7 **pararam**, cada um por um motivo medido.

**Descartados, e o registro fica para ninguém reabrir:** unificar as duas
`blocagem` (invalidaria duas calibrações), analisador CABAC próprio (tiraria do
libavcodec o papel de oráculo) e tirar `gera_combos` da memória (risco de erro
de índice sem ganho no relógio).

## O que não mexer

- `ctx->thread_count = 1` — determinismo, seção 1 do `PARALELIZACAO.md`.
- A regra do menor índice na parada antecipada — seção 7 do `AGENTS.md`.
- `weighted_pred_flag` fica em 1.

## 6. O arnês, e o que ele custou para ficar de pé

`ferramentas/regressao.sh` roda um caso de cada um dos 27 modos e compara o
SHA-256 da saída. É ele que dá sentido a "saída byte a byte idêntica".

**Seis defeitos apareceram montando o arnês, nenhum no código que ele protege**
— e cada um teria produzido um falso "tudo ok":

| defeito | o que teria acontecido |
|---|---|
| `unico` com argumentos errados — são tamanhos de janela, não faixa de quadros | varria os 132 IDRs e matava a captura em 2 de 27 |
| `testa` recebendo o número do quadro no lugar do arquivo | hash da string vazia, passaria em qualquer refatoração |
| arquivo auxiliar do `testa` nunca criado | idem |
| `[6.7s]` no stdout de 21 lugares do código | `idr` acusava mudança em toda conferência |
| caminho do `mktemp` no stdout, convertido para a forma Windows pelo MSYS2 | `dump`, `dumpyuv` e `serie` idem |
| `varre1` com janela de 22.336 candidatos | caso de minutos, inviável como arnês |

Os dois do meio são os piores: **alarme que toca sempre não alarma.** Em duas
conferências eu já teria aprendido a ignorar quatro modos.

O filtro de tempo já estava escrito na seção 7 do `AGENTS.md`, para a validação
de paralelismo. Eu tinha a regra e não a apliquei no arnês que existe para
aplicar regras.

**O arnês se prova antes de provar qualquer coisa:** `grava` seguido de
`confere`, sem tocar no código, tem que dar 27 de 27. Ele também recusa gravar
um caso cujo hash seja o da string vazia, que foi o defeito que eu cometi.

### A compilação aqui não é reproduzível

Testado: **dois builds da mesma fonte dão binários com hashes diferentes**, e
mudar o nome do arquivo de saída muda o hash também. Eu ia usar "o binário saiu
idêntico" como prova de que uma mudança era só comentário — não serve.

O que serve é comparar o **pré-processado** (`gcc -E -P`) contra o `HEAD`: o
pré-processador descarta comentários, então saída idêntica prova que a mudança
não pode alterar comportamento. Custa segundos contra os minutos do arnês, e é
a prova certa para os itens 2, 3 e 5, que não mexem em lógica.

### O padrão vale para as que já tinham comentário

A primeira passada documentou só as 26 que não tinham nada, e ficou pela
metade: as outras 27 tinham justificativa medida mas **nenhuma dizia os
parâmetros nem o valor de retorno**. Comentário que explica o porquê e não diz o
contrato resolve metade do problema.

A segunda passada acrescentou o bloco de interface a essas 27, **sem tocar na
justificativa** — ela entra antes do `*/` que já existia. Hoje são **46 de 46**
funções no padrão, fora os `modo_*`.

E o padrão passou a ser conferível: `ferramentas/confere_doc.py` verifica
mecanicamente que o nome de cada parâmetro e o retorno aparecem no bloco. Ele
não garante que o texto esteja **certo** — só que existe. Garantir que está
certo é trabalho de quem lê, e o teste mecânico é o que impede a regressão
silenciosa quando alguém acrescentar uma função.

## 7. Por que os itens 6 e 7 não entraram

### O item 6 não é refatoração

Extrair a linha de pontuação para uma função é seguro. Mas o **ganho** que eu
escrevi — "os pisos passam a valer em todo modo" — é mudança de comportamento:
hoje `PISO_TARJA` no `varrek` ou no `cresce` é ignorado, e fazer valer muda o
que esses modos produzem.

Isso pode ser desejável, mas é decisão de função, não arrumação de código, e o
arnês não a detectaria: os casos de `varrek` e `cresce` não ligam piso nenhum.
Seria a armadilha do `TOL_COPIA` outra vez — caso que só exercita o default.

### O item 7 tinha a premissa errada, e a medida é minha

Escrevi "menos ~400 linhas". **410 é o tamanho total dos nove workers**, não a
duplicação entre eles. Medido:

| | linhas |
|---|---|
| total dos 9 workers | 410 |
| prólogo repetido | 49 |
| epílogo repetido | 36 |
| **duplicação real** | **85, ou 20%** |

E o esqueleto compartilhado é **parametrizado em cinco eixos**: de onde vem o
alvo (struct ou global), se calcula a âncora, se pré-preenche a cópia, como
aloca o `cap_buf` (sempre, condicional, ou só com métrica) e se aloca buffer
extra. Só `worker_par2` e `worker_varrek` são realmente iguais.

Um motor com cinco botões para remover 85 linhas de moldura, **na parte do
código que produz todo resultado deste projeto**, é troca ruim. O que eu chamei
de "o mesmo esqueleto" foi leitura superficial de semelhança: contei padrões
presentes nos nove sem olhar se a estrutura em volta era a mesma.

### O que fica no lugar

Os nove workers estão documentados (item 5), com o esqueleto comum escrito uma
vez e cada um dizendo o que tem de próprio. Isso resolve o problema real — "de
onde começo a ler" — sem tocar no código que decide os resultados.

**O plano fecha no item 5.** Os cinco itens feitos removeram dois bugs, criaram
o arnês de 28 casos, isolaram a camada testável e puseram 46 funções no padrão.
Os dois que sobraram custam mais do que rendem, e isso está medido, não achado.

