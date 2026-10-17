VERSION = "1.0.0"
# usage: atest.py "cmd@delay ..." seconds   -- AIR module: sends commands, records 0x702 heartbeat, 0x28 airspeed/alt, 0x2A OAT
import sys,time,subprocess,glob,os,re,struct
P=glob.glob("/dev/serial/by-id/*AIR*")[0]
subprocess.run(["stty","-F",P,"115200","raw","-echo","hupcl"])
fd=os.open(P,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
log=open("/tmp/a.log","w")
cd=subprocess.Popen(["candump","-ta","can0,702:7FF,028:7FF,02A:7FF,02B:7FF"],stdout=log)
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
    if any(k in l for k in ("Pitot","AOA","Static","OAT","RTD","Test build","CAN tx")): print("  report:",l)
os.close(fd)
# analysis
t00=None; last={}
for line in open("/tmp/a.log"):
    m=re.match(r"\s*\((\d+\.\d+)\)\s+can0\s+([0-9A-F]+)\s+\[8\]\s+(.*)",line)
    if not m: continue
    t=float(m.group(1)); i=m.group(2); b=bytes(int(x,16) for x in m.group(3).split())
    if t00 is None: t00=t
    if i=="702": print("%5.1f HB state %d mask %02X"%(t-t00,b[4],b[5]))
    elif i=="028" and t-last.get(i,-9)>=1.0:
        last[i]=t; a,alt,v=struct.unpack("<hih",b); print("%5.1f   airspeed %d alt %d vsi %d"%(t-t00,a,alt,v))
    elif i=="02A" and t-last.get(i,-9)>=1.0:
        last[i]=t; print("%5.1f   OAT %.1f"%(t-t00,struct.unpack("<h",b[:2])[0]/10))
