import socket
import json
import os
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

HOST = "0.0.0.0"
PORT = 9999

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

print(f"Server UDP in ascolto su {HOST}:{PORT}...")

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

    except json.JSONDecodeError:
        print(f"[WARN] Payload non JSON: {data}")