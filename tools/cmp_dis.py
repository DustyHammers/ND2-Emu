import re,sys,collections
SFR={'SPLIM':0x20,'ACCAL':0x22,'ACCAH':0x24,'ACCAU':0x26,'ACCBL':0x28,'ACCBH':0x2a,'ACCBU':0x2c,'PCL':0x2e,'PCH':0x30,
     'DSRPAG':0x32,'DSWPAG':0x34,'RCOUNT':0x36,'DCOUNT':0x38,'SR':0x42,'CORCON':0x44,'MODCON':0x46,'TBLPAG':0x54,'DISICNT':0x52}
def norm(s, is_ghidra):
    s=s.strip()
    s=re.sub(r'\s+',' ',s)
    # map SFR names in operand context (not in movpag target) to hex
    if not s.startswith('movpag'):
        def rep(m):
            n=m.group(0); return '0x%04x'%SFR[n]
        s=re.sub(r'\b(SPLIM|ACCAL|ACCAH|ACCAU|ACCBL|ACCBH|ACCBU|PCL|PCH|DSRPAG|DSWPAG|RCOUNT|DCOUNT|SR|CORCON|MODCON|TBLPAG|DISICNT)\b',rep,s)
    s=re.sub(r'0x0+([0-9a-f])',r'0x\1',s)
    s=s.lower()
    # known cosmetic aliases / Ghidra quirks
    s=re.sub(r'^pop\.d (w\d+)$',r'mov.d [--w15],\1',s)
    s=re.sub(r'^push\.d (w\d+)$',r'mov.d \1,[w15++]',s)
    s=re.sub(r'^pop (w\d+)$',r'mov.w [--w15],\1',s)
    s=re.sub(r'^push (w\d+)$',r'mov.w \1,[w15++]',s)
    s=re.sub(r'^mov\.b (0x[0-9a-f]+),',r'mov.b #\1,',s) if is_ghidra else s
    s=re.sub(r'^repeat 0x11 .*$','repeat #0x11',s)
    s=re.sub(r',$','',s)
    s=s.replace('[w11],w','[w11],,w').replace('[w10],w','[w10],,w')
    return s
g={}; 
for ln in open(sys.argv[1]):
    a,l,t=ln.rstrip('\n').split('\t',2); g[int(a,16)//2]=(int(l),t)
mism=collections.Counter(); ex={}
n=0;bad=0
for ln in open(sys.argv[2]):
    a,l,t,raw=ln.rstrip('\n').split('\t'); a=int(a,16)
    gl,gt=g[a]; n+=1
    G=norm(gt,True); M=norm(t,False)
    if G!=M or int(l)!=gl:
        bad+=1; key=(G.split(' ')[0], M.split(' ')[0])
        mism[key]+=1
        ex.setdefault(key,[]).append((hex(a),raw,gt,t))
print('total',n,'mismatch',bad)
for k,c in mism.most_common(60):
    print(c,k, ex[k][0], ex[k][-1] if len(ex[k])>1 else '')
