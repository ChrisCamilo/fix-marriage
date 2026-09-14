# Ideia: paralelizar o reparador sem perder determinismo

Proposta, não implementação. Nada aqui foi executado ainda.

## 1. O que **não** fazer

Subir `ctx->thread_count`. O comentário no `reparador.c` que fixa em 1 —
"determinismo acima de velocidade" — está certo, por dois motivos:

- `meu_log` escreve em globais (`log_erros`, `log_bytestream`, `log_ocultados`)
  sem nenhum mutex. Com threads internas do libavcodec isso é corrida de dados.
- Threading de frame muda a ordem de emissão e a atribuição de erros. É a
  armadilha 3 da `ARMADILHAS.md`, que já produziu dois reparos falsos.

Ganhar 3x aceitando reparo errado não é ganho. **`thread_count` continua 1.**

## 2. Onde o tempo realmente está

Não em decodificar um frame, e sim em decodificar **milhares de candidatos**.
Uma janela de 4096 bytes são 32.768 decodificações, cada uma independente das
outras. É paralelismo trivial — e o dado que confirma: a corrida `idr` com
janela 4096 levou 31 minutos, quase toda em varredura de candidatos.

## 3. Desenho

O ponto-chave é que **a infraestrutura de isolamento já existe**: `decodifica()`
aceita um buffer alternativo para o frame alvo (`alt`, `alt_len`). Cada worker
testa seu candidato na própria cópia do NAL e nunca muta o `arq` compartilhado.
Os demais frames da cadeia são leitura concorrente, que é segura.

Três mudanças:

```c
/* 1. Estado por thread, em vez de global */
static _Thread_local AVCodecContext *ctx = NULL;
static _Thread_local int  log_erros = 0;
static _Thread_local long log_bytestream = -1;
static _Thread_local int  log_ocultados = -1;

/* 2. Cada worker tem sua cópia mutável do NAL alvo */
uint8_t *meu = malloc(len);
memcpy(meu, arq + ix[alvo].off, len);
/* aplica o flip em `meu` e chama decodifica(anc, alvo, meu, len, NULL) */

/* 3. Fila de candidatos: um contador atômico */
static atomic_int proximo;       /* índice do próximo candidato */
static atomic_int melhor;        /* menor índice com solução; INT_MAX se nenhum */
```

`av_log_set_callback` continua global — só o **estado** que o callback escreve
vira `_Thread_local`. Como o libavcodec com `thread_count=1` chama o callback na
própria thread que decodifica, isso é correto.

## 4. Determinismo — o ponto crítico

A busca sequencial varre de `fim-1` para trás e devolve **a primeira solução
nessa ordem**. Em paralelo, várias threads podem achar soluções diferentes, e
qual chega primeiro vira corrida. Isso quebraria a reprodutibilidade e, pior,
poderia escolher um bit errado — o IDR 1802 já mostrou que existem múltiplas
soluções válidas para o mesmo frame.

A regra que preserva o resultado exato:

1. Numerar os candidatos de `0..n-1` **na mesma ordem da varredura sequencial**.
2. Workers puxam índices do contador atômico, em ordem crescente.
3. Ao achar solução no índice `k`, fazer `melhor = min(melhor, k)` atomicamente.
4. Parar de puxar índices `>= melhor`, mas **terminar os já em voo com índice
   `< melhor`**.
5. A resposta é o candidato em `melhor`.

Isso devolve bit a bit o mesmo que a versão sequencial, para qualquer número de
threads. Não é "quase igual": é igual por construção.

## 5. Quantas threads

**Não usar todas.** A máquina tem 28 núcleos; o default é **12**, configurável
por variável de ambiente, com teto em `nproc - 2`:

```c
int n = getenv("THREADS") ? atoi(getenv("THREADS")) : 12;
if (n > nucleos - 2) n = nucleos - 2;
if (n < 1) n = 1;
```

O default começou em 6, pela eficiência por thread. Passou a 12 por decisão
explícita: o que importa é o relógio, não a eficiência — mesmo com retorno
decrescente, terminar antes vale mais do que manter núcleos ociosos.

Razões para não saturar:

- Cada worker carrega um `AVCodecContext` de 1920×1080 mais buffers de
  referência — alguns MB por thread. Com 28 workers isso vira pressão de cache
  que come parte do ganho.
- Corridas duram dezenas de minutos; deixar a máquina usável importa.
- O ganho é sublinear a partir de certo ponto, e o risco de erro sutil cresce
  com a concorrência. 6 threads já dá ~5x, que resolve o problema prático.

## 6. Onde o ganho é real — e onde não é

Vale ser honesto sobre isto:

| caso | ganho |
|---|---|
| varredura exaustiva (modo `unico`, sem parada antecipada) | ~linear, o melhor caso |
| IDR sem solução (varre a janela inteira) | ~linear |
| IDR que resolve no 3º candidato | **nenhum**, e um pouco de overhead |

A mediana das soluções `@corte` está a 12 bytes do corte, ou seja, muitas são
achadas quase imediatamente e não se beneficiam. O ganho concentra-se nos casos
caros — que são justamente os que hoje dominam o relógio.

**Ortogonal e provavelmente maior para frames comuns:** a melhoria 2 do `MELHORIAS.md`, cachear o estado do decoder na âncora. Hoje cada candidato de um
frame comum redecodifica o GOP inteiro desde o IDR. Para IDR a cadeia tem
tamanho 1 e só o threading ajuda; para o resto do filme, as duas se somam.

## 7. Validação — obrigatória antes de confiar

Já existem referências sequenciais gravadas. A paralela **tem que reproduzi-las
exatamente**:

- `idr 64 8` deve dar os mesmos 44 resultados de `idr64.txt`.
- `idr 4096 16` deve dar os mesmos 45 de `idr_full.txt`, com offsets e bits
  idênticos, incluindo os quatro casos que mudaram de estratégia.
- Rodar duas vezes com números de threads diferentes (ex.: 2 e 6) e conferir que
  a saída é byte a byte igual entre si e igual à sequencial.

Se qualquer offset divergir, o desenho está errado — não ajustar a referência.

## 8. Ordem sugerida

1. `_Thread_local` no estado do decoder e do log; rodar sequencial e conferir
   que nada mudou. É a mudança de maior risco e vale isolá-la.
2. Cópia por worker do NAL alvo, via o `alt` que já existe.
3. Fila atômica com a regra do menor índice.
4. Validar contra as três referências acima.
5. Só então usar em corrida de produção.
