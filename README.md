# 🌱 Smart Farm Irrigation Monitoring and Control System

A small IoT system that checks soil, temperature, and humidity, and can turn a water pump on or off — by itself (Auto) or by hand (Manual).

## What it does

- Reads soil moisture, temperature, and humidity every 2 seconds.
- Shows live readings on a web dashboard.
- Controls a water pump (or LED for testing).
- Two modes:
  - **Auto** — pump turns on when soil is dry, off when soil is wet enough.
  - **Manual** — you turn the pump on or off yourself.
- Keeps a short history of past readings.

## What you need

**Hardware**
- ESP8266 NodeMCU
- Soil moisture sensor
- DHT11 (temperature + humidity sensor)
- I2C LCD (16x2)
- Relay module + water pump (or an LED for testing)

**Software**
- Arduino IDE (for the ESP8266 code)
- Python + Flask (for the web server)
- A PythonAnywhere account (to host the dashboard online)

## Project files

| File | What it's for |
|---|---|
| `esp8266/smart_irrigation.ino` | Code that runs on the ESP8266 |
| `app.py` | The web server (Flask) |
| `requirements.txt` | Python packages needed |
| `templates/index.html` | The dashboard page |
| `static/style.css` | Dashboard design |
| `static/script.js` | Dashboard live updates |

## Setup (short version)

1. **Wire the hardware** — connect the sensors, LCD, and relay/LED to the ESP8266 as shown in your pin table.
2. **Edit the ESP8266 code** — open `smart_irrigation.ino` in Arduino IDE and set:
   - Your Wi-Fi name and password
   - Your server URL (local IP first, then your PythonAnywhere link later)
3. **Run the server locally first**
   ```
   pip install -r requirements.txt
   python app.py
   ```
   Open `http://localhost:5000` in your browser to check it works.
4. **Upload the ESP8266 code** through Arduino IDE.
5. **Deploy to PythonAnywhere** — upload the same files, set up the web app, then update `SERVER_URL` in the ESP8266 code to your PythonAnywhere link.
6. **Re-upload** the ESP8266 code with the new URL.

## How to use the dashboard

1. **Check the top-right dot.** Green and pulsing = live data. Orange = waiting. Red = device offline.
2. **Look at the soil ring.** Shows soil moisture as a percent. Below it says "Dry," "Very dry," or "Moist."
3. **Check Temperature and Humidity** in their own boxes.
4. **Pick a mode:**
   - Tap **Auto** to let the system water the plant by itself.
   - Tap **Manual** to control the pump yourself.
5. **Control the pump (Manual mode only).** Tap the round button. It shows **ON** or **OFF**. While it's changing, the button dims and can't be tapped again — wait a few seconds for it to update.
6. **Set your watering rule (Auto mode).** Under Irrigation Settings:
   - First slider: soil % where the pump turns ON.
   - Second slider: how much wetter the soil must get before the pump turns OFF.
   - Tap **Save settings**.
7. **Check history.** Scroll down to Sensor History to see past readings.

## Notes

- The dashboard updates every ~2 seconds. This is normal — it is not truly instant, but close enough to feel live.
- If the device goes offline, the ESP8266 keeps watering the plant on its own using the last known settings. It does not stop just because the internet is down.
- Never wire a real pump directly to the ESP8266. Always use a relay or driver, with power from a separate supply.# Smart_Farm-Embedded-Project
