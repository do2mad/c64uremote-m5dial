# C64uRemote für M5Dial

Fernbedienung für den **Commodore 64 Ultimate (c64u)** bzw. **Ultimate64 Elite-II**
über die ReST-API der Ultimate-Firmware (ab 3.11) – mit dem **im M5Dial verbauten
RFID/NFC-Leser (WS1850S)** und einer **nachgerüsteten microSD-Karte**.

Funktional gleichwertig zur Core-Version, Bedienung angepasst an Drehgeber,
Taste und Touch. Basiert auf dem Original von Karl Prosser (@klumsy) und den
Versionen für M5StickC Plus2 / M5Stack Core von Martin Oswald (@mad, https://1MHz.de).

---

## 1. Programmierumgebung

**PlatformIO in Visual Studio Code.** Die komplette Konfiguration (Board,
Bibliotheksversionen, Partitionstabelle) liegt versioniert in `platformio.ini`,
ein Build ist damit reproduzierbar. Das ist hier wichtig, weil ArduinoJson
bewusst auf Version 6 festgenagelt ist (Version 7 kennt `DynamicJsonDocument`
nicht mehr) und weil ein größeres App-Image nötig ist.

### Einrichtung

1. [Visual Studio Code](https://code.visualstudio.com) installieren
2. Erweiterung **PlatformIO IDE** installieren (Extensions → „platformio")
3. Diesen Ordner `M5Dial-C64uRemote` in VS Code öffnen
4. `src/build_env.h` mit WLAN-Daten und c64u-Adresse füllen
   (Vorlage: `src/build_env.h.example`) – das sind nur noch **Startwerte**,
   ändern lässt sich beides später am Gerät unter *Settings → WLAN*
   (siehe [Abschnitt 3a](#3a-wlan-einrichten))
5. M5Dial per USB-C anschließen, dann in der PlatformIO-Statusleiste unten
   auf **→ (Upload)** klicken

Auf der Kommandozeile:

```bash
pio run                 # kompilieren
pio run -t upload       # flashen
pio device monitor      # serielle Ausgabe (115200 Baud)
```

Kommt kein serieller Port zustande: den StampS3 beim Einstecken mit gedrückter
Taste in den Download-Modus bringen, flashen, danach einmal Reset.

### Was `platformio.ini` festlegt

| Einstellung | Wert | Warum |
|---|---|---|
| `board` | `m5stack-stamps3` | Der M5Dial ist ein StampS3 (ESP32-S3, **kein PSRAM**) |
| `board_build.partitions` | `huge_app.csv` | WiFi + SD + RFID passen nicht in die Standardtabelle |
| `M5Dial` | `^1.0.2` | bringt Display, Touch, Drehgeber **und** den RFID-Treiber mit |
| `ArduinoJson` | `^6.21.5` | Version 7 wäre API-inkompatibel |

Der RFID-Treiber (`MFRC522`) steckt in der M5Dial-Bibliothek – anders als beim
Core wird **keine** zusätzliche MFRC522-Bibliothek aus GitHub gebraucht.

---

## 2. Verkabelung microSD

Der M5Dial hat nur vier freie GPIOs, verteilt auf zwei Grove-Buchsen. SPI
braucht genau vier Leitungen – also werden **beide Ports** belegt.

| SD-Modul | M5Dial | Buchse | GPIO |
|---|---|---|---|
| `SCK`  / `CLK` | Port A, Pin „SCL" | rot     | **G15** |
| `MOSI` / `SI` / `CMD` | Port A, Pin „SDA" | rot     | **G13** |
| `MISO` / `SO` / `DAT0` | Port B, Pin 3 (gelbe Ader) | schwarz | **G2** |
| `CS`   / `SS` | Port B, Pin 4 (weiße Ader) | schwarz | **G1** |
| `GND` | Port A **oder** B, GND | beide | GND |
| `VCC` | siehe Abschnitt Spannung | | |

Grove HY2.0-4P: `1 = GND` (schwarz), `2 = 5V` (rot), `3` (gelb), `4` (weiß).
Port A: gelb = G13, weiß = G15. **Port B: gelb = G2, weiß = G1** – hier liegt
die kleinere Nummer außen. Bis v1.2.1 stand das in der Doku vertauscht.

Die Pins stehen als Konstanten ganz oben in `src/main.cpp` und lassen sich dort
in einer Zeile ändern:

```cpp
constexpr int kSdSckPin  = 15;   // Port A, Pin "SCL"
constexpr int kSdMosiPin = 13;   // Port A, Pin "SDA"
constexpr int kSdMisoPin = 2;    // Port B, Pin 3 (gelbe Ader)
constexpr int kSdCsPin   = 1;    // Port B, Pin 4 (weisse Ader)
```

### Spannung – bitte einmal genau hinsehen

Die Grove-Buchsen liefern **5 V**. Micro-SD-Karten arbeiten mit **3,3 V**.
Welche der beiden Varianten du hast, erkennst du am Modul:

* **Modul mit Spannungsregler und Pegelwandler** (meist ein kleiner
  `AMS1117-3.3` und ein `74LVC125`, Platine typisch 42 × 24 mm):
  → `VCC` darf direkt an die **5 V** der Grove-Buchse.

* **Reines Breakout ohne Regler** (18,5 × 17,5 mm, wie in den beiden PDFs im
  Ordner `Infos-mad/SD-Card-Modul-doku/` beschrieben – dort steht ausdrücklich
  „ensure your microcontroller's logic level matches"):
  → `VCC` **muss** an 3,3 V. Am einfachsten ein winziger LDO (HT7333, MCP1700,
  AMS1117-3.3) zwischen die 5 V der Grove-Buchse und `VCC` des Moduls.
  Alternativ 3,3 V direkt am StampS3 abgreifen.

Die Signalleitungen sind unkritisch: der ESP32-S3 gibt 3,3 V aus, und ein
Breakout ohne Pegelwandler reicht das 1:1 an die Karte weiter – genau richtig.
Gefährlich wird nur eine 5-V-Versorgung eines Moduls ohne Regler.

### Praxistipps

* Grove-Kabel **kurz** halten. Der Code probiert automatisch 20 MHz, dann
  4 MHz, dann 1 MHz – bei langen Leitungen landet er auf der langsamen Stufe.
* Karte als **FAT32** formatieren (bis 32 GB unkritisch).
* Ist die Karte erkannt, steht unten am Displayrand ein grünes **SD**.
* Karte bitte nicht ziehen, während ein Upload läuft.

---

## 3. RFID / NFC

Der M5Dial hat den Leser **bereits eingebaut**: ein WS1850S am internen
I2C-Bus (G11 = SDA, G12 = SCL, Adresse `0x28`). Es ist also keine Unit RFID2
und keine Verkabelung nötig – die Karte wird einfach vorne aufs Gehäuse gelegt.
Wird der Leser erkannt, steht unten am Displayrand ein grünes **NFC**.

Kartenformat und Bedienung sind **identisch zur Core-Version**, dieselbe Karte
funktioniert an beiden Geräten (und am TeensyROM bzw. Zaparoo/TapTo):

* ein einzelner **NDEF-Text-Record** (Well Known, UTF-8)
* Inhalt = Pfad zur Datei, z. B. `SD:OneLoad v5/Bubble Bobble.crt`
* Präfixe `SD:`, `USB:`, `TR:` oder gar keins
* `?` als Dateiname oder ein reiner Verzeichnispfad → **Zufallsstart**
* unterstützt **MIFARE Classic** 1K/4K/Mini und **NTAG213/215/216** / Ultralight
* das alte Rohformat `C64UPATH` wird beim Lesen weiterhin erkannt

**Karte einfach auflegen.** Auf dem Startbild und im Ring-Menü fragt die Firmware
den Leser im Hintergrund ab. Wird eine Karte erkannt, wechselt sie von selbst in
den Lesemodus und startet das hinterlegte Programm — der Menüpunkt *RFID / NFC*
ist dafür nicht nötig. Den Abstand stellst du unter *Settings → Auto-NFC* ein
(*Off*, 1.5s, 0.7s, 0.3s; Werkseinstellung 0.7s).

Damit das nicht bremst, wird für die reine Anwesenheitsprobe das Zeitfenster des
MFRC522 von 25 ms auf rund 2 ms verkürzt und danach sofort wiederhergestellt —
eine Karte antwortet in unter 0,1 ms, alle anderen Vorgänge behalten das volle
Zeitfenster. Grundlast bei 0.7s: unter ein Prozent.

**Befehlskarten.** Eine Karte kann statt eines Dateipfads einen Befehl tragen,
der sofort ausgeführt wird — ohne Menü und ohne SD-Karte:

```
CMD:RESET      CMD:REBOOT      CMD:MENU
CMD:POWEROFF=0     sofort ausschalten
CMD:POWEROFF=8     nachfragen, 8 s Zeit zum Bestätigen
CMD:M5OFF          den M5Dial selbst ausschalten
CMD:CPU=10         CPU auf 10 MHz
CMD:JOY            Joystickports umschalten (Normal <-> Swapped)
CMD:JOY=SWAPPED    Ports fest setzen; auch NORMAL, WASD1, WASD2
```

Die Wartezeit steht also **auf der Karte**, `0` heißt ohne Nachfrage. Bestätigt
wird durch erneutes Auflegen derselben Karte (die UID muss passen) oder per
Taste; eine andere Karte oder ein abgelaufener Countdown brechen ab. Angelegt
werden solche Karten über *Settings → NFC-Cmd*, die Liste enthält neben den
festen Befehlen die Joystick-Belegungen und alle CPU-Stufen, die dein c64u
anbietet (rechts als *JOY* bzw. *CPU* gekennzeichnet).

Fünf weitere Funktionen stecken im Menü **Settings** ganz oben:

| Eintrag | Wirkung |
|---|---|
| `NFC-Write` | Datei im SD-Browser wählen → Karte auflegen → Pfad wird geschrieben |
| `NFC-Info` | Karte auflegen → UID, Typ, Speicher, Inhalt, Rohdaten (scrollbar) |
| `NFC-Dump` | Karteninhalt nach `/NFC-DUMPS/<uid>.nfc` auf die SD sichern |
| `NFC-Restore` | Dump auswählen → Karte auflegen → Inhalt zurückschreiben |

Beim Restore bleiben UID, Lock-Bytes und Konfigurationsseiten unangetastet –
eine Karte kann dadurch nicht unbrauchbar werden. Sektor-Trailer werden nur
geschrieben, wenn ihre Zugriffsbits in sich stimmig sind.

---

## 3a. WLAN einrichten

Zugangsdaten müssen nicht mehr einkompiliert werden. Sie liegen im internen
Speicher (NVS); `build_env.h` liefert nur die Startwerte, solange dort noch
nichts steht. Bis zu **vier Netze** lassen sich hinterlegen – beim Verbinden
werden sie der Reihe nach durchprobiert, nach dem Einschalten beginnt der
M5Dial mit dem stärksten bekannten.

Alles steckt unter *Settings → WLAN*:

| Eintrag | Wirkung |
|---|---|
| `Netz suchen` | Umgebung scannen, Netz aus der Liste wählen |
| `Von SD laden` | `/wifi.txt` von der microSD einlesen |
| `Setup-Portal` | eigener Accesspoint mit Weboberfläche |
| `Gespeichert` | gespeichertes Netz auswählen und verbinden |
| `Auf NFC-Karte` | gespeichertes Netz auf eine NFC-Karte schreiben |
| `Auf SD sichern` | alle gespeicherten Netze als `/wifi.txt` auf die microSD schreiben |
| `Netz löschen` | einzelnen Eintrag entfernen |
| `Alle löschen` | alle Zugangsdaten verwerfen |

### Weg 1: NFC-Karte

Nach *Netz suchen* → Netz auswählen fragt der M5Dial nach dem Passwort und
wartet auf eine Karte. Auf der Karte steht ein ganz normaler **NDEF-Text-Record**:

```
WIFI:S:MeinWLAN;T:WPA;P:MeinPasswort;;
```

Das ist dasselbe Schema wie bei WLAN-QR-Codes. Steht auf der Karte nur ein
Passwort ohne `WIFI:`-Präfix, wird es dem vorher gewählten Netz zugeordnet.

Eine vollständige `WIFI:`-Karte wirkt auch **außerhalb** der Einrichtung: wird
sie auf dem Startbild oder im Ring-Menü aufgelegt, speichert der M5Dial das Netz
und verbindet sich sofort damit. Bis zu acht Sekunden wartet er auf die
Verbindung und meldet dann `WLAN AKTIV` mit IP-Adresse bzw. `NETZ NICHT DA`,
wenn das Netz nicht in Reichweite ist. Ist das Netz bereits verbunden, passiert
nichts (`SCHON VERBUNDEN`) – die Karte darf also liegen bleiben.

Geschrieben wird die Karte mit dem Handy, z. B. mit **NFC Tools**
(Android und iOS): *Schreiben → Datensatz hinzufügen → Text*. Wichtig für
iPhone-Nutzer: iOS beschreibt nur NTAG213/215/216 bzw. Ultralight, **kein**
MIFARE Classic.

> Das Passwort steht unverschlüsselt auf der Karte und ist mit jedem Handy
> lesbar. Nach dem Einlernen die Karte am besten wieder überschreiben – im
> Gerät liegt es danach ohnehin im NVS.

### Weg 2: `/wifi.txt` auf der SD-Karte

Vorlage: `wifi.txt.example`. Die Datei wird beim Start automatisch gelesen,
solange noch kein Netz gespeichert ist, und jederzeit über
*Settings → WLAN → Von SD laden*.

Umgekehrt schreibt *Settings → WLAN → Auf SD sichern* alle gespeicherten Netze
samt `host` und `hostpass` als `/wifi.txt` auf die Karte. Eine dort schon
vorhandene `wifi.txt` wird vorher nach `wifi.bak` umbenannt. Damit lässt sich
ein zweites Gerät ohne Tipperei einrichten – die Passwörter stehen dabei im
Klartext auf der Karte.

```
ssid = MeinWLAN
pass = MeinWlanPasswort

ssid = Hotspot
pass = geheim123

host     = 192.168.0.64
hostpass =
```

Jede `ssid`-Zeile beginnt einen neuen Eintrag; `#` und `;` leiten Kommentare
ein. `host` und `hostpass` sind optional und setzen die c64u-Adresse.

### Weg 3: Setup-Portal

*Settings → WLAN → Setup-Portal* macht einen eigenen Accesspoint auf:

| | |
|---|---|
| Netz | `C64uRemote-Setup` |
| Passwort | `c64ultimate` |
| Adresse | `192.168.4.1` (Captive Portal, öffnet sich meist von selbst) |

SSID und Passwort stehen währenddessen auf dem Display. Im Browser lassen
sich Netz, Passwort und optional die c64u-Adresse eintragen; nach dem
Speichern schaltet der M5Dial den Accesspoint ab und verbindet sich. Ohne
Zugriff endet das Portal nach fünf Minuten von selbst.

### Direktmodus (ohne Router)

Für Treffen ohne WLAN: **Settings → WLAN → Direktmodus**. Der M5Dial spannt dann selbst
das Netz `C64uRemote-Direct` (Passwort `c64ultimate`) auf und hat die Adresse
`192.168.4.1`; der c64u bekommt per DHCP die `192.168.4.64`. Am c64u wird das
Netz einmal eingetragen – er kann sich nur eines merken. *Direkt-Netz* schaltet
auf `192.168.2.x` um. In der `wifi.txt` heißen die Zeilen `direct`, `direct_ssid`,
`direct_pass` und `direct_net`. Einzelheiten stehen im Benutzerhandbuch, Kapitel *Direktmodus*.

---

## 4. Bedienung

| Eingabe | Wirkung |
|---|---|
| **Drehen** | Auf dem Home-Screen: Ring-Menü öffnen. Sonst: Auswahl bewegen bzw. scrollen |
| **Taste kurz** | Auswählen / Aktion ausführen |
| **Taste lang** | Eine Ebene zurück. Auf dem Home-Screen: frei belegbares Kürzel (Standard *Reset*) |
| **Touch Mitte (Logo)** | Ring-Menü → Home-Screen |
| **Touch auf den Ring** | Menüpunkt direkt ausführen |
| **Touch auf Listenzeile** | Zeile anwählen, zweiter Tipper führt sie aus |
| **Touch auf Titelzeile** | Zurück |
| **Touch lang (Home)** | Zweites frei belegbares Kürzel (Standard *Ultimate Menu*) |

Die Trefferflächen sind bewusst großzügig: im Ring-Menü zählt zuerst ein
direkter Treffer im Umkreis von 30 px um ein Icon, sonst wird über den **Winkel
zur Mitte** das nächstgelegene Icon bestimmt. Damit ist der komplette Ring
außerhalb des Logos aktiv – man muss das Kästchen nicht exakt treffen. Nur die
Mitte (Radius 50 px, also das Logo) führt zurück zum Home-Screen. In Listen
gehört der Zwischenraum zwischen zwei Zeilen zur oberen Zeile, es gibt also
keine toten Streifen.

Falls dir die Zuordnung zu großzügig oder zu knapp ist: die drei Werte stehen
als `kMenuHitRadius`, `kMenuRingInner` und `kMenuCenterRadius` beieinander in
`src/main.cpp`.

Der Home-Screen ist gleichzeitig der Bildschirmschoner: er zeigt das
1MHz-Logo, wahlweise statisch oder mit den Effekten *Water, RotoZoom,
SineWave, Ripple, Raster*. Nach der eingestellten *Home Timeout*-Zeit ohne
Eingabe kehrt das Gerät automatisch dorthin zurück.

### Ring-Menü

`c64u Power Off` · `Reset` · `Reboot` · `Ultimate Menu` · `CPU Speed` ·
`RFID / NFC` · `SD-Karte` · `Joystick Swap` · `Status` · `Settings`

`Joystick Swap` tauscht die Joystickports im c64u (Config *U64 Specific
Settings → Joystick Swapper*, Werte *Normal* / *Swapped* und je nach Firmware
*WASD Port 1* / *WASD Port 2*); jeder Druck schaltet zwischen Normal und
Swapped um. Der frühere Punkt `Connection Test` ist entfallen – dieselbe
Prüfung löst ein Druck auf der `Status`-Seite aus.

`c64u Power Off` fragt immer nach: erst beim **zweiten** Druck innerhalb des
eingestellten Zeitfensters wird der C64 tatsächlich ausgeschaltet. **1,5 s
gedrückt halten** (Taste oder Finger auf dem Symbol) schaltet dagegen den
**M5Dial** aus.

### Settings

WLAN (siehe [Abschnitt 3a](#3a-wlan-einrichten)), *M5Dial Power Off* (siehe unten),
Anzeige (Animationen, Effekt, Tempo, Dauer, Helligkeit), Bedienung
(Encoder-Empfindlichkeit, Home-Timeout, Tastenkürzel, PowerOff-Abfrage und
-Zeitfenster), Upload (*Disk Action*: nur mounten / mounten + Reset /
mounten + Reset + `LOAD"*",8,1` + `RUN`; *Disk Drive*: Laufwerk automatisch
über Bus 8 suchen oder fest A/B), *Joystick* (Portbelegung im c64u, schaltet
durch alle vom Gerät gemeldeten Werte), Beep und Factory Reset.

### Akku und Ausschalten

An der Akkubuchse (1,25 mm, 2-polig) kann ein Li-Ion-/LiPo-Akku 3,7 V hängen;
geladen wird per USB. *Settings → M5Dial Power Off* (zweimal drücken), 1,5 s
Halten auf *c64u Power Off* oder eine Karte `CMD:M5OFF` schalten den M5Dial ab (G46/HOLD auf LOW, Ruhestrom laut M5Stack rund 2 µA). Die Taste
schaltet wieder ein. Am USB-Kabel schläft das Gerät stattdessen, bis die Taste
gedrückt wird. Eine Spannungsanzeige gibt es nicht – der M5Dial hat keine
Messleitung zum Akku (Details in der technischen Dokumentation).

---

## 5. Was der SD-Browser startet

| Endung | Weg |
|---|---|
| `.prg` | `/v1/runners:run_prg` |
| `.crt` | `/v1/runners:run_crt` |
| `.sid` | `/v1/runners:sidplay` |
| `.mod` | `/v1/runners:modplay` |
| `.d64` `.d71` `.d81` `.g64` `.g71` | `/v1/drives/<a\|b>:mount`, danach je nach *Disk Action* Reset und Autostart |

Der Upload läuft **streamend** in 1-kB-Blöcken direkt von der SD zum c64u –
auch ein 175-kB-.d64 braucht deshalb keinen Puffer in Dateigröße. Beim
Autostart wird nicht blind gewartet, sondern `$CC` (BLNSW) im C64-Speicher
abgefragt: blinkt der Cursor, ist BASIC bereit.

---

## 6. Gegenüber dem alten M5Dial-Code

Der ursprüngliche Entwurf (`../src.M5Dial/main.cpp`, 1634 Zeilen) konnte nur
die sieben Basiskommandos. Neu bzw. überarbeitet:

* **SD-Browser, Upload und Autostart** komplett neu
* **RFID/NFC** komplett neu (lesen, schreiben, Info, Dump, Restore)
* **Logo-Effekte** aus der Core-Version übernommen: das Logo ist exakt
  240 × 135 Pixel groß und wird jetzt **1:1** dargestellt statt skaliert –
  das spart pro Pixel zwei Divisionen und sieht schärfer aus
* **Kreismaske** wird per Sehnenbreite (`chordHalfWidth`) berechnet, nicht
  mehr über eine Abstandsprüfung pro Pixel
* **Text bricht am runden Rand nicht mehr ab**: jede Zeile bekommt ihre
  maximale Breite aus der Sehnenbreite auf ihrer Höhe
* **Offscreen-Puffer mit Rückfalllösung**: schlägt `createSprite` mangels RAM
  fehl, zeichnet der Code direkt ins Display weiter statt abzustürzen
* **Ring-Menü** auf zehn Einträge erweitert, drei neue Piktogramme
* Einstellungen liegen unter einem eigenen NVS-Namensraum (`c64udial`), die
  alten Werte des Vorgängers stören also nicht

---

## 7. Fehlersuche

| Symptom | Ursache / Abhilfe |
|---|---|
| `KEINE SD-KARTE` | Verkabelung prüfen, Karte FAT32, Modul-Versorgung (3,3 V?) prüfen, Grove-Kabel kürzen |
| `KEINE SD-KARTE` nach Aufbau laut Doku bis v1.2.1 | Port B: gelbe und weiße Ader tauschen – gelb = MISO (G2), weiß = CS (G1) |
| `KEIN NFC-LESER` | Serielle Ausgabe ansehen: `RFID (I2C 0x28) VersionReg = …`. `0x00`/`0xFF` heißt: kein Kontakt zum internen Bus |
| `SETTINGS > WLAN` | noch kein WLAN gespeichert – über *Settings → WLAN* einrichten |
| `c64u-ADRESSE FEHLT` | WLAN steht, aber die Zieladresse fehlt – Setup-Portal oder `host =` in `wifi.txt` |
| `FALSCHE KARTE` | beim Passwort-Dialog lag eine Programm- oder Befehlskarte auf |
| `AUTH?` in der Statusanzeige | c64u erreichbar, aber Passwort falsch → `C64U_TARGET_PASSWORD` |
| Anzeige ruckelt | *FX* auf `Static` stellen oder Effektdauer verkürzen |
| Menü springt beim Drehen | *Encoder Steps* höher stellen (2 → 4 → 6) |
| Touch reagiert manchmal nicht | Der regelmäßige Verbindungstest blockiert die Schleife bis zu zwei Sekunden. Er läuft deshalb nur noch im Leerlauf (Home/Status), nicht während der Bedienung |
| `Kein Speicher für den Offscreen-Puffer` im Log | Heap knapp – funktioniert weiter, ist nur langsamer |

---

## Entstehung

Portierung, Erweiterungen und Handbücher sind mit Unterstützung von
Claude (Anthropic) entstanden. Konzept, Idee, Hardware-Entscheidungen und
sämtliche Tests auf den echten Geräten: Martin Oswald (@mad).

---

## Lizenz

Dieses Projekt steht unter der **MIT-Lizenz**. Der vollständige Text liegt in
[`LICENSE`](LICENSE) im Projektstamm.

Ursprung ist das Projekt
[C64uRemote von Karl Prosser (@klumsy)](https://github.com/ReadyOS-C64/C64uRemote),
das er unter der
[MIT-Lizenz](https://github.com/ReadyOS-C64/C64uRemote/blob/main/LICENSE)
veröffentlicht hat. Diese Fassung für den M5Dial ist eine daraus abgeleitete
Erweiterung und steht unter denselben Bedingungen:

* Copyright (c) 2026 Karl Prosser – Originalprojekt
* Copyright (c) 2026 Martin Oswald (@mad, [1MHz.de](https://1MHz.de)) – Portierung und Erweiterungen

Die MIT-Lizenz erlaubt Benutzung, Veränderung und Weitergabe – auch
kommerziell. Einzige Bedingung: **Copyright-Vermerk und Lizenztext müssen
erhalten bleiben**, also in jeder Kopie oder abgeleiteten Fassung mitgeliefert
werden. Eine Gewährleistung gibt es nicht.

### Fremde Bestandteile

Die eingebundenen Bibliotheken haben ihre eigenen Lizenzen und Copyright-Inhaber:

| Bibliothek | Lizenz |
|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | MIT, (c) M5Stack |
| [M5GFX](https://github.com/m5stack/M5GFX) | MIT, (c) M5Stack |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT, (c) Benoit Blanchon |
| [MFRC522_I2C](https://github.com/kkloesener/MFRC522_I2C) | siehe Repository |

Sie werden von PlatformIO beim Bauen geladen und liegen diesem Archiv nicht bei.
