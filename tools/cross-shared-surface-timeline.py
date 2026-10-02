"""Cruza, pelo token de alocacao, quando o dwm.exe abriu/copiou cada superficie
compartilhada e quando o processo produtor desenhou nela (Corrida 185, doc 543).

Uso: python tools/cross-shared-surface-timeline.py <log FINAL do dwm.exe> <log do produtor>
Coluna final: draws do produtor DEPOIS da primeira copia do DWM / total.
Limite: tokens repetidos entre aberturas podem misturar a correspondencia.
"""
import sys,re
def t(s):
    try: h,m,x=s[:12].split(':');return int(h)*3600+int(m)*60+float(x)
    except: return None
def fmt(v): 
    if v is None: return '   -   '
    h=int(v//3600);m=int(v%3600//60);return '%02d:%02d:%06.3f'%(h,m,v%60)
D=open(sys.argv[1],encoding='utf-8',errors='replace').read().splitlines()
S=open(sys.argv[2],encoding='utf-8',errors='replace').read().splitlines()
# DWM: OpenResource geometria token -> next "carregada via SDMA GPU" for that res
opens={}  # token -> (t_open, res)
pend_tok=None
for l in D:
    m=re.search(r'OpenResource: geometria extraida (\d+x\d+).*token=(0x[0-9A-F]+)',l)
    if m: pend_tok=(m.group(2),m.group(1),t(l)); continue
    m=re.search(r'OpenResource: res=([0-9A-F]+)',l)
    if m and pend_tok:
        opens[m.group(1)]=pend_tok; pend_tok=None
loads={}
for i,l in enumerate(D):
    if 'carregada via SDMA GPU' in l:
        # find res from preceding CopyStagingBufferToImage? use hKM; fallback: last opened res needing load: search back for "UploadImageData"/"Bc250UmdVkCreateResource" not reliable
        pass
# simpler: DWM load lines carry WxH and hKM; OpenResource res line carries hKMResource
hk={}
for l in D:
    m=re.search(r'OpenResource: res=([0-9A-F]+) hKMResource=(\d+)',l)
    if m and m.group(1) in opens: hk[m.group(2)]=opens[m.group(1)]
dl={}
for l in D:
    m=re.search(r'carregada via SDMA GPU: (\d+x\d+) .*hKM=(\d+)',l)
    if m and m.group(2) in hk: dl.setdefault(hk[m.group(2)][0],[]).append(t(l))
# StartMenu: token -> res, draws into res (RT0=res), flushes
tok2res={};last_tok=None
for l in S:
    m=re.search(r'Allocate ENTER: Token=(0x[0-9A-F]+)',l)
    if m: last_tok=m.group(1); continue
    m=re.search(r'CreateResource: res=([0-9A-F]+) ',l)
    if m and last_tok: tok2res[last_tok]=m.group(1); last_tok=None
flush_t=[t(l) for l in S if 'CONCLUIDOS com SUCESSO' in l]
import bisect
print('%-20s %-9s %-13s %-13s %-13s %-13s %s'%('token','WxH','dwm_open','dwm_carga','sm_1o_draw','sm_ultimo_draw','draws/depois_da_carga'))
for tok,(tk,wh,to) in sorted(opens.values().__iter__().__class__ and {v[0]:v for v in opens.values()}.items(), key=lambda kv: kv[1][2] or 0):
    res=tok2res.get(tok)
    draws=[t(l) for l in S if res and ('RT0=%s'%res) in l and 'DRAW GRAVADO' in l]
    loadt=(dl.get(tok) or [None])[0]
    after=sum(1 for d in draws if loadt and d>loadt)
    print('%-20s %-9s %-13s %-13s %-13s %-13s %d/%d'%(tok,wh,fmt(to),fmt(loadt),fmt(draws[0] if draws else None),fmt(draws[-1] if draws else None),after,len(draws)))
