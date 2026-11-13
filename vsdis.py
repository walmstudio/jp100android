import struct,sys
OP={0:'nop',1:'mov',2:'add',3:'sub',4:'mad',5:'mul',6:'rcp',7:'rsq',8:'dp3',9:'dp4',10:'min',11:'max',12:'slt',13:'sge',14:'exp',15:'log',16:'lit',17:'dst',18:'lrp',19:'frc',20:'m4x4',21:'m4x3',22:'m3x4',23:'m3x3',24:'m3x2',25:'call',26:'callnz',27:'loop',28:'ret',29:'endloop',30:'label',31:'dcl',32:'pow',33:'crs',34:'sgn',35:'abs',36:'nrm',37:'sincos',38:'rep',39:'endrep',40:'if',41:'ifc',42:'else',43:'endif',44:'break',45:'breakc',46:'mova',47:'defb',48:'defi',81:'def'}
USE=['position','blendweight','blendindices','normal','psize','texcoord','tangent','binormal','tessfactor','positiont','color','fog','depth','sample']
def regname(t,n,vs=True):
    if t==0:return 'r%d'%n
    if t==1:return 'v%d'%n
    if t==2:return 'c%d'%n
    if t==3:return 'a%d'%n
    if t==4:return ['oPos','oFog','oPts'][n] if n<3 else 'oR%d'%n
    if t==5:return 'oD%d'%n
    if t==6:return 'oT%d'%n
    if t==7:return 'o%d'%n
    if t==8:return 'i%d'%n
    if t==14:return 'b%d'%n
    if t==15:return 'aL'
    return 't%d_%d'%(t,n)
def parse(buf):
    ver=struct.unpack_from('<I',buf,0)[0]
    out=['// version %08x'%ver]; i=1; n=len(buf)//4
    w=struct.unpack('<%dI'%n,buf[:n*4])
    while i<n:
        t=w[i]; op=t&0xFFFF
        if op==0xFFFF: break
        if op==0xFFFE:
            ln=(t>>16)&0x7FFF; i+=1+ln; continue
        ln=(t>>24)&0xF
        if (ver>>8)&0xFF<2:  # SM1.x: length implicit
            ln={81:5,31:2,0:0,1:2,2:3,3:3,4:4,5:3,6:2,7:2,8:3,9:3,10:3,11:3,12:3,13:3,14:2,15:2,16:2,17:3,18:4,19:2,20:3,21:3,22:3,23:3,24:3,32:3,35:2,36:2,37:2}.get(op,0)
        args=w[i+1:i+1+ln]
        def regtype(x): return ((x>>28)&7)|(((x>>11)&3)<<3)
        def dst(x):
            m=(x>>16)&0xF; s=''.join(c for k,c in enumerate('xyzw') if m>>k&1)
            mod=(x>>20)&0xF; sat='_sat' if mod&1 else ''
            return regname(regtype(x),x&0x7FF)+('.'+s if m!=15 else ''),sat
        def src(args,k):
            x=args[k]; sw=(x>>16)&0xFF; s=''.join('xyzw'[(sw>>(2*j))&3] for j in range(4))
            if s=='xyzw': s=''
            else:
                while len(s)>1 and s[-1]==s[-2]: s=s[:-1]
            nm=regname(regtype(x),x&0x7FF)
            extra=0
            if (x>>13)&1:   # relative
                nm+='[%s]'%('a0.x' if True else '');extra=1
            mod=(x>>24)&0xF
            nm={1:'-%s',2:'bias(%s)',3:'-bias(%s)',4:'sign(%s)',5:'-sign(%s)',6:'comp(%s)',7:'%s_x2',8:'-%s_x2',11:'abs(%s)',12:'-abs(%s)'}.get(mod,'%s')%nm if mod else nm
            return nm+('.'+s if s else ''),extra
        name=OP.get(op,'op%d'%op)
        if op==31:
            u=args[0]&0x1F; ui=(args[0]>>16)&0xF
            d,_=dst(args[1]); out.append('dcl_%s%d %s'%(USE[u] if u<len(USE) else u,ui,d))
        elif op==81:
            d,_=dst(args[0]); out.append('def %s, %g, %g, %g, %g'%(d,*struct.unpack('<4f',struct.pack('<4I',*args[1:5]))))
        elif op in (30,25,26,27,28,29,38,39,40,41,42,43,44,45,47,48,0):
            out.append(name)
        else:
            d,sat=dst(args[0]); ss=[];k=1
            while k<len(args):
                s,ex=src(args,k); ss.append(s); k+=1+ex
            out.append('%s%s %s, %s'%(name,sat,d,', '.join(ss)))
        i+=1+ln
    return out
def pak(path):
    b=open(path,'rb').read(); assert b[:8]==b'ME1SHPK1'
    p=8; res=[]
    while p+8<=len(b):
        crc,nb=struct.unpack_from('<II',b,p); p+=8; res.append((crc,b[p:p+nb])); p+=nb
    return res
if __name__=='__main__':
    sh=pak('shaders.pak'); print(len(sh))
    for crc,bc in sh:
        v=struct.unpack_from('<I',bc,0)[0]
        if v>>16!=0xFFFE: continue
        d=parse(bc)
        ds=[x for x in d if x.startswith('dcl')]
        if any('texcoord1' in x for x in ds) and any('position0' in x for x in ds) and (v&0xFFFF0000)==0xFFFE0000:
            print('%08x len=%d ver=%08x'%(crc,len(bc),v), ds)
