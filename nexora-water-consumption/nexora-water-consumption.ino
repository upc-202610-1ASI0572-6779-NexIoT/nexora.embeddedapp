/**
 * @file sketch.ino
 * @brief Código para Wokwi Simulator - Monitoreo de consumo de agua y detección de fugas/anomalías.
 * Arquitectura basada estrictamente en el Modest-IoT Nano-Framework.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ModestIoT.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- CONFIGURACIÓN DE RED Y EDGE SERVICE ---
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
// Debe apuntar al túnel/edge activo (mismo edge que los demás dispositivos).
const char* EDGE_URL      = "https://better-clubs-march.loca.lt/api/v1/monitoring/telemetry";
const char* DEVICE_ID     = "water-safety-unit-apt-402";
const char* APARTMENT_ID  = "Apt-402";

void setup_wifi() {
    delay(10);
    Serial.print("\n[WiFi] Conectando a ");
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

// --- 1. CLASES DE SENSORES PERSONALIZADOS ---

/**
 * @class WaterFlowSensor
 * @brief Sensor de flujo de agua personalizado para el potenciómetro en el pin 34.
 * En esta simulación, el potenciómetro representa un caudalímetro.
 */
class WaterFlowSensor : public Sensor {
private:
    float lastFlowValue;
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            int rawAdc = analogRead(pin);

            // Mapeo del ADC de 12 bits del ESP32: 0-4095
            // Rango simulado del caudal: 0.0 a 30.0 litros por minuto
            float flowValue = (rawAdc / 4095.0f) * 30.0f;

            // Supresión de eventos: si el cambio es menor a 0.15 L/min, no propagamos evento
            if (isInitialized && (fabsf(flowValue - lastFlowValue) < 0.15f)) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }

            lastFlowValue = flowValue;
            isInitialized = true;

            // Emitimos evento con la lectura de datos, usando el pin como sourceId
            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    WaterFlowSensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler), isInitialized(false) {
        pinMode(pin, INPUT);

        int rawAdc = analogRead(pin);
        lastFlowValue = (rawAdc / 4095.0f) * 30.0f;
    }

    float getFlowLitersPerMinute() const {
        return lastFlowValue;
    }
};

/**
 * @class WaterSupplySensor
 * @brief Sensor de suministro de agua personalizado para el interruptor deslizante en el pin 12.
 */
class WaterSupplySensor : public Sensor {
private:
    bool lastSupplyState; // true = suministro activo, false = suministro inactivo
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            // Conexión usada:
            // Switch pin 1 a GND
            // Switch pin 2 a D12 con INPUT_PULLUP
            //
            // LOW  = suministro activo
            // HIGH = suministro inactivo
            bool currentSupplyState = (digitalRead(pin) == LOW);

            if (isInitialized && (currentSupplyState == lastSupplyState)) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }

            lastSupplyState = currentSupplyState;
            isInitialized = true;

            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    WaterSupplySensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler), isInitialized(false) {
        pinMode(pin, INPUT_PULLUP);
        lastSupplyState = (digitalRead(pin) == LOW);
    }

    bool isSupplyActive() const {
        return lastSupplyState;
    }
};

// --- 2. PAQUETE DE TELEMETRÍA PERSONALIZADO ---

/**
 * @class WaterTelemetry
 * @brief Paquete de telemetría que encapsula el estado del consumo de agua.
 */
class WaterTelemetry : public TelemetryPackage {
public:
    float flowLpm;
    float flowDeltaLpm;
    bool supplyActive;
    bool anomaly;

    WaterTelemetry(float flow, float delta, bool supply, bool anom)
        : flowLpm(flow), flowDeltaLpm(delta), supplyActive(supply), anomaly(anom) {}

    void serialize(JsonDocument& serializationDestination) const override {
        // Campos que consume el Edge Service (contrato unificado).
        serializationDestination["device_id"] = DEVICE_ID;
        serializationDestination["apartment_id"] = APARTMENT_ID;
        serializationDestination["water_flow"] = flowLpm;   // L/min
        serializationDestination["voltaje_ok"] = true;       // el medidor de agua no evalúa la red

        // Campos descriptivos adicionales del dominio de agua.
        serializationDestination["flujo_L_min"] = flowLpm;
        serializationDestination["cambio_flujo_L_min"] = flowDeltaLpm;
        serializationDestination["suministro_activo"] = supplyActive;
        serializationDestination["anomalia_fuga"] = anomaly;
        serializationDestination["timestamp"] = millis();
    }
};

// --- 3. DISPOSITIVO MEDIADOR DE MONITOREO ---

class WaterMonitorDevice : public Device {
private:
    // Sensores
    WaterFlowSensor flowSensor;
    WaterSupplySensor supplySensor;

    // Actuadores
    Led ledFlujoNormal;
    Led ledFuga;

    // Estado interno
    float waterFlowLpm;
    float previousWaterFlowLpm;
    bool waterSupplyActive;

    // Umbrales de detección
    const float MAX_SAFE_FLOW_LPM = 20.0f;
    const float ABRUPT_CHANGE_THRESHOLD_LPM = 8.0f;

    /**
     * @brief Evalúa el estado de flujo de agua para detectar fugas o consumos anómalos.
     */
    void evaluateWaterState() {
        float flowDelta = fabsf(waterFlowLpm - previousWaterFlowLpm);

        bool hasAnomaly = waterSupplyActive &&
                          (
                              waterFlowLpm > MAX_SAFE_FLOW_LPM ||
                              flowDelta >= ABRUPT_CHANGE_THRESHOLD_LPM
                          );

        // 1. Suministro de agua inactivo
        if (!waterSupplyActive) {
            ledFlujoNormal.handle(Led::TURN_OFF_COMMAND);
            ledFuga.handle(Led::TURN_OFF_COMMAND);

            Serial.println(F("[DISPOSITIVO] Estado: SIN SUMINISTRO | Agua inactiva. Ambos LEDs apagados."));
        }

        // 2. Anomalía por flujo excesivo o cambio abrupto
        else if (hasAnomaly) {
            ledFlujoNormal.handle(Led::TURN_OFF_COMMAND);
            ledFuga.handle(Led::TURN_ON_COMMAND);

            Serial.println(F("\n[DISPOSITIVO] --- ALERTA DE CONSUMO ANÓMALO DE AGUA ---"));

            if (waterFlowLpm > MAX_SAFE_FLOW_LPM) {
                Serial.printf(
                    "  [!] DETALLE: Flujo excesivo detectado: %.2f L/min. Límite seguro: %.2f L/min.\n",
                    waterFlowLpm,
                    MAX_SAFE_FLOW_LPM
                );
            }

            if (flowDelta >= ABRUPT_CHANGE_THRESHOLD_LPM) {
                Serial.printf(
                    "  [!] DETALLE: Cambio abrupto de flujo detectado: %.2f L/min. Umbral: %.2f L/min.\n",
                    flowDelta,
                    ABRUPT_CHANGE_THRESHOLD_LPM
                );
            }

            Serial.println(F("  [!] Posible fuga o consumo inesperado."));
            Serial.println(F("-------------------------------------------------------\n"));
        }

        // 3. Operación normal
        else {
            ledFlujoNormal.handle(Led::TURN_ON_COMMAND);
            ledFuga.handle(Led::TURN_OFF_COMMAND);

            Serial.printf(
                "[DISPOSITIVO] Estado: NORMAL | Flujo: %.2f L/min | Cambio: %.2f L/min | Suministro: ACTIVO\n",
                waterFlowLpm,
                flowDelta
            );
        }

        // Telemetría estructurada
        WaterTelemetry* telemetryPacket = new WaterTelemetry(
            waterFlowLpm,
            flowDelta,
            waterSupplyActive,
            hasAnomaly
        );

        if (!enqueueTelemetryPayload(reinterpret_cast<TelemetryPackage**>(&telemetryPacket))) {
            delete telemetryPacket;
        }
    }

protected:
    /**
     * @brief Procesa la telemetría en segundo plano simulando envío a una pasarela IoT.
     */
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) override {
        if (rawQueueItemPayload == nullptr) return;

        const WaterTelemetry* telemetry = static_cast<const WaterTelemetry*>(rawQueueItemPayload);

        JsonDocument doc;
        telemetry->serialize(doc);

        String jsonString;
        serializeJson(doc, jsonString);

        Serial.printf("[TELEMETRÍA] Dispatcher asíncrono enviando: %s\n", jsonString.c_str());

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[ERROR] WiFi no conectado. Registro conservado localmente.");
            return;
        }

        // Plain HTTP transport: the local edge (http://host.wokwi.internal:5050)
        // is not TLS, so a secure client would cause a "Bad request version" error.
        WiFiClient client;
        client.setInsecure();

        HTTPClient http;
        http.begin(client, EDGE_URL);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", "test-api-key-123");
        http.addHeader("Bypass-Tunnel-Reminder", "true");

        int httpResponseCode = http.POST(jsonString);
        Serial.printf("[EDGE] Código HTTP: %d\n", httpResponseCode);
        http.end();
    }

public:
    WaterMonitorDevice(int flowPin, int supplyPin, int normalLedPin, int anomalyLedPin)
        : Device(1000, 0),
          flowSensor(flowPin, this),
          supplySensor(supplyPin, this),
          ledFlujoNormal(normalLedPin, false, this),
          ledFuga(anomalyLedPin, false, this)
    {
        waterFlowLpm = flowSensor.getFlowLitersPerMinute();
        previousWaterFlowLpm = waterFlowLpm;
        waterSupplyActive = supplySensor.isSupplyActive();

        // Motor asíncrono basado en FreeRTOS
        initializeAsynchronousEngine(8);

        // Registro de sensores al planificador
        appendSensorToScheduler(&flowSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        appendSensorToScheduler(&supplySensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);

        // Evaluación inicial
        evaluateWaterState();
    }

    /**
     * @brief Manejo reactivo de eventos provenientes de sensores.
     */
    void on(Event event) override {
        if (event.identifier == Sensor::DATA_READ_EVENT_IDENTIFIER) {
            if (event.sourceId == 34) {
                previousWaterFlowLpm = waterFlowLpm;
                waterFlowLpm = flowSensor.getFlowLitersPerMinute();
                evaluateWaterState();
            }
            else if (event.sourceId == 12) {
                waterSupplyActive = supplySensor.isSupplyActive();
                evaluateWaterState();
            }
        }
    }
};

// --- 4. CONFIGURACIÓN PRINCIPAL ---

static const int PIN_SENSOR_FLUJO_AGUA       = 34; // Potenciómetro SIG
static const int PIN_SENSOR_SUMINISTRO_AGUA  = 12; // Switch terminal 2
static const int PIN_LED_FLUJO_NORMAL        = 25; // LED verde
static const int PIN_LED_FUGA                = 26; // LED rojo

static WaterMonitorDevice* waterMonitor = nullptr;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println(F("[Sistema] Iniciando monitor IoT de consumo de agua y detección de fugas..."));

    setup_wifi();

    waterMonitor = new WaterMonitorDevice(
        PIN_SENSOR_FLUJO_AGUA,
        PIN_SENSOR_SUMINISTRO_AGUA,
        PIN_LED_FLUJO_NORMAL,
        PIN_LED_FUGA
    );

    Serial.println(F("[Sistema] Inicialización completada. Monitoreo activo con Modest-IoT y FreeRTOS."));
}

void loop() {
    // El procesamiento queda a cargo de las tareas asíncronas del framework
    vTaskDelay(pdMS_TO_TICKS(60000));
}