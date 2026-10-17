VERSION = "1.0.0"
# usage: ftest.py "cmd@delay cmd@delay ..." seconds
import sys,time,subprocess,glob,os
P=glob.glob("/dev/serial/by-id/*IMU*")[0]
subprocess.run(["stty","-F",P,"115200","raw","-echo","hupcl"])
fd=os.open(P,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)    # stays open for the whole run
log=open("/tmp/f.log","w")
cd=subprocess.Popen(["candump","-ta","can0,701:7FF,48:7FF,49:7FF"],stdout=log)
t0=time.time()
cmds=open("/tmp/f.cmds","w")
for item in sys.argv[1].split():
    c,d=item.split("@")
    time.sleep(max(0,float(d)-(time.time()-t0)))
    os.write(fd,c.encode())
    cmds.write("%.2f sent %s\n"%(time.time()-t0,c)); cmds.flush()
time.sleep(max(0,float(sys.argv[2])-(time.time()-t0)))
cd.terminate(); time.sleep(0.2)
# drain whatever the module printed and ask for the report
os.write(fd,b"?"); time.sleep(0.8)
try: out=os.read(fd,65536).decode(errors="replace")
except OSError: out=""
for l in out.splitlines():
    if any(k in l for k in ("ISM330DHCX accel","LIS3MDL magn","Test build","Mag pred")): print("  report:",l)
os.close(fd)
