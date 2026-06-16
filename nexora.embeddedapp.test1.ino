/**
 * @file NexoraSafetyVesselUnified.ino
 * @brief Unified implementation of the Nexora Safety Vessel system using the Modest-IoT framework.
 *
 * This single-file implementation brings together the event-driven Modest-IoT Nano-Framework
 * asynchronous architecture (matching the professor's scheduling and telemetry engine) to monitor
 * gas levels, control a servo-actuated gas valve, actuate warning alerts, and transmit telemetry
 * to an edge server via HTTP requests.
 * 
 * @date 2026-06-13
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ModestIoT.h" // Umbrella header for framework components

// ============================================================================
// --- 1. APPLICATION CONSTANTS ---
// ============================================================================

// Unique IDs for Events (Sensors -> Device)
#define EVENT_GAS_READ_ID         10

// Unique IDs for Commands (Device -> Actuators / Network)
#define CMD_CLOSE_VALVE_ID        20
#define CMD_OPEN_VALVE_ID         21
#define CMD_ALERT_ON_ID           30  // Command to turn on LED and Buzzer
#define CMD_ALERT_OFF_ID          31  // Command to turn off LED and Buzzer

// ============================================================================
// --- 2. CONCRETE SENSOR & ACTUATOR IMPLEMENTATIONS ---
// ============================================================================

/**
 * @brief Analog reader sensor wrapper integrated with the Modest-IoT scheduler.
 */
class NexoraAnalogReader : public Sensor {
private:
    int pin;
    float currentMappedValue;
    EventHandler* handler;

public:
    NexoraAnalogReader(int pin, EventHandler* eventHandler = nullptr)
        : Sensor(pin, eventHandler), pin(pin), currentMappedValue(0.0), handler(eventHandler) {
        pinMode(pin, INPUT);
    }

    /**
     * @brief Responds to the scheduler's request to measure data.
     */
    void processEvent(Event& event) override {
        if (event.identifier == Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            int raw = analogRead(pin);
            // Map the raw ESP32 reading (0 to 4095) to 0-600 PPM
            currentMappedValue = map(raw, 0, 4095, 0, 600);
            
            // Notify the device mediator that data read is complete
            if (handler != nullptr) {
                Event readEvent(Sensor::DATA_READ_EVENT_IDENTIFIER);
                handler->on(readEvent);
            }
        }
    }

    float getMappedValue() const {
        return currentMappedValue;
    }
};

/**
 * @brief LED warning actuator.
 */
class NexoraLed : public Actuator {
private:
    bool state;

public:
    NexoraLed(int pin, bool initialState = false, CommandHandler* commandHandler = nullptr)
        : Actuator(pin, commandHandler), state(initialState) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, state);
    }

    void executeCommand(Command command) override {
        if (command.identifier == CMD_ALERT_ON_ID) {
            state = true;
            digitalWrite(pin, state);
            Serial.println("[LED ACTUATOR] -> ON (HIGH).");
        } else if (command.identifier == CMD_ALERT_OFF_ID) {
            state = false;
            digitalWrite(pin, state);
            Serial.println("[LED ACTUATOR] -> OFF (LOW).");
        }
    }

    bool getState() const { return state; }
};

/**
 * @brief Buzzer warning actuator.
 */
class NexoraBuzzer : public Actuator {
private:
    bool state;

public:
    NexoraBuzzer(int pin, CommandHandler* commandHandler = nullptr)
        : Actuator(pin, commandHandler), state(false) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }

    void executeCommand(Command command) override {
        if (command.identifier == CMD_ALERT_ON_ID) {
            state = true;
            tone(pin, 1000); // Emits a 1000 Hz tone
            Serial.println("[BUZZER ACTUATOR] -> SOUNDING (1000 Hz).");
        } else if (command.identifier == CMD_ALERT_OFF_ID) {
            state = false;
            noTone(pin);
            digitalWrite(pin, LOW);
            Serial.println("[BUZZER ACTUATOR] -> SILENCED.");
        }
    }

    bool getState() const { return state; }
};

/**
 * @brief Gas valve actuator using PWM servo control.
 */
class NexoraGasValve : public Actuator {
private:
    bool isClosed;
    void moverServo(int grados) {
        int duty = map(grados, 0, 180, 26, 123);
        ledcWrite(pin, duty);
    }

public:
    NexoraGasValve(int pin, CommandHandler* commandHandler = nullptr)
        : Actuator(pin, commandHandler), isClosed(false) {
        ledcAttach(pin, 50, 10); // Native PWM setup: 50Hz, 10-bit resolution
        moverServo(90);          // Initially open
    }

    void executeCommand(Command command) override {
        if (command.identifier == CMD_CLOSE_VALVE_ID) {
            isClosed = true;
            moverServo(0); // Cut gas flow
            Serial.println("[VALVULA ACTUATOR] Leak detected: Servo rotated to 0 degrees.");
        } else if (command.identifier == CMD_OPEN_VALVE_ID) {
            isClosed = false;
            moverServo(90); // Open secure gas flow
            Serial.println("[VALVULA ACTUATOR] Safe state: Servo rotated to 90 degrees.");
        }
    }

    bool getStatus() const { return isClosed; }
};

// ============================================================================
// --- 3. CONCRETE APPLICATION TELEMETRY SCHEMA ---
// ============================================================================

/**
 * @brief Telemetry package for Nexora Safety Vessel data.
 */
class NexoraTelemetryPackage : public TelemetryPackage {
private:
    String deviceId;
    float gasPpm;
    bool smokeDetected;

public:
    NexoraTelemetryPackage(const String& id, float ppm, bool smoke)
        : deviceId(id), gasPpm(ppm), smokeDetected(smoke) {}

    virtual ~NexoraTelemetryPackage() override = default;

    void serialize(JsonDocument& serializationDestination) const override {
        serializationDestination["device_id"] = this->deviceId;
        serializationDestination["apartment_id"] = "Apt-402";
        serializationDestination["gas_ppm"] = this->gasPpm;
        serializationDestination["smoke_detected"] = this->smokeDetected;
        serializationDestination["motion_detected"] = false;
        serializationDestination["door_open"] = false;
    }
};

// ============================================================================
// --- 4. APPLICATION MEDIATOR IMPLEMENTATION ---
// ============================================================================

/**
 * @brief Orchestrates sensors and actuators for gas leakage detection and mitigation.
 */
class NexoraDevice : public Device {
private:
    NexoraAnalogReader gasSensor;
    NexoraGasValve controlValve;
    NexoraLed alertLed;
    NexoraBuzzer alertBuzzer;
    
    float currentGasPPM;
    const char* edgeUrl;
    const char* apiKey;
    const char* devId;
 
protected:
    /**
     * @brief Processes queued telemetry records by sending them via HTTP POST.
     */
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) override {
        if (rawQueueItemPayload != nullptr && WiFi.status() == WL_CONNECTED) {
            JsonDocument doc;
            rawQueueItemPayload->serialize(doc);
            
            String payload;
            serializeJson(doc, payload);

            HTTPClient http;
            http.begin(edgeUrl);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("X-API-Key", apiKey);
            http.addHeader("Bypass-Tunnel-Reminder", "true");

            Serial.println("\n[HTTP DISPATCH] Sending JSON to Edge:");
            Serial.println(payload);

            int httpResponseCode = http.POST(payload);
            Serial.printf("[HTTP INFRA] Edge Response Code: %d\n", httpResponseCode);

            if (httpResponseCode == 200 || httpResponseCode == 201) {
                String response = http.getString();
                Serial.println("Response body received:");
                Serial.println(response);

                if (response.indexOf("\"valve_command\":\"CLOSE\"") != -1 ||
                    response.indexOf("\"valve_command\": \"CLOSE\"") != -1) {
                    Serial.println("[REMOTE COMMAND] Server orders safety shutdown.");
                    controlValve.handle(Command(CMD_CLOSE_VALVE_ID));
                    alertLed.handle(Command(CMD_ALERT_ON_ID));
                    alertBuzzer.handle(Command(CMD_ALERT_ON_ID));
                }
            } else {
                if (currentGasPPM >= 400.0) {
                    Serial.println("[AUTONOMOUS] Network error. Local mitigation has already been activated by the sensor.");
                }
            }
            http.end();
        }
    }

public:
    NexoraDevice(int pinGas, int pinValve, int pinLed, int pinBuzzer, 
                 const char* url, const char* key, const char* id,
                 unsigned long samplingIntervalInMilliseconds, uint8_t timerGroupIndex = 0)
        : Device(samplingIntervalInMilliseconds, timerGroupIndex),
          gasSensor(pinGas, this), 
          controlValve(pinValve, this),
          alertLed(pinLed, this),
          alertBuzzer(pinBuzzer, this),
          currentGasPPM(0.0), edgeUrl(url), apiKey(key), devId(id)
    {
        // Set event handler
        gasSensor.setHandler(this);

        // Initialize framework's asynchronous telemetry engine with queue depth of 10
        initializeAsynchronousEngine(10);

        // Register gas sensor with the framework's scheduler
        appendSensorToScheduler(&gasSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
    }

    /**
     * @brief Processes sensor read events asynchronously.
     */
    void on(Event event) override {
        if (event.identifier == Sensor::DATA_READ_EVENT_IDENTIFIER) {
            currentGasPPM = gasSensor.getMappedValue();

            Serial.println("\n================================================");
            Serial.printf("[NEXORA OS] Physical Kitchen Gas: %.1f PPM\n", currentGasPPM);

            // Execute local safety logic
            if (currentGasPPM >= 400.0) {
                controlValve.handle(Command(CMD_CLOSE_VALVE_ID));
                alertLed.handle(Command(CMD_ALERT_ON_ID));
                alertBuzzer.handle(Command(CMD_ALERT_ON_ID));
            } else {
                controlValve.handle(Command(CMD_OPEN_VALVE_ID));
                alertLed.handle(Command(CMD_ALERT_OFF_ID));
                alertBuzzer.handle(Command(CMD_ALERT_OFF_ID));
            }

            // Enqueue telemetry payload
            TelemetryPackage* telemetry = new NexoraTelemetryPackage(
                devId,
                currentGasPPM,
                currentGasPPM > 100.0
            );
            if (!enqueueTelemetryPayload(&telemetry)) {
                delete telemetry; // Clean up if queue is full
            }
        }
    }

    void handle(Command command) override {
        // Concrete command handling if needed
    }
};

// ============================================================================
// --- 5. TOP-LEVEL CONFIGURATIONS & ARDUINO LOOP ---
// ============================================================================

// ESP32 HARDWARE MAPPING
#define PIN_SENSOR_MQ2        34  // Analog pin ADC1_CH6 (VP)
#define PIN_SERVO_VALVULA     18  // PWM pin for the Servomotor
#define PIN_LED_ALERTA        26  // Digital output pin for the LED
#define PIN_BUZZER_ACTIVO     27  // Digital output pin for the Buzzer

// Wi-Fi NETWORK ACCESS CONFIGURATION (WOKWI)
const char* wifi_ssid  = "Sunflower";
const char* wifi_pass  = "iexdnQBTHWvPjUWYmvs3";
// Public tunnel URL -> changes depending on the active tunnel
const char* target_url = "https://dirty-worlds-fly.loca.lt/api/v1/monitoring/telemetry";

// ASYNCHRONOUS DEVICE MEDIATOR INSTANTIATION
static NexoraDevice* nexoraApartment = nullptr;

void setup() {
    Serial.begin(115200);
    Serial.println("[NEXORA START] Waking up decoupled embedded system...");

    // Initialize wireless network connection
    WiFi.begin(wifi_ssid, wifi_pass);
    Serial.print("Searching for Wi-Fi network");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[RED READY] ESP32 securely connected.");
    Serial.print("Locally assigned IP: ");
    Serial.println(WiFi.localIP());

    // Initialize asynchronous mediator device sampling every 3 seconds (3000ms)
    nexoraApartment = new NexoraDevice(
        PIN_SENSOR_MQ2,
        PIN_SERVO_VALVULA,
        PIN_LED_ALERTA,
        PIN_BUZZER_ACTIVO,
        target_url,
        "test-api-key-123",
        "gas-safety-unit-apt-402",
        3000 // Sampling interval for the sensor
    );

    Serial.println("[NEXORA OS] Device initialized. Asynchronous monitoring active.");
}

void loop() {
    // Since execution runs asynchronously via the library's internal engine (using FreeRTOS),
    // the main loop remains free and simply yields CPU time to prevent WDT resets.
    vTaskDelay(pdMS_TO_TICKS(60000));
}
