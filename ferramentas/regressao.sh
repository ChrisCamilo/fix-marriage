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
PT=patches.txt
EXE=./reparador.exe

acao=${1:-confere}
dir=${2:-dados/regressao}
# Diretorio FIXO, nao mktemp. Os modos que gravam arquivo ecoam o caminho no
# stdout, e o MSYS2 converte o caminho POSIX para a forma Windows ao passar para
# programa nativo -- entao filtrar "$tmp" no stdout nao funciona: o programa
# imprime C:/msys64/tmp/... e o filtro procurava /tmp/... Caminho constante
# resolve na origem, sem filtro.
tmp=logs/.regressao
rm -rf "$tmp"; mkdir -p "$tmp"

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
  "vizinho|vizinho 2361 2361|"
  "recupera|recupera 2361 2361|"
  "ranking|ranking|"
  "varre1|varre1 11 0 5 30|"
  "varre2|varre2 11 5 24|"
  "varrek|varrek 11 2 5 24|"
  "cresce|cresce 11 5 24|"
  "avanco_mb|avanco 11 1 5 40|"
  "avanco_consumo|avanco 11 1 5 40|PONTUA_CONSUMO=1"
  "avanco_topo|avanco 11 1 5 40|PISO_TOPO=2"
  "corta|corta 11 5 60 5|"
  "corte|corte 11|"
  "cortes|cortes 11 13|"
  "campo|campo 11 LISTA|"
  "trinca|trinca 3047 LISTA|TOL_COPIA=1"
  "testa|testa TESTALST|"
  "mapa|mapa|"
  "panorama|panorama|"
  "dump|dump 2333 SAIDA.pgm|"
  "dumpyuv|dumpyuv 2333 SAIDA.yuv|"
  "serie|serie 2333 2340 SAIDA.yuv SAIDA.txt|"
  "verify|verify|BASE_N=1830"
  "report|report|"
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
  h=$(env THREADS=1 $env timeout 300 $EXE "$MP4" $IX $PT $args 2>/dev/null \
      | sed -E "s|$tmp|TMP|g; s/ *\[[0-9.]+s\]//g" | sha256sum | cut -c1-16)
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

if [ "$acao" != grava ]; then
  echo
  if [ "$falhas" -eq 0 ]; then echo "[+] os $((${#casos[@]})) modos iguais a referencia"
  else echo "[!] $falhas modo(s) mudaram -- a refatoracao NAO entra"; exit 1; fi
fi
