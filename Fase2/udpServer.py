import socket
import json
import os
import csv
from datetime import datetime, timezone
from pymongo import MongoClient
from pymongo.errors import ConnectionFailure, OperationFailure

# Configurazione MongoDB
# Recupera la stringa di connessione dalle variabili d'ambiente per sicurezza
MONGO_URI = os.getenv("MONGO_URI", "mongodb://localhost:27017/")
DB_NAME = os.getenv("MONGO_DB", "iot_data")
COLLECTION_NAME = os.getenv("MONGO_COLLECTION", "telemetry")

try:
    client = MongoClient(MONGO_URI)
    db = client[DB_NAME]
    collection = db[COLLECTION_NAME]
    # Verifica connessione
    client.admin.command('ping')
    print(f"Connesso a MongoDB: {DB_NAME}.{COLLECTION_NAME}")
except Exception as e:
    print(f"Errore connessione MongoDB: {e}")
    client = None

# Configurazione CSV
CSV_FILE = os.getenv("CSV_FILE", os.path.join(os.path.dirname(os.path.abspath(__file__)), "telemetry.csv"))
CSV_FIELDS = ["timestamp", "sender_ip", "device_id", "voltage", "current", "power", "temperature", "relay", "uptime"]

HOST = "0.0.0.0"
PORT = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

print(f"Server UDP in ascolto su {HOST}:{PORT}...")
print(f"Salvataggio CSV abilitato su: {CSV_FILE}")

while True:
    data, addr = sock.recvfrom(512)
    try:
        payload = json.loads(data.decode("utf-8"))
        
        # Salvataggio su MongoDB
        if client:
            try:
                # Aggiunge metadati al payload
                document = payload.copy()
                document["timestamp"] = datetime.now(timezone.utc) # Formato ISO per DB
                document["sender_ip"] = addr[0]
                
                collection.insert_one(document)
                print("  [DB] Dati salvati con successo.")
            except Exception as e:
                print(f"  [DB] Errore durante il salvataggio: {e}")

        # Salvataggio su CSV 
        try:
            timestamp_str = datetime.now(timezone.utc).isoformat()
            
            csv_row = {
                "timestamp": timestamp_str,
                "sender_ip": addr[0],
                "device_id": payload.get("device_id", ""),
                "voltage": payload.get("voltage", ""),
                "current": payload.get("current", ""),
                "power": payload.get("power", ""),
                "temperature": payload.get("temperature", ""),
                "relay": payload.get("relay", ""),
                "uptime": payload.get("uptime", "")
            }
            
            file_exists = os.path.exists(CSV_FILE)
            with open(CSV_FILE, mode="a", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
                if not file_exists:
                    writer.writeheader()
                writer.writerow(csv_row)
            print("  [CSV] Dati salvati con successo.")
        except Exception as e:
            print(f"  [CSV] Errore durante il salvataggio su CSV: {e}")

    except json.JSONDecodeError:
        print(f"[WARN] Payload non JSON: {data}")