/**
 * @file sketch.ino
 * @brief Código para Wokwi Simulator - Monitoreo de consumo eléctrico y detección de anomalías.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ModestIoT.h"
#include <WiFiClientSecure.h>

// --- CONFIGURACIÓN DE PINES ---
static const int PIN_SENSOR_CORRIENTE = 34;
static const int PIN_SENSOR_VOLTAJE   = 14;
static const int PIN_LED_CARGA        = 25;
static const int PIN_LED_AVERIA       = 26;

// --- CONFIGURACIÓN DE RED Y EDGE SERVICE ---
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* EDGE_URL = "https://better-clubs-march.loca.lt/api/v1/monitoring/telemetry";

void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("[WiFi] Conectando a ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[WiFi] Conexión establecida.");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
}

// --- SENSOR DE CORRIENTE ---
class CurrentSensor : public Sensor {
private:
    float lastCurrentValue;
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            int rawAdc = analogRead(pin);
            float currentValue = (rawAdc / 4095.0f) * 30.0f;

            if (isInitialized && abs(currentValue - lastCurrentValue) < 0.2f) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }

            lastCurrentValue = currentValue;
            isInitialized = true;

            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    CurrentSensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler),
          lastCurrentValue(0.0f),
          isInitialized(false) {
        pinMode(pin, INPUT);

        int rawAdc = analogRead(pin);
        lastCurrentValue = (rawAdc / 4095.0f) * 30.0f;
    }

    float getCurrentAmps() const {
        return lastCurrentValue;
    }
};

// --- SENSOR DE VOLTAJE ---
class VoltageSensor : public Sensor {
private:
    bool lastVoltageState;
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            bool currentVoltageState = digitalRead(pin) == LOW;

            if (isInitialized && currentVoltageState == lastVoltageState) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }

            lastVoltageState = currentVoltageState;
            isInitialized = true;

            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    VoltageSensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler),
          lastVoltageState(false),
          isInitialized(false) {
        pinMode(pin, INPUT_PULLUP);
        lastVoltageState = digitalRead(pin) == LOW;
    }

    bool isVoltageOk() const {
        return lastVoltageState;
    }
};

// --- PAQUETE DE TELEMETRÍA ---
class PowerTelemetry : public TelemetryPackage {
public:
    float current;
    bool voltageOk;
    bool anomaly;

    PowerTelemetry(float cur, bool volt, bool anom)
        : current(cur), voltageOk(volt), anomaly(anom) {}

    void serialize(JsonDocument& serializationDestination) const override {
        serializationDestination["device_id"] = "voltage-safety-unit-apt-402";
        serializationDestination["apartment_id"] = "Apt-402";
        serializationDestination["electricity_kwh"] = current;

        serializationDestination["corriente_A"] = current;
        serializationDestination["voltaje_ok"] = voltageOk;
        serializationDestination["anomalia"] = anomaly;
        serializationDestination["timestamp"] = millis();
    }
};

// --- DISPOSITIVO MEDIADOR ---
class ElectricalMonitorDevice : public Device {
private:
    CurrentSensor currentSensor;
    VoltageSensor voltageSensor;
    Led ledCarga;
    Led ledAveria;

    float currentAmps;
    bool voltageOk;

    const float MAX_SAFE_CURRENT = 20.0f;

    void evaluatePowerState() {
        Serial.printf(
            "[DEBUG] Voltaje=%d | Corriente=%.2f A\n",
            voltageOk,
            currentAmps
        );

        if (!voltageOk) {
            ledCarga.handle(Led::TURN_OFF_COMMAND);
            ledAveria.handle(Led::TURN_OFF_COMMAND);

            Serial.println("[DISPOSITIVO] Estado: STANDBY | Red inactiva.");
        }
        else if (currentAmps > MAX_SAFE_CURRENT) {
            ledCarga.handle(Led::TURN_OFF_COMMAND);
            ledAveria.handle(Led::TURN_ON_COMMAND);

            Serial.printf(
                "[DISPOSITIVO] ALERTA: Sobrecarga de %.2f A\n",
                currentAmps
            );
        }
        else {
            ledCarga.handle(Led::TURN_ON_COMMAND);
            ledAveria.handle(Led::TURN_OFF_COMMAND);

            Serial.printf(
                "[DISPOSITIVO] Estado: NORMAL | Consumo: %.2f A\n",
                currentAmps
            );
        }

        bool hasAnomaly = voltageOk && currentAmps > MAX_SAFE_CURRENT;

        TelemetryPackage* telemetryPacket = new PowerTelemetry(
            currentAmps,
            voltageOk,
            hasAnomaly
        );

        if (!enqueueTelemetryPayload(&telemetryPacket)) {
            delete telemetryPacket;
        }
    }

protected:
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) override {
      Serial.println("[DEBUG] Intentando despachar telemetría...");

      if (rawQueueItemPayload == nullptr) {
          Serial.println("[ERROR] Payload nulo.");
          return;
      }

      if (WiFi.status() != WL_CONNECTED) {
          Serial.println("[ERROR] WiFi no conectado.");
          return;
      }

      JsonDocument doc;
      rawQueueItemPayload->serialize(doc);

      String jsonString;
      serializeJson(doc, jsonString);

      Serial.printf("[TELEMETRÍA] Despachando: %s\n", jsonString.c_str());

      WiFiClientSecure client;
      client.setInsecure();

      HTTPClient http;
      http.begin(client, EDGE_URL);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-API-Key", "test-api-key-123");
      http.addHeader("Bypass-Tunnel-Reminder", "true");

      int httpResponseCode = http.POST(jsonString);

      Serial.printf("[EDGE] Código HTTP: %d\n", httpResponseCode);

      String responseBody = http.getString();
      Serial.printf("[EDGE] Body: %s\n", responseBody.c_str());

      http.end();
  }

public:
    ElectricalMonitorDevice(
        int currentPin,
        int voltagePin,
        int ledCargaPin,
        int ledAveriaPin
    )
        : Device(1000, 0),
          currentSensor(currentPin, this),
          voltageSensor(voltagePin, this),
          ledCarga(ledCargaPin, false, this),
          ledAveria(ledAveriaPin, false, this),
          currentAmps(0.0f),
          voltageOk(true) {

        currentAmps = currentSensor.getCurrentAmps();
        voltageOk = voltageSensor.isVoltageOk();

        initializeAsynchronousEngine(8);

        appendSensorToScheduler(
            &currentSensor,
            Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER
        );

        appendSensorToScheduler(
            &voltageSensor,
            Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER
        );

        evaluatePowerState();
    }

    void on(Event event) override {
        if (event.identifier == Sensor::DATA_READ_EVENT_IDENTIFIER) {
            if (event.sourceId == PIN_SENSOR_CORRIENTE) {
                currentAmps = currentSensor.getCurrentAmps();
                evaluatePowerState();
            }
            else if (event.sourceId == PIN_SENSOR_VOLTAJE) {
                voltageOk = voltageSensor.isVoltageOk();
                evaluatePowerState();
            }
        }
    }
};

static ElectricalMonitorDevice* systemMonitor = nullptr;

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("==========================================");
    Serial.println("[Sistema] Monitor Serie Iniciado Correctamente");
    Serial.println("==========================================");

    setup_wifi();

    systemMonitor = new ElectricalMonitorDevice(
        PIN_SENSOR_CORRIENTE,
        PIN_SENSOR_VOLTAJE,
        PIN_LED_CARGA,
        PIN_LED_AVERIA
    );

    Serial.println("[Sistema] Setup finalizado con éxito.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(60000));
}