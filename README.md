# BrewSphere – Das runde Auge deines Brauprozesses

BrewSphere fungiert je nach Phase als völlig unterschiedliches Instrument.

1. Live-Brautag (Dashboard Mode)
Während des Maischens und Kochens benötigt man eine dynamische, gut lesbare Anzeige aus ein paar Metern Entfernung.

    Maischrast-Timer: Großer Countdown in der Mitte mit kreisförmigem Fortschrittsbalken entlang des Displayrands.

    Temperatur-Dial: Soll- vs. Ist-Temperatur im Stil einer analogen Anzeige.

    Nächster Schritt: Prominente Textzeile (z.B. "Rast 2: 67°C - noch 35 Min" oder "Hopfengabe @ 60 Min").

2. Gärungs-Monitoring (Status Mode)
In der Gärphase reicht eine ruhigere, informative Übersicht, die sich ideal für den Dauerbetrieb eignet.

    Sud-Kopfzeile: Name des aktuellen Biers und Tag seit Anstellen.

    Gär-Status: Ist-Temperatur, aktueller SG / °P und Ziel-EVG.

    Trend-Indikator: Ein kleiner Pfeil oder Mini-Graph für den Temperatur- und Dichteverlauf der letzten 24h.

## What it does
... COMING SOON - UNDER CONSTRUCTION ...

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini



## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

Runtime data services:


## Credits
This project is baded on fork of WatskeBart/ESP32-Plane-Radar by WatskeBart which is bases (fork) of MatixYo/ESP32-Plane-Radar by MatixYo. All credit for the original concept and implementation goes to them.
