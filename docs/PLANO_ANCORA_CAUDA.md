# Plano 2 — Âncora pela cauda

**Estado: não iniciado** (proposto em 2026-09-23). Um dos três planos para o
dano denso dos IDRs quebrados; os outros são
[`PLANO_JUIZ_ENCODER.md`](PLANO_JUIZ_ENCODER.md) e
[`PLANO_SUBSTITUICAO_REFERENCIA.md`](PLANO_SUBSTITUICAO_REFERENCIA.md).

## Por que este plano existe

- **O gabarito forte está no fim.** A tarja inferior (linhas 962–1079, `Y=16`,
  `U=V=128`) é o único gabarito que nunca cedeu, e tarja certa implica quadro
  inteiro certo. Mas ela é a última coisa do slice, e em rajada nenhuma cadeia
  de bits chega até ela — então ela só julga no final, nunca guia.
- **O 1773 tem dano na própria cauda.** Três violações de escape nos últimos
  60 bytes do NAL (rel 34.257, 34.285 e 34.312). Nos quadros bons essa classe
  só aparece nos últimos 4–7 bytes; se forem dano, são mais 3 bits além do da
  frente, e nenhuma busca de 2 bits fecharia o quadro (RASTREIO.md, "IDR 1773
  no JM").

## A ideia

A tarja de um IDR tem **sintaxe conhecida**: I16x16 com predição DC, CBP 0,
`mb_qp_delta = 0`, croma DC, sem coeficientes — repetida em ~840 MBs
(fileiras 61–67; a fileira 60 ainda tem 2 linhas de cena). Símbolos quase
certos custam quase nada em CABAC: a tarja inteira ocupa **algumas dezenas de
bytes no fim do NAL** — justamente onde estão as violações do 1773.

Recodificando essa sequência conhecida de símbolos com um codificador CABAC,
dá para achar **em que bit e em que estado do decodificador a tarja começa**,
e corrigir a cauda com o valor certo — correção determinística, não busca por
imagem. E o estado de entrada vira um **teste intermediário**: o conserto do
meio tem que chegar ao início da fileira 61 exatamente naquele bit e naquele
estado. São ~18 bits de estado mais a posição exata: acaso na casa de 1 em
10⁸.

## Passos

1. **Medir a tarja nos 6 IDRs íntegros com o JM** (`ldecod` com trace, no
   scratchpad `JM/`).
   - Confirmar que as fileiras 61–67 têm sintaxe fixa.
   - Instrumentar o decodificador aritmético do JM (uma linha em
     `biaridecod.c`) para imprimir, no início de cada MB, a posição em bits e
     o estado: `codIRange`, `codIOffset` e os contextos usados pela tarja.
   - Saber quantos bytes a tarja ocupa e em quantos MBs os contextos saturam.
2. **Codificador CABAC mínimo** só para os elementos da tarja (`mb_type`,
   `intra_chroma_pred_mode`, `mb_qp_delta`, `coded_block_flag` do DC,
   `end_of_slice_flag`, terminação), com as tabelas do JM.
   - **Teste que decide o plano:** partindo do estado medido na fileira 61,
     reproduzir **bit a bit** o fim do NAL dos 6 IDRs íntegros.
3. **Nos IDRs quebrados, achar o ponto de entrada:** varrer posição em bits ×
   estado aritmético (`codIRange`, `codIOffset`), com os contextos saturados
   depois de poucos MBs de tarja, e ficar com o que reproduz a cauda do
   arquivo com o menor número de bits trocados.
   - Saída: a correção da cauda (valor certo) e o estado de entrada da tarja.
4. **Usar o estado como teste no meio:** candidato só passa se o decodificador
   chegar ao início da fileira 61 no bit e no estado do passo 3. Combina com o
   juiz rápido do plano 1: um guia a busca, o outro confirma.
5. **No 1773:** explicar as 3 violações de escape como trocas específicas, e
   rodar a busca das fileiras 57–60 com o teste do passo 4.

## Custo e riscos

- **Custo:** 2–3 dias; o passo 2 é a maior parte.
- **Risco:** se os contextos não saturarem rápido, o passo 3 vira busca sobre
  estados de contexto também, e cresce. O passo 1 mede isso antes de tudo.
- **Limite:** a correção da cauda sozinha não faz o quadro decodificar — o
  dano do meio continua. O ganho é transformar a tarja de juiz final em teste
  intermediário forte, e provar bits da cauda.
- Correção de cauda que entrar no `patches.txt` passa pelo usuário, como
  qualquer linha.

## Onde registrar

Medidas da tarja e do codificador no CABAC.md (seção 4, "a tarja não é
separável" — este plano é a exceção a verificar); corridas no RASTREIO.md;
bits provados, com a prova, no `patches.txt` depois de aprovados.
