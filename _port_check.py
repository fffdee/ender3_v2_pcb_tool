import serial
import serial.tools.list_ports as lp

for p in sorted(lp.comports(), key=lambda x: x.device):
    try:
        s = serial.Serial(p.device, 2000000, timeout=0.1)
        ok = True
        s.close()
    except Exception as e:
        ok = False
        err = str(e)
    print(p.device, '|', p.description, '|', '可用' if ok else '被占用: ' + err.splitlines()[-1][:80])
