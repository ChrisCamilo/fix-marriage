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

## Os 23 que faltam

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
