# jm_trace.py -- le o trace_dec.txt do JM (build com ENABLE_TRACING) e devolve a
# sintaxe de cada MB, por quadro.
#
#   le(caminho) -> {poc: {'tipo': t, 'mbs': {mb: [(elemento, (valores...)), ...]}}}
#   cabecalhos(caminho) -> {poc: {'campos': {...}, 'byte_ini': n}}
#
# `tipo` e o Type do cabecalho de MB do trace (0 = P, 1 = B, 2 = I). Os valores
# sao os numeros que o JM imprime depois do nome: para elementos de cabecalho de
# MB, o valor entre parenteses; para coeficientes ("Luma sng", "DC luma 16x16",
# "AC Chroma"...), as duas colunas numericas. No trace do JM, `mb_skip_flag` (1)
# quer dizer MB NAO pulado, e o `mb_type` de P conta os intra a partir de 6
# (6 = I4x4; 7-30 = I16x16 na ordem do slice I; 8 = I16x16 horizontal, 9 = DC,
# ambos sem resíduo).
#
# Uso (da raiz): python tools/anchor/jm_trace.py <trace_dec.txt> [saida.pkl]
# imprime, para cada quadro P, quantos MBs fogem do padrao do fade (I16x16 DC,
# croma DC, sem residuo) e onde.
import re, sys, collections, pickle

def le(path):
    q = collections.OrderedDict(); cur = None
    for l in open(path, errors='replace'):
        m = re.match(r'\*+ POC: (\d+) \(I/P\) MB: (\d+) Slice: \d+ Type (\d+)', l)
        if m:
            poc, mb, ty = map(int, m.groups())
            q.setdefault(poc, {'tipo': ty, 'mbs': {}})
            cur = []; q[poc]['mbs'][mb] = cur; continue
        if cur is None or not l.startswith('@'): continue
        m = re.match(r'@(\d+)\s+(.+?)\s{2,}(.*)$', l.rstrip())
        if not m: continue
        if re.match(r'(SH|PPS|SPS|SEI):', m.group(2)):
            cur = None; continue                 # cabecalho seguinte: o MB acabou
        cur.append((m.group(2).strip(), tuple(int(x) for x in re.findall(r'-?\d+', m.group(3)))))
    return q

def cabecalhos(path):
    """Cabecalho de cada slice no trace: {poc: dict(campos, byte_ini)}.

      path  o trace_dec.txt

    Devolve: por POC (o pic_order_cnt_lsb), os campos do cabecalho do slice
    (nome -> valor) e `byte_ini`, o byte do NAL (contando o byte de cabecalho
    do NAL) onde comeca o slice data do CABAC -- depois dos
    cabac_alignment_one_bit. O @ do trace conta bits de forma continua; o
    primeiro campo (first_mb_in_slice) comeca logo depois do byte do NAL."""
    out = {}; cur = None
    for l in open(path, errors='replace'):
        m = re.match(r'@(\d+)\s+SH: (\w+)\s+([01]+)\s+\(\s*(-?\d+)\)', l)
        if not m: continue
        pos, nome, bits, val = int(m.group(1)), m.group(2), m.group(3), int(m.group(4))
        if nome == 'first_mb_in_slice':
            cur = {'_ini': pos, 'campos': {}}
        if cur is None: continue
        cur['campos'].setdefault(nome, val); cur['_fim'] = pos + len(bits)
        if nome == 'pic_order_cnt_lsb': out[val] = cur
    for c in out.values():
        c['byte_ini'] = (8 + c['_fim'] - c['_ini'] + 7) // 8
    return out

PADRAO = [('mb_skip_flag', (1,)), ('mb_type', (9,)), ('intra_chroma_pred_mode', (0,)),
          ('mb_qp_delta', (0,)), ('DC luma 16x16', (0, 0))]

def fora_do_padrao(mbs):
    """MBs cuja sintaxe nao e o padrao do fade (o end_of_slice nao conta)."""
    return {m: se for m, se in mbs.items() if [x for x in se if x[0] != 'end_of_slice_flag'] != PADRAO}

if __name__ == '__main__':
    q = le(sys.argv[1])
    if len(sys.argv) > 2: pickle.dump(q, open(sys.argv[2], 'wb'))
    for poc, f in q.items():
        if f['tipo'] != 0: continue
        exc = fora_do_padrao(f['mbs'])
        fil = collections.Counter(m // 120 for m in exc)
        print('POC %d (P): %d MBs, %d fora do padrao; fileiras: %s' % (poc, len(f['mbs']), len(exc),
              dict(sorted(fil.items())) if len(fil) < 8 else '%d fileiras (%d-%d)' % (len(fil), min(fil), max(fil))))
