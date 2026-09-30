# prefixo.py -- quadros sinteticos para acelerar as buscas do `reparador`.
#
# O `avanco` (e toda busca) decodifica o GOP desde o IDR a cada combinacao, e o
# contexto do libavcodec nao se copia. No fim do GOP 0 isso e ~23 quadros por
# combinacao, 90-150 combinacoes por segundo (2026-09-30).
#
# Mas a leitura CABAC do alvo nao depende dos pixels das referencias: o que ela
# usa dos quadros anteriores e so a ESTRUTURA do DPB (quais quadros existem,
# frame_num, POC, marcacao de referencia) -- e isso vem dos cabecalhos. Entao
# cada quadro anterior pode ser trocado por um com o MESMO cabecalho (do arquivo
# remendado) e o corpo todo pulado: 8.160 MBs de mb_skip_flag, que o ffmpeg
# decodifica em ~1 ms. O MB onde o alvo para e o mesmo; a imagem do alvo nao.
# Por isso o prefixo so serve para nota de progresso (mb_alcancado), nunca para
# os pisos de imagem nem para aprovar reparo -- o `reparador` recusa as duas
# coisas com PREFIXO ligado.
#
# A faixa da busca comeca nos DADOS do slice: troca no cabecalho do alvo mexe no
# POC e no DPB, que e o que o prefixo simplifica. Conferido nas seis buscas de 1
# bit do fim do GOP 0 (2026-09-30): mesmo histograma e mesmo arquivo, exceto o
# bit do cabecalho do 17 que leva o POC de 36 a 164 (MB 1.151 sem prefixo, -1
# com ele: sem os B o quadro nao e emitido).
#
# Ficam como estao: o IDR (ancora) e todo quadro cujo cabecalho nao se le.
# Saem de todo (linha `indice -`, o `reparador` nem manda o pacote): os quadros
# de nal_ref_idc 0, que nunca entram no DPB como referencia -- nem o B direto
# le deles (o colocalizado e a L1[0], sempre de referencia).
#
# uso (da raiz):
#   python tools/anchor/prefixo.py <alvo> <saida.txt>
#     saida.txt  uma linha por quadro trocado: `indice hexa`, o hexa com o
#                prefixo de 4 bytes do tamanho, ou `indice -` para o quadro
#                que sai; o `reparador` le com PREFIXO= e so usa os anteriores
#                ao alvo da busca
import sys, os
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI); sys.path.insert(0, os.path.dirname(AQUI))
import cabac_p as C, f11
import cabecalho_slice as CS
from cabeca import escapa, SKIP


def sintetico(nal):
    """O NAL com o cabecalho dele e o corpo todo pulado.

      nal  bytes do NAL sem o prefixo de tamanho (do byte de cabecalho em diante)

    Devolve: os bytes do NAL sintetico, sem prefixo; ou None se o cabecalho nao
    se le ou o quadro e I (I nao tem MB pulado)."""
    try: h = CS.le_cabecalho(nal)
    except CS.Invalido: return None
    tipo = {0: 'P', 1: 'B', 2: 'I'}[h['slice_type'] % 5]
    if tipo == 'I': return None
    rb, _ = f11.rbsp(nal)
    qp = 26 + h['qp_delta']; mod = h.get('cabac_init', 0)
    # o ultimo MB leva end_of_slice_flag = 1; o flush do term(1) ja poe o
    # rbsp_stop_one_bit, falta so completar o byte
    bits = C.codifica([SKIP] * 8160, qp, mod, tipo=tipo)
    eb = [int(x) for x in rb[:h['bits']]] + bits
    eb += [0] * (-len(eb) % 8)
    rbe = bytes(int(''.join(map(str, eb[k:k + 8])), 2) for k in range(0, len(eb), 8))
    return escapa(rbe)[0]


def main():
    alvo, saida = int(sys.argv[1]), sys.argv[2]
    d, ix = f11.buffer()
    anc = max(i for i, _, _, idr in ix[:alvo + 1] if idr)
    n = 0
    with open(saida, 'w') as f:
        for i in range(anc + 1, alvo):
            _, o, s, _ = ix[i]
            if not (d[o + 4] >> 5) & 3:
                f.write('%d -\n' % i); n += 1; continue
            nal = sintetico(bytes(d[o + 4:o + s]))
            if nal is None:
                print('quadro %d fica como esta' % i); continue
            f.write('%d %s\n' % (i, (len(nal).to_bytes(4, 'big') + nal).hex())); n += 1
    print('%d quadros trocados ou tirados entre %d e %d' % (n, anc, alvo))


if __name__ == '__main__':
    main()
