VERSION = "1.0.0"
import sys,time,subprocess,glob,os,re,struct
P=glob.glob("/dev/serial/by-id/*GPS*")[0]
subprocess.run(["stty","-F",P,"115200","raw","-echo","hupcl"])
fd=os.open(P,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
log=open("/tmp/g.log","w")
cd=subprocess.Popen(["candump","-ta","can0,703:7FF,063:7FF,064:7FF,065:7FF"],stdout=log)
t0=time.time()
for item in sys.argv[1].split():
    c,d=item.split("@")
    time.sleep(max(0,float(d)-(time.time()-t0)))
    os.write(fd,c.encode()); print("%.1f sent %s"%(time.time()-t0,c))
time.sleep(max(0,float(sys.argv[2])-(time.time()-t0)))
cd.terminate(); time.sleep(0.2)
try: os.read(fd,65536)
except OSError: pass
os.write(fd,b"?"); time.sleep(1.0)
try: out=os.read(fd,65536).decode(errors="replace")
except OSError: out=""
lines=out.splitlines()
i=max([k for k,l in enumerate(lines) if "uptime" in l] or [0])
for l in lines[i:]:
    if any(k in l for k in ("GPS:","Test build","CAN tx")): print("  report:",l)
os.close(fd)
t00=None; last={}
for line in open("/tmp/g.log"):
    m=re.match(r"\s*\((\d+\.\d+)\)\s+can0\s+([0-9A-F]+)\s+\[8\]\s+(.*)",line)
    if not m: continue
    t=float(m.group(1)); i=m.group(2); b=bytes(int(x,16) for x in m.group(3).split())
    if t00 is None: t00=t
    if i=="703": print("%5.1f HB state %d mask %02X"%(t-t00,b[4],b[5]))
    elif i=="063": la,lo=struct.unpack("<ii",b); print("%5.1f   pos %.5f %.5f"%(t-t00,la/1e6,lo/1e6))
    elif i=="065": print("%5.1f   status fixq %d fix3d %d sats %d"%(t-t00,b[0],b[1],b[2]))
