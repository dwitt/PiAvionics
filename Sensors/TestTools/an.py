VERSION = "1.0.0"
import re,struct
t0=None; last=-9
for line in open("/tmp/f.log"):
    m=re.match(r"\s*\((\d+\.\d+)\)\s+can0\s+([0-9A-F]+)\s+\[8\]\s+(.*)",line)
    if not m: continue
    t=float(m.group(1)); i=m.group(2); b=bytes(int(x,16) for x in m.group(3).split())
    if t0 is None: t0=t
    if i=="701": print("%5.1f HB state %d mask %02X up %d"%(t-t0,b[4],b[5],b[6]|b[7]<<8))
    elif i=="048" and t-last>=0.5:
        last=t; y,p,r,tr=struct.unpack("<hhhh",b); print("%5.1f   yaw %.1f pitch %.1f roll %.1f turn %.1f"%(t-t0,y/10,p/10,r/10,tr/10))
    elif i=="049" and False: pass
