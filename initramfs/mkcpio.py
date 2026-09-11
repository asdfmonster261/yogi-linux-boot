import os, sys
root=sys.argv[1]; out=sys.argv[2]
S_IFDIR=0o040000; S_IFREG=0o100000; S_IFCHR=0o020000; ino=[720]
def fld(x): return b"%08X"%(x&0xFFFFFFFF)
def e(f,name,mode,data=b"",rmaj=0,rmin=0,nlink=1):
    ino[0]+=1; nb=name.encode()+b"\0"
    h=(b"070701"+fld(ino[0])+fld(mode)+fld(0)+fld(0)+fld(nlink)+fld(0)+fld(len(data))
       +fld(0)+fld(0)+fld(rmaj)+fld(rmin)+fld(len(nb))+fld(0))
    f.write(h); f.write(nb); f.write(b"\0"*((-(len(h)+len(nb)))%4)); f.write(data); f.write(b"\0"*((-len(data))%4))
rd=lambda p: open(os.path.join(root,p),"rb").read()
def add_dir_tree(f, rel):  # add a directory subtree (dirs + regular files), sorted
    for dp,dns,fns in os.walk(os.path.join(root,rel)):
        r=os.path.relpath(dp,root)
        if r!=".": e(f,r,S_IFDIR|0o755,nlink=2)
        for fn in sorted(fns):
            rp=os.path.join(r,fn) if r!="." else fn
            mode=0o755 if (fn in ("aocd","linker64")) else 0o644
            e(f,rp,S_IFREG|mode,open(os.path.join(dp,fn),"rb").read())
with open(out,"wb") as f:
    e(f,"bin",S_IFDIR|0o755,nlink=2); e(f,"bin/busybox",S_IFREG|0o755,rd("bin/busybox"))
    e(f,"linuxboot_init",S_IFREG|0o755,rd("linuxboot_init"))
    e(f,"modeset_test",S_IFREG|0o755,rd("modeset_test"))
    e(f,"lib",S_IFDIR|0o755,nlink=2); e(f,"lib/modules",S_IFDIR|0o755,nlink=2)
    for ko in sorted(os.listdir(os.path.join(root,"lib/modules"))):
        if ko.endswith(".ko"): e(f,"lib/modules/"+ko,S_IFREG|0o644,rd("lib/modules/"+ko))
    e(f,"vendor",S_IFDIR|0o755,nlink=2); e(f,"vendor/firmware",S_IFDIR|0o755,nlink=2)
    for fn in sorted(os.listdir(os.path.join(root,"vendor/firmware"))): e(f,"vendor/firmware/"+fn,S_IFREG|0o644,rd("vendor/firmware/"+fn))
    add_dir_tree(f,"aoc")
    e(f,"dev",S_IFDIR|0o755,nlink=2)
    e(f,"dev/console",S_IFCHR|0o600,rmaj=5,rmin=1); e(f,"dev/null",S_IFCHR|0o666,rmaj=1,rmin=3); e(f,"dev/kmsg",S_IFCHR|0o644,rmaj=1,rmin=11)
    e(f,"proc",S_IFDIR|0o755,nlink=2); e(f,"sys",S_IFDIR|0o755,nlink=2); e(f,"tmp",S_IFDIR|0o1777,nlink=2)
    nb=b"TRAILER!!!\0"; h=(b"070701"+fld(0)+fld(0)+fld(0)+fld(0)+fld(1)+fld(0)+fld(0)+fld(0)+fld(0)+fld(0)+fld(0)+fld(len(nb))+fld(0))
    f.write(h); f.write(nb); f.write(b"\0"*((-(len(h)+len(nb)))%4))
print("wrote",out,os.path.getsize(out),"bytes")
