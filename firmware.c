/*
  ReCell — Automatic mHPPC Pulse Test Firmware
  Runs continuously: rests, applies one pulse across the assembled 4S pack,
  computes real per-cell features from sensor data during that pulse, and
  sends each cell's feature vector to the QRB2210 for AI inference via Bridge.

  Since all 4 cells are in series, one pulse produces readings for all 4
  cells simultaneously — same shared current (INA260), individually
  differing voltage response per cell (from the divider taps).

  I2C: D20 (SDA) / D21 (SCL) — confirmed via bus scan.
*/

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_INA260.h>
#include <math.h>
#include "Arduino_RouterBridge.h"

Adafruit_ADS1115 adsVoltage;   // 0x48 — voltage taps
Adafruit_ADS1115 adsThermal;   // 0x49 — NTC channels
Adafruit_INA260 ina260;        // 0x40 — current + bus voltage

bool adsVoltageFound = false;
bool adsThermalFound = false;
bool inaFound = false;

const int GATE_PIN = 5;  // TODO: confirm actual wired pin

const float DIVIDER_RATIO[4] = {
  (13000.0 + 10000.0) / 10000.0,
  (36000.0 + 10000.0) / 10000.0,
  (60000.0 + 10000.0) / 10000.0,
  (82000.0 + 10000.0) / 10000.0,
};

const float NTC_R_FIXED = 10000.0;
const float NTC_R0 = 10000.0;
const float NTC_BETA = 3950.0;      // confirm against actual NTC datasheet
const float NTC_T0_KELVIN = 298.15;
const float SUPPLY_VOLTAGE = 3.3;

const unsigned long PULSE_DURATION_MS = 10000;   // ~10s pulse, per HPPC standard
const unsigned long REST_INTERVAL_MS  = 180000;  // 3 min rest between tests
const int SAMPLES_DURING_PULSE = 10;             // for averaging temp / capturing v_end

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Bridge.begin();

  pinMode(GATE_PIN, OUTPUT);
  digitalWrite(GATE_PIN, LOW);

  adsVoltage.setGain(GAIN_TWO);
  adsThermal.setGain(GAIN_TWO);

  adsVoltageFound = adsVoltage.begin(0x48);
  adsThermalFound = adsThermal.begin(0x49);
  inaFound = ina260.begin(0x40);

  Serial.println("=== ReCell Automatic Pulse Test Firmware ===");
  Serial.print("ADS1115 voltage: "); Serial.println(adsVoltageFound ? "OK" : "MISSING");
  Serial.print("ADS1115 thermal: "); Serial.println(adsThermalFound ? "OK" : "MISSING");
  Serial.print("INA260: "); Serial.println(inaFound ? "OK" : "MISSING");
}

float voltageToTempC(float dividerVoltage) {
  if (dividerVoltage <= 0.0 || dividerVoltage >= SUPPLY_VOLTAGE) return NAN;
  float R_ntc = NTC_R_FIXED * (dividerVoltage / (SUPPLY_VOLTAGE - dividerVoltage));
  float tempKelvin = 1.0 / ((1.0 / NTC_T0_KELVIN) + (1.0 / NTC_BETA) * log(R_ntc / NTC_R0));
  return tempKelvin - 273.15;
}

// Reads all four cumulative taps and returns individual cell voltages.
void readCellVoltages(float cellVolts[4]) {
  float cumulative[4];
  for (int ch = 0; ch < 4; ch++) {
    int16_t raw = adsVoltage.readADC_SingleEnded(ch);
    float measured = adsVoltage.computeVolts(raw);
    cumulative[ch] = measured * DIVIDER_RATIO[ch];
  }
  cellVolts[0] = cumulative[0];
  cellVolts[1] = cumulative[1] - cumulative[0];
  cellVolts[2] = cumulative[2] - cumulative[1];
  cellVolts[3] = cumulative[3] - cumulative[2];
}

// Reads all four NTC channels, returns temperature in Celsius per cell.
void readCellTemps(float cellTemps[4]) {
  for (int ch = 0; ch < 4; ch++) {
    int16_t raw = adsThermal.readADC_SingleEnded(ch);
    float measured = adsThermal.computeVolts(raw);
    cellTemps[ch] = voltageToTempC(measured);
  }
}

void runPulseTestAndReport() {
  if (!adsVoltageFound || !adsThermalFound || !inaFound) {
    Serial.println("Skipping pulse test — one or more sensors not found.");
    return;
  }

  Serial.println("\n--- Starting pulse test ---");

  // 1. Rest reading — baseline voltage/temp before any load is applied.
  float vStart[4], tempSum[4] = {0, 0, 0, 0};
  readCellVoltages(vStart);

  // 2. Apply pulse.
  digitalWrite(GATE_PIN, HIGH);
  unsigned long pulseStart = millis();

  float vEnd[4];
  float currentSum = 0.0;
  int sampleCount = 0;

  unsigned long sampleInterval = PULSE_DURATION_MS / SAMPLES_DURING_PULSE;
  for (int i = 0; i < SAMPLES_DURING_PULSE; i++) {
    delay(sampleInterval);

    float tempsThisSample[4];
    readCellTemps(tempsThisSample);
    for (int c = 0; c < 4; c++) tempSum[c] += tempsThisSample[c];

    currentSum += ina260.readCurrent();  // mA
    sampleCount++;

    // Capture the last voltage reading during the pulse as v_end.
    readCellVoltages(vEnd);
  }

  // 3. End pulse.
  digitalWrite(GATE_PIN, LOW);
  unsigned long actualDurationMs = millis() - pulseStart;

  float avgCurrentA = (currentSum / sampleCount) / 1000.0;  // mA -> A
  float actualDurationS = actualDurationMs / 1000.0;

  // 4. Compute features per cell and send to QRB2210.
  for (int cell = 1; cell <= 4; cell++) {
    int idx = cell - 1;
    float v_start = vStart[idx];
    float v_end = vEnd[idx];
    float v_sag = v_start - v_end;
    float mean_temp = tempSum[idx] / sampleCount;

    // Internal resistance proxy: voltage sag / pulse current, guarding
    // against a divide-by-zero if current somehow read as zero.
    float resistance_proxy = (avgCurrentA > 0.0) ? (v_sag / avgCurrentA) : 0.0;

    Serial.print("Cell "); Serial.print(cell);
    Serial.print(": R="); Serial.print(resistance_proxy, 4);
    Serial.print(" v_start="); Serial.print(v_start, 3);
    Serial.print(" v_end="); Serial.print(v_end, 3);
    Serial.print(" v_sag="); Serial.print(v_sag, 3);
    Serial.print(" temp="); Serial.print(mean_temp, 1);
    Serial.print(" dur="); Serial.println(actualDurationS, 1);

    Bridge.call("run_inference_cell", cell,
                resistance_proxy, v_start, v_end, v_sag, mean_temp, actualDurationS);
  }

  Serial.println("--- Pulse test complete, results sent ---\n");
}

void loop() {
  runPulseTestAndReport();
  delay(REST_INTERVAL_MS);
}
