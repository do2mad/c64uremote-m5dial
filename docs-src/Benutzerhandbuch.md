% C64uRemote für M5Dial
% Benutzerhandbuch
% Version 1.0

# Willkommen

C64uRemote verwandelt einen **M5Dial** in eine komfortable Fernbedienung für den
**Commodore 64 Ultimate (c64u)** und den **Ultimate64 Elite-II**. Über das WLAN
steuerst du den Rechner fern: Reset, Reboot, Ausschalten, das Ultimate-Menü
öffnen und die CPU-Geschwindigkeit umstellen.

Der M5Dial bringt einen **NFC-Leser schon ab Werk mit**. Zusammen mit einer
nachgerüsteten **microSD-Karte** wird daraus eine Spielekonsole zum Auflegen: Du
hältst eine NFC-Karte an das Gerät, und das zugehörige Spiel startet auf dem C64.
Die Karten sind kompatibel zum TeensyROM- und Zaparoo-Format – dieselbe Karte
funktioniert an beiden Systemen und an der M5Stack-Core-Fassung dieses Projekts.

Dieses Projekt baut auf der ursprünglichen Idee von Karl Prosser (@klumsy) auf
und wurde von Martin Oswald (@mad, 1MHz.de) für den M5Dial erweitert.

# Das brauchst du

**Zwingend erforderlich:**

- Einen M5Dial
- Einen Commodore 64 Ultimate bzw. Ultimate64 Elite-II im selben WLAN

**Optional, für Spiele von der Speicherkarte:**

- Ein microSD-Modul, angeschlossen an Port A und Port B
  (Einbau siehe technische Dokumentation)
- Eine microSD-Karte, FAT32 formatiert
- NFC-Karten: empfohlen NTAG215, es gehen auch NTAG213/216 und MIFARE Classic

Ohne SD-Karte laufen alle übrigen Funktionen normal weiter. Der NFC-Leser ist
immer vorhanden, ohne SD-Karte kann er aber nichts starten.

# Erste Einrichtung

Damit der M5Dial deinen C64 findet, müssen einmalig die WLAN-Zugangsdaten und
die Adresse des C64 hinterlegt werden. Das geht **direkt am Gerät** unter
*Settings → WLAN* – der Quelltext muss dafür nicht mehr angefasst werden. Die
Daten landen im internen Speicher und überstehen jeden Neustart. Wie das im
Einzelnen läuft, steht im Kapitel *WLAN einrichten*.

Wer die Zugangsdaten lieber schon beim Programmieren mitgibt, trägt sie
weiterhin in `build_env.h` ein (siehe technische Dokumentation). Sie gelten dann
als Startwerte für den allerersten Start.

Der Verbindungsstatus ist immer sichtbar: ein kleiner **Punkt ganz oben** im
runden Display.

| Punkt | Bedeutung |
|---|---|
| **grün** | Alles verbunden, bereit |
| **blau** | WLAN da, aber der C64 antwortet nicht |
| **gelb** | C64 erreichbar, aber das Passwort stimmt nicht |
| **rot** | Keine WLAN-Verbindung |

Am unteren Rand stehen auf allen Menüseiten zwei Kürzel: **NFC** und **SD**.
Grün bedeutet erkannt, grau bedeutet nicht vorhanden. Ausführliche Angaben
findest du unter *Status*.

# Bedienung

Der M5Dial hat drei Eingabemöglichkeiten: den **Drehring**, die **Taste**
(auf den Ring drücken) und den **Touchscreen**.

| Eingabe | Wirkung |
|---|---|
| **Drehen** | Auf dem Startbild: Menü öffnen. Sonst: Auswahl bewegen bzw. blättern |
| **Taste kurz** | Auswählen, bestätigen, ausführen |
| **Taste lang** (ab 0,6 s) | Eine Ebene zurück. Auf dem Startbild: Sonderfunktion |
| **Touch auf den Ring** | Menüpunkt direkt ausführen |
| **Touch auf die Mitte** | Vom Menü zurück zum Startbild |
| **Touch auf eine Zeile** | Zeile anwählen, zweiter Tipper führt sie aus |
| **Touch auf die Überschrift** | Zurück |
| **Touch lang** (Startbild, Menü) | Zweite Sonderfunktion |

Du musst die Symbole nicht genau treffen: Der gesamte äußere Ring ist aktiv, das
Gerät nimmt das Symbol, das deinem Finger am nächsten liegt. Nur die Mitte mit
dem Logo führt zurück zum Startbild.

# Das Startbild

Das Startbild zeigt das 1MHz-Logo – wahlweise ruhig oder mit wechselnden
Effekten (*Water*, *RotoZoom*, *SineWave*, *Ripple*, *Raster*). Es ist zugleich
der Bildschirmschoner: Nach der eingestellten Wartezeit ohne Bedienung kehrt das
Gerät automatisch dorthin zurück.

Eine Drehung, ein Tastendruck oder ein Tipper öffnet das Menü.

**Zwei Abkürzungen ohne Umweg über das Menü:** Auf dem Startbild löst ein langer
Tastendruck eine frei wählbare Aktion aus (ab Werk *Reset*), ein langes Berühren
des Displays eine zweite (ab Werk *Ultimate Menu*). Beide stellst du unter
*Settings* ein. Das lange Berühren wirkt zusätzlich im Ring-Menü, du musst dafür
also nicht erst zum Startbild zurück.

# Das Menü

Zehn Symbole liegen im Kreis, das Logo sitzt in der Mitte. Das ausgewählte
Symbol ist größer und hell umrandet; sein Name erscheint kurz über dem Logo.

| Symbol | Was passiert |
|---|---|
| **PowerOff** | Schaltet den C64 aus – zur Sicherheit zweimal drücken |
| **Reset** | Der C64 wird zurückgesetzt (wie die Reset-Taste) |
| **Reboot** | Der C64 startet komplett neu |
| **Ultimate Menu** | Öffnet oder schließt das Ultimate-Menü am C64 |
| **CPU Speed** | CPU-Geschwindigkeit ansehen und ändern |
| **RFID / NFC** | Karte auflegen und das gespeicherte Spiel starten |
| **SD-Karte** | Ein Spiel direkt von der SD-Karte auswählen und starten |
| **Connection Test** | Verbindung zum C64 sofort prüfen |
| **Status** | Ausführliche Verbindungsinfos |
| **Settings** | Einstellungen und NFC-Werkzeuge |

**Ausschalten (PowerOff):** Aus Versehen ausschalten wäre ärgerlich, deshalb
kommt zuerst die Abfrage *POWER OFF? NOCHMAL!*. Erst ein zweiter Druck innerhalb
des Zeitfensters schaltet wirklich aus.

# CPU-Geschwindigkeit ändern

Im Menü **CPU Speed** wählen. Oben steht die aktuelle Geschwindigkeit, darunter
die Auswahlliste. Mit dem Drehring die gewünschte Stufe wählen, mit der Taste
setzen. Der M5Dial liest die verfügbaren Stufen direkt vom C64 aus – du bekommst
also genau die Werte, die dein Gerät kann.

# Spiele per Karte starten

Voraussetzung: microSD mit deinen Spielen eingelegt und die Karte wurde vorher
beschrieben (siehe unten).

## Einfach auflegen (Automatik)

Im Normalfall musst du gar nichts bedienen: Solange das Startbild oder das Menü
zu sehen ist, schaut der M5Dial im Hintergrund regelmäßig nach, ob eine Karte
aufliegt. Sobald er eine erkennt, wechselt er von selbst in den Lesemodus und
startet das hinterlegte Programm.

Danach bleibt die Leseseite noch stehen, du kannst also gleich die nächste Karte
auflegen. Passiert eine Weile nichts, kehrt das Gerät zum Startbild zurück.

Wie oft nachgeschaut wird, stellst du unter *Settings → Auto-NFC* ein:

| Einstellung | Bedeutung |
|---|---|
| **Off** | keine Hintergrundabfrage, Karten nur über den Menüpunkt |
| **1.5s** | sehr sparsam |
| **0.7s** | Werkseinstellung, guter Kompromiss |
| **0.3s** | reagiert am schnellsten |

Die Abfrage ist so kurz, dass sie selbst in der Stellung *0.3s* weder die
Animation noch die Bedienung merklich bremst.

## Über den Menüpunkt

Willst du bewusst eine Karte einlesen – oder ist *Auto-NFC* ausgeschaltet – geht
es auch von Hand:

1. Im Menü **RFID / NFC** wählen.
2. Die NFC-Karte vorne an den M5Dial halten.
3. Das Gerät liest den gespeicherten Pfad, holt die Datei von der SD-Karte und
   schickt sie an den C64. Ein Fortschrittsbalken zeigt den Upload.
4. Nach *START: …* (bei Diskettenabbildern *LAEUFT: …*) läuft das Spiel.

Karte abnehmen, nächste auflegen – der Bildschirm bleibt im Lesemodus, du kannst
also mehrere Karten hintereinander abspielen.

## Welche Dateien funktionieren

| Endung | Was es ist |
|---|---|
| `.prg` | Programm (wird geladen und gestartet) |
| `.crt` | Modul/Cartridge |
| `.sid` | Musikstück (wird abgespielt) |
| `.mod` | Amiga-Musikmodul |
| `.d64` `.d71` `.d81` | Diskettenabbild |
| `.g64` `.g71` | Diskettenabbild (GCR-Format) |

Diskettenabbilder werden in das Laufwerk eingelegt. Was danach passiert, legst
du unter *Settings → Disk Action* fest: nur einlegen, zusätzlich Reset, oder
zusätzlich das erste Programm laden und starten.

## Zufallsstart

Steht auf der Karte statt eines Dateinamens ein `?` oder nur ein Ordnerpfad,
dann wählt der M5Dial jedes Mal eine zufällige Datei aus diesem Ordner. Praktisch
für eine „Überraschungskarte".

# Befehlskarten

Eine Karte muss nicht auf ein Spiel zeigen – sie kann auch einen **Befehl**
tragen. Aufgelegt löst sie ihn sofort aus, ganz ohne Menü und ohne SD-Karte.

| Karte | Wirkung |
|---|---|
| **Reset** | Setzt den C64 zurück |
| **Reboot** | Startet den C64 komplett neu |
| **Ultimate Menu** | Öffnet oder schließt das Ultimate-Menü |
| **PowerOff direkt** | Schaltet sofort aus |
| **PowerOff mit Abfrage** | Fragt nach – zum Bestätigen die Karte innerhalb des Zeitfensters ein zweites Mal auflegen |
| **CPU x MHz** | Stellt die CPU auf den auf der Karte hinterlegten Wert |

## Eine Befehlskarte anlegen

1. **NFC-Cmd** in den Einstellungen wählen.
2. Aus der Liste den gewünschten Befehl aussuchen. Nach den festen Einträgen
   folgen alle CPU-Stufen, die dein C64 anbietet – eine Karte „CPU 10 MHz" ist
   also ein einziger Klick.
3. Karte auflegen, *KARTE OK* bedeutet: geschrieben und geprüft.

## PowerOff mit Abfrage

Die Wartezeit steht **auf der Karte**, nicht im Gerät. Beim Anlegen wird der
Wert aus *NFC-Cmd PowOff* übernommen (3, 5, 8 oder 15 Sekunden, Werkseinstellung
8 s). Legst du so eine Karte auf, erscheint *POWER OFF? NOCHMAL!* mit einem
Countdown. Zum Ausschalten:

- die **gleiche Karte** noch einmal auflegen, oder
- die **Taste** drücken

Läuft der Countdown ab oder kommt eine andere Karte, passiert nichts. Eine Karte
mit der Zeit **0** schaltet ohne Nachfrage sofort aus.

## Was auf der Karte steht

Der Befehl ist gewöhnlicher Text in einem NDEF-Record – du kannst ihn mit jeder
NFC-App am Telefon ansehen oder selbst schreiben:

```
CMD:RESET
CMD:REBOOT
CMD:MENU
CMD:POWEROFF=0      sofort ausschalten
CMD:POWEROFF=8      nachfragen, 8 Sekunden Zeit
CMD:CPU=10          CPU auf 10 MHz
```

Groß- und Kleinschreibung sind egal. Dasselbe Format verstehen die M5Dial- und
die M5Stack-Core-Fassung, eine Karte läuft also an beiden Geräten.

# Spiele direkt von der SD starten

Ohne Karte geht es genauso: Im Menü **SD-Karte** wählen. Du siehst den Inhalt
der Speicherkarte, gefiltert auf startbare Dateien.

- **Drehen** blättert durch die Liste
- **Taste kurz** öffnet einen Ordner oder startet eine Datei
- **Taste lang** geht einen Ordner zurück, auf oberster Ebene ins Menü
- `..` in der Liste führt ebenfalls einen Ordner nach oben

Rechts neben jedem Eintrag steht die Dateiendung bzw. `dir` für Ordner.

# NFC-Karten beschreiben und verwalten

Die vier NFC-Werkzeuge stehen unter **Settings** ganz oben.

## NFC-Write: Karte mit einem Spiel belegen

1. **Settings → NFC-Write** wählen.
2. Im Dateibrowser das gewünschte Spiel auswählen.
3. Die NFC-Karte an den M5Dial halten.
4. *KARTE OK* bedeutet: geschrieben und geprüft.

Der Pfad wird als NDEF-Textrecord gespeichert, also im gleichen Format, das auch
TeensyROM und Zaparoo verwenden.

## NFC-Info: Karte auslesen

**Settings → NFC-Info** wählen und eine Karte auflegen. Angezeigt werden UID,
Kartentyp, Speichergröße, der gespeicherte Text, der daraus abgeleitete Pfad und
ob die Datei auf der SD-Karte tatsächlich vorhanden ist. Darunter folgen die
Rohdaten der Karte.

Mit dem Drehring blätterst du durch die Liste; ein Tipper auf die obere bzw.
untere Hälfte springt ebenfalls.

## NFC-Dump und NFC-Restore: Karten kopieren

- **NFC-Dump** liest die Karte aus und legt sie als Textdatei unter
  `/NFC-DUMPS/<UID>.nfc` auf der SD-Karte ab.
- **NFC-Restore** schreibt so eine Datei auf eine andere Karte zurück. Dazu
  zuerst die Dump-Datei auswählen, dann die Zielkarte auflegen.

**Wichtig:** Die UID einer Karte ist fest im Chip gebrannt und lässt sich nicht
kopieren. Der *Inhalt* wird kopiert – und darauf kommt es hier an. Bereiche, die
eine Karte dauerhaft sperren könnten, werden bewusst nicht beschrieben.

# WLAN einrichten

Alles dazu steckt unter **Settings → WLAN**. Der M5Dial merkt sich bis zu
**vier Netze** und probiert sie beim Verbinden nacheinander durch – praktisch,
wenn du zwischen Zuhause und einem Handy-Hotspot wechselst.

| Eintrag | Wirkung |
|---|---|
| **Netz suchen** | Umgebung durchsuchen und ein Netz aus der Liste wählen |
| **Von SD laden** | `wifi.txt` von der microSD einlesen |
| **Setup-Portal** | Eigener Accesspoint mit Weboberfläche |
| **Gespeichert** | Gespeichertes Netz auswählen und verbinden |
| **Auf NFC-Karte** | Gespeichertes Netz auf eine NFC-Karte schreiben |
| **Auf SD sichern** | Alle gespeicherten Netze als `wifi.txt` auf die microSD |
| **Netz löschen** | Einen einzelnen Eintrag entfernen |
| **Alle löschen** | Alle Zugangsdaten verwerfen |

## Weg 1: Netz suchen und Passwort per Karte

*Netz suchen* zeigt nach ein paar Sekunden die gefundenen Netze mit ihrer
Signalstärke. Ein bereits gespeichertes Netz ist mit *bekannt* markiert und
verbindet sich sofort, ein offenes Netz mit *offen* braucht kein Passwort.

Bei allen anderen fragt der M5Dial nach dem Passwort und wartet auf eine
NFC-Karte. Auf der Karte steht ein ganz normaler Textrecord im selben Schema,
das auch WLAN-QR-Codes benutzen:

```
WIFI:S:MeinWLAN;T:WPA;P:MeinPasswort;;
```

Geschrieben wird die Karte mit dem Handy, zum Beispiel mit der App **NFC Tools**
(*Schreiben → Datensatz hinzufügen → Text*). Steht auf der Karte nur das
Passwort ohne das `WIFI:`-Präfix, wird es dem vorher gewählten Netz zugeordnet.

> Das Passwort steht unverschlüsselt auf der Karte. Nach dem Einrichten die
> Karte am besten wieder überschreiben – im Gerät liegt es danach ohnehin im
> internen Speicher.

## Karte auflegen genügt

Eine vollständige `WIFI:`-Karte wirkt auch **außerhalb** der Einrichtung: Legst
du sie auf dem Startbild oder im Ring-Menü auf, speichert der M5Dial das Netz
und verbindet sich sofort damit. Er wartet bis zu acht Sekunden und meldet dann
*WLAN AKTIV* mit der IP-Adresse oder *NETZ NICHT DA*, wenn das Netz nicht in
Reichweite ist. Ist das Netz schon verbunden, passiert nichts weiter
(*SCHON VERBUNDEN*) – die Karte darf also liegen bleiben.

So bekommt ein zweites Gerät seine Zugangsdaten in wenigen Sekunden.

## Weg 2: Datei auf der SD-Karte

Lege eine Textdatei `wifi.txt` in das Hauptverzeichnis der microSD:

```
ssid = MeinWLAN
pass = MeinWlanPasswort

ssid = Hotspot
pass = geheim123

host     = 192.168.0.64
hostpass =
```

Jede neue `ssid`-Zeile beginnt einen neuen Eintrag, Zeilen mit `#` sind
Kommentare. Gelesen wird die Datei beim Start (solange noch kein Netz
gespeichert ist) und jederzeit über *Von SD laden*.

Umgekehrt schreibt **Auf SD sichern** alle gespeicherten Netze samt `host` und
`hostpass` wieder in genau dieses Format heraus. Eine schon vorhandene
`wifi.txt` wird dabei nach `wifi.bak` umbenannt, es geht also nichts verloren.
Damit lässt sich ein zweites Gerät ohne Tipperei einrichten – die Passwörter
stehen dabei im Klartext auf der Karte.

## Weg 3: Setup-Portal

*Setup-Portal* macht aus dem M5Dial für fünf Minuten einen eigenen Accesspoint.
Auf dem Display stehen Netzname, Passwort und die Adresse, die du im Browser
öffnest. Dort trägst du SSID und Passwort ein – wahlweise aus der Liste der
gefundenen Netze oder von Hand – und optional Adresse und Passwort des C64.

Nach dem Speichern schaltet der M5Dial den Accesspoint ab und verbindet sich
mit dem neuen Netz. Dass die Browserverbindung dabei abbricht, ist normal.

# Einstellungen im Überblick

Alle Einstellungen werden sofort gespeichert und überstehen einen Neustart.

## NFC-Werkzeuge

| Eintrag | Bedeutung |
|---|---|
| **NFC-Write** | Datei wählen und auf eine Karte schreiben |
| **NFC-Info** | Karte auslesen und alle Angaben anzeigen |
| **NFC-Dump** | Karteninhalt auf die SD sichern |
| **NFC-Restore** | Gesicherten Inhalt auf eine Karte zurückschreiben |
| **NFC-Zufall** | Zufallskarte für ein Verzeichnis anlegen |
| **NFC-Cmd** | Befehlskarte anlegen (siehe Kapitel *Befehlskarten*) |
| **NFC-Cmd PowOff** | Vorgabe für die Abfragezeit einer PowerOff-Befehlskarte: 3, 5, 8, 15 s |
| **WLAN** | Untermenü der WLAN-Einrichtung (siehe Kapitel *WLAN einrichten*) |

## Bedienung

| Eintrag | Bedeutung |
|---|---|
| **Taste lang** | Aktion für langen Tastendruck auf dem Startbild |
| **Touch lang** | Aktion für langes Berühren auf dem Startbild und im Ring-Menü |
| **PowerOff Abfrage** | *Abfrage* = Sonderfunktion PowerOff fragt nach, *Direkt* = schaltet sofort |
| **PowerOff Zeit** | Zeitfenster für die Bestätigung (0,5 bis 3,0 s) |
| **Auto-NFC** | Abstand der Hintergrundabfrage (*Off*, 1.5s, 0.7s, 0.3s) |
| **Home Timeout** | Wartezeit bis zum Startbild (*Off*, 10 s, 20 s, 45 s) |
| **Encoder Steps** | Wie viele Rastschritte ein Menüschritt braucht (1, 2, 4, 6) |

Für *Taste lang* und *Touch lang* stehen zur Wahl: *Off*, *Reset*, *Reboot*,
*Menu* und *PowerOff*.

Reagiert das Menü beim Drehen zu hektisch, stelle *Encoder Steps* höher.

## Anzeige

| Eintrag | Bedeutung |
|---|---|
| **Animations** | Effekte auf dem Startbild ein- oder ausschalten |
| **Effect** | *Auto* wechselt durch alle, sonst fest einer |
| **Anim Speed** | Tempo der Effekte |
| **Effect Time** | Wie lange ein Effekt läuft |
| **Static Time** | Wie lange das ruhige Logo dazwischen steht |
| **Brightness** | Helligkeit in acht Stufen |

## C64-Optionen

| Eintrag | Bedeutung |
|---|---|
| **Disk Action** | *Mount* nur einlegen, *Mnt+Reset* zusätzlich Reset, *Mnt+Run* zusätzlich laden und starten |
| **Disk Drive** | *Auto (8)* sucht das Laufwerk an Bus 8, sonst fest *A* oder *B* |

## Sonstiges

| Eintrag | Bedeutung |
|---|---|
| **Beep** | Bestätigungstöne ein- oder ausschalten |
| **Factory Reset** | Alle Einstellungen auf Werkszustand (WLAN-Daten bleiben) |

# Häufige Fragen

**Der Touch reagiert manchmal nicht.**
Der gesamte äußere Ring ist aktiv, du musst die Symbole also nicht genau treffen.
Reagiert trotzdem nichts, prüfe unter *Status*, ob die Verbindung steht – bei
Netzwerkproblemen wartet das Gerät kurz auf Antwort.

**Die Karte wird nicht von allein erkannt.**
Die Hintergrundabfrage läuft nur auf dem Startbild und im Ring-Menü, nicht in
den Einstellungen oder im Dateibrowser. Prüfe außerdem, ob *Settings → Auto-NFC*
auf *Off* steht.

**Auf dem Startbild passiert beim Drehen nichts außer, dass das Menü aufgeht.**
Das ist Absicht: Die erste Drehung öffnet nur das Menü, damit nicht versehentlich
ein Menüpunkt weiterspringt.

**SD wird nicht erkannt.**
Karte als FAT32 formatieren, Verkabelung und vor allem die Versorgungsspannung
des Moduls prüfen (siehe technische Dokumentation) und die Grove-Kabel kurz
halten.

**Der Upload eines .d64 dauert lange.**
Ein Diskettenabbild ist rund 175 kB groß und wird über WLAN übertragen; einige
Sekunden sind normal. Danach kann das Laden im C64 selbst noch etwas dauern –
der Fortschritt steht im Display.

**Eine Karte wird gelesen, aber es startet nichts.**
Meist stimmt der Pfad auf der Karte nicht mit der SD-Karte überein. *NFC-Info*
zeigt an, ob die Datei gefunden wurde.

**Kann ich eine Karte vom TeensyROM benutzen?**
Ja. Das Kartenformat ist identisch. Nur müssen die Ordner- und Dateinamen auf
deiner SD-Karte gleich heißen.

**Bleiben meine Einstellungen erhalten?**
Ja, sie liegen im Flash-Speicher des M5Dial. Nur *Factory Reset* setzt sie
zurück.

# Lizenz

C64uRemote steht unter der **MIT-Lizenz**. Der vollständige Lizenztext liegt als
Datei `LICENSE` im Projektstamm.

Ursprung ist das Projekt **C64uRemote von Karl Prosser (@klumsy)**,
<https://github.com/ReadyOS-C64/C64uRemote>, das er unter der MIT-Lizenz
veröffentlicht hat. Diese Fassung ist eine daraus abgeleitete Erweiterung und
steht unter denselben Bedingungen:

* Copyright (c) 2026 Karl Prosser – Originalprojekt
* Copyright (c) 2026 Martin Oswald (@mad, <https://1MHz.de>) – Portierung und Erweiterungen

Die MIT-Lizenz erlaubt es, die Software zu benutzen, zu verändern und
weiterzugeben, auch kommerziell. Einzige Bedingung: **Copyright-Vermerk und
Lizenztext müssen erhalten bleiben** und jeder Kopie beiliegen. Eine
Gewährleistung oder Haftung ist ausgeschlossen.

Die eingebundenen Bibliotheken haben ihre eigenen Lizenzen: M5Unified und M5GFX
(MIT, © M5Stack), ArduinoJson (MIT, © Benoit Blanchon) sowie MFRC522_I2C
(<https://github.com/kkloesener/MFRC522_I2C>). Sie werden beim Bauen von
PlatformIO geladen.

# Entstehung

Portierung, Erweiterungen und Handbücher sind mit Unterstützung von Claude
(Anthropic) entstanden. Konzept, Idee, Hardware-Entscheidungen und sämtliche
Tests auf den echten Geräten: Martin Oswald (@mad).
