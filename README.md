# Security-Shelly-Plug-S-gen1

Questo progetto esplora la sicurezza dei dispositivi IoT, focalizzandosi sullo smart plug **Shelly Plug S (Gen 1)**. Il progetto è suddiviso in 4 fasi principali che coprono l'analisi delle vulnerabilità, lo sviluppo di attacchi (Offensive Security) e la successiva implementazione di contromisure di sicurezza e hardening (Defensive Security).

## Obiettivi del Progetto

Il progetto mira a dimostrare le potenziali vulnerabilità dei dispositivi smart home commerciali e a proporre soluzioni pratiche per mitigarle, attraverso l'analisi dei vettori di attacco e la scrittura di firmware più sicuri.

---

## Struttura e Fasi del Progetto

### Fase 1: Accesso Fisico e Analisi del Firmware
- **Obiettivo:** Ottenere accesso fisico al microcontrollore del dispositivo (ESP8266), estrarre il firmware originale e analizzarne il contenuto.
- **Attività:** Reverse engineering, interfacciamento tramite pin seriali e analisi della struttura del firmware per individuare potenziali punti deboli e dati sensibili.

### Fase 2: Sviluppo Firmware Malevolo (Red Team)
- **Obiettivo:** Creare e installare un firmware modificato per dimostrare un attacco di tipo Side-Channel (NILM - Non-Intrusive Load Monitoring).
- **Attività:** 
  - Lettura continua e invisibile all'utente dei dati di consumo elettrico.
  - Esfiltrazione dei dati verso un server esterno via UDP.
  - Analisi dei dati (tramite tecniche di Machine Learning) per inferire le abitudini dell'utente e stabilire la presenza in casa.

### Fase 3: Hardening e Firmware Sicuro (Blue Team)
- **Obiettivo:** Sviluppare da zero un firmware sicuro che risolva le vulnerabilità strutturali riscontrate.
- **Attività:**
  - **MQTTS (MQTT over TLS):** Implementazione di comunicazioni sicure ed end-to-end crittografate con il broker MQTT, utilizzando l'autenticazione mTLS (Mutual TLS) con certificati client.
  - **Secure OTA (Over-The-Air) Updates:** Implementazione di un meccanismo di verifica della firma crittografica per gli aggiornamenti OTA, impedendo a un attaccante di flashare firmware malevoli sulla rete locale.

### Fase 4: Attacco OTA e MITM
- **Obiettivo:** Dimostrare la debolezza dei meccanismi di aggiornamento tradizionali non firmati.
- **Attività:** Esecuzione di un attacco Man-In-The-Middle (MITM) o un flashing OTA fraudolento sfruttando le vulnerabilità del firmware originale.

---

## Tecnologie e Strumenti Utilizzati
- **Hardware:** Shelly Plug S (Gen 1), adattatori USB-to-TTL / FTDI.
- **Software/Linguaggi:** C/C++ (Framework Arduino / ESP8266), Python (Server ricezione dati e Script di firma OTA).
- **Protocolli:** MQTT, HTTP, UDP.
- **Sicurezza:** TLS 1.2 (BearSSL), mTLS, Crittografia Asimmetrica (Firma Digitale per Secure Boot/OTA).

---

## ⚠️ Disclaimer
*Questo progetto è stato sviluppato a scopo puramente didattico, accademico e di ricerca nell'ambito della sicurezza informatica per dispositivi IoT. Non utilizzare i software o le tecniche descritte su dispositivi e reti per cui non si dispone di esplicita autorizzazione.*
