from flask import Flask, render_template, request, jsonify
import sqlite3
from datetime import datetime, timezone
import os

app = Flask(__name__)

DATABASE = "smart_farm.db"

# ---------------------------------------------------------
# Database
# ---------------------------------------------------------

def get_db():
    conn = sqlite3.connect(DATABASE)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_db()

    conn.execute("""
        CREATE TABLE IF NOT EXISTS sensor_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            soil_moisture REAL NOT NULL,
            temperature REAL NOT NULL,
            humidity REAL NOT NULL,
            pump INTEGER NOT NULL,
            mode TEXT NOT NULL
        )
    """)

    conn.execute("""
        CREATE TABLE IF NOT EXISTS settings (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            mode TEXT NOT NULL DEFAULT 'AUTO',
            pump INTEGER NOT NULL DEFAULT 0,
            threshold REAL NOT NULL DEFAULT 40,
            hysteresis REAL NOT NULL DEFAULT 5,
            updated_at TEXT NOT NULL
        )
    """)

    existing = conn.execute(
        "SELECT id FROM settings WHERE id = 1"
    ).fetchone()

    if existing is None:
        now = datetime.now(timezone.utc).isoformat()

        conn.execute("""
            INSERT INTO settings
            (id, mode, pump, threshold, hysteresis, updated_at)
            VALUES (1, 'AUTO', 0, 40, 5, ?)
        """, (now,))

    conn.commit()
    conn.close()


# ---------------------------------------------------------
# Helper functions
# ---------------------------------------------------------

def get_settings():
    conn = get_db()

    row = conn.execute("""
        SELECT mode, pump, threshold, hysteresis, updated_at
        FROM settings
        WHERE id = 1
    """).fetchone()

    conn.close()

    return dict(row)


def update_settings(mode=None, pump=None,
                    threshold=None, hysteresis=None):

    conn = get_db()

    current = conn.execute("""
        SELECT mode, pump, threshold, hysteresis
        FROM settings
        WHERE id = 1
    """).fetchone()

    new_mode = mode if mode is not None else current["mode"]
    new_pump = pump if pump is not None else current["pump"]
    new_threshold = (
        threshold
        if threshold is not None
        else current["threshold"]
    )
    new_hysteresis = (
        hysteresis
        if hysteresis is not None
        else current["hysteresis"]
    )

    now = datetime.now(timezone.utc).isoformat()

    conn.execute("""
        UPDATE settings
        SET mode = ?,
            pump = ?,
            threshold = ?,
            hysteresis = ?,
            updated_at = ?
        WHERE id = 1
    """, (
        new_mode,
        int(new_pump),
        new_threshold,
        new_hysteresis,
        now
    ))

    conn.commit()
    conn.close()


# ---------------------------------------------------------
# Main webpage
# ---------------------------------------------------------

@app.route("/")
def index():
    return render_template("index.html")


# ---------------------------------------------------------
# Latest sensor reading
# ---------------------------------------------------------

@app.route("/api/latest", methods=["GET"])
def latest():

    conn = get_db()

    row = conn.execute("""
        SELECT
            timestamp,
            soil_moisture,
            temperature,
            humidity,
            pump,
            mode
        FROM sensor_logs
        ORDER BY id DESC
        LIMIT 1
    """).fetchone()

    conn.close()

    settings = get_settings()

    if row is None:
        return jsonify({
            "soil_moisture": 0,
            "temperature": 0,
            "humidity": 0,
            "pump": bool(settings["pump"]),
            "mode": settings["mode"],
            "threshold": settings["threshold"],
            "hysteresis": settings["hysteresis"],
            "updated_at": None
        })

    return jsonify({
        "soil_moisture": row["soil_moisture"],
        "temperature": row["temperature"],
        "humidity": row["humidity"],
        "pump": bool(row["pump"]),
        "mode": row["mode"],
        "threshold": settings["threshold"],
        "hysteresis": settings["hysteresis"],
        "updated_at": row["timestamp"]
    })


# ---------------------------------------------------------
# Sensor history
# ---------------------------------------------------------

@app.route("/api/logs", methods=["GET"])
def logs():

    try:
        limit = int(request.args.get("limit", 8))
    except ValueError:
        limit = 8

    limit = max(1, min(limit, 100))

    conn = get_db()

    rows = conn.execute("""
        SELECT
            timestamp,
            soil_moisture,
            temperature,
            humidity
        FROM sensor_logs
        ORDER BY id DESC
        LIMIT ?
    """, (limit,)).fetchall()

    conn.close()

    return jsonify([
        {
            "timestamp": row["timestamp"],
            "soil_moisture": row["soil_moisture"],
            "temperature": row["temperature"],
            "humidity": row["humidity"]
        }
        for row in rows
    ])


# ---------------------------------------------------------
# ESP8266 sends sensor data here
# ---------------------------------------------------------

@app.route("/api/sensor", methods=["POST"])
def receive_sensor():

    data = request.get_json(silent=True)

    if not data:
        return jsonify({
            "success": False,
            "message": "No JSON data received"
        }), 400

    try:
        soil_moisture = float(data["soil_moisture"])
        temperature = float(data["temperature"])
        humidity = float(data["humidity"])
    except (KeyError, TypeError, ValueError):
        return jsonify({
            "success": False,
            "message": "Invalid sensor data"
        }), 400

    # Keep values within reasonable ranges
    soil_moisture = max(0, min(100, soil_moisture))
    humidity = max(0, min(100, humidity))

    settings = get_settings()

    mode = settings["mode"]
    pump = bool(settings["pump"])

    # -----------------------------------------------------
    # Automatic irrigation logic
    #
    # Pump ON:
    #     soil <= threshold
    #
    # Pump OFF:
    #     soil >= threshold + hysteresis
    #
    # This prevents rapid ON/OFF switching.
    # -----------------------------------------------------

    if mode == "AUTO":

        if soil_moisture <= settings["threshold"]:
            pump = True

        elif soil_moisture >= (
            settings["threshold"] + settings["hysteresis"]
        ):
            pump = False

        update_settings(pump=int(pump))

    # -----------------------------------------------------
    # Save sensor reading
    # -----------------------------------------------------

    timestamp = datetime.now(timezone.utc).isoformat()

    conn = get_db()

    conn.execute("""
        INSERT INTO sensor_logs
        (
            timestamp,
            soil_moisture,
            temperature,
            humidity,
            pump,
            mode
        )
        VALUES (?, ?, ?, ?, ?, ?)
    """, (
        timestamp,
        soil_moisture,
        temperature,
        humidity,
        int(pump),
        mode
    ))

    conn.commit()
    conn.close()

    return jsonify({
        "success": True,
        "pump": pump,
        "mode": mode,
        "threshold": settings["threshold"],
        "hysteresis": settings["hysteresis"]
    })


# ---------------------------------------------------------
# ESP8266 asks for current settings
# ---------------------------------------------------------

@app.route("/api/settings", methods=["GET"])
def device_settings():

    settings = get_settings()

    return jsonify({
        "mode": settings["mode"],
        "pump": bool(settings["pump"]),
        "threshold": settings["threshold"],
        "hysteresis": settings["hysteresis"]
    })


# ---------------------------------------------------------
# AUTO / MANUAL mode
# ---------------------------------------------------------

@app.route("/api/mode", methods=["POST"])
def set_mode():

    data = request.get_json(silent=True)

    if not data or data.get("mode") not in ["AUTO", "MANUAL"]:
        return jsonify({
            "success": False,
            "message": "Mode must be AUTO or MANUAL"
        }), 400

    mode = data["mode"]

    # When switching to AUTO, immediately turn the pump off.
    # The next sensor reading will determine whether it needs
    # to turn on.
    if mode == "AUTO":
        update_settings(
            mode="AUTO",
            pump=0
        )
    else:
        update_settings(
            mode="MANUAL"
        )

    return jsonify({
        "success": True,
        "mode": mode
    })


# ---------------------------------------------------------
# Manual pump control
# ---------------------------------------------------------

@app.route("/api/pump", methods=["POST"])
def set_pump():

    data = request.get_json(silent=True)

    if not data or "pump" not in data:
        return jsonify({
            "success": False,
            "message": "Pump value is required"
        }), 400

    settings = get_settings()

    # Pump can only be manually controlled in MANUAL mode
    if settings["mode"] != "MANUAL":
        return jsonify({
            "success": False,
            "message": "Switch to MANUAL mode first"
        }), 400

    pump = bool(data["pump"])

    update_settings(pump=int(pump))

    return jsonify({
        "success": True,
        "pump": pump
    })


# ---------------------------------------------------------
# Irrigation threshold settings
# ---------------------------------------------------------

@app.route("/api/threshold", methods=["POST"])
def set_threshold():

    data = request.get_json(silent=True)

    if not data:
        return jsonify({
            "success": False,
            "message": "No settings received"
        }), 400

    try:
        threshold = float(data["threshold"])
        hysteresis = float(data["hysteresis"])
    except (KeyError, TypeError, ValueError):
        return jsonify({
            "success": False,
            "message": "Invalid threshold values"
        }), 400

    threshold = max(0, min(100, threshold))
    hysteresis = max(0, min(30, hysteresis))

    update_settings(
        threshold=threshold,
        hysteresis=hysteresis
    )

    return jsonify({
        "success": True,
        "threshold": threshold,
        "hysteresis": hysteresis
    })


# ---------------------------------------------------------
# Start application
# ---------------------------------------------------------

init_db()

if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=int(os.environ.get("PORT", 5000)),
        debug=False
    )