# Nexora Embedded Applications (ESP32 + Wokwi)

Este repositorio contiene los firmwares y diagramas de simulación para los distintos módulos embebidos que componen el ecosistema de automatización y seguridad de **Nexora**.

Todos los módulos utilizan el microcontrolador ESP32 y se estructuran siguiendo la arquitectura de eventos y despacho de tareas asíncronas basada en el framework **Modest-IoT** (FreeRTOS).

## Estructura del Repositorio

El proyecto está dividido en carpetas independientes para cumplir con las especificaciones de Arduino IDE y mantener la modularidad:

*   📂 **[nexora-gas-safety/](file:///c:/Users/REYNALDO/Documents/GitHub/iot/nexora.embeddedapp/nexora-gas-safety/)**: Sistema de monitoreo de fugas de gas con válvula servo-actuada y alertas sonoras. Lee más detalles en su [README local](file:///c:/Users/REYNALDO/Documents/GitHub/iot/nexora.embeddedapp/nexora-gas-safety/README.md).
*   📂 **[nexora-voltage-consumption/](file:///c:/Users/REYNALDO/Documents/GitHub/iot/nexora.embeddedapp/nexora-voltage-consumption/)**: Sistema de monitoreo de consumo eléctrico (Amperaje) y estado de la red (Voltaje) con protección contra sobrecargas. Lee más detalles en su [README local](file:///c:/Users/REYNALDO/Documents/GitHub/iot/nexora.embeddedapp/nexora-voltage-consumption/README.md).
*   📂 **[nexora-water-consumption/](file:///c:/Users/REYNALDO/Documents/GitHub/iot/nexora.embeddedapp/nexora-water-consumption/)**: Módulo para monitoreo de caudal y control de consumo de agua (por implementar).

---

## Cómo Ejecutar las Simulaciones en VS Code

Si deseas probar y simular cualquiera de los módulos directamente desde VS Code:

1.  Asegúrate de tener instalada la extensión **Wokwi Simulator** para VS Code.
2.  Presiona la tecla `F1` (o `Ctrl+Shift+P`) y busca el comando **Wokwi: Start Simulator**.
3.  La extensión detectará automáticamente los múltiples archivos `diagram.json` en las carpetas del repositorio y te mostrará un menú desplegable para elegir qué módulo deseas arrancar.
4.  Abre el Monitor Serial para observar los despachos de telemetría y logs de consola.
