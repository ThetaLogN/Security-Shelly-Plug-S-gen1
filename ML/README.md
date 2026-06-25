# Shelly Plug S — Analizzatore delle Abitudini (`analyzer.py`)

Questo componente si occupa di analizzare i dati storici di telemetria inviati dallo Shelly Plug S e memorizzati in formato CSV. L'obiettivo è estrarre statistiche sul consumo, individuare le abitudini di presenza degli utenti in casa e rilevare potenziali anomalie di sicurezza o funzionamento.

Il modulo fa parte del pacchetto **ML (Machine Learning)** ed è integrato con l'applicazione web Flask.

---

## Indice
1. [Flusso di Elaborazione dei Dati](#1-flusso-di-elaborazione-dei-dati)
2. [Analisi di Presenza (Privacy Impact)](#2-analisi-di-presenza-privacy-impact)
3. [Algoritmi di Machine Learning (Scikit-Learn)](#3-algoritmi-di-machine-learning-scikit-learn)
4. [Rilevamento delle Anomalie](#4-rilevamento-delle-anomalie)
5. [Struttura dell'Output (JSON)](#5-struttura-delloutput-json)

---

## 1. Flusso di Elaborazione dei Dati

L'analizzatore segue una sequenza lineare di elaborazione:

1. **Lettura del File CSV**: Accede al file `telemetry.csv` (generato dal server UDP in `Fase2`).
2. **Parsing & Normalizzazione**:
   - Converte i timestamp ISO 8601 in oggetti `datetime` nativi di Python (gestendo il suffisso `Z` o i fusi orari).
   - Estrae ed effettua il casting dei parametri `power` (potenza in Watt, convertita in float) e `relay` (stato del relè, convertito in booleano).
   - Salta automaticamente eventuali righe malformate o vuote per evitare crash.
3. **Calcolo Statistiche di Base**:
   - **Energia Totale (kWh)**: Calcolata moltiplicando la potenza istantanea per la frazione oraria. Poiché il campionamento dello Shelly avviene ogni 30 minuti, ciascun campione equivale a $0.5$ ore:
     $$\text{Energia (kWh)} = \sum \frac{\text{Potenza (W)} \times 0.5 \text{ ore}}{1000}$$
   - **Ore Relè Attivo**: Somma dei periodi in cui il relè era in stato `True` (ogni record positivo equivale a $0.5$ ore).
   - **Consumo Medio Settimanale**: Calcola la media dei consumi aggregando i dati per giorno della settimana (Lunedì-Domenica) per evidenziare differenze sistematiche tra giorni feriali e festivi.

---

## 2. Analisi di Presenza (Privacy Impact)

Una parte fondamentale del progetto riguarda la profilazione delle abitudini dell'utente a scopi di analisi di sicurezza e privacy (dimostrando come un attaccante in ascolto della telemetria possa dedurre se l'utente è in casa):

- **Soglia di Presenza**: Se la potenza misurata supera i **30W**, si assume che un elettrodomestico rilevante sia attivo e che ci sia qualcuno in casa.
- **Probabilità di Presenza Oraria**: Per ogni ora della giornata ($00:00$ - $23:00$), calcola la percentuale di giorni in cui la soglia è stata superata in quella specifica ora.
- **Fasce di Presenza (In Casa)**: Finestre orarie in cui la probabilità di presenza è **$\ge 50\%$**.
- **Fasce di Assenza (Fuori Casa)**: Finestre orarie (nella fascia diurna $08:00$ - $20:00$) in cui la probabilità di presenza scende sotto il **$25\%$**.

---

## 3. Algoritmi di Machine Learning (Scikit-Learn)

L'analizzatore sfrutta due algoritmi principali di **Scikit-Learn**:

### A. K-Means Clustering (`KMeans`)
Viene impiegato per segmentare i profili di consumo giornalieri. 
- **Input**: Ogni giorno viene convertito in un vettore a 24 elementi (uno per ogni ora del giorno con la rispettiva potenza media).
- **Cluster**: Il modello suddivide i vettori in $k=2$ gruppi:
  - **Alta Attività**: Rappresenta il profilo del centroide con i consumi medi più alti.
  - **Basso Consumo / Eco**: Rappresenta i giorni in cui il dispositivo è rimasto per lo più in standby.
- **Gestione dei Casi Limite**: Se il dataset contiene meno di 2 giorni di dati, l'analizzatore applica automaticamente una logica di fallback restituendo profili standard per evitare errori matematici.

### B. Regressione Lineare (`LinearRegression`)
Utilizzata per identificare il trend generale a lungo termine dei consumi.
- **Input**: L'indice temporale del giorno ($x = [0, 1, 2, ...]$) e il consumo energetico totale di quel giorno ($y$ in kWh).
- **Output**: La pendenza della linea di regressione (`slope`).
- **Interpretazione**:
  - `slope > 0.01`: Trend **"In crescita"**.
  - `slope < -0.01`: Trend **"In calo"**.
  - Altrimenti: Trend **"Stabile"**.

---

## 4. Rilevamento delle Anomalie

L'analizzatore esegue controlli euristici e logici su ciascun record per evidenziare anomalie:

1. **Consumo Notturno Insolito**: Generato quando viene misurato un carico superiore a **200W** nelle ore comprese tra la $01:00$ e le $05:00$ del mattino (orario di riposo standard).
2. **Carico a Relè Spento**: Generato se viene rilevato un assorbimento di potenza significativo (soglia **$> 5W$**) mentre lo stato del relè dichiarato dallo Shelly è spento (`False`). Questa discrepanza può segnalare un malfunzionamento, un bypass elettrico hardware o una manomissione.

---

## 5. Struttura dell'Output (JSON)

La funzione `analyze_habits()` restituisce un dizionario Python con la seguente struttura:

```json
{
  "stats": {
    "total_days": 8,
    "avg_power_w": 194.16,
    "max_power_w": 995.77,
    "total_energy_kwh": 32.62,
    "daily_avg_kwh": 4.08,
    "est_monthly_cost_eur": 30.58,
    "relay_on_hours": 108.0,
    "relay_avg_hours_day": 13.5,
    "trend": "In calo",
    "trend_slope": -0.202
  },
  "profiles": {
    "overall_hourly": [ ... ], // 24 valori float (potenza media oraria)
    "weekday_averages": [ ... ], // 7 valori float (Lunedì -> Domenica)
    "cluster_high": [ ... ], // Centroide alta attività (24 valori)
    "cluster_low": [ ... ]  // Centroide basso consumo (24 valori)
  },
  "day_assignments": {
    "2026-06-19": "Alta Attività",
    "2026-06-20": "Basso Consumo"
  },
  "presence": {
    "presence_probability": [ ... ], // 24 percentuali di presenza
    "active_windows": "07:00-09:59, 18:00-22:59",
    "absence_windows": "10:00-17:59"
  },
  "anomalies": [
    {
      "timestamp": "2026-06-21 02:00:00",
      "type": "Consumo Notturno Insolito",
      "description": "Rilevato picco di 250.0W alle ore 2:00, tipicamente periodo di riposo."
    }
  ],
  "recommendations": [
    "Il picco massimo di consumo si concentra alle ore 20:00. Consigliamo di programmare i carichi pesanti al di fuori di questa fascia."
  ]
}
```
