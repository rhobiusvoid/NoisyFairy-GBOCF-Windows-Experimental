#!/usr/bin/env python3
# Adds an icon (RT_GROUP_ICON id 1 + RT_ICON) as a new last section of a PE with no .rsrc.
import struct,sys
exe,ico,out=sys.argv[1:4]
d=bytearray(open(exe,'rb').read()); ic=open(ico,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; osz=struct.unpack_from('<H',d,pe+20)[0]
opt=pe+24; salign,falign=struct.unpack_from('<II',d,opt+32)
dd=opt+112
assert struct.unpack_from('<I',d,dd+16)[0]==0,'already has resources'
sh=opt+osz
# last section
l=sh+(nsec-1)*40
vs,va,rs,ro=struct.unpack_from('<IIII',d,l+8)
nva=(va+max(vs,rs)+salign-1)//salign*salign
nro=(ro+rs+falign-1)//falign*falign
assert nro>=len(d)-falign and len(d)<=nro, 'trailing data after last section'
first_raw=min(struct.unpack_from('<I',d,sh+i*40+20)[0] for i in range(nsec))
assert sh+(nsec+1)*40<=first_raw
_,_,cnt=struct.unpack_from('<HHH',ic,0)
ents=[struct.unpack_from('<BBBBHHII',ic,6+i*16) for i in range(cnt)]
def dirt(entries,idbase=0):  # entries: list of (id, offset, isdir)
    b=struct.pack('<IIHHHH',0,0,0,0,0,len(entries))
    for i,o,isd in entries: b+=struct.pack('<II',i,(o|0x80000000) if isd else o)
    return b
# layout: root(2 entries) | icon dir(cnt) | grp dir(1) | per-icon lang dirs | grp lang dir | data entries | data
n=cnt
root_sz=16+2*8; icondir_sz=16+n*8; grpdir_sz=16+8; lang_sz=16+8
off_icondir=root_sz; off_grpdir=off_icondir+icondir_sz
off_lang0=off_grpdir+grpdir_sz; off_glang=off_lang0+n*lang_sz
off_de=off_glang+lang_sz; off_data=off_de+(n+1)*16
off_data=(off_data+15)//16*16
grp=struct.pack('<HHH',0,1,n)
for i,e in enumerate(ents):
    w,h,cc,r,pl,bc,sz,o=e; grp+=struct.pack('<BBBBHHIH',w,h,cc,0,pl,bc,sz,i+1)
blobs=[ic[e[7]:e[7]+e[6]] for e in ents]+[grp]
offs=[];p=off_data
for b in blobs: offs.append(p); p+=(len(b)+3)//4*4
total=p
sec=bytearray(total)
sec[0:root_sz]=dirt([(3,off_icondir,1),(14,off_grpdir,1)])
sec[off_icondir:off_icondir+icondir_sz]=dirt([(i+1,off_lang0+i*lang_sz,1) for i in range(n)])
sec[off_grpdir:off_grpdir+grpdir_sz]=dirt([(1,off_glang,1)])
for i in range(n): sec[off_lang0+i*lang_sz:off_lang0+(i+1)*lang_sz]=dirt([(0x409,off_de+i*16,0)])
sec[off_glang:off_glang+lang_sz]=dirt([(0x409,off_de+n*16,0)])
for i,b in enumerate(blobs):
    sec[off_de+i*16:off_de+i*16+16]=struct.pack('<IIII',nva+offs[i],len(b),0,0)
    sec[offs[i]:offs[i]+len(b)]=b
raw=(total+falign-1)//falign*falign
d+=b'\0'*(nro-len(d))
d+=sec+b'\0'*(raw-total)
struct.pack_into('<H',d,pe+6,nsec+1)
h=sh+nsec*40
d[h:h+40]=b'.rsrc\0\0\0'+struct.pack('<IIIIIIHHI',total,nva,raw,nro,0,0,0,0,0x40000040)
soi=nva+(total+salign-1)//salign*salign
struct.pack_into('<I',d,opt+56,soi)
struct.pack_into('<II',d,dd+16,nva,total)
struct.pack_into('<I',d,opt+64,0)  # checksum
open(out,'wb').write(d)
