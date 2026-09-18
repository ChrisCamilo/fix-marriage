#!/usr/bin/env bash
# regressao.sh -- prova que uma refatoracao nao mudou nada.
#
# Roda um caso representativo de cada modo do reparador e grava o SHA-256 da
# saida. O criterio de aceitacao do docs/REFATORACAO.md e saida BYTE A BYTE
# identica, modo a modo, e e isto que mede.
#
#   bash ferramentas/regressao.sh grava <dir>    captura a referencia
#   bash ferramentas/regressao.sh confere <dir>  compara contra a referencia
#
# Cada caso e pequeno de proposito: o arnes tem que rodar em minutos, senao nao
# se roda. Modo que so faz sentido em corrida longa entra com janela minima --
# o que se prova aqui e que o CAMINHO nao mudou, nao que a busca acha algo.
#
# Cada caso tem tempo-limite proprio de 300 s: modo lento nao pode derrubar a
# captura inteira. Foi o que aconteceu na primeira tentativa -- o `unico` ignora
# os argumentos como faixa de quadros (sao tamanhos de JANELA) e varre os 132
# IDRs, estourou o tempo global e matou a corrida em 2 de 27 casos.
#
# THREADS=1 em tudo. Nao e sobre velocidade: e para a saida nao depender de
# escalonamento. Ver armadilha 47 e a secao 7 do AGENTS.md.
set -u
export PATH=/c/msys64/ucrt64/bin:$PATH
cd "$(dirname "$0")/.."

MP4="Caio & Lizandra - Making- Caio-Balu.mp4"
IX=index.txt
EXE=./reparador.exe
# NUNCA o patches.txt verdadeiro. O modo `repair` GRAVA no arquivo de patches
# que recebe, e o caso dele recebia a fonte de verdade: em 2026-09-18, quando o
# frame 2361 passou a quebrar, uma regravacao achou um bit para ele e o escreveu
# no patches.txt -- e ele entrou num commit sem aprovacao. Agora cada caso
# recebe uma copia recem-feita, e o fim do script confere o original intacto.
PT_REAL=patches.txt
PT_ANTES=$(sha256sum "$PT_REAL" | cut -c1-64)

acao=${1:-confere}
dir=${2:-dados/regressao}
# Diretorio FIXO, nao mktemp. Os modos que gravam arquivo ecoam o caminho no
# stdout, e o MSYS2 converte o caminho POSIX para a forma Windows ao passar para
# programa nativo -- entao filtrar "$tmp" no stdout nao funciona: o programa
# imprime C:/msys64/tmp/... e o filtro procurava /tmp/... Caminho constante
# resolve na origem, sem filtro.
tmp=logs/.regressao
rm -rf "$tmp"; mkdir -p "$tmp"
PT="$tmp/patches.txt"

# Listas auxiliares que alguns modos exigem como argumento.
printf '147697 5\n147699 4\n' > "$tmp/lista.txt"
# O modo `testa` le outro formato: t gop pos campo visto esp off bit est.
printf '11 0 11 x 0 0 147697 5 x\n11 0 11 x 0 0 147699 4 x\n' > "$tmp/testa.lst"

# nome|argumentos|variaveis de ambiente
casos=(
  "repair|repair 2360 2361 256|"
  "idr|idr 512 16 2|"
  "unico|unico 8 4 3|"
  "oraculo|oraculo 2 8|"
  # Alvo colado no IDR e janela pequena: com 2361/2361 o modo varria o NAL inteiro
  # com a cadeia de 28 quadros e estourava os 300 s -- o hash era da saida cortada.
  "vizinho|vizinho 3320 3319 64 1|"
  "recupera|recupera 2361 2361|"
  "ranking|ranking|"
  "varre1|varre1 11 0 5 30|"
  # SEM varre2: ele varre TODOS os pares de bits do quadro inteiro (o menor quadro
  # do filme da 2 milhoes) e o uso e `<alvo> [saida]` -- o "5" deste caso virava
  # arquivo de saida, e um `5` vazio chegou a ser commitado. Nunca coube nos 300 s:
  # o hash sempre foi so da primeira linha. Nao ha caso pequeno que o exercite.
  # Janela [5,16): 3.828 combinacoes. A [5,24) tinha 11.476 e ficava no limite
  # dos 300 s com THREADS=1 -- dependia da carga da maquina.
  "varrek|varrek 11 2 5 16|"
  "cresce|cresce 11 5 24|"
  "avanco_mb|avanco 11 1 5 40|"
  "avanco_consumo|avanco 11 1 5 40|PONTUA_CONSUMO=1"
  "avanco_topo|avanco 11 1 5 40|PISO_TOPO=2"
  "corta|corta 11 5 60 5|"
  "corte|corte 11|"
  "cortes|cortes 11 13|"
  "campo|campo 11 LISTA|"
  "trinca|trinca 3047 LISTA|TOL_COPIA=1"
  # Caso com valor NAO PADRAO da variavel. Sem ele, mover `tol_copia` para
  # outro arquivo e esquecer de ligar o ajustador passaria despercebido: o
  # caso acima usa 1, que e o default, e a saida fica igual de qualquer jeito.
  "trinca_tol0|trinca 3047 LISTA|TOL_COPIA=0"
  "testa|testa TESTALST|"
  "mapa|mapa|"
  "panorama|panorama|"
  "dump|dump 2333 SAIDA.pgm|"
  "dumpyuv|dumpyuv 2333 SAIDA.yuv|"
  "serie|serie 2333 2340 SAIDA.yuv SAIDA.txt|"
  # Cadeia QUEBRADA: o IDR 953 e borrao e o serie tem que recusar os quadros
  # que decodificam sem erro em cima dele (armadilha 56). O caso acima cai
  # numa ilha, onde a cadeia inteira e boa, e nao exercita a regra.
  "serie_cadeia|serie 953 962 SAIDA.yuv SAIDA.txt|"
  "verify|verify|BASE_N=1830"
  # SEM report: decodifica o filme inteiro quadro a quadro e nao aceita faixa.
  # Estourava os 300 s em toda corrida; o hash era de saida parcial.
)

mkdir -p "$dir"
falhas=0
for caso in "${casos[@]}"; do
  nome=${caso%%|*}; resto=${caso#*|}
  args=${resto%%|*}; env=${resto#*|}
  args=${args//TESTALST/$tmp/testa.lst}
  args=${args//LISTA/$tmp/lista.txt}
  args=${args//SAIDA/$tmp/$nome}
  # shellcheck disable=SC2086
  t0=$(date +%s)
  # Duas coisas saem do hash antes de comparar, e as duas foram descobertas
  # conferindo o arnes contra ELE MESMO, sem mudanca de codigo:
  #
  #   o tempo   21 lugares do codigo imprimem "[6.7s]", que muda a cada corrida.
  #             A secao 7 do AGENTS.md ja mandava filtrar isso na validacao de
  #             paralelismo; eu nao tinha aplicado aqui.
  #   o caminho o mktemp cria um diretorio novo por corrida e os modos que
  #             gravam arquivo ecoam o caminho no stdout.
  #
  # Sem os dois filtros, `idr`, `dump`, `dumpyuv` e `serie` acusam mudanca em
  # toda conferencia -- e alarme que sempre toca nao alarma.
  # A saida vai para arquivo ANTES do hash para o codigo de retorno do timeout
  # nao se perder no pipe. Caso morto pelo tempo-limite (124) tem saida
  # PARCIAL, e o hash dela muda com a carga da maquina: o `varrek` ficou no
  # limite dos 300 s, uma regravacao gravou a saida cortada e a conferencia
  # seguinte acusaria mudanca sem mudanca nenhuma. Estouro de tempo e falha.
  cp "$PT_REAL" "$PT"                    # copia nova: o que o caso gravar se perde
  env THREADS=1 $env timeout 300 $EXE "$MP4" $IX $PT $args > "$tmp/$nome.out" 2>/dev/null
  if [ $? -eq 124 ]; then
    echo "  $nome: TEMPO ESGOTADO -- saida parcial, caso invalido"; falhas=$((falhas+1)); continue
  fi
  h=$(sed -E "s|$tmp|TMP|g; s/ *\[[0-9.]+s\]//g" "$tmp/$nome.out" | sha256sum | cut -c1-16)
  dt=$(( $(date +%s) - t0 ))
  # Modo que escreve arquivo: o conteudo dele entra no hash tambem.
  for extra in "$tmp/$nome.pgm" "$tmp/$nome.yuv" "$tmp/$nome.txt"; do
    [ -f "$extra" ] && h="$h-$(sha256sum "$extra" | cut -c1-16)"
  done
  # e3b0c442... e o SHA-256 da string vazia. Caso que nao produz saida passaria
  # em QUALQUER refatoracao -- e criterio vazio, o defeito que este arnes existe
  # para pegar. Aconteceu com o `testa`, que le o arquivo em argv[5] e recebia o
  # numero do quadro ali.
  case "$h" in e3b0c44298fc1c14*) echo "  $nome: SAIDA VAZIA -- caso invalido, corrija o argumento"; falhas=$((falhas+1)); continue;; esac
  if [ "$acao" = grava ]; then
    echo "$h" > "$dir/$nome"
    printf '  %-16s %s\n' "$nome" "$h"
  else
    ref=$(cat "$dir/$nome" 2>/dev/null || echo AUSENTE)
    if [ "$h" = "$ref" ]; then printf '  %-16s ok\n' "$nome"
    else printf '  %-16s MUDOU: %s -> %s\n' "$nome" "$ref" "$h"; falhas=$((falhas+1)); fi
  fi
done

if [ "$(sha256sum "$PT_REAL" | cut -c1-64)" != "$PT_ANTES" ]; then
  echo "[!!] O patches.txt MUDOU durante o arnes -- nada disto vale; conferir com git diff"
  exit 2
fi

if [ "$acao" != grava ]; then
  echo
  if [ "$falhas" -eq 0 ]; then echo "[+] os $((${#casos[@]})) modos iguais a referencia"
  else echo "[!] $falhas modo(s) mudaram -- a refatoracao NAO entra"; exit 1; fi
fi

# --- prova rapida para mudanca que NAO deveria mexer em comportamento ---
#
# Para mudanca so de comentario ou de nome interno, comparar o PRE-PROCESSADO
# contra o HEAD prova mais e custa segundos:
#
#   git show HEAD:src/reparador.c > /tmp/ant.c
#   gcc -E -P $(pkg-config --cflags libavcodec libavutil) /tmp/ant.c > /tmp/ant.i
#   gcc -E -P $(pkg-config --cflags libavcodec libavutil) src/reparador.c > /tmp/dep.i
#   cmp /tmp/ant.i /tmp/dep.i
#
# NAO usar o hash do BINARIO para isso: a compilacao aqui nao e reproduzivel.
# Medido -- dois builds da MESMA fonte dao hashes diferentes, e mudar o nome do
# arquivo de saida muda o hash tambem. Binario diferente nao prova nada.

# --- confere o padrao de documentacao (secao 4c do REFATORACAO.md) ---
#
# Toda funcao, fora os modo_* cujo contrato vive na tabela MODOS, tem que ter
# comentario com UM parametro por linha e o valor de retorno. O teste e
# mecanico: o nome de cada parametro e a palavra "devolve" aparecem no bloco?
#
#   python ferramentas/confere_doc.py
