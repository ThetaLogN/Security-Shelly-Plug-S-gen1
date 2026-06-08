import sys
import struct
import os
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives import serialization

PRIVATE_KEY_FILE = "private_key.pem"
PUBLIC_KEY_FILE = "public_key.pem"



def firma_firmware(file_bin):
    if not os.path.exists(PRIVATE_KEY_FILE):
        print(f"Errore: La chiave privata {PRIVATE_KEY_FILE} non esiste. Genera le chiavi prima di firmare.")
        sys.exit(1)

    print(f"Lettura del firmware: {file_bin}")
    with open(file_bin, "rb") as f:
        dati_firmware = f.read()

    # Caricamento chiave privata
    with open(PRIVATE_KEY_FILE, "rb") as key_file:
        private_key = serialization.load_pem_private_key(
            key_file.read(),
            password=None,
        )

    print("Calcolo dell'impronta (SHA-256) e firma in corso...")
    # Firma dei dati del firmware
    firma = private_key.sign(
        dati_firmware,
        padding.PKCS1v15(),
        hashes.SHA256()
    )

    assert len(firma) == 256, "Errore: La firma non è di 256 byte!"

    # Creazione nuovo firmware firmato
    file_output = file_bin.replace(".bin", "_firmato.bin")
    with open(file_output, "wb") as f:
        f.write(dati_firmware)
        f.write(firma)
        f.write(struct.pack("<I", len(firma)))

    print(f"Successo! Firmware firmato salvato come: {file_output}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python firma_ota.py <percorso_firmware.bin>")
        sys.exit(1)
    
    target_bin = sys.argv[1]
    if not os.path.exists(target_bin):
        print(f"Errore: Il file {target_bin} non esiste.")
        sys.exit(1)
        
    firma_firmware(target_bin)
