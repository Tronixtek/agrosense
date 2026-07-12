/*
 * ╔══════════════════════════════════════════════════════════╗
 * ║     AgroSense — Sensor Calibration & Test Tool           ║
 * ║     ESP32  ·  3× Capacitive Soil Moisture Sensors        ║
 * ║     FUTO Computer Science  ·  2025                       ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * PURPOSE:
 *   This sketch helps you find the correct DRY_VAL and WET_VAL
 *   for YOUR specific sensors, then verifies the moisture %
 *   readings are accurate before flashing the main firmware.
 *
 * WIRING (same as main firmware):
 *   Sensor 1  AO → GPIO 34    VCC → 3.3V    GND → GND
 *   Sensor 2  AO → GPIO 35    VCC → 3.3V    GND → GND
 *   Sensor 3  AO → GPIO 32    VCC → 3.3V    GND → GND
 *
 * HOW TO USE:
 *   Step 1 — Flash this sketch to the ESP32
 *   Step 2 — Open Serial Monitor at 115200 baud
 *   Step 3 — Follow the on-screen menu
 *
 * CALIBRATION PROCEDURE:
 *   1. Leave all sensors in dry air for 60 seconds
 *      → Send command "D" to record dry values
 *   2. Dip all sensor tips ~2cm into water
 *      → Send command "W" to record wet values
 *   3. Send "T" to test with soil samples
 *   4. Copy the printed DRY_VAL / WET_VAL into AgroSense.ino
 */

// ════════════════════════════════════════════════
//  PINS
// ════════════════════════════════════════════════
const int SENSOR_PINS[3]  = {34, 35, 32};
const char* SENSOR_NAMES[3] = {"Sensor 1 (Zone A)", "Sensor 2 (Zone B)", "Sensor 3 (Zone C)"};

// ════════════════════════════════════════════════
//  CALIBRATION VALUES
//  These will be updated when you run the dry/wet steps.
//  Starting defaults — will be overwritten during calibration.
// ════════════════════════════════════════════════
int dryVal[3] = {3200, 3200, 3200};  // raw ADC in dry air
int wetVal[3] = {1400, 1400, 1400};  // raw ADC fully wet
bool dryCalibrated = false;
bool wetCalibrated = false;

// ════════════════════════════════════════════════
//  ADC SAMPLING
//  Average N samples to reduce noise.
// ════════════════════════════════════════════════
const int NUM_SAMPLES  = 20;
const int SAMPLE_DELAY = 10;   // ms between samples

int readRaw(int pin) {
  long sum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(pin);
    delay(SAMPLE_DELAY);
  }
  return (int)(sum / NUM_SAMPLES);
}

// ════════════════════════════════════════════════
//  CONVERT RAW → MOISTURE %
// ════════════════════════════════════════════════
float rawToMoisture(int raw, int dry, int wet) {
  float pct = (float)(dry - raw) / (float)(dry - wet) * 100.0f;
  return constrain(pct, 0.0f, 100.0f);
}

// ════════════════════════════════════════════════
//  PRINT SEPARATOR
// ════════════════════════════════════════════════
void separator(char c = '-', int n = 52) {
  for (int i = 0; i < n; i++) Serial.print(c);
  Serial.println();
}

// ════════════════════════════════════════════════
//  PRINT MENU
// ════════════════════════════════════════════════
void printMenu() {
  Serial.println();
  separator('=');
  Serial.println(F("  AgroSense Sensor Calibration Tool"));
  separator('=');
  Serial.println(F("  Commands (type letter + Enter):"));
  Serial.println(F("  ─────────────────────────────────"));
  Serial.println(F("  R  — Read raw ADC values (all sensors)"));
  Serial.println(F("  M  — Read moisture % (all sensors)"));
  Serial.println(F("  D  — Record DRY values  (sensors in air)"));
  Serial.println(F("  W  — Record WET values  (sensors in water)"));
  Serial.println(F("  T  — Run continuous test (every 2s, Ctrl+C to stop)"));
  Serial.println(F("  C  — Show current calibration values"));
  Serial.println(F("  P  — Print code snippet for AgroSense.ino"));
  Serial.println(F("  1  — Test Sensor 1 only"));
  Serial.println(F("  2  — Test Sensor 2 only"));
  Serial.println(F("  3  — Test Sensor 3 only"));
  Serial.println(F("  H  — Show this menu"));
  separator('=');

  // Show calibration status
  Serial.print(F("  Status: Dry calibration "));
  Serial.print(dryCalibrated ? F("✓ DONE") : F("✗ PENDING"));
  Serial.print(F("   Wet calibration "));
  Serial.println(wetCalibrated ? F("✓ DONE") : F("✗ PENDING"));
  separator('-');
}

// ════════════════════════════════════════════════
//  READ ALL RAW
// ════════════════════════════════════════════════
void readAllRaw() {
  separator();
  Serial.println(F("  RAW ADC VALUES  (0 = fully wet,  4095 = fully dry)"));
  separator();
  for (int i = 0; i < 3; i++) {
    int raw = readRaw(SENSOR_PINS[i]);
    Serial.printf("  %-22s  GPIO %2d  →  raw = %4d",
                  SENSOR_NAMES[i], SENSOR_PINS[i], raw);
    // Visual bar
    Serial.print(F("  ["));
    int bar = map(raw, 0, 4095, 0, 30);
    for (int b = 0; b < 30; b++) Serial.print(b < bar ? '#' : '.');
    Serial.println(F("]"));
  }
  separator();
}

// ════════════════════════════════════════════════
//  READ ALL MOISTURE %
// ════════════════════════════════════════════════
void readAllMoisture() {
  separator();
  Serial.println(F("  MOISTURE %  (using current calibration)"));
  separator();
  for (int i = 0; i < 3; i++) {
    int   raw = readRaw(SENSOR_PINS[i]);
    float pct = rawToMoisture(raw, dryVal[i], wetVal[i]);

    // Status label
    const char* status;
    if      (pct < 25)  status = "DRY   ← irrigate now";
    else if (pct < 45)  status = "LOW   ← monitor closely";
    else if (pct < 70)  status = "GOOD  ← optimal range";
    else if (pct < 85)  status = "MOIST ← no irrigation needed";
    else                status = "WET   ← overwatered?";

    Serial.printf("  %-22s  raw=%4d  moisture=%5.1f%%  %s\n",
                  SENSOR_NAMES[i], raw, pct, status);

    // Moisture bar
    Serial.print(F("    ["));
    int bar = (int)(pct / 100.0f * 40);
    for (int b = 0; b < 40; b++) {
      if (b < bar) {
        if      (pct < 25) Serial.print('!');
        else if (pct < 45) Serial.print('-');
        else               Serial.print('#');
      } else {
        Serial.print('.');
      }
    }
    Serial.printf("] %4.1f%%\n", pct);
  }
  separator();
}

// ════════════════════════════════════════════════
//  CALIBRATE DRY
// ════════════════════════════════════════════════
void calibrateDry() {
  separator('=');
  Serial.println(F("  DRY CALIBRATION"));
  Serial.println(F("  Make sure ALL sensors are in DRY AIR."));
  Serial.println(F("  Sampling in 3 seconds..."));
  separator();
  for (int c = 3; c > 0; c--) {
    Serial.printf("  %d...\n", c);
    delay(1000);
  }
  Serial.println(F("  Sampling..."));
  for (int i = 0; i < 3; i++) {
    dryVal[i] = readRaw(SENSOR_PINS[i]);
    Serial.printf("  %-22s  DRY raw = %d\n", SENSOR_NAMES[i], dryVal[i]);
  }
  dryCalibrated = true;
  Serial.println(F("\n  ✓ Dry values saved!"));
  separator('=');
}

// ════════════════════════════════════════════════
//  CALIBRATE WET
// ════════════════════════════════════════════════
void calibrateWet() {
  separator('=');
  Serial.println(F("  WET CALIBRATION"));
  Serial.println(F("  Dip ALL sensor tips ~2cm into clean water."));
  Serial.println(F("  Do NOT submerge the electronics — only the probes."));
  Serial.println(F("  Sampling in 5 seconds..."));
  separator();
  for (int c = 5; c > 0; c--) {
    Serial.printf("  %d...\n", c);
    delay(1000);
  }
  Serial.println(F("  Sampling..."));
  for (int i = 0; i < 3; i++) {
    wetVal[i] = readRaw(SENSOR_PINS[i]);
    Serial.printf("  %-22s  WET raw = %d\n", SENSOR_NAMES[i], wetVal[i]);
  }
  wetCalibrated = true;
  Serial.println(F("\n  ✓ Wet values saved!"));

  // Sanity check
  Serial.println(F("\n  Sanity check:"));
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    int diff = dryVal[i] - wetVal[i];
    if (diff < 200) {
      Serial.printf("  ⚠ Sensor %d: dry-wet difference is only %d — "
                    "sensor may not be working correctly!\n", i+1, diff);
      ok = false;
    } else {
      Serial.printf("  ✓ Sensor %d: range = %d  (dry=%d, wet=%d)  — looks good\n",
                    i+1, diff, dryVal[i], wetVal[i]);
    }
  }
  if (!ok) {
    Serial.println(F("\n  ⚠ Check wiring and that sensors are actually in water."));
  }
  separator('=');
}

// ════════════════════════════════════════════════
//  SHOW CALIBRATION VALUES
// ════════════════════════════════════════════════
void showCalibration() {
  separator('=');
  Serial.println(F("  CURRENT CALIBRATION VALUES"));
  separator();
  Serial.printf("  %-22s  DRY_VAL   WET_VAL   RANGE\n", "Sensor");
  separator();
  for (int i = 0; i < 3; i++) {
    int range = dryVal[i] - wetVal[i];
    Serial.printf("  %-22s  %4d      %4d      %4d\n",
                  SENSOR_NAMES[i], dryVal[i], wetVal[i], range);
  }
  separator('=');
}

// ════════════════════════════════════════════════
//  PRINT CODE SNIPPET
// ════════════════════════════════════════════════
void printCodeSnippet() {
  separator('=');
  Serial.println(F("  Copy these values into AgroSense.ino:"));
  separator();
  Serial.println();

  // If sensors have different values, use per-sensor arrays
  bool allSameDry = (dryVal[0]==dryVal[1] && dryVal[1]==dryVal[2]);
  bool allSameWet = (wetVal[0]==wetVal[1] && wetVal[1]==wetVal[2]);

  if (allSameDry && allSameWet) {
    Serial.printf("  const int DRY_VAL = %d;\n", dryVal[0]);
    Serial.printf("  const int WET_VAL = %d;\n", wetVal[0]);
  } else {
    Serial.println(F("  // Sensors have different calibration values"));
    Serial.println(F("  // Replace the arrays in AgroSense.ino:"));
    Serial.println();
    Serial.print(F("  const int DRY_VALS[3] = {"));
    for (int i = 0; i < 3; i++) {
      Serial.print(dryVal[i]);
      if (i < 2) Serial.print(F(", "));
    }
    Serial.println(F("};"));
    Serial.print(F("  const int WET_VALS[3] = {"));
    for (int i = 0; i < 3; i++) {
      Serial.print(wetVal[i]);
      if (i < 2) Serial.print(F(", "));
    }
    Serial.println(F("};"));
  }
  Serial.println();
  separator('=');
}

// ════════════════════════════════════════════════
//  TEST SINGLE SENSOR
// ════════════════════════════════════════════════
void testSingleSensor(int idx) {
  separator();
  Serial.printf("  TESTING %s  (GPIO %d)\n",
                SENSOR_NAMES[idx], SENSOR_PINS[idx]);
  Serial.println(F("  Reading every second for 10 seconds..."));
  separator();
  for (int t = 0; t < 10; t++) {
    int   raw = readRaw(SENSOR_PINS[idx]);
    float pct = rawToMoisture(raw, dryVal[idx], wetVal[idx]);
    Serial.printf("  [%2ds]  raw = %4d   moisture = %5.1f%%\n",
                  t+1, raw, pct);
    delay(1000);
  }
  separator();
  Serial.println(F("  Done. Type H for menu."));
}

// ════════════════════════════════════════════════
//  CONTINUOUS TEST
// ════════════════════════════════════════════════
void continuousTest() {
  Serial.println(F("\n  CONTINUOUS TEST — reading every 2 seconds"));
  Serial.println(F("  Send any character to stop.\n"));
  int reading = 0;
  while (Serial.available() == 0 || reading < 3) {
    Serial.flush();
    reading++;
    Serial.printf("  [%4d]  ", reading);
    for (int i = 0; i < 3; i++) {
      int   raw = readRaw(SENSOR_PINS[i]);
      float pct = rawToMoisture(raw, dryVal[i], wetVal[i]);
      Serial.printf("S%d: raw=%4d  %5.1f%%    ", i+1, raw, pct);
    }
    Serial.println();
    // Check for stop command
    unsigned long t = millis();
    while (millis() - t < 2000) {
      if (Serial.available()) {
        while (Serial.available()) Serial.read(); // flush
        Serial.println(F("\n  Stopped. Type H for menu."));
        return;
      }
    }
  }
}

// ════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);

  // Quick self-test on boot
  separator('*');
  Serial.println(F("*   AgroSense Sensor Calibration Tool      *"));
  Serial.println(F("*   Open Serial Monitor at 115200 baud     *"));
  separator('*');
  Serial.println();
  Serial.println(F("  Running power-on self test..."));

  bool allOk = true;
  for (int i = 0; i < 3; i++) {
    int raw = readRaw(SENSOR_PINS[i]);
    Serial.printf("  %-22s  GPIO %2d  raw = %4d  →  ",
                  SENSOR_NAMES[i], SENSOR_PINS[i], raw);
    if (raw < 100 || raw > 4000) {
      Serial.println(F("⚠ CHECK WIRING (value out of expected range)"));
      allOk = false;
    } else if (raw > 3800) {
      Serial.println(F("→ Very dry / sensor in air"));
    } else if (raw < 1600) {
      Serial.println(F("→ Very wet"));
    } else {
      Serial.println(F("→ OK"));
    }
  }

  if (allOk) {
    Serial.println(F("\n  ✓ All sensors responding"));
  } else {
    Serial.println(F("\n  ⚠ Check wiring on flagged sensors"));
    Serial.println(F("    VCC → 3.3V,  GND → GND,  AO → GPIO pin"));
  }

  printMenu();
}

// ════════════════════════════════════════════════
//  LOOP — process Serial commands
// ════════════════════════════════════════════════
void loop() {
  if (Serial.available() == 0) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  input.toUpperCase();

  if (input.length() == 0) return;

  char cmd = input.charAt(0);

  switch (cmd) {
    case 'R': readAllRaw();       break;
    case 'M': readAllMoisture();  break;
    case 'D': calibrateDry();     break;
    case 'W': calibrateWet();     break;
    case 'T': continuousTest();   break;
    case 'C': showCalibration();  break;
    case 'P': printCodeSnippet(); break;
    case '1': testSingleSensor(0); break;
    case '2': testSingleSensor(1); break;
    case '3': testSingleSensor(2); break;
    case 'H': printMenu();        break;
    default:
      Serial.printf("  Unknown command: '%c'  —  type H for menu\n", cmd);
      break;
  }
}
