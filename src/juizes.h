/* juizes.h -- a camada de medidas. Ver o cabecalho de juizes.c.
 *
 * Nenhuma destas funcoes le estado global: o que elas medem esta inteiramente
 * nos argumentos. E o que permite testa-las com quadro sintetico, sem
 * decodificador.
 */
#ifndef JUIZES_H
#define JUIZES_H
#include <stdint.h>

/* Ajusta a tolerancia da comparacao entre linhas vizinhas (TOL_COPIA). Zero
 * exige igualdade byte a byte, que NAO mede o borrao -- ver armadilha 40. */
void juizes_tolerancia(int t);

double blocagem(const uint8_t *Y, int w, int h);
double propagacao(const uint8_t *Y, int w, int h);
int linhas_reais(const uint8_t *Y, int w, int h);
int sem_imagem(const uint8_t *Y, int w, int h);
int linha_copia(const uint8_t *Y, int w, int y);
int fronteira_borrao(const uint8_t *Y, int w, int h, int y0);
void estrutura_borrao(const uint8_t *Y, int w, int h, int y0,
                             int *ntrechos, int *maior, int *linhas, int *longos);
double blocagem_faixa(const uint8_t *Y, int w, int y0, int y1);
double tarja_baixo_desvio(const uint8_t *Y, int w, int h);
int tarja_baixo_uniforme(const uint8_t *Y, int w, int h);
int tarja_topo_uniforme(const uint8_t *Y, int w, int h, int valor_exigido);
int linhas_identicas(const uint8_t *Y, int w, int h);
int tarja_baixo_e_16(const uint8_t *Y, int w, int h);

#endif
