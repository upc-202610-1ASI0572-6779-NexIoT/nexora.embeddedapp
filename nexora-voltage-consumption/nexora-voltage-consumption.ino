/**
 * @file sketch.ino
 * @brief Código para Wokwi Simulator - Monitoreo de consumo eléctrico y detección de anomalías.
 * Arquitectura basada estrictamente en el Modest-IoT Nano-Framework.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ModestIoT.h>

// --- 1. CLASES DE SENSORES PERSONALIZADOS ---

/**
 * @class CurrentSensor
 * @brief Sensor de corriente personalizado para el Potenciómetro en el pin 34.
 */
class CurrentSensor : public Sensor {
private:
    float lastCurrentValue;
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            int rawAdc = analogRead(pin);
            // Mapeamos el valor analógico de 12 bits (0-4095) a un rango simulado de 0.0 a 30.0 Amperios
            float currentValue = (rawAdc / 4095.0f) * 30.0f;
            
            // Supresión de eventos: si el cambio es menor a 0.2A, no propagamos para ahorrar recursos
            if (isInitialized && (abs(currentValue - lastCurrentValue) < 0.2f)) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }
            lastCurrentValue = currentValue;
            isInitialized = true;
            
            // Emitimos el evento con la lectura de datos, indicando el pin como sourceId
            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    CurrentSensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler), isInitialized(false) {
        pinMode(pin, INPUT);
        // Calibración inicial: Capturar el valor real del ADC desde el arranque
        int rawAdc = analogRead(pin);
        lastCurrentValue = (rawAdc / 4095.0f) * 30.0f;
    }

    float getCurrentAmps() const {
        return lastCurrentValue;
    }
};

/**
 * @class VoltageSensor
 * @brief Sensor de voltaje personalizado para el interruptor deslizante en el pin 12.
 */
class VoltageSensor : public Sensor {
private:
    bool lastVoltageState; // true = Voltaje Normal, false = Falla de Voltaje
    bool isInitialized;
    static const int SENSOR_ERROR_SUPPRESSION_IDENTIFIER = -1;

protected:
    void processEvent(Event& event) override {
        if (event.identifier == MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            // Con la conexión en Wokwi (Switch pin 1 a GND, Switch pin 2 a D12):
            // - LOW (conectado a GND) representa estado normal (Voltaje OK)
            // - HIGH (flotante con INPUT_PULLUP) representa caída o falla de voltaje
            bool currentVoltageState = (digitalRead(pin) == LOW);
            
            if (isInitialized && (currentVoltageState == lastVoltageState)) {
                event.identifier = SENSOR_ERROR_SUPPRESSION_IDENTIFIER;
                return;
            }
            lastVoltageState = currentVoltageState;
            isInitialized = true;
            
            // Emitimos el evento con la lectura de datos, indicando el pin como sourceId
            event = Event(Sensor::DATA_READ_EVENT_IDENTIFIER, pin);
        }
    }

public:
    VoltageSensor(int signalGpioPin, EventHandler* parentHandler = nullptr)
        : Sensor(signalGpioPin, parentHandler), isInitialized(false) {
        pinMode(pin, INPUT_PULLUP);
        // Sincronización inicial: Capturar la posición real del switch en el milisegundo cero
        lastVoltageState = (digitalRead(pin) == LOW);
    }

    bool isVoltageOk() const {
        return lastVoltageState;
    }
};

// --- 2. PAQUETE DE TELEMETRÍA PERSONALIZADO ---

/**
 * @class PowerTelemetry
 * @brief Paquete de telemetría que encapsula el estado eléctrico para el envío a la pasarela (Gateway).
 */
class PowerTelemetry : public TelemetryPackage {
public:
    float current;
    bool voltageOk;
    bool anomaly;

    PowerTelemetry(float cur, bool volt, bool anom)
        : current(cur), voltageOk(volt), anomaly(anom) {}

    void serialize(JsonDocument& serializationDestination) const override {
        serializationDestination["corriente_A"] = current;
        serializationDestination["voltaje_ok"] = voltageOk;
        serializationDestination["anomalia"] = anomaly;
        serializationDestination["timestamp"] = millis();
    }
};

// --- 3. DISPOSITIVO MEDIADOR DE MONITOREO ---

class ElectricalMonitorDevice : public Device {
private:
    // Sensores (Entradas)
    CurrentSensor currentSensor;
    VoltageSensor voltageSensor;

    // Actuadores (Salidas)
    Led ledCarga;
    Led ledAveria;

    // Estado interno del dispositivo
    float currentAmps;
    bool voltageOk;

    // Umbral de seguridad para sobrecarga
    const float MAX_SAFE_CURRENT = 20.0f; // Amperios

    /**
     * @brief Evalúa el estado de consumo y voltaje para detectar anomalías y enviar comandos a los actuadores.
     */
    void evaluatePowerState() {
    // 1. CONDICIÓN DE CORTE DE ENERGÍA / STANDBY
    if (!voltageOk) {
        // Si no hay voltaje en la red, la luminaria está apagada por completo.
        // No hay anomalía de hardware, solo ausencia de suministro.
        ledCarga.handle(Led::TURN_OFF_COMMAND);
        ledAveria.handle(Led::TURN_OFF_COMMAND);

        Serial.println(F("[DISPOSITIVO] Estado: STANDBY | Red eléctrica principal inactiva. Ambos LEDs apagados."));
    } 
    // 2. CONDICIÓN DE ANOMALÍA POR SOBRECARGA
    else if (currentAmps > MAX_SAFE_CURRENT) {
        // Hay voltaje, pero el consumo es peligrosamente alto.
        ledCarga.handle(Led::TURN_OFF_COMMAND); // Apagar carga para proteger
        ledAveria.handle(Led::TURN_ON_COMMAND);  // Encender alerta roja

        Serial.println(F("\n[DISPOSITIVO] --- ALERTA DE ANOMALÍA ELÉCTRICA ---"));
        Serial.printf(F("  [!] DETALLE: Exceso de corriente (Sobrecarga): %.2f A (Límite seguro: %.2f A).\n"), 
                      currentAmps, MAX_SAFE_CURRENT);
        Serial.println(F("---------------------------------------------------\n"));
    } 
    // 3. CONDICIÓN OPERATIVA NORMAL
    else {
        // Red energizada y consumo dentro de los parámetros de diseño.
        ledCarga.handle(Led::TURN_ON_COMMAND);  // Encender luz gestionada (Verde)
        ledAveria.handle(Led::TURN_OFF_COMMAND); // Apagar alerta roja

        Serial.printf("[DISPOSITIVO] Estado: NORMAL | Consumo: %.2f A | Voltaje: OK\n", currentAmps);
    }

    // Determinar si el estado actual se considera anomalía para el reporte de telemetría
    bool hasAnomaly = voltageOk && (currentAmps > MAX_SAFE_CURRENT);

    // Enviar el paquete estructurado al despachador asíncrono
    PowerTelemetry* telemetryPacket = new PowerTelemetry(currentAmps, voltageOk, hasAnomaly);
    if (!enqueueTelemetryPayload(reinterpret_cast<TelemetryPackage**>(&telemetryPacket))) {
        delete telemetryPacket;
    }
}

protected:
    /**
     * @brief Subclass hook: Procesa la telemetría en segundo plano (simulando envío a pasarela).
     */
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) override {
        const PowerTelemetry* telemetry = static_cast<const PowerTelemetry*>(rawQueueItemPayload);
        
        // Serialización a JSON para depuración por puerto Serie
        JsonDocument doc;
        telemetry->serialize(doc);
        String jsonString;
        serializeJson(doc, jsonString);
        Serial.printf("[TELEMETRÍA] Dispatcher asíncrono de red enviando: %s\n", jsonString.c_str());
    }

public:
    ElectricalMonitorDevice(int currentPin, int voltagePin, int ledCargaPin, int ledAveriaPin)
        : Device(1000, 0), // Frecuencia de muestreo de hardware: cada 1000ms
          currentSensor(currentPin, this),
          voltageSensor(voltagePin, this),
          ledCarga(ledCargaPin, false, this),
          ledAveria(ledAveriaPin, false, this)
    {
        // Sincronizar estados locales previos con la realidad de los sensores recién instanciados
        currentAmps = currentSensor.getCurrentAmps();
        voltageOk = voltageSensor.isVoltageOk();

        // 1. Inicializa el motor asíncrono basado en tareas FreeRTOS (Buffer depth: 8)
        initializeAsynchronousEngine(8);

        // 2. Registra los sensores al planificador de hardware para lecturas periódicas
        appendSensorToScheduler(&currentSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        appendSensorToScheduler(&voltageSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);

        // 3. Evaluación de estado inicial con datos reales limpios
        evaluatePowerState();
    }

    /**
     * @brief Manejo reactivo de eventos provenientes de los sensores.
     */
    void on(Event event) override {
        if (event.identifier == Sensor::DATA_READ_EVENT_IDENTIFIER) {
            if (event.sourceId == 34) { // Lectura del sensor de corriente (Pin 34)
                currentAmps = currentSensor.getCurrentAmps();
                evaluatePowerState();
            } 
            else if (event.sourceId == 12) { // Lectura del interruptor de voltaje (Pin 12)
                voltageOk = voltageSensor.isVoltageOk();
                evaluatePowerState();
            }
        }
    }
};

// --- 4. CONFIGURACIÓN Y MÁQUINA DE ESTADOS PRINCIPAL ---

static const int PIN_SENSOR_CORRIENTE = 34; // Potenciómetro (SIG)
static const int PIN_SENSOR_VOLTAJE   = 12; // Interruptor deslizante (Term. 2)
static const int PIN_LED_CARGA        = 25; // LED Verde (Luz Gestionada)
static const int PIN_LED_AVERIA       = 26; // LED Rojo (Alerta Avería)

static ElectricalMonitorDevice* systemMonitor = nullptr;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[Sistema] Iniciando monitor de consumo eléctrico y detección de anomalías...");

    // Instanciamos el dispositivo principal y configuramos los pines correspondientes
    systemMonitor = new ElectricalMonitorDevice(
        PIN_SENSOR_CORRIENTE,
        PIN_SENSOR_VOLTAJE,
        PIN_LED_CARGA,
        PIN_LED_AVERIA
    );

    Serial.println("[Sistema] Inicialización completada. Corriendo asíncronamente en FreeRTOS.");
}

void loop() {
    // Cede los recursos del núcleo 1 permanentemente a las tareas asíncronas de FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(60000));
}