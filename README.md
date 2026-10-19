# Modulo_IN — módulo de entradas de InduCore

Firmware del módulo de 8 entradas digitales y 2 analógicas (0–10 V).
STM32F103C8, se compila con PlatformIO.

Habla el **protocolo del backplane v2** (`src/Core/Inc/induproto.h`,
descripción en [`docs/PROTOCOLO.md`](docs/PROTOCOLO.md)). Funciona con la
rama `Inducore-Innovatech` de **Modulo_CPU** (lógica en C) o con
**inducoreOpenPLC**; no es compatible con el firmware de `main` de la CPU.

## Funcionamiento

El módulo es esclavo SPI. Espera a que la CPU lo seleccione (NSS en bajo) y
responde:

- **Detección (`0x48`)**: `ID 0x12` · versión · CRC. Responde en cualquier
  momento, así la CPU lo puede volver a detectar si alguno de los dos se reinicia.
- **Lectura (`0x01`)**: 8 entradas digitales y las 2 analógicas de 12 bits
  (0–4095), con CRC-8.

## Cambios en esta versión

- Tramas con CRC-8 y versión del protocolo (`induproto.h`, común a los tres firmwares).
- Analógicas de **12 bits** (antes 8).
- Canales analógicos corregidos: `AN_1` es la bornera InAn1 (PB1, canal 9) y
  `AN_2` es InAn2 (PB0, canal 8). Antes estaban cruzados.
- Tiempo de muestreo del ADC: 55,5 ciclos (antes 1,5).
- Entradas digitales como `GPIO_NOPULL`: la placa tiene pull-up externo de
  10 kΩ y el pull-down interno lo contrarrestaba.
- **Watchdog** (IWDG) de ~250 ms. Se detiene con el depurador en pausa.
- Se eliminó la carpeta `EWARM` (IAR).

## Pendiente de probar en las placas

1. Que la CPU lo detecte (`slots[]` en la CPU) y que `tramasError` no crezca.
2. Que `AN_1` lea la bornera InAn1 y `AN_2` la InAn2.
3. Que las entradas digitales lean bien sin el pull-down interno.
