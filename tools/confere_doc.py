"""Confere o padrao de documentacao da secao 4c do docs/REFATORACAO.md.

Toda funcao precisa de um bloco com UM parametro por linha e o valor de
retorno. Os modo_* sao a excecao deliberada: o contrato deles vive na tabela
MODOS, e repetir criaria dois lugares para dessincronizar.

O teste e mecanico -- o nome de cada parametro e a palavra "devolve" aparecem no
comentario? -- entao ele nao garante que o texto esteja CERTO, so que existe.
Garantir que esta certo e trabalho de quem le.

  python tools/confere_doc.py
"""
import re, sys, os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
falhas = 0
for arq in ('src/reparador.c', 'src/juizes.c'):
    L = open(arq, encoding='utf-8', errors='surrogateescape').read().split('\n')
    bons = total = 0
    for i, l in enumerate(L):
        m = re.match(r'^(?:static\s+)?[A-Za-z_][\w]*[\w \*]*?\*?([A-Za-z_]\w*)\s*\(([^;]*)\)\s*\{\s*$', l)
        if not m or re.match(r'^\s*(if|for|while|switch|else)\b', l):
            continue
        nome, args = m.group(1), m.group(2)
        if nome.startswith('modo_'):
            continue
        total += 1
        j, bloco = i - 1, []
        while j >= 0 and (L[j].strip().startswith('*') or L[j].strip().startswith('/*')):
            bloco.insert(0, L[j]); j -= 1
        txt = '\n'.join(bloco).lower()
        ps = [a.strip().split()[-1].lstrip('*') for a in args.split(',')
              if a.strip() and a.strip() != 'void']
        falta = [p for p in ps if not re.search(r'\b' + re.escape(p.lower()) + r'\b', txt)]
        if bloco and 'devolve' in txt and not falta:
            bons += 1
        else:
            motivo = []
            if not bloco: motivo.append('sem comentario')
            elif 'devolve' not in txt: motivo.append('sem o retorno')
            if falta: motivo.append('sem os parametros ' + ', '.join(falta))
            print('  %-24s %s' % (nome, '; '.join(motivo)))
            falhas += 1
    print('%-20s %d de %d no padrao' % (arq, bons, total))

sys.exit(1 if falhas else 0)
