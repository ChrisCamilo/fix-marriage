#!/usr/bin/env python3
"""ferramentas.py - apoio ao reparador.c

  index   <orig.mp4> <index.txt>              gera o indice de amostras
  base    <orig.mp4> <patches.txt>            gera os patches deterministicos
                                              (prefixos de NAL e cabecalhos)
  build   <orig.mp4> <patches.txt> <out.mp4>  aplica os patches e remonta o moov

O MP4 original nunca e alterado. patches.txt e a unica fonte de verdade.
"""
import struct, sys, os

V_N, V_TS, V_DUR, V_DELTA = 3445, 30000, 3448445, 1001
MOOV_OFF, MOOV_SIZE = 24, 61164
MVHD_TS, MVHD_DUR, TRAK_DUR = 90000, 10348816, 10345335
W, H, A_N = 1920, 1080, 5390
SPS = bytes.fromhex("674d4029965200f0044fcb29010101400000fa40003a9821")
PPS = bytes.fromhex("68eb7352")

def indice(d):
    stsz=[struct.unpack_from(">I",d,4713+20+4*i)[0] for i in range(V_N)]
    vco =[struct.unpack_from(">I",d,18513+16+4*i)[0] for i in range(345)]
    spc=[10]*344+[5]; pos=[]; si=0
    for ci in range(345):
        off=vco[ci]
        for _ in range(spc[ci]): pos.append(off); off+=stsz[si]; si+=1
    return pos, stsz, vco

PREF=[0x01,0x41,0x65,0x21,0x61]

# Os 17 bits que seguem o byte NAL num IDR sao fixos: first_mb ue(0),
# slice_type ue(7), pps_id ue(0) e frame_num de 8 bits em zero. Serve para
# desempatar o byte de cabecalho sem inventar. Ver molde_idr.py.
FIXO_IDR = "10001000100000000"

def cheira_a_idr(d, q, folga=2):
    bs = "".join(f"{x:08b}" for x in d[q+5:q+8])[:17]
    return sum(1 for a, b in zip(FIXO_IDR, bs) if a != b) <= folga

def main():
    cmd=sys.argv[1]
    d=bytearray(open(sys.argv[2],"rb").read())
    pos,stsz,vco=indice(d)

    if cmd=="index":
        # o tipo (IDR) e lido do byte de cabecalho JA corrigido pelos patches base
        pat={}
        pb=sys.argv[4] if len(sys.argv)>4 else None
        if pb and os.path.exists(pb):
            for l in open(pb):
                o,b=l.split(); d[int(o)]^=1<<int(b)
        with open(sys.argv[3],"w",newline="\n") as f:
            for i in range(V_N):
                idr = 1 if (d[pos[i]+4]&0x1f)==5 else 0
                f.write(f"{i} {pos[i]} {stsz[i]} {idr}\n")
        print(f"[+] indice com {V_N} amostras -> {sys.argv[3]}")

    elif cmd=="base":
        if os.path.exists(sys.argv[3]):
            raise SystemExit(
                f"[!] {sys.argv[3]} ja existe e seria SOBRESCRITO.\n"
                f"    Esse arquivo e append-only e guarda os reparos reais.\n"
                f"    Para regerar do zero, mova o atual para outro nome antes.")
        linhas=[]
        for i in range(V_N):
            q,sz=pos[i],stsz[i]
            lido=struct.unpack_from(">I",d,q)[0]
            x=lido ^ (sz-4)
            for bit in range(32):                 # prefixo de tamanho do NAL
                if x>>bit & 1:
                    linhas.append((q+3-bit//8, bit%8))
            h=d[q+4]
            if h not in PREF:                     # byte de cabecalho do NAL
                c=sorted((bin(h^v).count("1"),PREF.index(v),v) for v in PREF)
                # Empate aqui NAO pode ser resolvido pela ordem do PREF: o byte
                # 0x45 fica a 1 bit tanto de 0x41 (comum) quanto de 0x65 (IDR),
                # e a ordem da lista dava 0x41 -- foi assim que os IDRs 2554 e
                # 2913 viraram frame comum e sumiram do censo. Desempata pelo
                # cabecalho de slice, que e evidencia e esta no arquivo.
                empate=[v for dd,_,v in c if dd==c[0][0]]
                escolha=c[0][2]
                if len(empate) > 1 and 0x65 in empate:
                    # O desempate e simetrico: o cabecalho de slice decide para
                    # os dois lados. Sem isso o 1452, que empata entre 0x65 e
                    # 0x21 e NAO tem cabecalho de IDR, era promovido a IDR so
                    # por 0x65 vir antes na ordenacao.
                    if cheira_a_idr(d, q):
                        escolha=0x65
                    else:
                        naoidr=[v for v in empate if v != 0x65]
                        if naoidr: escolha=naoidr[0]
                y=h^escolha
                for bit in range(8):
                    if y>>bit & 1: linhas.append((q+4, bit))
        with open(sys.argv[3],"w",newline="\n") as f:
            for o,b in linhas: f.write(f"{o} {b}\n")
        print(f"[+] {len(linhas)} patches deterministicos -> {sys.argv[3]}")

    elif cmd=="build":
        for l in open(sys.argv[3]):
            o,b=l.split(); d[int(o)]^=1<<int(b)
        key=[i+1 for i in range(V_N) if (d[pos[i]+4]&0x1f)==5]
        def atom(t,b): return struct.pack(">I",len(b)+8)+t+b
        MAT=struct.pack(">9i",0x10000,0,0,0,0x10000,0,0,0,0x40000000); CT=0xd5eb0125
        mvhd=atom(b"mvhd",struct.pack(">IIIII",0,CT,CT,MVHD_TS,MVHD_DUR)
              +struct.pack(">IHH",0x10000,0x100,0)+b"\0"*8+MAT+b"\0"*24
              +struct.pack(">I",3))
        tkhd=atom(b"tkhd",struct.pack(">IIIIII",7,CT,CT,1,0,TRAK_DUR)+b"\0"*8
              +struct.pack(">HHHH",0,0,0,0)+MAT+struct.pack(">II",W<<16,H<<16))
        edts=atom(b"edts",atom(b"elst",struct.pack(">IIiII",0,1,TRAK_DUR,V_DELTA,0x10000)))
        mdhd=atom(b"mdhd",struct.pack(">IIIIIHH",0,CT,CT,V_TS,V_DUR,0x55C4,0))
        hdlr=atom(b"hdlr",struct.pack(">I",0)+b"\0\0\0\0"+b"vide"+b"\0"*12+b"VideoHandler\0")
        avcC=atom(b"avcC",bytes([1,SPS[1],SPS[2],SPS[3],0xFF,0xE1])
              +struct.pack(">H",len(SPS))+SPS+bytes([1])+struct.pack(">H",len(PPS))+PPS)
        avc1=atom(b"avc1",b"\0"*6+struct.pack(">H",1)+b"\0"*16
              +struct.pack(">HHIIIH",W,H,0x480000,0x480000,0,1)
              +bytes([10])+b"AVC Coding"+b"\0"*21+struct.pack(">Hh",0x18,-1)+avcC)
        stbl=atom(b"stbl",atom(b"stsd",struct.pack(">II",0,1)+avc1)
              +atom(b"stts",struct.pack(">IIII",0,1,V_N,V_DELTA))
              +atom(b"stss",struct.pack(">II",0,len(key))
                    +b"".join(struct.pack(">I",k) for k in key))
              +bytes(d[19909:34669])+bytes(d[4673:4713])
              +bytes(d[4713:18513])+bytes(d[18513:19909]))
        vtrak=atom(b"trak",tkhd+edts+atom(b"mdia",mdhd+hdlr
              +atom(b"minf",atom(b"vmhd",struct.pack(">IHHHH",1,0,0,0,0))
              +atom(b"dinf",atom(b"dref",struct.pack(">II",0,1)
              +atom(b"url ",struct.pack(">I",1))))+stbl)))
        # --- trak de audio: reconstroi stsc/stsz por restricao de espaco ---
        aco=[struct.unpack_from(">I",d,59728+16+4*i)[0] for i in range(345)]
        espaco=[(vco[i+1] if i+1<345 else len(d))-aco[i] for i in range(345)]
        astsz=[struct.unpack_from(">I",d,38148+20+4*i)[0] for i in range(A_N)]
        sane=[v for v in astsz if 700<=v<=1100]; MED=sum(sane)/len(sane)
        nn=[round(e/MED) for e in espaco]
        cache={}; inc=set()
        for i,v in enumerate(astsz):
            if not (700<=v<=1100):
                if v not in cache:
                    cache[v]=min(range(700,1101),
                                 key=lambda c:(bin(v^c).count("1"),abs(c-MED)))
                astsz[i]=cache[v]; inc.add(i)
        si2=0
        for ci in range(345):
            k=nn[ci]; dd=espaco[ci]-sum(astsz[si2:si2+k])
            if dd:
                alvo=[j for j in range(si2,si2+k) if j in inc] or list(range(si2,si2+k))
                por=dd//len(alvo)
                for j in alvo: astsz[j]+=por
                astsz[alvo[-1]]+=dd-por*len(alvo)
            si2+=k
        astsc=[]
        for i,k in enumerate(nn):
            if not astsc or astsc[-1][1]!=k: astsc.append([i+1,k,1])
        atrak=bytearray(d[34669:61124])
        ap=lambda o,v: struct.pack_into(">I",atrak,o-34669,v)
        ap(35156,16+12*len(astsc)); ap(35160,0x73747363); ap(35168,len(astsc))
        for i,e in enumerate(astsc):
            ap(35172+12*i,e[0]); ap(35176+12*i,e[1]); ap(35180+12*i,e[2])
        ap(38148,20+4*A_N); ap(38152,0x7374737a); ap(38160,0); ap(38164,A_N)
        for i,v in enumerate(astsz): ap(38168+4*i,v)
        corpo=mvhd+vtrak+bytes(atrak)+bytes(d[61124:61188])
        folga=(MOOV_SIZE-8)-len(corpo)
        if folga<0: raise SystemExit("moov nao cabe")
        corpo+=atom(b"free",b"\0"*(folga-8)) if folga>=8 else b"\0"*folga
        d[0:4]=struct.pack(">I",24)
        d[MOOV_OFF:MOOV_OFF+MOOV_SIZE]=struct.pack(">I",MOOV_SIZE)+b"moov"+corpo
        struct.pack_into(">Q",d,110176,len(d)-110168)
        open(sys.argv[4],"wb").write(bytes(d))
        print(f"[+] {sys.argv[4]} gerado ({len(key)} keyframes)")

main()
