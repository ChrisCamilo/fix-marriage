# Mapa dos alvos — o que falta, medido

Gerado pelo modo `mapa` do `reparador.c`, que decodifica cada GOP numa passada e
registra em que macrobloco **cada quadro** para. O filme inteiro em **11 s**.

```bash
./reparador.exe "$MP4" index.txt patches.txt mapa > logs/mapa.txt
```

## O número grande não vale, e é importante dizer por quê

O mapa acusa **1.069 quadros** que param antes do fim — 31% do filme. **Não são
1.069 quadros quebrados.** A distribuição denuncia:

| trecho | mapa acusa | cópias | listra > 40% |
|---|---|---|---|
| 0–499 | 185 | 18 | 125 |
| 500–999 | 160 | 14 | 106 |
| 1000–1499 | 149 | 17 | 123 |
| 2500–2999 | 162 | 9 | 105 |

Uniforme pelo filme inteiro — e bit rot localizado não se distribui assim. Os
três detectores também discordam: dos 1.069, só 43 são cópia e 339 têm listra
alta. A leitura é que o ffmpeg registra erro de macrobloco e oculta sem estragar
o quadro, e a maioria desses 1.069 é disso.

**Escolher alvo por esse número seria varrer o filme inteiro atrás de dano que
não existe.**

## E a tarja não é gabarito universal

Medido no panorama, quadros com tarja 16,000 / desvio 0,000:

| trecho | com tarja |
|---|---|
| 0–499 | 12 de 419 |
| 500–999 | **0** de 384 |
| 1000–1499 | **0** de 404 |
| 1500–1999 | **0** de 380 |
| 2000–2499 | 29 de 411 |
| 2500–2999 | **0** de 402 |
| 3000–3499 | 120 de 369 |

**A tarja só existe nos três trechos que são as ilhas de dano.** No meio do filme
a imagem ocupa a tela toda. Eu vinha tratando a tarja como gabarito de qualquer
quadro; ela é gabarito só onde existe — que, por sorte, é onde o dano está.

## Os 23 que faltavam — hoje são 17

> **Atualizado.** Desfazer os dois patches errados do GOP 3426 fechou os seis do
> grupo (a). Sobram **17**, todos no GOP 0: 11, 13 a 28. As ilhas 2333–2361 e
> 3319–3444 estão completas.

Restringindo às ilhas, onde há gabarito:

```
 frame    bytes para no mb  % decod     tarja situacao
    11     2832         15     0.2%    15.000 trava na fileira 0
    13    36532       6600    80.9%    49.177 trava na fileira 55
    14     4114       8160   100.0%         - nao emite quadro
    15    85300       8160   100.0%         - nao emite quadro
    16    18008       2156    26.4%    49.177 trava na fileira 17
    17   127660       8160   100.0%         - nao emite quadro
    18    13923       3456    42.4%    49.177 trava na fileira 28
    19   143870          1     0.0%         - nao emite quadro
    20    14088       8160   100.0%    49.170 decodifica limpo, tarja 49.17
    21   143110       8160   100.0%         - nao emite quadro
    22    14906       8160   100.0%         - nao emite quadro
    23   129550       3947    48.4%         - nao emite quadro
    24     8209       8160   100.0%         - nao emite quadro
    25   156380       1680    20.6%         - nao emite quadro
    26    16982       8160   100.0%         - nao emite quadro
    27    45239       1155    14.2%         - nao emite quadro
    28    39057       8160   100.0%         - nao emite quadro
  3435      963       8160   100.0%    16.204 decodifica limpo, tarja 16.20
  3436      261       8160   100.0%    16.206 decodifica limpo, tarja 16.21
  3438     2887       8160   100.0%    16.206 decodifica limpo, tarja 16.21
  3439      988       8160   100.0%    16.127 decodifica limpo, tarja 16.13
  3440      261       8160   100.0%    16.069 decodifica limpo, tarja 16.07
  3442     2939       8160   100.0%    16.127 decodifica limpo, tarja 16.13
```

### Três grupos, e eles pedem ataques diferentes

**a) Os seis do GOP 3426 — tarja entre 16,07 e 16,21.** Decodificam limpos,
sem erro nenhum, e erram a tarja por **0,07 a 0,21**. Não é dessincronização:
é desvio pequeno e uniforme. São os alvos mais promissores do filme — quadros
pequenos (261 a 2.939 bytes), inteiros, e a distância até o gabarito é mínima.

**b) Os que travam, com fileira conhecida** — 11, 13, 16, 18, 23, 25, 27. Para
esses o `corta` localiza a borda em byte e a janela sai medida.

**c) Os que não emitem quadro** — 14, 15, 17, 19, 21, 22, 24, 26, 28. O frame
**19 é a raiz**: o erro `MB 1 0, bytestream 143806` aparece no log dos outros,
mas 143.806 de 143.870 é o tamanho dele.

**A ordem que os números sugerem é (a), depois (b), depois (c)** — e não a ordem
em que eu vinha atacando, que foi pelo número do quadro.

# Recontagem com a listra — o dano é muito maior que 168 quadros

## A listra sozinha não serve, e por quê

`linhas_identicas` conta linha exatamente igual à de cima. **Quadro de fade dá
100% e está perfeito** — campo chapado faz toda linha repetir a anterior. Os
frames 0, 12, 3441 e 3443 dão 100% e são bons.

O sinal de defeito é listra alta **com conteúdo real**: a imagem tem textura e
mesmo assim as linhas se repetem. Limiar usado: `listra > 40%` **e**
`desvio do campo > 20`.

| classe | quadros |
|---|---|
| listra > 40% com campo chapado (fade, **bom**) | 243 |
| listra > 40% com conteúdo real (**borrado**) | **279** |
| listra 0% com conteúdo real (**limpo**) | 633 |

## O controle, que é o que valida o limiar

| trecho | ruins / medidos | |
|---|---|---|
| ilha 2333–2361 (fechada) | **0 / 29** | OK |
| ilha 3319–3444 (fechada) | **0 / 126** | OK |
| bloco de abertura 0–12 (fechado) | **0 / 13** | OK |

Zero nas três. Um critério que acusasse quadro bom apareceria justamente ali.

## A contagem

| sinal | quadros |
|---|---|
| **borrados** (listra alta com conteúdo) | 279 |
| **cópia** de outro do mesmo GOP | 232 |
| **união** | **414** |
| destes, que também param cedo | 197 |
| (param cedo, sozinho — sinal fraco, ver acima) | 1.069 |

**414 de 2.769 quadros medidos, 15%.** O registro do projeto dizia **168**, e
esse número contava só as três ilhas que foram investigadas.

| trecho | ruins |
|---|---|
| 0–499 | 77 |
| 500–999 | 66 |
| 1000–1499 | 69 |
| 1500–1999 | 48 |
| 2000–2499 | 60 |
| 2500–2999 | 55 |
| 3000–3499 | 39 |

Espalhado pelo filme inteiro, não em ilhas.

## O caso que motivou a recontagem

O **IDR 29** analisa até o macrobloco 8160 **sem um único erro** — e tem listra
**71,6%** com desvio 82,9. Está borrado.

| linhas | média | desvio | repetidas |
|---|---|---|---|
| 0–119 | 16,00 | 0,00 | 119/120 (tarja) |
| 240–359 | 119,19 | 83,52 | **93/120** |
| 360–479 | 119,22 | 83,15 | **97/120** |
| 720–839 | 119,32 | 82,03 | **95/120** |

**Quadro borrado que analisa sem erro escapa dos dois detectores anteriores** —
não para cedo e não é cópia de ninguém. Só a listra pega. E IDR borrado envenena
o GOP inteiro: os 12 quadros do GOP 29 que param cedo podem estar parando por
causa dele.

# Exploração dos quadros fora das ilhas

## 81 dos 132 IDRs estão borrados, e isso ordena tudo

| | GOPs | média de quadros ruins no GOP |
|---|---|---|
| com IDR borrado | **81** | **4,05** |
| com IDR bom | 51 | 1,69 |

**Nenhum dos 81 GOPs com IDR borrado tem GOP limpo.** Dez dos 51 com IDR bom
estão perfeitos. IDR danificado garante GOP danificado — e são 122 de 132 GOPs
afetados.

Varrer IDR é **barato**: a cadeia é ele mesmo, **650 candidatos/s** contra ~200
de um quadro no meio de GOP.

## Duas modalidades de dano em IDR

Medindo a razão de consumo dos oito IDRs borrados mais baratos:

| IDR | bytes | dispara em | % usado |
|---|---|---|---|
| 3047 | 68.616 | 4.609 | **6,7%** |
| 1495 | 75.749 | 8.779 | 11,6% |
| 323 | 72.185 | 9.934 | 13,8% |
| 1833 | 69.664 | 15.293 | 22,0% |
| 814 | 71.455 | 25.707 | 36,0% |
| 1143 | 50.635 | 50.635 | **100,0%** |
| 843 | 57.974 | 57.974 | **100,0%** |
| 3097 | 76.099 | 76.099 | **100,0%** |

**a) Dessincronização silenciosa** — consome uma fração e dispara. Armadilha 38.
**b) Consome tudo e ainda borra** — modalidade diferente, não explicada.

## O limiar da listra tem faixa, não valor

Os **135 quadros comprovadamente bons com conteúdo real** (ilha fechada, tarja
16,000/0,000, desvio > 20) dão listra **0,0% — mínimo, mediana e máximo**.
Nenhum passa de zero.

**Mas os 135 são todos de trecho de fade**, cena simples. Cena complexa pode ter
linha repetida legitimamente, e extrapolar dali para o filme inteiro é o mesmo
erro que cometi com o juiz de consumo na armadilha 36.

| limiar | borrados no filme | nas ilhas fechadas |
|---|---|---|
| > 60% | **176** | 0 |
| > 40% | 279 | 0 |
| > 25% | 384 | 0 |
| > 0% | 1.246 | 0 |

**O núcleo defensável é 176**, o corcunda em 60–80%. Entre 10% e 60% é incerto.

## O obstáculo de verdade: fora das ilhas não há gabarito

Não há tarja (só existe nos três trechos) e não há rampa. Dano se **detecta**
— cópia, disparo, listra — mas reparo não se **confirma**.

**Com uma exceção, e ela é a linha de ataque:** a **razão de consumo**. Um quadro
de conteúdo real consome ~100% do payload, medido nos três IDRs reparados. Um
IDR que usa 6,7% está disparando, e um reparo tem que levá-lo a ~100%. Isso é
gabarito *a priori*, não depende de pixel nem de referência, e funciona em
qualquer lugar do filme.

**Os cinco IDRs que disparam — 3047, 1495, 323, 1833, 814 — são os alvos com
juiz utilizável fora das ilhas.** O 3047 é o mais extremo (6,7%) e um dos mais
baratos (68.616 bytes).
