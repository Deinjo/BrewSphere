# Brewfather API testen

`test_brewfather_api.py` prueft den Zugriff auf die Brewfather API, waehlt
einen Batch aus und liest dessen letzten Messwert. Das Skript gibt mindestens
Batch und Temperatur aus. Falls vorhanden, werden auch Status, Specific Gravity
(SG) und Sensortyp angezeigt.

Die grafische Anwendung ist aktuell Version **1.4.0**. Die Versionsnummer wird
im Fenstertitel angezeigt und bei funktionalen Erweiterungen erhoeht. Sie wird
nicht in Datei- oder EXE-Namen verwendet, damit stabile Pfade fuer GitHub und
die Benutzer erhalten bleiben.

## Windows-EXE

Eine eigenstaendige Windows-Version liegt nach dem Build hier:

```text
tools/TestingBrewfatherApi/dist/BrewfatherApiTest.exe
```

Die EXE benoetigt keine separate Python-Installation. Fuer die Zugangsdaten
eine lokale `config_local.h` neben die EXE legen:

```text
dist/
  BrewfatherApiTest.exe
  config_local.h
```

Die Datei muss diese Defines enthalten:

```cpp
#define BREWFATHER_USER_ID "DEINE_USER_ID"
#define BREWFATHER_API_KEY "DEIN_API_KEY"
```

Alternativ koennen die Zugangsdaten nach dem Start direkt in der GUI eingetragen
und mit den beiden **Speichern**-Buttons gespeichert werden. Die EXE liest beim
Start die Datei neben sich ein. Private Zugangsdaten niemals zusammen mit der
EXE weitergeben oder committen.

Die EXE wird mit folgendem Befehl neu erzeugt:

```powershell
pyinstaller --noconfirm --clean --onefile --windowed --icon=brewsphere-emblem.ico --add-data "..\..\docs\assets\brand\brewsphere-emblem-512.png;." --name BrewfatherApiTest .\brewfather_gui.py
```

Der Build verwendet das KOSTAL-unabhaengige BrewSphere-Branding-Icon aus
`docs/assets/brand/brewsphere-emblem-512.png`. Die fuer Windows konvertierte
Datei liegt als `brewsphere-emblem.ico` im Toolverzeichnis und wird mit
`--icon=brewsphere-emblem.ico` eingebettet.

Das CLI-Skript verwendet ausschliesslich Module aus der Python-Standardbibliothek.
Fuer den Start der GUI aus Python werden zusaetzlich Tkinter und Pillow fuer
die hochaufloesende Emblem-Anzeige benoetigt. Die fertige EXE bringt diese
Abhaengigkeiten bereits mit.

Fuer Anwender ohne Programmierkenntnisse steht zusaetzlich eine kleine
grafische Oberflaeche zur Verfuegung. Sie benoetigt ebenfalls nur Python und
Tkinter, das bei der offiziellen Windows-Python-Installation normalerweise
enthalten ist.

## Voraussetzungen

- Python 3
- Internetzugriff auf `https://api.brewfather.app`
- Brewfather User-ID
- Brewfather API-Key mit dem Scope `batches.read`

Die Zugangsdaten werden nicht auf der Konsole ausgegeben.

## Zugangsdaten konfigurieren

### Lokale Konfigurationsdatei

Im Verzeichnis des Tools die Beispieldatei kopieren:

```powershell
Copy-Item config_local.example.py config_local.py
```

Danach die beiden Werte in `config_local.py` eintragen:

```python
BREWFATHER_USER_ID = "DEINE_USER_ID"
BREWFATHER_API_KEY = "DEIN_API_KEY"
```

`config_local.py` ist in der `.gitignore` eingetragen und darf nicht committed
oder weitergegeben werden.

### Umgebungsvariablen

Alternativ koennen die Zugangsdaten fuer die aktuelle PowerShell-Sitzung gesetzt
werden:

```powershell
$env:BREWFATHER_USER_ID = "DEINE_USER_ID"
$env:BREWFATHER_API_KEY = "DEIN_API_KEY"
python .\test_brewfather_api.py
```

Unter Windows CMD:

```bat
set BREWFATHER_USER_ID=DEINE_USER_ID
set BREWFATHER_API_KEY=DEIN_API_KEY
python test_brewfather_api.py
```

Unter Bash:

```bash
BREWFATHER_USER_ID="DEINE_USER_ID" \
BREWFATHER_API_KEY="DEIN_API_KEY" \
python3 test_brewfather_api.py
```

Wenn Umgebungsvariablen und `config_local.py` gleichzeitig vorhanden sind,
haben die Umgebungsvariablen Vorrang.

### Firmware-Konfigurationsdatei `config_local.h`

Die GUI sucht beim Start nach `config_local.h` in dieser Reihenfolge:

1. `tools/TestingBrewfatherApi/config_local.h`
2. `include/config_local.h` im BrewSphere-Projekt

Wenn eine Datei gefunden wird, liest die GUI daraus diese beiden Defines:

```cpp
#define BREWFATHER_USER_ID "DEINE_USER_ID"
#define BREWFATHER_API_KEY "DEIN_API_KEY"
```

Die beiden **Speichern**-Buttons rechts neben User-ID und API-Key schreiben den
jeweiligen aktuellen Feldinhalt in die gefundene Datei. Existiert noch keine
Datei, wird `include/config_local.h` angelegt. Bestehende andere Defines, zum
Beispiel WLAN-Einstellungen, bleiben erhalten. Die Datei ist lokal und wird
nicht committed.

## Aufruf

### Grafische Oberflaeche fuer Anwender

Aus dem Verzeichnis `tools/TestingBrewfatherApi` starten:

```powershell
python .\brewfather_gui.py
```

Alternativ aus dem Projektstamm:

```powershell
python .\tools\TestingBrewfatherApi\brewfather_gui.py
```

In der GUI:

1. User-ID und API-Key eintragen.
2. Optional den Batch-Status aendern oder eine konkrete Batch-ID eintragen.
3. Fuer eine Batch-Abfrage eine Batch-ID eintragen oder per Doppelklick aus der
   Batch-Tabelle uebernehmen.
4. Einen der vier API-Buttons verwenden:
   - **Get Batch**
   - **Get Batch Last Reading**
   - **Get Batch All Readings**
   - **Get Batch Brew Tracker**
5. Optional JSON- oder HTML-Export bei der allgemeinen Abfrage verwenden.
6. Bei aktivierten Exporten jeweils den Speicherort auswaehlen.

Der Status kann ueber eine Auswahlbox auf die von Brewfather gueltigen Werte
gesetzt werden. Wenn ein Batch zwar vorhanden ist, aber noch keinen Sensor-
Messwert besitzt, wird dies als „Kein letzter Messwert vorhanden“ angezeigt.
Ein HTTP-404 von `/readings/last` wird in diesem Fall nicht mehr als Fehler der
Batch-Abfrage behandelt.

Mit **Alle Batches laden** werden alle Batches des Kontos abgefragt und in einer
Tabelle mit Recipe Name, Name, ID und Status angezeigt. Die API liefert maximal 50 Batches
pro Antwort; die GUI laedt automatisch weitere Seiten nach.

Der **Recipe Name** aus `batch.recipe.name` steht dabei an erster Stelle. Die
Sortierung erfolgt zuerst nach der festgelegten Statusreihenfolge und innerhalb
des Status alphabetisch nach Recipe Name.

Rechts neben Tabelle und Ergebnis befindet sich der Bereich **JSON Response**.
Dort wird die geladene API-Antwort formatiert angezeigt. JSON-Schluessel,
Strings, Zahlen und Wahrheits-/Nullwerte werden farblich hervorgehoben. Der
Bereich besitzt vertikale und horizontale Scrollleisten und wird bei jeder
neuen Abfrage aktualisiert.

Oberhalb des JSON-Bereichs zeigt **API Requests** die tatsächlich gesendeten
GET-Requests inklusive Query-Parametern. Beim Laden aller Batches wird dort
jede nachgeladene `start_after`-Seite einzeln aufgeführt. Zugangsdaten werden
nicht angezeigt.

Die API-Abfrage laeuft im Hintergrund. Das Fenster bleibt deshalb auch bei
langsamen Netzwerkverbindungen bedienbar. Der API-Key wird standardmaessig
verdeckt angezeigt; mit **API-Key anzeigen** kann die Eingabe kontrolliert
werden. Die Zugangsdaten werden nicht automatisch gespeichert.

Falls beim Start eine Meldung zu Tkinter erscheint, muss Python mit der
Tk-Komponente installiert werden. Bei der offiziellen Windows-Installation
ist diese Option standardmaessig enthalten.

### Kommandozeile

Aus `tools/TestingBrewfatherApi`:

```powershell
python .\test_brewfather_api.py [OPTIONEN]
```

Alternativ aus dem Projektstamm:

```powershell
python .\tools\TestingBrewfatherApi\test_brewfather_api.py [OPTIONEN]
```

Die integrierte Hilfe zeigt ebenfalls alle Optionen:

```powershell
python .\test_brewfather_api.py --help
```

## Optionen

| Option | Bedeutung | Standardwert |
|---|---|---|
| `--status STATUS` | Listet Batches mit diesem Status und verwendet den ersten Treffer. | `Fermenting` |
| `--batch-id ID` | Verwendet eine konkrete Batch-ID statt der Batch-Suche. | nicht gesetzt |
| `--base-url URL` | Ueberschreibt die Basis-URL der Brewfather API. | `https://api.brewfather.app/v2` |
| `--timeout SEKUNDEN` | HTTP-Timeout als Ganz- oder Dezimalzahl. | `10.0` |
| `--save-json PFAD` | Speichert einen reduzierten Diagnoseexport als JSON. | kein Export |
| `--save-full-json PFAD` | Speichert die vollstaendige geladene API-Antwort als formatiertes JSON. | kein Export |
| `--save-html PFAD` | Speichert die vollstaendige geladene API-Antwort als aufklappbare HTML-Seite. | kein Export |
| `-h`, `--help` | Zeigt die Kommandozeilenhilfe an und beendet das Skript. | - |

Alle Exportoptionen koennen einzeln oder gemeinsam verwendet werden. Benoetigte
Unterverzeichnisse im angegebenen Ausgabepfad werden automatisch erstellt.

## Aufrufbeispiele

### Letzten Messwert des ersten gaerenden Batches abrufen

```powershell
python .\test_brewfather_api.py
```

Das Skript fragt bis zu 50 Batches mit Status `Fermenting` ab und verwendet den
ersten Batch aus der API-Antwort.

### Einen anderen Batch-Status verwenden

```powershell
python .\test_brewfather_api.py --status Conditioning
```

Der Status wird unveraendert als Filter an Brewfather uebergeben. Werte mit
Leerzeichen muessen in Anfuehrungszeichen stehen:

```powershell
python .\test_brewfather_api.py --status "Ready to Package"
```

### Einen Batch direkt ueber seine ID abrufen

```powershell
python .\test_brewfather_api.py --batch-id 0123456789abcdef01234567
```

Ohne Vollstaendigkeits-Export ruft dieser Modus direkt den letzten Messwert des
Batches ab. Mit `--save-full-json` oder `--save-html` wird zusaetzlich der
vollstaendige Batch-Endpunkt abgefragt.

### Einen reduzierten Diagnoseexport speichern

```powershell
python .\test_brewfather_api.py --save-json brewfather_response_redacted.json
```

Dieser Export maskiert technische IDs, enthaelt aber weiterhin ausgewaehlte
Batch-, Rezept- und Messdaten. Die Datei sollte deshalb weiterhin als intern
und potenziell sensibel behandelt werden.

### Vollstaendige Antwort als JSON speichern

```powershell
python .\test_brewfather_api.py --save-full-json brewfather_response_full.json
```

### Vollstaendige Antwort als HTML speichern

```powershell
python .\test_brewfather_api.py --save-html brewfather_response.html
```

Die HTML-Datei kann anschliessend lokal im Browser geoeffnet werden. Die
einzelnen Antwortbereiche sind ein- und ausklappbar.

### Alle Exportformate gleichzeitig erzeugen

```powershell
python .\test_brewfather_api.py `
  --save-json brewfather_response_redacted.json `
  --save-full-json brewfather_response_full.json `
  --save-html brewfather_response.html
```

### Eigenen Timeout setzen

```powershell
python .\test_brewfather_api.py --timeout 30
```

Auch Dezimalwerte sind zulaessig:

```powershell
python .\test_brewfather_api.py --timeout 2.5
```

### Alternative API-Basis-URL verwenden

```powershell
python .\test_brewfather_api.py --base-url https://api.brewfather.app/v2
```

Die Option ist vor allem fuer API-Proxys, Mock-Server oder Tests gegen eine
andere kompatible Basis-URL vorgesehen. Ein abschliessender Schraegstrich ist
optional.

### Optionen kombinieren

```powershell
python .\test_brewfather_api.py `
  --batch-id 0123456789abcdef01234567 `
  --timeout 20 `
  --save-full-json output\batch.json `
  --save-html output\batch.html
```

## Verwendete API-Aufrufe

Je nach Optionen verwendet das Skript folgende Endpunkte:

```text
GET /v2/batches?status=<STATUS>&limit=50&complete=<true|false>
GET /v2/batches?limit=50&complete=false&start_after=<LAST_ID>
GET /v2/batches/<BATCH_ID>
GET /v2/batches/<BATCH_ID>/readings/last
GET /v2/batches/<BATCH_ID>/readings
GET /v2/batches/<BATCH_ID>/brewtracker
```

- Die Batch-Liste wird ohne Vollstaendigkeits-Export mit `complete=false`
  abgerufen.
- Bei `--save-full-json` oder `--save-html` wird die Batch-Liste mit
  `complete=true` abgerufen.
- Bei `--batch-id` entfaellt die Batch-Liste.
- Der Batch-Endpunkt wird im direkten ID-Modus nur fuer einen vollstaendigen
  JSON- oder HTML-Export zusaetzlich geladen.
- Der letzte Messwert wird immer ueber `/readings/last` abgefragt.
- Die vier GUI-Buttons rufen jeweils genau einen der Batch-Endpunkte auf.
- **Alle Batches laden** ruft die Batch-Liste ohne Statusfilter ab und verwendet
  `start_after` mit der letzten ID der vorherigen Antwort, bis alle Seiten
  geladen wurden.

Die Authentifizierung erfolgt per HTTP Basic Auth mit User-ID und API-Key.

## Konsolenausgabe

Eine erfolgreiche Abfrage sieht beispielsweise so aus:

```text
Batch: Test Batch (0123456789abcdef01234567)
Status: Fermenting
Temperatur: 20.4 C
SG: 1.012
Sensor: iSpindel
```

`SG` und `Sensor` erscheinen nur, wenn die Brewfather-Antwort die Felder `sg`
beziehungsweise `type` enthaelt.

## Rueckgabecodes

| Code | Bedeutung |
|---|---|
| `0` | Abfrage erfolgreich |
| `1` | Kein passender Batch, HTTP-/Netzwerkfehler oder ungueltige API-Antwort |
| `2` | Zugangsdaten fehlen |

## Fehlerbehebung

### `Fehler: Bitte config_local.py ausfuellen ...`

User-ID oder API-Key fehlen. `config_local.py` ausfuellen oder beide
Umgebungsvariablen setzen.

### `HTTP-Fehler: 401`

User-ID und API-Key pruefen. Auch fuehrende oder nachgestellte Leerzeichen in
den Werten vermeiden.

### `HTTP-Fehler: 403`

Der API-Key benoetigt mindestens den Brewfather-Scope `batches.read`.

### `Keine Batches mit Status ... gefunden.`

Den Status mit `--status` anpassen oder den gewuenschten Batch direkt mit
`--batch-id` angeben.

### `Netzwerkfehler` oder Timeout

Internetverbindung und API-Erreichbarkeit pruefen. Bei langsamen Verbindungen
den Timeout erhoehen, zum Beispiel mit `--timeout 30`.

### `Antwortfehler`

Die Antwort hatte nicht das erwartete Format oder enthielt keinen numerischen
Temperaturwert im Feld `temp`. Fuer die Diagnose kann eine vollstaendige
Antwort mit `--save-full-json` oder `--save-html` gespeichert werden.

## Datenschutz und Git

Folgende lokale Dateien werden durch die Projekt-`.gitignore` ausgeschlossen:

```text
tools/TestingBrewfatherApi/config_local.py
tools/TestingBrewfatherApi/brewfather_response*.json
tools/TestingBrewfatherApi/brewfather_response*.html
```

Andere frei gewaehlte Exportnamen sind nicht automatisch ausgeschlossen. Vor
einem Commit deshalb immer pruefen, ob erzeugte JSON- oder HTML-Dateien private
Batch-, Rezept-, Sensor- oder Accountdaten enthalten.
