![BrewSphere – ESP32 Fermentation Display](docs/assets/brand/brewsphere-readme-banner-1200x360.png)

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

### BrewSphere UI-Assets neu erzeugen

Das Display verwendet einen mit Inkscape gerenderten, RLE-komprimierten
Hintergrund und passend zur SVG-Baseline gerasterte Noto-Sans-Glyphen. Die
generierte Datei `src/ui/brew_assets.cpp` ist eingecheckt; zum normalen
Firmware-Build werden daher weder Inkscape noch die TTF-Dateien benötigt.

Nach Änderungen an der SVG oder Typografie werden die Assets mit Python,
Pillow, Inkscape sowie `NotoSans-Regular.ttf` und `NotoSans-Bold.ttf` neu
erzeugt:

```bash
python scripts/build_brew_assets.py --font-dir X:/Noto_Sans/static
```

Ein anderer Font-Pfad kann auch über `BREWSPHERE_NOTO_SANS` gesetzt werden.

### Marken- und Startbild-Assets neu erzeugen

Das BrewSphere-Markensystem liegt unter `docs/assets/brand`. Der Generator
erstellt das skalierbare Emblem, horizontale Wortmarke, Kompakt- und
Monochromvariante, README-Banner sowie den RGB565-geprüften 240×240-
Startbildschirm. Die komprimierten Firmwaredaten werden nach
`src/ui/brand_assets.cpp` geschrieben.

```bash
python scripts/build_brand_assets.py --font-dir X:/Noto_Sans/static
```

Die Firmware zeigt nach dem Displaystart zuerst das Primary Emblem und danach
die zweizeilige BrewSphere-Wortmarke für jeweils 1 Sekunde. Anschließend folgt
der bestehende Verbindungs- und Anzeigepfad. Das ursprüngliche Konzeptbild bleibt unter
`tools/BrewSphereMockup/BrewSphere_Logo.png` als Referenz unverändert erhalten.

## Brewfather und simulierte Werte

Im Webinterface führt **Brewfather / Simulation** zur Seite `/brew`. Dort kann
die Datenquelle persistent zwischen **Brewfather API** und **Simulierte Werte**
umgeschaltet werden. Im Simulationsmodus lassen sich alle Werte der
Gärungsanzeige frei einstellen; nach dem Speichern aktualisieren sich das
GC9A01-Display und die Webvorschau ohne Brewfather-Abfrage.

Simulationsänderungen werden bereits während der Eingabe nach 300 ms live in
den RAM übernommen. **Speichern** schreibt den aktuellen Stand zusätzlich
dauerhaft nach NVS. Damit erzeugt die Live-Vorschau keine unnötigen
Flash-Schreibzyklen.

Der aktuelle Vergärgrad steuert den hellblauen Fortschrittsbogen über der
cyanfarbenen Grundskala. Der getrennte Endvergärgrad bestimmt den Farbwechsel
der 2,5-%-Punkteskala von Blau zu Blassgelb; jeder 10-%-Schritt einschließlich
0 % wird als größerer Punkt dargestellt. Im Brewfather-Modus wird der
Endvergärgrad aus gemessener OG und geschätzter FG berechnet.

Die Eingaben für den aktuellen und den Zielwert erfolgen direkt in `°P`. Die
Firmware rechnet diese intern in SG um, damit simulierte und echte Daten den
gleichen `BrewData`- und Renderingpfad verwenden.



## Verwendete Bibliotheken

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

Die Bibliotheken stehen jeweils unter ihren eigenen Lizenzbedingungen.


## Lizenz und Herkunft

BrewSphere basiert teilweise auf dem Projekt WatskeBart/ESP32-Plane-Radar by WatskeBart welches auf of MatixYo/ESP32-Plane-Radar by MatixYo basiert.

Der übernommene Code steht unter der MIT-Lizenz. Die ursprüngliche
Lizenzinformation befindet sich in der Datei [LICENSE](LICENSE).

Eigene Weiterentwicklungen dieses Projekts stammen von Deinjo und stehen,
soweit nicht anders angegeben, ebenfalls unter der MIT-Lizenz.
