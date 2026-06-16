# Nexora Safety Vessel (ESP32 + Wokwi)

An ESP32-based gas safety and leakage mitigation project that monitors gas concentration levels, controls a servo-actuated gas valve, activates local alert indicators (LED and buzzer), and transmits telemetry over HTTP.

## Overview

This project uses the event-driven **Modest-IoT** Nano-Framework and an asynchronous architecture (running via FreeRTOS on the ESP32) to orchestrate:

- MQ-2 gas sensor (analog reader integrated with the scheduler)
- PWM servo-actuated gas valve
- LED and Buzzer warning actuators for audible and visual alerts
- Wi-Fi + HTTP client telemetry dispatching with remote mitigation override support

The source sketch is designed for asynchronous operation on ESP32 and runs in simulation with Wokwi.

## Features

- **Asynchronous Scheduler**: Utilizes the Modest-IoT scheduler to poll the gas sensor every 3000 ms.
- **Autonomous Mitigation**: Local safety logic automatically shuts the gas valve (rotates servo to 0°) and triggers warnings (LED & Buzzer) if gas levels reach or exceed 400.0 PPM.
- **JSON Telemetry Dispatch**: Publishes JSON payloads via HTTP POST to an Edge monitoring server.
- **Remote Command Execution**: Actively parses server responses. If the server responds with a `CLOSE` command, it overrides the local state to trigger a remote safety shutdown.

## Hardware/Simulation Setup

From `diagram.json`, the main parts are:

- ESP32 DevKit v1
- Wokwi Gas Sensor (MQ-2 style analog input)
- Wokwi Servo Motor (simulating the gas valve)
- Wokwi LED (red warning light)
- Wokwi Buzzer (audible warning alarm)

### Pin Mapping

- Gas Sensor analog output (`AOUT`) -> ESP32 `GPIO34` (`PIN_SENSOR_MQ2`)
- Servo Motor (Gas Valve PWM) -> ESP32 `GPIO18` (`PIN_SERVO_VALVULA`)
- LED Alerta (Digital Out) -> ESP32 `GPIO26` (`PIN_LED_ALERTA`)
- Buzzer Activo (Digital Out) -> ESP32 `GPIO27` (`PIN_BUZZER_ACTIVO`)

## Project Structure

- `nexora.embeddedapp.test1.ino`: Main firmware and application logic.
- `diagram.json`: Wokwi wiring diagram.
- `libraries.txt`: Wokwi library dependencies.

## Dependencies

Defined in `libraries.txt`:

- `ArduinoJson`
- `PubSubClient`
- `DHT sensor library`
- `ESP32Servo`
- `LiquidCrystal_I2C`
- `ModestIoT@wokwi:cf3b377520637d396b451edb39e21f8a3b80dd12`

## Configuration

Key settings are configured in `nexora.embeddedapp.test1.ino`:

- Wi-Fi Credentials (`wifi_ssid` = `"Sunflower"`, `wifi_pass` = `"iexdnQBTHWvPjUWYmvs3"`)
- Edge server URL (`target_url`)
- Sampling interval (`3000 ms` by default)

Update these constants in the source sketch before running against your own network/server.

## Quick Start (Wokwi)

1. Open the project in Wokwi.
2. Ensure dependencies from `libraries.txt` are available.
3. Run the simulation.
4. Open the Serial Monitor at `115200` baud.

Expected behavior:

- Device boots and connects to the Wi-Fi network.
- The gas sensor is polled asynchronously every 3 seconds.
- Telemetry records are posted to the Edge server as JSON.
- If gas concentration is below 400.0 PPM, the valve remains open (90°) and alerts are OFF.
- If gas concentration exceeds 400.0 PPM or the server returns a `CLOSE` command, the valve closes (0°) and alerts turn ON.

## Telemetry Format

Telemetry is serialized from `NexoraTelemetryPackage` with these fields:

- `device_id` (string)
- `apartment_id` (string, defaults to `"Apt-402"`)
- `gas_ppm` (float)
- `smoke_detected` (bool, active when `gasPpm > 100.0`)
- `motion_detected` (bool, false by default)
- `door_open` (bool, false by default)

## License

This project is licensed under the MIT License.
