# Nexora Water Flow Anomaly Monitor (ESP32 + Wokwi)

Este proyecto simula un sistema IoT de monitoreo de flujo de agua y detección de anomalías utilizando un **ESP32 en Wokwi**. Está diseñado bajo el patrón arquitectónico del framework de eventos asíncronos **Modest-IoT**, ejecutándose sobre tareas de **FreeRTOS**.

El objetivo del sistema es detectar cambios bruscos en el flujo de agua, los cuales pueden representar posibles fugas, consumos anormales o fallas en una tubería. Para ello, el dispositivo lee periódicamente el caudal de agua, evalúa si existe una variación peligrosa y activa alertas visuales según el estado del sistema.

## Características y Lógica de Control

* **Planificador de Muestreo**: Monitorea de forma periódica el sensor de flujo de agua cada 1000 ms.

* **Lectura de Caudal de Agua**: Obtiene el valor del flujo en litros por minuto (`L/min`) desde el sensor de flujo configurado en Wokwi.

* **Supresión de Ruido/Redundancia**: Evita generar telemetría innecesaria cuando la variación del caudal es mínima o no representa un cambio relevante en el sistema.

* **Detección de Cambio Abrupto**: Compara el flujo actual con la lectura anterior. Si la diferencia supera el umbral configurado, se considera una anomalía.

* **Lógica de Estados del Sistema**:

  1. **Sin Flujo o Flujo Bajo (`flujo_Lmin <= 0`)**:
     El sistema permanece en estado de reposo o monitoreo pasivo. El LED Verde puede permanecer apagado o indicar estado estable, según la configuración del firmware.

  2. **Operación Normal (`flujo_Lmin` estable)**:
     El sistema interpreta que el consumo de agua es normal. Enciende el **LED Verde** y mantiene apagado el **LED Rojo**.

  3. **Cambio Abrupto de Flujo (`variacion_Lmin >= UMBRAL_CAMBIO`)**:
     El sistema detecta una posible fuga, consumo anómalo o evento crítico. Enciende el **LED Rojo de alerta** y desactiva el LED Verde.

  4. **Confirmación o Reinicio Manual**:
     El botón permite simular una acción manual del usuario, como reconocer la alerta, reiniciar el estado o validar el comportamiento del sistema durante la simulación.

## Hardware y Conexiones

* **ESP32 DevKit v1**

* **Water Flow Sensor Breakout**:
  Sensor utilizado para simular el flujo de agua en Wokwi. Permite modificar manualmente el caudal en litros por minuto durante la simulación.

* **Señal del Sensor de Flujo**:
  Conectada al pin digital configurado en el firmware como entrada de pulsos del sensor.

* **Botón de Control Manual**:
  Conectado a un pin digital del ESP32 usando `INPUT_PULLUP`.
  Permite simular una acción del usuario, como reiniciar o confirmar la alerta.

* **LED Verde — Estado Normal**:
  Indica que el flujo de agua se encuentra dentro de un comportamiento esperado.

* **LED Rojo — Alerta de Anomalía**:
  Indica que se detectó un cambio brusco o comportamiento anómalo en el flujo de agua.

* **Resistencias para LEDs**:
  Utilizadas para proteger los LEDs y limitar la corriente eléctrica del circuito.

## Estructura del Código

* `nexora-water-flow-monitor.ino`: Firmware principal del dispositivo IoT. Contiene la lectura del sensor de flujo, la lógica de detección de anomalías, el control de LEDs, el botón manual y el despacho de telemetría.

* `diagram.json`: Configuración física del circuito en Wokwi, incluyendo el ESP32, sensor de flujo, LEDs, resistencias y botón.

* `wokwi-project.txt`: Archivo con el enlace del proyecto en la nube de Wokwi.

## Funcionamiento del Sensor de Flujo

El **Water Flow Sensor Breakout** simula el comportamiento de un sensor de caudal real. En un sistema físico, este tipo de sensor genera pulsos eléctricos cuando el agua pasa por su interior. A mayor flujo de agua, mayor cantidad de pulsos por segundo.

En la simulación de Wokwi, el usuario puede modificar directamente el valor del flujo en `L/min`. El firmware interpreta ese valor como una lectura del caudal actual y lo compara con lecturas anteriores para identificar cambios abruptos.

Por ejemplo:

```text
Lectura anterior: 2.0 L/min
Lectura actual:   8.0 L/min
Variación:        6.0 L/min
```

Si el umbral configurado para detectar anomalías es de `5.0 L/min`, entonces el sistema considera esta variación como una alerta.

## Telemetría Despachada (JSON)

Formato enviado por el despachador asíncrono para su posterior procesamiento por el Gateway:

```json
{
  "flujo_Lmin": 8.0,
  "variacion_Lmin": 6.0,
  "anomalia": true,
  "estado": "ALERTA_FLUJO",
  "timestamp": 24500
}
```

## Ejemplo de Estados

### Estado Normal

```json
{
  "flujo_Lmin": 2.4,
  "variacion_Lmin": 0.3,
  "anomalia": false,
  "estado": "NORMAL",
  "timestamp": 18000
}
```

### Estado de Alerta

```json
{
  "flujo_Lmin": 9.1,
  "variacion_Lmin": 6.7,
  "anomalia": true,
  "estado": "ALERTA_FLUJO",
  "timestamp": 22000
}
```

## Simulación en Wokwi

Durante la simulación, puedes modificar el valor del flujo de agua desde el componente **Water Flow Sensor Breakout**. Si el flujo aumenta de forma repentina y supera el umbral definido en el código, el sistema activará la alerta visual mediante el **LED Rojo**.

Puedes simular interactivamente el proyecto ingresando a:

👉 [Wokwi Project Link](COLOCA_AQUI_EL_LINK_DE_TU_PROYECTO)
