# Convenções deste repositório

Recuperação de um vídeo de casamento de 2017 danificado por bit-rot. Leia o
`ESTADO.md` antes de qualquer coisa: ele tem os parâmetros já resolvidos, os
comandos e o mapa dos outros documentos. E leia o `docs/ARMADILHAS.md` antes de medir
qualquer coisa — são **51** maneiras de medir errado que já custaram horas aqui.

## 1. Privacidade — inegociável

Este repositório é **local e privado**. Material pessoal de família.

- **Nunca** adicionar remoto, fazer push, ou criar repositório no GitHub.
- **Nunca** publicar nada disto: nem artifact, nem gist, nem pastebin, nem
  serviço de diagrama ou de conversão online.
- Se um remoto for pedido um dia, tem que nascer privado
  (`gh repo create --private`). Criar público e trocar depois não resolve:
  o conteúdo fica em cache e indexado.

## 2. As duas fontes de verdade

| arquivo | regra |
|---|---|
| `Caio & Lizandra - Making- Caio-Balu.mp4` | **nunca modificar.** Fica fora do git; integridade em `CHECKSUMS.txt` |
| `patches.txt` | cada linha é `offset bit`. **Toda mudança passa pelo usuário antes** — incluindo remoção |

Tudo o mais é derivado e reconstruível: `index.txt`, `reparado.mp4` e o binário
saem desses dois. Se precisar mexer em algo, mexa nos patches, nunca no MP4.

Antes de qualquer operação de git que possa descartar trabalho, confira o
`git status` e o número de linhas do `patches.txt`.

**Linha duplicada se cancela** — XOR duas vezes é identidade. Isso foi usado de
propósito para desfazer reparos aceitos por engano, e hoje há **18 pares assim**
no arquivo. Nenhum é acidente: conferidos um a um, remover qualquer um deles
piora o filme. Não "limpar" duplicata sem medir os dois estados.

## 3. Só entra patch que passa no critério rigoroso

O critério é o do `reparador.c`: **flush do decoder e exigir quadros == pacotes,
com zero linhas de log**. Nada mais conta.

A `docs/ARMADILHAS.md` lista **51** maneiras de medir errado que já produziram
conclusões falsas neste projeto. As três que mais enganam:

- **Contagem de frames do ffmpeg não mede nada** — ele emite quadros de
  ocultação cinza e infla o número. Foi assim que "2808 de 3445" virou verdade
  por um tempo. Hoje o filme emite ~3.100 quadros e **209 passam no critério
  rigoroso**; os outros 3.236 são ocultação. Armadilhas 1 e 48.
- **Toda medida sobre bytes sai do buffer remendado, nunca do MP4 cru.** Script
  em Python que abre o arquivo direto enxerga dano que já foi consertado há
  centenas de commits. Armadilha 49.
- **Métrica agregada esconde falha em subgrupo.** Medir sempre separado por
  tipo (I, P, B). Um "conserto" no `weighted_pred_flag` já matou 100% dos
  frames P sem que a média acusasse.

Não mexer no `weighted_pred_flag`: ele fica em 1.

Depois de gerar patches novos, revalidar com `BASE_N=1338 ... verify`. Espera-se
hoje **`5 válidos, 10 falsos, 479 determinísticos pulados`**, e os 10 falsos são
todos explicados — nenhum é patch ruim:

| falsos | frames | leitura |
|---|---|---|
| 4 | 2362, 2364, 2365, 2366 | `com=1 sem=1` — insuficientes, não errados (`docs/INVESTIGACOES.md`) |
| 2 | 12 | `com=1 sem=1` — a cadeia do GOP 0 sempre erra por causa do frame 11; o `verify` não usa `ERROS_BASE` |
| 4 | 3435, 3439 | `com=1 sem=0` — pares **cancelados de propósito**; o `verify` testa cada ocorrência e não entende cancelamento |

**Qualquer falso além desses 10 é problema.** Rodar `verify` **sem** `BASE_N`
acusa ~1328 falsos por construção, o que é esperado e não é bug.

## 4. Manter a documentação viva

Ele é a memória do projeto entre sessões. Depois de qualquer corrida que mude
resultado, **atualizar o `docs/RESULTADOS.md`** com números medidos, não estimados:

- frames perfeitos / 3445, e a que tempo isso corresponde
- trechos contínuos e quanto dá para assistir de fato
- quantos IDRs estão limpos (isto governa o que é reparável)
- resultado do `verify`

Distinguir sempre **"consertado por reparo"** de **"já estava intacto"**. Somar
os dois num número só foi uma confusão real que aconteceu aqui.

Quando um parâmetro for resolvido, escrever no `ESTADO.md` que está resolvido,
para ninguém reinvestigar. Quando uma hipótese for descartada, registrar que foi
testada e o resultado — o `docs/INVESTIGACOES.md` existe para isso.

## 5. Ordem de ataque

Todo reparo ancora no IDR anterior (`ancora_de`). Um GOP cujo IDR está quebrado
é **irreparável** enquanto o IDR não for consertado: os ~27 frames dele estão
bloqueados por dependência, não por dano próprio.

Portanto: **consertar IDR quebrado primeiro.** Rende ~27 frames destravados por
conserto, contra 1 de um frame comum. O modo `idr` do `reparador.c` faz esse
diagnóstico sem gravar nada.

Duas janelas importam, e a segunda foi ignorada por muito tempo:

1. em volta do ponto de corte que o decoder informa;
2. **os primeiros ~256 bytes do NAL** — quando o corte cai antes do byte ~64,
   a janela do corte colapsa e esta é a única busca que existe de verdade.

## 6. Ambiente

Windows com **MSYS2 UCRT64** — não é Linux, não há `apt-get`. Toda sessão:

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
MP4="Caio & Lizandra - Making- Caio-Balu.mp4"
```

UCRT64 e não MINGW64: o reparador lê `%td` das mensagens do decoder, que a
msvcrt antiga não interpreta — a heurística falharia em silêncio. Detalhes na
seção 3 do `ESTADO.md`.

Python é o do Windows (`C:\Python314`), chamado como `python`. Dentro de string
passada com `python -c`, usar caminho `C:/...`; o Git Bash só converte `/c/...`
quando é argumento.

## 7. Paralelizar tudo que der, sem abrir mão do determinismo

Trabalho que testa candidatos independentes **deve** ser paralelizado. As
corridas duram dezenas de minutos; deixar isso numa thread só é desperdício.

**`THREADS=12` é o certo, e é o padrão.** A máquina tem 28 núcleos e o teto do
código é `NUMBER_OF_PROCESSORS - 2` = 26, mas **não se satura**: cada worker
carrega um `AVCodecContext` de 1920×1080 com buffers de referência, e 26 deles
viram pressão de cache que come o ganho; o ganho já é sublinear bem antes disso;
e a máquina precisa continuar usável durante corrida de meia hora. A
justificativa completa está na seção 5 do `docs/PARALELIZACAO.md`, junto com o
histórico — o default foi 6, subiu para 12 por decisão explícita, e 12 fica.

Não "otimizar" isto para 26 achando que metade da máquina está parada: ela está
parada de propósito.

A maquinaria de paralelização já existe no `reparador.c` (`varre_par`) — use-a em
vez de escrever laço sequencial novo. Já aconteceu de eu paralelizar um modo e,
logo depois, escrever outro com laços sequenciais próprios ao lado.

**Onde não paralelizar:** o `thread_count` do libavcodec fica em 1. Detalhes e
justificativa na seção 1 do `docs/PARALELIZACAO.md`.

O determinismo vem do desenho, não de sorte de escalonamento:

- candidatos numerados na ordem da varredura sequencial;
- distribuídos por contador atômico, não por blocos fixos — o custo varia muito
  com a posição do bit;
- saída ordenada pelo índice do candidato no final;
- com parada antecipada, vale a **regra do menor índice**: ao achar solução em
  `k`, parar de puxar índices `>= k` mas terminar os já em voo com índice menor.
  Nunca "a primeira que chegar" — há frames com milhares de soluções válidas, e
  a escolha por ordem de chegada pegaria bit errado.

### Validação obrigatória

**Toda paralelização se prova comparando `THREADS=1` com o default.** Não é
opcional e não se pula por parecer óbvio:

```bash
THREADS=1 ./reparador.exe "$MP4" index.txt patches.txt <modo> > seq.txt
              ./reparador.exe "$MP4" index.txt patches.txt <modo> > par.txt
# iguais ignorando só o campo de tempo:
diff <(sed -E 's/ *\[[0-9.]+s\]//' seq.txt) <(sed -E 's/ *\[[0-9.]+s\]//' par.txt)
```

`THREADS=1` cai no caminho sequencial original, então a comparação é honesta:
mesmo binário, mesma máquina, mesmos dados. Se divergir, **o desenho está errado
— corrija o código, nunca ajuste a referência.**

Referência já validada: modo `unico` sobre os IDRs deu saída byte a byte
idêntica (mesmo SHA-256) em 1, 6 e 10 threads, com ganho de 4,8x e 6,5x. (O
filme tem **132** IDRs no `index.txt`; anotações antigas falam em 71 ou 128 —
ver `docs/IDRS.md`.)

## 8. Commits — um por ideia

**Cada commit carrega uma ideia só.** Se a mensagem precisa de "e" para dizer o
que foi feito, provavelmente são dois commits.

Não misturar num mesmo commit: correção de bug, funcionalidade nova, atualização
de documento, ajuste de formatação. Mesmo que tenham nascido do mesmo trabalho,
são ideias distintas e se revisam separado. Em particular, **atualizar o
`ESTADO.md` com números medidos é commit próprio**, separado do código que
produziu os números — o código é uma ideia, o resultado é outra.

**Commitar assim que a ideia fecha**, não acumular para o fim da sessão. Se o
próximo passo é disparar uma corrida de 30 minutos, o código vai commitado
antes: aí o que se perde num acidente é o resultado, que se refaz, e não o
código.

Mensagem em português explicando o **porquê**, não o quê — o diff já diz o quê.
E a descoberta que motivou a mudança entra na mensagem: foi assim que ficou
registrado que o IDR 1802 tem duas soluções de 1 bit válidas, informação que se
perderia se o commit dissesse apenas "adiciona modo unico".

## 9. Higiene

- Finais de linha **LF** nos dados. O `.gitattributes` segura isso contra o
  `core.autocrlf`; o `reparador.c` grava com `fopen(...,"ab")` e o
  `ferramentas.py` com `newline="\n"`. Não reverter.
- Corridas longas: rodar em background e redirecionar a saída, que é volumosa.
  Não filtrar a saída com `grep` no redirecionamento — os dados dos outros casos
  se perdem e a corrida tem que ser refeita.
