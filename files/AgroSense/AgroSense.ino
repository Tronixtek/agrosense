/*
 * ╔══════════════════════════════════════════════════════════╗
 * ║        AgroSense — Autonomous Maize Irrigation           ║
 * ║        ESP32 Firmware  v3.1                              ║
 * ║        FUTMX Computer Science  ·  2025                    ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * HARDWARE:
 *   3× Capacitive soil moisture sensors  → GPIO 34, 35, 32
 *   4-channel relay module (active HIGH) → GPIO 26, 27, 14
 *
 * LIBRARIES REQUIRED:
 *   - ArduinoJson  by Benoit Blanchon  (v6.x)
 *     Install via: Sketch → Include Library → Manage Libraries
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_system.h>   // esp_reset_reason() — tells a true power-on apart from our own restart
#include "decision_tree_rules.h"

// ════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════
const char* WIFI_SSID     = "VT";
const char* WIFI_PASSWORD = "password";

const char* FB_URL    = "https://agrosense-e8582-default-rtdb.firebaseio.com";
const char* FB_SECRET = "wM9loNSpnjREDCLgXqHOoRR6G0I5izVgPzdESg8i";
const char* FB_HOST   = "agrosense-e8582-default-rtdb.firebaseio.com";

// ════════════════════════════════════════════════
//  PIN MAP
// ════════════════════════════════════════════════
const int SENSOR_PINS[3] = {34, 35, 32};
const int RELAY_PINS[3]  = {26, 27, 14};
// NOTE: GPIO12 is an ESP32 strapping pin (MTDI) that selects flash
// voltage at boot/reset. If the relay board's input idles HIGH before
// this firmware can drive it LOW, the chip may sample the wrong strap
// value on a cold power cycle and boot unreliably — which can look
// exactly like "WiFi has trouble connecting after power-on". If WiFi
// connect issues persist after the firmware fixes below, move this
// wire to a non-strapping GPIO (e.g. 25 or 33) and update this constant.
const int RELAY_SPARE    = 12;

// ════════════════════════════════════════════════
//  SENSOR CALIBRATION  (per-sensor, from calibration tool)
//
//  Sensor 1 (Zone A · GPIO 34):  dry=2266  wet= 440
//  Sensor 2 (Zone B · GPIO 35):  dry=2356  wet= 459
//  Sensor 3 (Zone C · GPIO 32):  dry=2630  wet=1109
//
//  To recalibrate: flash SensorCalibration.ino, run
//  commands D then W, then press P and copy new values here.
// ════════════════════════════════════════════════
const int DRY_VALS[3] = {2266, 2356, 2630};
const int WET_VALS[3] = { 440,  459, 1109};

// ════════════════════════════════════════════════
//  ZONE STATE
// ════════════════════════════════════════════════
struct Zone {
  const char* name;
  int         sensorPin;
  int         relayPin;
  bool        relayActiveLow;  // true if THIS channel energizes on LOW, not HIGH
  char        texture[12];
  int         growthStage;
  float       moisture;
  bool        pumpOn;
  bool        manualOverride;
};

// Zone Alpha's relay channel (GPIO 26) was found wired/behaving inverted
// relative to Beta and Gamma — manual "Start pump" turns it OFF, not ON.
// Rather than re-wire, flip its logic level in firmware so all three zones
// present the same ON/OFF behavior to the rest of the code.
Zone zones[3] = {
  { "Zone Alpha", 34, 26, false,  "loamy", 1, 0.0f, false, false },
  { "Zone Beta",  35, 27, false, "sandy", 1, 0.0f, false, false },
  { "Zone Gamma", 32, 14, false, "clay",  1, 0.0f, false, false },
};

inline int relayOnLevel(int i)  { return zones[i].relayActiveLow ? LOW  : HIGH; }
inline int relayOffLevel(int i) { return zones[i].relayActiveLow ? HIGH : LOW;  }

// ════════════════════════════════════════════════
//  TIMING
// ════════════════════════════════════════════════
const unsigned long SENSOR_INTERVAL  = 2000;
const unsigned long WRITE_INTERVAL   = 5000;
const unsigned long COMMAND_INTERVAL = 3000;
const unsigned long WIFI_RETRY       = 15000;

unsigned long lastSensorRead    = 0;
unsigned long lastFirebaseWrite = 0;
unsigned long lastCommandPoll   = 0;
unsigned long lastWifiCheck     = 0;
unsigned long bootTime          = 0;

int  irrigationsToday = 0;
bool systemMode_auto  = true;

// ════════════════════════════════════════════════
//  WIFI — connect with detailed diagnostics
// ════════════════════════════════════════════════
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println(F("\n[WiFi] Starting connection..."));
  Serial.printf( "[WiFi] SSID     : \"%s\"\n", WIFI_SSID);
  Serial.printf( "[WiFi] Password : \"%s\"\n", WIFI_PASSWORD);

  // Full radio reset before each attempt. After an abrupt power cycle the
  // WiFi driver / router ARP cache can be left in a stale state, which is
  // what causes "SSID and password are correct but it won't connect after
  // power-on" — a plain WiFi.begin() retry doesn't always clear that, but
  // power-cycling the radio (OFF → STA) does.
  WiFi.persistent(false);   // don't wear flash with redundant config writes
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("[WiFi] Waiting"));
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);   // modem sleep causes flaky / slow reconnects on ESP32
    Serial.println(F("[WiFi] ✓ Connected!"));
    Serial.printf( "[WiFi] IP      : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf( "[WiFi] Gateway : %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf( "[WiFi] RSSI    : %d dBm\n", WiFi.RSSI());
    return true;
  }

  // Print the exact failure reason
  Serial.print(F("[WiFi] ✗ Failed. Status code: "));
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
      Serial.println(F("WL_NO_SSID_AVAIL — SSID not found. Check network name."));
      break;
    case WL_CONNECT_FAILED:
      Serial.println(F("WL_CONNECT_FAILED — Wrong password or auth rejected."));
      break;
    case WL_CONNECTION_LOST:
      Serial.println(F("WL_CONNECTION_LOST — Signal lost during connection."));
      break;
    case WL_DISCONNECTED:
      Serial.println(F("WL_DISCONNECTED — Could not connect."));
      break;
    default:
      Serial.println(WiFi.status());
      break;
  }

  // Scan nearby networks so user can verify SSID
  Serial.println(F("[WiFi] Scanning nearby networks..."));
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println(F("[WiFi] No networks found at all!"));
  } else {
    Serial.printf("[WiFi] %d network(s) found:\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %d) SSID: %-30s  RSSI: %d dBm  %s\n",
        i + 1,
        WiFi.SSID(i).c_str(),
        WiFi.RSSI(i),
        (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured");
    }
  }
  WiFi.scanDelete();
  return false;
}

// ════════════════════════════════════════════════
//  SENSOR READING
// ════════════════════════════════════════════════
float readMoisture(int pin, int sensorIndex) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  int raw = (int)(sum / 10);
  Serial.printf("    [ADC] pin %d  raw=%4d  dry=%4d  wet=%4d\n",
                pin, raw, DRY_VALS[sensorIndex], WET_VALS[sensorIndex]);
  float pct = (float)(DRY_VALS[sensorIndex] - raw)
            / (float)(DRY_VALS[sensorIndex] - WET_VALS[sensorIndex])
            * 100.0f;
  return constrain(pct, 0.0f, 100.0f);
}

// ════════════════════════════════════════════════
//  RELAY CONTROL  (active HIGH)
// ════════════════════════════════════════════════
void setPump(int i, bool on) {
  digitalWrite(zones[i].relayPin, on ? relayOnLevel(i) : relayOffLevel(i));
  zones[i].pumpOn = on;
}

void stopAllPumps() {
  for (int i = 0; i < 3; i++) {
    setPump(i, false);
    zones[i].manualOverride = false;
  }
  Serial.println(F("[PUMP] All pumps stopped"));
}

// ════════════════════════════════════════════════
//  FIREBASE HELPERS
// ════════════════════════════════════════════════
String fbUrl(const String& path) {
  return String(FB_URL) + path + ".json?auth=" + FB_SECRET;
}

bool fbPut(const String& path, const String& body) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[FB] PUT skipped — no WiFi"));
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();   // skip SSL cert verification (fine for RTDB)
  HTTPClient http;
  http.begin(client, fbUrl(path));
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PUT", body);
  http.end();
  if (code > 0) {
    Serial.printf("[FB] PUT %s → HTTP %d\n", path.c_str(), code);
  } else {
    Serial.printf("[FB] PUT %s → error %d (%s)\n",
                  path.c_str(), code, http.errorToString(code).c_str());
  }
  return (code == 200);
}

String fbGet(const String& path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[FB] GET skipped — no WiFi"));
    return "null";
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, fbUrl(path));
  int code = http.GET();
  String payload = "null";
  if (code == 200) {
    payload = http.getString();
  } else {
    Serial.printf("[FB] GET %s → error %d (%s)\n",
                  path.c_str(), code, http.errorToString(code).c_str());
  }
  http.end();
  return payload;
}

// ════════════════════════════════════════════════
//  WRITE STATE TO FIREBASE
// ════════════════════════════════════════════════
void writeToFirebase() {
  StaticJsonDocument<1536> doc;

  JsonObject sensors = doc.createNestedObject("sensors");
  for (int i = 0; i < 3; i++) {
    String key = "zone" + String(i);
    JsonObject z = sensors.createNestedObject(key);
    z["name"]        = zones[i].name;
    z["moisture"]    = (float)round(zones[i].moisture * 10.0f) / 10.0f;
    z["texture"]     = zones[i].texture;
    z["growthStage"] = zones[i].growthStage;
    z["pump"]        = zones[i].pumpOn;
    z["action"]      = actionName(decide(zones[i].moisture,
                                         zones[i].texture,
                                         zones[i].growthStage));
    z["threshold"]   = getThreshold(zones[i].texture);
    z["manual"]      = zones[i].manualOverride;
  }

  JsonObject sys = doc.createNestedObject("system");
  sys["uptime"]   = (long)((millis() - bootTime) / 1000UL);
  sys["irrCount"] = irrigationsToday;
  sys["mode"]     = systemMode_auto ? "auto" : "manual";
  sys["ip"]       = WiFi.localIP().toString();
  sys["rssi"]     = WiFi.RSSI();
  sys["lastSeen"] = (long)(millis() / 1000UL);

  String body;
  serializeJson(doc, body);

  Serial.println(F("[FB] Writing state..."));
  fbPut("/", body);
}

// ════════════════════════════════════════════════
//  POLL COMMANDS FROM FIREBASE
// ════════════════════════════════════════════════
void pollCommands() {
  String raw = fbGet("/commands");
  if (raw == "null" || raw.length() < 3) return;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return;

  if (doc.containsKey("mode")) {
    const char* m = doc["mode"];
    bool newAuto  = (strcmp(m, "auto") == 0);
    if (newAuto != systemMode_auto) {
      systemMode_auto = newAuto;
      if (systemMode_auto) stopAllPumps();
      for (int i = 0; i < 3; i++) zones[i].manualOverride = !systemMode_auto;
      Serial.printf("[CMD] Mode → %s\n", systemMode_auto ? "AUTO" : "MANUAL");
    }
  }

  if (doc["stopAll"] == true) {
    stopAllPumps();
    systemMode_auto = true;
    fbPut("/commands/stopAll", "false");
    Serial.println(F("[CMD] Emergency stop"));
  }

  if (doc.containsKey("zones")) {
    for (int i = 0; i < 3; i++) {
      String key = "zone" + String(i);
      if (doc["zones"].containsKey(key)) {
        bool on = (bool)doc["zones"][key]["pump"];
        if (zones[i].pumpOn != on) {
          zones[i].manualOverride = true;
          setPump(i, on);
          Serial.printf("[CMD] Zone %d pump → %s\n", i+1, on?"ON":"OFF");
        }
      }
    }
  }

  if (doc.containsKey("textures")) {
    for (int i = 0; i < 3; i++) {
      String key = "zone" + String(i);
      if (doc["textures"].containsKey(key)) {
        const char* tex = doc["textures"][key];
        strncpy(zones[i].texture, tex, sizeof(zones[i].texture) - 1);
        zones[i].texture[sizeof(zones[i].texture) - 1] = '\0';
        Serial.printf("[CMD] Zone %d texture → %s\n", i+1, zones[i].texture);
      }
    }
  }

  if (doc.containsKey("growthStage")) {
    int stage = constrain((int)doc["growthStage"], 0, 3);
    for (int i = 0; i < 3; i++) zones[i].growthStage = stage;
    const char* names[] = {"Germination","Vegetative","Tasseling","Grain fill"};
    Serial.printf("[CMD] Growth stage → %s\n", names[stage]);
  }
}

// ════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);  // give Serial Monitor time to open

  Serial.println(F("\n╔══════════════════════════════╗"));
  Serial.println(F("║  AgroSense v3.1 — Booting    ║"));
  Serial.println(F("╚══════════════════════════════╝"));

  // Relays OFF before setting pinMode (prevents boot glitch). Each channel's
  // "off" level depends on relayActiveLow — Zone Alpha's is inverted, so a
  // blanket LOW here would actually turn that one ON at every boot/reset.
  digitalWrite(RELAY_SPARE, LOW);
  for (int i = 0; i < 3; i++) {
    digitalWrite(RELAY_PINS[i], relayOffLevel(i));
    pinMode(RELAY_PINS[i], OUTPUT);
  }
  pinMode(RELAY_SPARE, OUTPUT);
  Serial.println(F("[RELAY] All channels OFF"));

  // One-time hard restart, but ONLY on a true power-on — never after our
  // own software restart. (RTC_DATA_ATTR flags do NOT reliably survive
  // ESP.restart(): that section is reloaded from flash on every boot
  // except a deep-sleep wake, so a flag-based guard re-triggers itself
  // forever. esp_reset_reason() reads the actual hardware reset cause,
  // so it correctly tells POWERON apart from our own SW restart.)
  if (esp_reset_reason() == ESP_RST_POWERON) {
    Serial.println(F("[BOOT]  True power-on detected — hard-restarting once for a clean radio state..."));
    delay(150);
    ESP.restart();
  }

  analogReadResolution(12);
  Serial.println(F("[ADC]   12-bit resolution"));

  // Let the capacitive sensors' oscillators settle after power-up. Reading
  // them immediately on a cold boot can return noisy/low values that the
  // decision tree misreads as "critically dry", firing every pump at once.
  delay(300);

  // Connect WiFi — prints scan if it fails
  bool wifiOk = connectWiFi();

  // Initial sensor read
  Serial.println(F("[SENSOR] Initial readings:"));
  for (int i = 0; i < 3; i++) {
    zones[i].moisture = readMoisture(zones[i].sensorPin, i);
    Serial.printf("  Zone %d (%s): %.1f%%\n",
                  i+1, zones[i].name, zones[i].moisture);
  }

  bootTime = millis();

  if (wifiOk) {
    // Pull the LAST KNOWN state (mode, manual pump states, soil texture,
    // growth stage) from Firebase before doing anything else. The firmware
    // used to unconditionally push mode="auto" here, which overwrote
    // whatever the user had actually set and is why the system "forgot"
    // manual mode and needed an Emergency Stop + re-enable after every
    // reboot. Reusing pollCommands() restores the real state instead.
    Serial.println(F("[BOOT]  Syncing last known state from Firebase..."));
    pollCommands();
    Serial.printf("[BOOT]  Restored mode: %s\n", systemMode_auto ? "AUTO" : "MANUAL");

    writeToFirebase();
    Serial.println(F("[FB]    Initial state pushed"));
    Serial.printf( "[BOOT]  Dashboard → https://agrosense-e8582.web.app\n");
  } else {
    Serial.println(F("[BOOT]  Running OFFLINE — fix WiFi and reset ESP32"));
  }

  // Reset the loop timers to "now" so the first loop() iteration doesn't
  // immediately re-run the sensor/command work we just finished above.
  unsigned long bootNow   = millis();
  lastSensorRead    = bootNow;
  lastFirebaseWrite = bootNow;
  lastCommandPoll   = bootNow;
  lastWifiCheck     = bootNow;

  Serial.println(F("\n[BOOT] Entering main loop...\n"));
}

// ════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // WiFi watchdog
  if (now - lastWifiCheck >= WIFI_RETRY) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[WiFi] Reconnecting..."));
      connectWiFi();
    }
  }

  // Read sensors + Decision Tree
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    for (int i = 0; i < 3; i++) {
      zones[i].moisture = readMoisture(zones[i].sensorPin, i);
      if (systemMode_auto && !zones[i].manualOverride) {
        int  action = decide(zones[i].moisture,
                             zones[i].texture,
                             zones[i].growthStage);
        bool should = (action >= ACTION_IRRIGATE);
        if (should  && !zones[i].pumpOn) { setPump(i,true);  irrigationsToday++; }
        if (!should &&  zones[i].pumpOn) { setPump(i,false); }
        Serial.printf("[AUTO] Zone %d → %-8s  %.1f%%  Pump %s\n",
                      i+1, actionName(action), zones[i].moisture,
                      zones[i].pumpOn ? "ON" : "OFF");
      }
    }
  }

  // Push to Firebase
  if (now - lastFirebaseWrite >= WRITE_INTERVAL) {
    lastFirebaseWrite = now;
    writeToFirebase();
  }

  // Poll commands
  if (now - lastCommandPoll >= COMMAND_INTERVAL) {
    lastCommandPoll = now;
    pollCommands();
  }
}
