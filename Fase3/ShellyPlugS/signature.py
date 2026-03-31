import sys
import os
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives import serialization

PRIVATE_KEY_FILE = "private_key.pem"
PUBLIC_KEY_FILE = "public_key.pem"

def genera_chiavi():
    print("🔑 Generazione nuova coppia di chiavi RSA a 2048 bit...")
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )
    
    # Salva la chiave privata
    with open(PRIVATE_KEY_FILE, "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        ))
        
    # Salva la chiave pubblica (questa andrà nello Shelly)
    public_key = private_key.public_key()
    with open(PUBLIC_KEY_FILE, "wb") as f:
        f.write(public_key.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo
        ))
    print(f"✅ Chiavi salvate: {PRIVATE_KEY_FILE} (Tieni segreta!) e {PUBLIC_KEY_FILE}")

def firma_firmware(file_bin):
    if not os.path.exists(PRIVATE_KEY_FILE):
        genera_chiavi()

    print(f"📄 Lettura del firmware: {file_bin}")
    with open(file_bin, "rb") as f:
        dati_firmware = f.read()

    # Carica la chiave privata
    with open(PRIVATE_KEY_FILE, "rb") as key_file:
        private_key = serialization.load_pem_private_key(
            key_file.read(),
            password=None,
        )

    print("✍️  Calcolo dell'impronta (SHA-256) e firma in corso...")
    # Firma i dati del firmware (Padding PKCS1v15 è il più facile da verificare su ESP8266)
    firma = private_key.sign(
        dati_firmware,
        padding.PKCS1v15(),
        hashes.SHA256()
    )

    # Verifica dimensioni (RSA 2048 produce una firma di 256 byte)
    assert len(firma) == 256, "Errore: La firma non è di 256 byte!"

    # Crea il nuovo file firmato: Firmware Originale + Firma (256 byte) + Magic Word (4 byte)
    file_output = file_bin.replace(".bin", "_firmato.bin")
    with open(file_output, "wb") as f:
        f.write(dati_firmware)
        f.write(firma)

    print(f"🚀 Successo! Firmware firmato salvato come: {file_output}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python firma_ota.py <percorso_del_tuo_firmware.bin>")
        sys.exit(1)
    
    target_bin = sys.argv[1]
    if not os.path.exists(target_bin):
        print(f"❌ Errore: Il file {target_bin} non esiste.")
        sys.exit(1)
        
    firma_firmware(target_bin)