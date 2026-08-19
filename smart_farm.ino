#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =====================================================
// WIFI
// =====================================================

struct WiFiNetwork
{
  const char* ssid;
  const char* password;
};

WiFiNetwork wifiNetworks[] =
{
  {"Shesh.", "POGI-AKO"},
  {"T.I.P.ian Student", ""},
  {"TIPMLA-AUN", "AnotherPassword"}

};

const int WIFI_NETWORK_COUNT =
  sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// =====================================================
// FLASK SERVER
// =====================================================

const char* SERVER_URL =
"https://nixxinix.pythonanywhere.com";

// =====================================================
// PIN DEFINITIONS
// =====================================================

#define SOIL_PIN A0

// GPIO2 = NodeMCU D4
#define DHT_PIN 2
#define DHT_TYPE DHT11

// GPIO14 = NodeMCU D5
#define RELAY_PIN 14

// I2C
// GPIO4 = NodeMCU D2 = SDA
// GPIO5 = NodeMCU D1 = SCL

#define I2C_SDA 4
#define I2C_SCL 5

// =====================================================
// RELAY CONFIGURATION
// =====================================================

// Based on your test:
// Serial says Pump OFF -> relay LED turns ON
// Serial says Pump ON  -> relay LED turns OFF
//
// Therefore your relay appears to be ACTIVE HIGH.
//
// If this is still reversed, change this to true.

const bool RELAY_ACTIVE_LOW = false;

// =====================================================
// SOIL SENSOR CALIBRATION
// =====================================================

// Your measured values:
//
// Dry = approximately 13
// Wet = approximately 200

const int SOIL_DRY_VALUE = 13;
const int SOIL_WET_VALUE = 200;

// =====================================================
// TIMING
// =====================================================

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long WIFI_CHECK_INTERVAL = 5000;

unsigned long lastSensorTime = 0;
unsigned long lastWifiCheckTime = 0;

// =====================================================
// OBJECTS
// =====================================================

DHT dht(DHT_PIN, DHT_TYPE);

LiquidCrystal_I2C lcd(0x3F, 16, 2);

// =====================================================
// CURRENT DEVICE STATE
// =====================================================

String currentMode = "AUTO";

bool pumpState = false;

float threshold = 40.0;
float hysteresis = 5.0;

// =====================================================
// RELAY CONTROL
// =====================================================

void setPump(bool state)
{
  if (pumpState != state)
  {
    Serial.print("Pump state changed: ");
    Serial.println(state ? "ON" : "OFF");
  }

  pumpState = state;

  if (RELAY_ACTIVE_LOW)
  {
    digitalWrite(
      RELAY_PIN,
      state ? LOW : HIGH
    );
  }
  else
  {
    digitalWrite(
      RELAY_PIN,
      state ? HIGH : LOW
    );
  }
}

// =====================================================
// SOIL MOISTURE
// =====================================================

float readSoilMoisture()
{
  int rawValue = analogRead(SOIL_PIN);

  Serial.print("Raw soil ADC: ");
  Serial.println(rawValue);

  float moisture =
    ((float)(rawValue - SOIL_DRY_VALUE) * 100.0) /
    (float)(SOIL_WET_VALUE - SOIL_DRY_VALUE);

  moisture = constrain(
    moisture,
    0.0,
    100.0
  );

  Serial.print("Soil moisture: ");
  Serial.print(moisture);
  Serial.println("%");

  return moisture;
}

// =====================================================
// AUTOMATIC PUMP CONTROL
// =====================================================

void automaticPumpControl(float soil)
{
  if (currentMode != "AUTO")
  {
    return;
  }

  // Turn pump ON when soil is sufficiently dry.
  if (
    soil <= (threshold - hysteresis) &&
    !pumpState
  )
  {
    setPump(true);

    Serial.println(
      "AUTO: Soil is dry -> PUMP ON"
    );
  }

  // Turn pump OFF when soil is sufficiently moist.
  else if (
    soil >= (threshold + hysteresis) &&
    pumpState
  )
  {
    setPump(false);

    Serial.println(
      "AUTO: Soil is moist -> PUMP OFF"
    );
  }
}

// =====================================================
// LCD
// =====================================================

void updateLCD(
  float soil,
  float temperature,
  float humidity
)
{
  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("Soil:");
  lcd.print((int)soil);
  lcd.print("% ");

  lcd.print(
    pumpState ? "ON" : "OFF"
  );

  lcd.setCursor(0, 1);

  lcd.print(temperature, 1);
  lcd.print("C ");

  lcd.print((int)humidity);
  lcd.print("%");
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.setSleepMode(
    WIFI_NONE_SLEEP
  );

  Serial.println();
  Serial.println("Connecting to WiFi...");

  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  unsigned long startAttempt =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startAttempt < 20000
  )
  {
    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi Connected");

    Serial.print("IP Address: ");
    Serial.println(
      WiFi.localIP()
    );

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");

    lcd.setCursor(0, 1);
    lcd.print(
      WiFi.localIP()
    );
  }
  else
  {
    Serial.println(
      "WiFi connection failed"
    );

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed");

    lcd.setCursor(0, 1);
    lcd.print("Retrying...");
  }

  delay(1000);
}

// =====================================================
// SEND SENSOR DATA TO FLASK
// =====================================================

void sendSensorData(
  float soil,
  float temperature,
  float humidity
)
{
  if (
    WiFi.status() != WL_CONNECTED
  )
  {
    Serial.println(
      "WiFi not connected - data not sent"
    );

    return;
  }

  WiFiClientSecure client;

  // Prototype/testing
  client.setInsecure();

  HTTPClient http;

  String url =
    String(SERVER_URL) +
    "/api/sensor";

  Serial.print("POST URL: ");
  Serial.println(url);

  if (!http.begin(client, url))
  {
    Serial.println(
      "Failed to connect to sensor API"
    );

    return;
  }

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  DynamicJsonDocument doc(512);

  doc["soil_moisture"] = soil;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;

  String requestBody;

  serializeJson(
    doc,
    requestBody
  );

  Serial.print("Sending: ");
  Serial.println(requestBody);

  int httpCode =
    http.POST(requestBody);

  Serial.print("Sensor HTTP: ");
  Serial.println(httpCode);

  if (
    httpCode == HTTP_CODE_OK
  )
  {
    String response =
      http.getString();

    Serial.print(
      "Server response: "
    );

    Serial.println(response);

    DynamicJsonDocument responseDoc(
      1024
    );

    DeserializationError error =
      deserializeJson(
        responseDoc,
        response
      );

    if (!error)
    {
      // -------------------------------------------
      // Get server settings
      // -------------------------------------------

      if (
        responseDoc.containsKey("mode")
      )
      {
        currentMode =
          responseDoc["mode"].as<String>();
      }

      if (
        responseDoc.containsKey("threshold")
      )
      {
        threshold =
          responseDoc["threshold"].as<float>();
      }

      if (
        responseDoc.containsKey("hysteresis")
      )
      {
        hysteresis =
          responseDoc["hysteresis"].as<float>();
      }

      // -------------------------------------------
      // If MANUAL mode, server controls pump
      // -------------------------------------------

      if (
        currentMode == "MANUAL" &&
        responseDoc.containsKey("pump")
      )
      {
        bool serverPump =
          responseDoc["pump"].as<bool>();

        setPump(serverPump);
      }

      Serial.print("Mode from server: ");
      Serial.println(currentMode);

      Serial.print("Threshold: ");
      Serial.println(threshold);

      Serial.print("Hysteresis: ");
      Serial.println(hysteresis);
    }
    else
    {
      Serial.println(
        "Sensor response JSON error"
      );
    }
  }
  else
  {
    Serial.println(
      "Server request failed"
    );
  }

  http.end();
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("SMART FARM STARTING");
  Serial.println("==============================");

  // ---------------------------------------------------
  // Relay
  // ---------------------------------------------------

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  // Pump OFF at startup
  setPump(false);

  // ---------------------------------------------------
  // I2C LCD
  // ---------------------------------------------------

  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Smart Farm");

  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  delay(1000);

  // ---------------------------------------------------
  // DHT11
  // ---------------------------------------------------

  dht.begin();

  // ---------------------------------------------------
  // WiFi
  // ---------------------------------------------------

  connectWiFi();

  // ---------------------------------------------------
  // Startup
  // ---------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Smart Farm");

  lcd.setCursor(0, 1);
  lcd.print("Online Ready");

  delay(1500);

  Serial.println();
  Serial.println("System ready.");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  unsigned long currentTime =
    millis();

  // ===================================================
  // WIFI CHECK
  // ===================================================

  if (
    currentTime - lastWifiCheckTime
    >= WIFI_CHECK_INTERVAL
  )
  {
    lastWifiCheckTime =
      currentTime;

    if (
      WiFi.status() != WL_CONNECTED
    )
    {
      Serial.println();
      Serial.println(
        "WiFi disconnected."
      );

      Serial.println(
        "Attempting reconnect..."
      );

      WiFi.disconnect();

      WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
      );
    }
  }

  // ===================================================
  // SENSOR LOOP
  // ===================================================

  if (
    currentTime - lastSensorTime
    >= SENSOR_INTERVAL
  )
  {
    lastSensorTime =
      currentTime;

    // -----------------------------------------------
    // Read sensors
    // -----------------------------------------------

    float soil =
      readSoilMoisture();

    float temperature =
      dht.readTemperature();

    float humidity =
      dht.readHumidity();

    // -----------------------------------------------
    // DHT11 check
    // -----------------------------------------------

    if (
      isnan(temperature) ||
      isnan(humidity)
    )
    {
      Serial.println(
        "DHT11 reading failed"
      );

      return;
    }

    // -----------------------------------------------
    // LOCAL AUTO CONTROL
    // -----------------------------------------------

    automaticPumpControl(
      soil
    );

    // -----------------------------------------------
    // Send to PythonAnywhere
    // -----------------------------------------------

    sendSensorData(
      soil,
      temperature,
      humidity
    );

    // -----------------------------------------------
    // LCD
    // -----------------------------------------------

    updateLCD(
      soil,
      temperature,
      humidity
    );

    // -----------------------------------------------
    // Serial Monitor
    // -----------------------------------------------

    Serial.println(
      "------------------------------"
    );

    Serial.print(
      "Soil: "
    );

    Serial.print(
      soil
    );

    Serial.println("%");

    Serial.print(
      "Temperature: "
    );

    Serial.print(
      temperature
    );

    Serial.println(" C");

    Serial.print(
      "Humidity: "
    );

    Serial.print(
      humidity
    );

    Serial.println("%");

    Serial.print(
      "Mode: "
    );

    Serial.println(
      currentMode
    );

    Serial.print(
      "Threshold: "
    );

    Serial.print(
      threshold
    );

    Serial.println("%");

    Serial.print(
      "Hysteresis: "
    );

    Serial.print(
      hysteresis
    );

    Serial.println("%");

    Serial.print(
      "Pump: "
    );

    Serial.println(
      pumpState
        ? "ON"
        : "OFF"
    );

    Serial.println(
      "------------------------------"
    );
  }
}
