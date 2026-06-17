# Nexora Voltage and Current Consumption Monitor (ESP32 + Wokwi)

Este proyecto simula un monitor de consumo eléctrico y detección de anomalías utilizando un ESP32 en Wokwi. Está diseñado bajo el patrón arquitectónico del framework de eventos asíncronos **Modest-IoT** que corre sobre tareas de FreeRTOS.

## Características y Lógica de Control

*   **Planificador de Muestreo**: Monitorea de forma periódica los sensores de corriente y voltaje cada 1000 ms.
*   **Supresión de Ruido/Redundancia**: Evita saturar la cola de telemetría si las variaciones de corriente son menores a `0.2 A` o si el estado del switch de voltaje no ha cambiado.
*   **Lógica de Estados de Carga**:
    1.  **Red Desenergizada (`!voltaje_ok`)**: Entra en estado `STANDBY`. Apaga tanto el LED Verde (Luz Gestionada) como el LED Rojo (Avería).
    2.  **Exceso de Corriente (`corriente_A > 20.0 A`)**: Entra en `SOBRECARGA / ANOMALÍA`. Apaga el LED Verde de la carga para proteger el circuito y enciende el LED Rojo de Avería.
    3.  **Operación Normal**: Activa la carga encendiendo el LED Verde y apaga el LED Rojo.

## Hardware y Conexiones

*   **ESP32 DevKit v1**
*   **Sensor de Corriente (Potenciómetro)**: Señal conectada al pin analógico `GPIO34` (`PIN_SENSOR_CORRIENTE`). Mapea valores ADC (0-4095) a un rango de 0.0 a 30.0 Amperios.
*   **Sensor de Presencia de Voltaje (Interruptor deslizante)**: Conectado a `GPIO12` (`PIN_SENSOR_VOLTAJE`) usando `INPUT_PULLUP`.
    *   *LOW (GND)* = Red normal (Voltaje OK)
    *   *HIGH (Flotante)* = Falla de red (Sin voltaje)
*   **LED Verde (Luz Gestionada)**: Conectado a `GPIO25` (`PIN_LED_CARGA`).
*   **LED Rojo (Alerta de Avería)**: Conectado a `GPIO26` (`PIN_LED_AVERIA`).

## Estructura del Código

*   `nexora-voltage-consumption.ino`: Firmware principal con las clases custom `CurrentSensor`, `VoltageSensor`, `PowerTelemetry` y el mediador `ElectricalMonitorDevice`.
*   `diagram.json`: Configuración física y conexiones visuales en Wokwi.
*   `wokwi-project.txt`: Enlace del proyecto en la nube.

## Telemetría Despachada (JSON)

Formato enviado por el despachador asíncrono para su posterior envío al Gateway:
```json
{
  "corriente_A": 15.4,
  "voltaje_ok": true,
  "anomalia": false,
  "timestamp": 24500
}
```

## Simulación en la Nube

Puedes simular interactivamente el proyecto ingresando a:
👉 [Wokwi Project Link](https://wokwi.com/projects/466639957541837825)
