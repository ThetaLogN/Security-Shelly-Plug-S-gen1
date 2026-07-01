import csv
import os
from datetime import datetime
import numpy as np
from sklearn.cluster import KMeans
from sklearn.linear_model import LinearRegression

# Calcolo dei percorsi dei file
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BASE_DIR, "..", "Fase2", "telemetry.csv")

def analyze_habits():
    """
    Analizza i dati di telemetria memorizzati nel file CSV.
    Restituisce statistiche di base, profilo orario, analisi di presenza, anomalie e raccomandazioni.
    Usa scikit-learn per il clustering dei profili giornalieri e per calcolare il trend dei consumi.
    """
    if not os.path.exists(CSV_PATH):
        return {"error": "No telemetry data available. Please upload a CSV file to get started."}

    data_points = []
    with open(CSV_PATH, mode="r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                # Normalizza ed analizza il timestamp (gestisce Z o offset +00:00)
                ts_str = row["timestamp"].replace("Z", "+00:00")
                ts = datetime.fromisoformat(ts_str)
                data_points.append({
                    "timestamp": ts,
                    "power": float(row["power"]),
                    "relay": row["relay"].lower() == "true"
                })
            except (ValueError, KeyError):
                # Salta righe vuote o malformate
                continue

    if not data_points:
        return {"error": "No telemetry data available in the CSV file."}

    # Ordina i punti per data/ora
    data_points.sort(key=lambda x: x["timestamp"])

    # Statistiche generali
    total_samples = len(data_points)
    powers = [dp["power"] for dp in data_points]
    max_power = max(powers)
    avg_power = sum(powers) / total_samples
    
    # Stima energia (ogni campione rappresenta un intervallo di 60 secondi = 1/60 ore)
    total_energy_kwh = sum(dp["power"] * (1.0 / 60.0) / 1000.0 for dp in data_points)
    relay_on_hours = sum(1 for dp in data_points if dp["relay"]) * (1.0 / 60.0)

    # Raggruppamento dati per data e ora
    daily_energy = {}
    daily_hourly_profiles = {}  # date_str -> [lista di 24 ore, ciascuna con una lista di letture]
    
    for dp in data_points:
        date_str = dp["timestamp"].strftime("%Y-%m-%d")
        hour = dp["timestamp"].hour
        
        # Accumulo energia giornaliera
        daily_energy[date_str] = daily_energy.get(date_str, 0.0) + (dp["power"] * (1.0 / 60.0) / 1000.0)
        
        if date_str not in daily_hourly_profiles:
            daily_hourly_profiles[date_str] = [[] for _ in range(24)]
        daily_hourly_profiles[date_str][hour].append(dp["power"])

    total_days = len(daily_hourly_profiles)

    # Calcolo profili orari medi per giorno e profilo orario medio complessivo
    clean_daily_profiles = {}
    for date_str, hourly_list in daily_hourly_profiles.items():
        profile_24h = []
        for hour in range(24):
            readings = hourly_list[hour]
            profile_24h.append(sum(readings) / len(readings) if readings else 0.0)
        clean_daily_profiles[date_str] = profile_24h

    overall_hourly_profile = [0.0] * 24
    for hour in range(24):
        hour_powers = [clean_daily_profiles[d][hour] for d in clean_daily_profiles]
        overall_hourly_profile[hour] = sum(hour_powers) / len(hour_powers) if hour_powers else 0.0

    # Calcolo consumi medi per giorno della settimana (0 = Lunedì, 6 = Domenica)
    weekday_sums = [0.0] * 7
    weekday_counts = [0] * 7
    for date_str, energy in daily_energy.items():
        dt = datetime.strptime(date_str, "%Y-%m-%d")
        wday = dt.weekday()
        weekday_sums[wday] += energy
        weekday_counts[wday] += 1
    weekday_averages = [
        (weekday_sums[i] / weekday_counts[i]) if weekday_counts[i] > 0 else 0.0
        for i in range(7)
    ]

    # ML con Scikit-Learn (K-Means Clustering sui profili giornalieri)
    if len(clean_daily_profiles) >= 2:
        X_cluster = np.array(list(clean_daily_profiles.values()))
        kmeans = KMeans(n_clusters=2, random_state=42, n_init='auto').fit(X_cluster)
        
        # Ordiniamo i cluster per consumo totale (basso consumo vs alta attività)
        sums = kmeans.cluster_centers_.sum(axis=1)
        high_idx = int(np.argmax(sums))
        low_idx = 1 - high_idx
        
        cluster_high = kmeans.cluster_centers_[high_idx].tolist()
        cluster_low = kmeans.cluster_centers_[low_idx].tolist()
        
        day_assignments = {
            date: ("High Activity" if label == high_idx else "Low Consumption")
            for date, label in zip(clean_daily_profiles.keys(), kmeans.labels_)
        }
    else:
        # Fallback se non ci sono abbastanza giorni
        cluster_high = overall_hourly_profile
        cluster_low = [v * 0.5 for v in overall_hourly_profile]
        day_assignments = {d: "Standard Activity" for d in clean_daily_profiles}

    # ML con Scikit-Learn (Regressione Lineare per il trend dei consumi)
    dates_sorted = sorted(list(daily_energy.keys()))
    if len(dates_sorted) >= 2:
        X_reg = np.arange(len(dates_sorted)).reshape(-1, 1)
        y_reg = np.array([daily_energy[d] for d in dates_sorted])
        
        reg = LinearRegression().fit(X_reg, y_reg)
        slope = float(reg.coef_[0])
    else:
        slope = 0.0

    if slope > 0.01:
        trend = "Growing"
    elif slope < -0.01:
        trend = "Decreasing"
    else:
        trend = "Stable"

    # Analisi della presenza/occupazione domestica (basata sulla potenza attiva)
    presence_threshold = 5.0
    presence_probability = [0.0] * 24
    for hour in range(24):
        active_days = sum(1 for d in clean_daily_profiles if clean_daily_profiles[d][hour] > presence_threshold)
        presence_probability[hour] = (active_days / total_days) * 100.0 if total_days > 0 else 0.0

    # Identifica le fasce orarie probabili di presenza e assenza
    probable_presence_hours = [h for h in range(24) if presence_probability[h] >= 50.0]
    probable_absence_hours = [h for h in range(8, 20) if presence_probability[h] < 35.0]

    def format_hours(hours_list):
        if not hours_list:
            return "None detected"
        ranges = []
        start = None
        prev = None
        for h in sorted(hours_list):
            if start is None:
                start = h
            elif h != prev + 1:
                ranges.append(f"{start:02d}:00-{prev:02d}:59")
                start = h
            prev = h
        if start is not None:
            ranges.append(f"{start:02d}:00-{prev:02d}:59")
        return ", ".join(ranges)

    presence_analysis = {
        "presence_probability": presence_probability,
        "active_windows": format_hours(probable_presence_hours),
        "absence_windows": format_hours(probable_absence_hours)
    }

    # Rilevamento anomalie
    anomalies = []
    for dp in data_points:
        hour = dp["timestamp"].hour
        # Picco notturno insolito (1-5 AM) con potenza > 200W
        if (1 <= hour <= 5) and dp["power"] > 200.0:
            anomalies.append({
                "timestamp": dp["timestamp"].strftime("%Y-%m-%d %H:%M:%S"),
                "type": "Unusual Nightly Consumption",
                "description": f"Detected peak of {dp['power']:.1f}W at {hour:02d}:00, typically a sleep period."
            })
        # Consumo rilevato a relè spento
        if not dp["relay"] and dp["power"] > 5.0:
            anomalies.append({
                "timestamp": dp["timestamp"].strftime("%Y-%m-%d %H:%M:%S"),
                "type": "Active Load on Off Relay",
                "description": f"Measured power of {dp['power']:.1f}W while the relay is off."
            })
    anomalies = anomalies[-10:]
    anomalies.reverse()

    # Consigli / Raccomandazioni per il risparmio
    recommendations = []
    peak_hour = overall_hourly_profile.index(max(overall_hourly_profile)) if overall_hourly_profile else 0
    recommendations.append(
        f"Maximum consumption peak occurs at {peak_hour:02d}:00. We recommend scheduling heavy loads outside this time window."
    )
    
    night_powers = [overall_hourly_profile[h] for h in range(1, 6)]
    avg_night_power = sum(night_powers) / len(night_powers) if night_powers else 0.0
    if avg_night_power > 25.0:
        recommendations.append(
            f"Your average nightly consumption is {avg_night_power:.1f}W. You might have standby devices wasting energy."
        )
    else:
        recommendations.append(
            "Excellent nightly standby consumption. Electronic devices are configured correctly."
        )

    # Costo stimato (es. 0.25 € / kWh)
    cost_per_kwh = 0.25

    return {
        "stats": {
            "total_days": total_days,
            "avg_power_w": avg_power,
            "max_power_w": max_power,
            "total_energy_kwh": total_energy_kwh,
            "daily_avg_kwh": total_energy_kwh / total_days if total_days > 0 else 0.0,
            "est_monthly_cost_eur": (total_energy_kwh / total_days) * 30 * cost_per_kwh if total_days > 0 else 0.0,
            "relay_on_hours": relay_on_hours,
            "relay_avg_hours_day": relay_on_hours / total_days if total_days > 0 else 0.0,
            "trend": trend,
            "trend_slope": slope
        },
        "profiles": {
            "overall_hourly": overall_hourly_profile,
            "weekday_averages": weekday_averages,
            "cluster_high": cluster_high,
            "cluster_low": cluster_low
        },
        "day_assignments": day_assignments,
        "presence": presence_analysis,
        "anomalies": anomalies,
        "recommendations": recommendations
    }

if __name__ == "__main__":
    report = analyze_habits()
    import json
    print(json.dumps(report.get("stats", report), indent=2))
