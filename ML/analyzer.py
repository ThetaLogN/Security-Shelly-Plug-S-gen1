import csv
import os
from datetime import datetime
from sklearn.cluster import KMeans
from sklearn.linear_model import LinearRegression
import numpy as np

# Path calculations
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BASE_DIR, "..", "Fase2", "telemetry.csv")

def analyze_habits():
    # Return error if CSV file does not exist
    if not os.path.exists(CSV_PATH):
        return {"error": "Nessun dato di telemetria disponibile. Carica un file CSV per iniziare."}
        
    data_points = []
    
    # Read telemetry CSV
    with open(CSV_PATH, mode="r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                # Parse timestamp (handles Z, +00:00 offset, etc.)
                ts_str = row["timestamp"].replace("Z", "+00:00")
                ts = datetime.fromisoformat(ts_str)
                
                data_points.append({
                    "timestamp": ts,
                    "power": float(row["power"]),
                    "relay": row["relay"].lower() == "true"
                })
            except (ValueError, KeyError):
                # Skip header or corrupt line
                continue
                
    if not data_points:
        return {"error": "Nessun dato di telemetria disponibile nel file CSV."}
        
    # Sort data by timestamp
    data_points.sort(key=lambda x: x["timestamp"])
    
    # Basic Stats
    total_samples = len(data_points)
    powers = [dp["power"] for dp in data_points]
    max_power = max(powers)
    avg_power = sum(powers) / total_samples
    
    # Calculate energy in kWh (since interval is 30 mins, each point is 0.5 hours)
    # Energy = P * hours / 1000
    total_energy_kwh = sum(dp["power"] * 0.5 / 1000.0 for dp in data_points)
    
    # Cost estimation (e.g. 0.25 € / kWh)
    cost_per_kwh = 0.25
    
    # Active relay hours
    relay_on_count = sum(1 for dp in data_points if dp["relay"])
    relay_on_hours = relay_on_count * 0.5
    
    # Group by date to get daily consumption and hourly profiles
    daily_profiles = {} # date_str -> list of 24 power readings
    daily_energy = {}   # date_str -> total kWh
    
    for dp in data_points:
        date_str = dp["timestamp"].strftime("%Y-%m-%d")
        hour = dp["timestamp"].hour
        
        if date_str not in daily_profiles:
            daily_profiles[date_str] = [[] for _ in range(24)]
            daily_energy[date_str] = 0.0
            
        daily_profiles[date_str][hour].append(dp["power"])
        daily_energy[date_str] += dp["power"] * 0.5 / 1000.0
        
    # Clean daily profiles (calculate average power for each hour)
    clean_daily_profiles = {}
    valid_daily_vectors = []
    dates_list = []
    
    for date_str, hours in daily_profiles.items():
        profile_24h = []
        has_data = False
        for hour in range(24):
            hour_readings = hours[hour]
            if hour_readings:
                profile_24h.append(sum(hour_readings) / len(hour_readings))
                has_data = True
            else:
                profile_24h.append(0.0) # no data
                
        if has_data:
            clean_daily_profiles[date_str] = profile_24h
            valid_daily_vectors.append(profile_24h)
            dates_list.append(date_str)
            
    # Calculate hourly average power profile (across the whole dataset)
    overall_hourly_profile = [0.0] * 24
    for hour in range(24):
        hour_powers = [clean_daily_profiles[d][hour] for d in clean_daily_profiles]
        overall_hourly_profile[hour] = sum(hour_powers) / len(hour_powers) if hour_powers else 0.0
        
    # Calculate weekday averages (0 = Monday, 6 = Sunday)
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
    
    # ML Part: K-Means Clustering on Daily Profiles using scikit-learn
    kmeans = KMeans(n_clusters=2, random_state=42, n_init='auto')
    kmeans.fit(valid_daily_vectors)
    centroids = kmeans.cluster_centers_
    
    # Classify clusters based on total daily power sum to identify which is "High" and "Low"
    cluster_sums = centroids.sum(axis=1)
    high_cluster_idx = int(np.argmax(cluster_sums)) if len(centroids) > 0 else 0
    low_cluster_idx = 1 - high_cluster_idx if len(centroids) > 1 else 0
    
    cluster_profiles = {
        "high_usage_centroid": centroids[high_cluster_idx].tolist() if len(centroids) > high_cluster_idx else [0.0]*24,
        "low_usage_centroid": centroids[low_cluster_idx].tolist() if len(centroids) > low_cluster_idx else [0.0]*24
    }
    
    # Assign days to clusters using the trained model
    labels = kmeans.labels_
    day_assignments = {}
    for date_str, label in zip(dates_list, labels):
        day_assignments[date_str] = "Alta Attività" if label == high_cluster_idx else "Basso Consumo"

    # ML Part 2: Linear Regression for Consumption Trend
    x_indices = np.array(range(len(dates_list))).reshape(-1, 1)
    y_energies = np.array([daily_energy[d] for d in dates_list])
    
    if len(dates_list) >= 2:
        reg = LinearRegression().fit(x_indices, y_energies)
        slope = float(reg.coef_[0])
        intercept = float(reg.intercept_)
    else:
        slope, intercept = 0.0, 0.0
        
    # Interpret slope
    if slope > 0.01:
        trend = "In crescita"
    elif slope < -0.01:
        trend = "In calo"
    else:
        trend = "Stabile"
        
    # ML Part 3: Occupancy & Presence Profiling (Privacy Impact Analysis)
    presence_threshold = 30.0
    presence_probability = [0.0] * 24
    total_days = len(clean_daily_profiles)
    
    if total_days > 0:
        for hour in range(24):
            active_days = sum(1 for d in clean_daily_profiles if clean_daily_profiles[d][hour] > presence_threshold)
            presence_probability[hour] = (active_days / total_days) * 100.0
            
    # Determine presence and absence windows based on probability thresholds
    probable_presence_hours = [h for h in range(24) if presence_probability[h] >= 50.0]
    probable_absence_hours = [h for h in range(8, 20) if presence_probability[h] < 25.0]
    
    def format_hours(hours_list):
        if not hours_list:
            return "Nessuna rilevata"
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
 
    # Anomalies detection
    anomalies = []
    for dp in data_points:
        hour = dp["timestamp"].hour
        if (1 <= hour <= 5) and dp["power"] > 200.0:
            anomalies.append({
                "timestamp": dp["timestamp"].strftime("%Y-%m-%d %H:%M:%S"),
                "type": "Consumo Notturno Insolito",
                "description": f"Rilevato picco di {dp['power']:.1f}W alle ore {hour}:00, tipicamente periodo di riposo."
            })
        if not dp["relay"] and dp["power"] > 5.0:
            anomalies.append({
                "timestamp": dp["timestamp"].strftime("%Y-%m-%d %H:%M:%S"),
                "type": "Carico a Relè Spento",
                "description": f"Misurata potenza di {dp['power']:.1f}W mentre il relè risulta spento."
            })
            
    anomalies = anomalies[-10:]
    anomalies.reverse()
    
    # Recommendations
    recommendations = []
    peak_hour = overall_hourly_profile.index(max(overall_hourly_profile)) if overall_hourly_profile else 0
    recommendations.append(
        f"Il picco massimo di consumo si concentra alle ore {peak_hour}:00. Consigliamo di programmare i carichi pesanti (es. lavatrice) al di fuori di questa fascia."
    )
    
    night_powers = [overall_hourly_profile[h] for h in range(1, 6)]
    avg_night_power = sum(night_powers) / len(night_powers) if night_powers else 0.0
    if avg_night_power > 25.0:
        recommendations.append(
            f"Il tuo consumo notturno medio è di {avg_night_power:.1f}W. Potresti avere dispositivi in standby che assorbono inutilmente energia; valuta l'uso di una multipresa con interruttore."
        )
    else:
        recommendations.append(
            "Ottimo consumo in standby nelle ore notturne. I dispositivi elettronici sono configurati correttamente."
        )
        
    if trend == "In crescita":
        recommendations.append(
            "I consumi giornalieri mostrano una tendenza al rialzo nell'arco delle ultime due settimane. Fai attenzione a eventuali nuovi carichi inseriti di recente."
        )
        
    return {
        "stats": {
            "total_days": len(clean_daily_profiles),
            "avg_power_w": avg_power,
            "max_power_w": max_power,
            "total_energy_kwh": total_energy_kwh,
            "daily_avg_kwh": total_energy_kwh / len(clean_daily_profiles) if clean_daily_profiles else 0.0,
            "est_monthly_cost_eur": (total_energy_kwh / len(clean_daily_profiles)) * 30 * cost_per_kwh if clean_daily_profiles else 0.0,
            "relay_on_hours": relay_on_hours,
            "relay_avg_hours_day": relay_on_hours / len(clean_daily_profiles) if clean_daily_profiles else 0.0,
            "trend": trend,
            "trend_slope": slope
        },
        "profiles": {
            "overall_hourly": overall_hourly_profile,
            "weekday_averages": weekday_averages,
            "cluster_high": cluster_profiles["high_usage_centroid"],
            "cluster_low": cluster_profiles["low_usage_centroid"]
        },
        "day_assignments": day_assignments,
        "presence": presence_analysis,
        "anomalies": anomalies,
        "recommendations": recommendations
    }

if __name__ == "__main__":
    report = analyze_habits()
    import json
    print(json.dumps(report["stats"], indent=2))
