# AgroSense

Autonomous irrigation system for maize, built around an ESP32, capacitive soil moisture sensors, and a decision-tree model trained on soil moisture, texture, and crop growth stage. Telemetry streams to Firebase Realtime Database and is visualized in a live web dashboard.

## How it works

1. **ESP32 firmware** reads three capacitive soil moisture sensors (one per irrigation zone) every 2 seconds.
2. A **decision tree** (trained offline in Python, exported as a C header) decides `HOLD`, `IRRIGATE`, or `URGENT` for each zone based on moisture %, soil texture, and growth stage.
3. The firmware drives a relay per zone to switch pumps on/off automatically, and pushes state to **Firebase Realtime Database** every 5 seconds.
4. The **web dashboard** (`index.html`) subscribes to Firebase in real time, showing per-zone moisture, pump status, and ML-recommended actions, plus manual override controls and an emergency stop.

## Project structure

| Path | Description |
|---|---|
| [files/AgroSense/AgroSense.ino](files/AgroSense/AgroSense.ino) | Main ESP32 firmware — WiFi, sensor reads, relay control, Firebase sync |
| [files/AgroSense/decision_tree_rules.h](files/AgroSense/decision_tree_rules.h) | Auto-generated decision tree rules (irrigation logic) |
| [SensorCalibration/SensorCalibration.ino](SensorCalibration/SensorCalibration.ino) | Standalone sketch to calibrate dry/wet ADC values per sensor |
| [index.html](index.html) | Web dashboard (Firebase Realtime Database + Chart.js) |
| [Training_doc/](Training_doc) | Model training report, dataset, and evaluation charts |
| [circuit_diagram.svg](circuit_diagram.svg), [block_diagram.pptx](block_diagram.pptx) | Hardware wiring and system architecture diagrams |

## Hardware

- ESP32 dev board
- 3× capacitive soil moisture sensors → GPIO 34, 35, 32
- 4-channel relay module → GPIO 26, 27, 14 (one spare on GPIO 12)
- Submersible/solenoid pumps, one per irrigation zone

## Machine learning model

Trained on 6,400 samples of moisture %, soil texture, and growth stage, predicting one of three irrigation actions:

- **Test accuracy:** 88.4%
- **Cross-validation accuracy:** 82.4% ± 3.5%
- **Most important feature:** moisture % (80.7% importance)

Full report in [Training_doc/training_report.txt](Training_doc/training_report.txt).

## Getting started

1. Flash [SensorCalibration.ino](SensorCalibration/SensorCalibration.ino) to the ESP32 and follow the serial menu to calibrate dry/wet values for your sensors.
2. Copy the resulting `DRY_VALS`/`WET_VALS` into [AgroSense.ino](files/AgroSense/AgroSense.ino).
3. Copy [files/AgroSense/secrets.h.example](files/AgroSense/secrets.h.example) to `secrets.h` in the same folder and fill in your WiFi credentials and Firebase project details (`secrets.h` is gitignored and never committed).
4. Flash the firmware and open the Serial Monitor at 115200 baud to confirm it connects and starts pushing data.
5. Open `index.html` (or the deployed dashboard URL) to monitor and control the system.

## Status

Live deployment: 3 zones (Alpha/Beta/Gamma), maize crop, auto ML + manual control modes.
