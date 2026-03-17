import socket
import json
from datetime import datetime

HOST = "0.0.0.0"
PORT = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

print(f"Server UDP in ascolto su {HOST}:{PORT}...")

while True:
    data, addr = sock.recvfrom(512)
    try:
        payload = json.loads(data.decode("utf-8"))
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"\n[{ts}] Da {addr[0]}:{addr[1]}")
        print(f"  Tensione  : {payload.get('voltage', '?')} V")
        print(f"  Corrente  : {payload.get('current', '?')} A")
        print(f"  Potenza   : {payload.get('power', '?')} W")
        print(f"  Potenza App: {payload.get('apparent', '?')} VA")
        print(f"  PF        : {payload.get('pf', '?')}")
        print(f"  Temp      : {payload.get('temperature', '?')} °C")
        print(f"  Relay     : {'ON' if payload.get('relay') else 'OFF'}")
        print(f"  Uptime    : {payload.get('uptime', '?')} s")
    except json.JSONDecodeError:
        print(f"[WARN] Payload non JSON: {data}")