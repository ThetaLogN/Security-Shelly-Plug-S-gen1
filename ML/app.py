import os
import sys
from flask import Flask, render_template, jsonify, request

# Add current directory to path to import local modules
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(BASE_DIR)

from analyzer import analyze_habits, CSV_PATH

app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/habits")
def api_habits():
    report = analyze_habits()
    return jsonify(report)



@app.route("/api/upload", methods=["POST"])
def api_upload():
    if 'file' not in request.files:
        return jsonify({"status": "error", "message": "No file provided"}), 400
    file = request.files['file']
    if file.filename == '':
        return jsonify({"status": "error", "message": "No file selected"}), 400
    if not file.filename.endswith('.csv'):
        return jsonify({"status": "error", "message": "The file must be in CSV format"}), 400
    try:
        file.save(CSV_PATH)
        return jsonify({"status": "success", "message": "CSV file imported successfully."})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == "__main__":
    # Run on port 5001 to avoid conflicts
    app.run(host="0.0.0.0", port=5001, debug=True)
