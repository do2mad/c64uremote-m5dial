# Änderungen / Changelog

C64uRemote für den **M5Dial**. Neueste Version zuerst.

## v1.3.0 – 2026-09-22

### Deutsch

**Korrektur der SD-Verdrahtung (Port B).** In der Dokumentation waren an Port B
die beiden Signalpins vertauscht. Richtig ist: **gelbe Ader (Pin 3) = G2 =
MISO**, **weiße Ader (Pin 4) = G1 = CS**. Wer nach der alten Anleitung verdrahtet
hat, bekommt *KEINE SD-KARTE* und muss nur diese zwei Adern tauschen. Die
Firmware selbst war richtig; korrigiert sind technische Dokumentation, README
und die Kommentare in `main.cpp`.

**Den M5Dial selbst ausschalten – drei Wege.**

- Im Ring-Menü auf **c64u Power Off** die Taste oder das Symbol **1,5 s
  halten**. Kürzer gedrückt bleibt es das gewohnte Ausschalten des C64 mit
  Abfrage.
- **Einstellungen → M5Dial Power Off**, direkt nach *WLAN*, zweimal drücken.
- **Befehlskarte `CMD:M5OFF`** (in *NFC-Cmd* als *M5Dial Power Off*). In den
  ersten 8 s nach dem Start wird sie ignoriert, damit eine aufliegende Karte das
  Gerät nicht sofort wieder abschaltet.

Im Akkubetrieb geht G46/HOLD auf LOW (Ruhestrom rund 2 µA); die Taste schaltet
wieder ein. Am USB-Kabel schläft das Gerät stattdessen, bis die Taste gedrückt
wird. Neues Kapitel *Akkubetrieb und Ausschalten* in beiden Handbüchern.

**Klarere Texte.** Das Menüsymbol heißt jetzt *c64u Power Off*, die Abfragen
*c64u OFF? NOCHMAL!* bzw. *M5DIAL OFF? NOCHMAL!* – so ist immer klar, welches
Gerät ausgeht.

**Verbindungsstatus im Ring-Menü sichtbar.** Der grüne Statuspunkt oben lag
unter dem Power-Off-Symbol. Im Ring-Menü zeigt jetzt das Status-Symbol (i) die
Verbindungsfarbe (grün / blau / gelb / rot).

Hinweis: v1.3.0 gibt es nur für den M5Dial.

### English

**SD wiring correction (Port B).** The documentation had the two signal pins on
Port B swapped. Correct is: **yellow wire (pin 3) = G2 = MISO**, **white wire
(pin 4) = G1 = CS**. Anyone who wired it according to the old instructions gets
*KEINE SD-KARTE* and only has to swap these two wires. The firmware itself was
right; fixed are the technical documentation, README and the comments in
`main.cpp`.

**Switching the M5Dial itself off - three ways.**

- In the ring menu on **c64u Power Off**, **hold** the button or the icon for
  **1.5 s**. A shorter press stays the usual C64 power-off with prompt.
- **Settings → M5Dial Power Off**, directly after *WiFi*, press twice.
- **Command card `CMD:M5OFF`** (listed in *NFC-Cmd* as *M5Dial Power Off*). It
  is ignored for the first 8 s after start, so a card lying on the device does
  not switch it straight off again.

On battery G46/HOLD goes LOW (about 2 µA standby); the button switches it back
on. With the USB cable attached the device sleeps instead until the button is
pressed. New chapter *Battery operation and switching off* in both manuals.

**Clearer texts.** The menu icon is now called *c64u Power Off*, the prompts
*c64u OFF? AGAIN!* and *M5DIAL OFF? AGAIN!* - so it is always clear which device
goes off.

**Connection status visible in the ring menu.** The green status dot at the top
sat under the Power Off icon. In the ring menu the Status icon (i) now shows the
connection colour (green / blue / yellow / red).

Note: v1.3.0 exists for the M5Dial only.

## v1.2.1 – 2026-09-04

### Deutsch

**Stabilere Verbindung zum c64u.** Der HTTP-Server der Ultimate-Firmware weist
gelegentlich eine Verbindung ab („connection refused"), auch wenn Netz und
Adresse in Ordnung sind. Das führte bisher sofort zu *FAILED* und zu *Not
reached* in der Statuszeile.

- Ein abgewiesener Aufruf wird nach kurzer Pause **einmal automatisch
  wiederholt**. Nur bei Transportfehlern – dann ist beim c64u nichts
  angekommen, ein Befehl kann sich also nicht doppeln.
- Der zyklische Verbindungstest kostet nur noch **eine statt zwei Anfragen**,
  wenn kein Passwort hinterlegt ist. Die zweite war byte-gleich mit der ersten.
- Beim **Wiederverbinden** wird zuerst wieder das Netz versucht, mit dem es
  zuletzt geklappt hat. Sind zwei Netze gespeichert und nur eines ist
  erreichbar, wurde vorher nach jedem Aussetzer jedes zweite Mal zehn Sekunden
  am toten Netz gewartet.
- Ein Tastendruck wird **sofort** mit einem kurzen Ton bestätigt; die
  Rückmeldung über Erfolg oder Fehler kommt danach. Vorher piepte das Gerät
  erst nach dem Netzwerkaufruf – bei zähem Netz wirkte es dadurch, als sei die
  Taste nicht angekommen.

Hinweis: v1.2.0 gab es nur für Core und CoreS3 (Akkuanzeige). Ab dieser Version
laufen wieder alle vier Geräte auf demselben Stand.

### English

**More robust connection to the c64u.** The HTTP server of the Ultimate firmware
occasionally refuses a connection ("connection refused") even though network and
address are fine. Until now that immediately produced *FAILED* and *Not reached*
in the status line.

- A refused call is **retried once automatically** after a short pause. Only on
  transport errors - nothing reached the c64u then, so a command cannot be
  doubled.
- The periodic connection test now costs **one request instead of two** when no
  password is stored. The second one was byte-identical to the first.
- When **reconnecting**, the network that last worked is tried first. With two
  networks stored of which only one is reachable, every other reconnect used to
  waste ten seconds on the dead one.
- A key press is acknowledged **immediately** with a short tone; the result,
  success or failure, follows afterwards. Previously the device only beeped
  after the network call - on a sluggish network that made it look as if the
  press had been lost.

Note: v1.2.0 existed for the Core and CoreS3 only (battery indicator). From this
version on all four devices are back on the same footing.

## v1.1.0 – 2026-09-03

### Deutsch

**Neu: Joystickports am C64 tauschen.** Manche Spiele wollen den Joystick in
Port 1, andere in Port 2 – jetzt lässt sich das umschalten, ohne das Kabel
umzustecken.

- Der Menüpunkt **Connection Test** ist zu **Joystick Swap** geworden (mit eigenem Symbol). Die gleiche Prüfung löst weiterhin ein Druck auf der *Status*-Seite aus. Jedes Auslösen schaltet zwischen *Normal* und *Swapped* um.
- Neue Einstellung **Joystick** (hinter *Disk Drive*): zeigt den aktuellen Stand und
  schaltet durch **alle** Werte, die der c64u meldet – je nach Firmware auch
  *WASD Port 1* und *WASD Port 2*.
- Neue Befehlskarten: `CMD:JOY` schaltet um, `CMD:JOY=NORMAL`, `=SWAPPED`,
  `=WASD1`, `=WASD2` setzen fest. Beim Anlegen einer Karte stehen die
  Joystick-Werte zwischen den festen Befehlen und den CPU-Stufen; das Kürzel
  rechts (*JOY* / *CPU*) trennt die beiden Blöcke.
- Die Aktion lässt sich zusätzlich als Kurzbefehl auf einen langen
  Tastendruck legen (*Joy Swap*).
- Handbücher und README auf den neuen Stand gebracht.

**Hintergrund:** Die ReST-API der Ultimate-Firmware hat dafür keinen eigenen
`machine:`-Befehl. Die Belegung ist ein Konfigurationseintrag – im Test
*Joystick Swapper* in der Kategorie *U64 Specific Settings*. Gesetzt wird sie
über `PUT /v1/configs/<Kategorie>/<Eintrag>?value=…`, genau wie die CPU-Stufe.
Kategorie und Eintragsname sucht die Firmware zur Laufzeit (Schlüsselwort
„Joystick"), damit eine Umbenennung in einer künftigen Ultimate-Version nichts
kaputt macht.

### English

**New: swap the joystick ports on the C64.** Some games want the joystick in
port 1, others in port 2 – this can now be toggled without moving the cable.

- The menu entry **Connection Test** has become **Joystick Swap** (with its own icon). The same check is still triggered by a press on the *Status* page. Every trigger toggles between *Normal* and *Swapped*.
- New setting **Joystick** (after *Disk Drive*): shows the current state and steps
  through **every** value the c64u reports – depending on the firmware also
  *WASD Port 1* and *WASD Port 2*.
- New command cards: `CMD:JOY` toggles, `CMD:JOY=NORMAL`, `=SWAPPED`, `=WASD1`,
  `=WASD2` set a fixed value. When writing a card the joystick values sit
  between the fixed commands and the CPU steps; the tag on the right
  (*JOY* / *CPU*) tells the two blocks apart.
- The action can also be assigned to a long press as a shortcut
  (*Joy Swap*).
- Manuals and README brought up to date.

**Background:** the ReST API of the Ultimate firmware has no dedicated
`machine:` command for this. The mapping is a configuration item – in testing
*Joystick Swapper* in the category *U64 Specific Settings*. It is set through
`PUT /v1/configs/<category>/<item>?value=…`, exactly like the CPU speed. The
firmware looks the category and item name up at runtime (keyword "Joystick") so
that a renaming in a future Ultimate version does not break anything.

## v1.0.0 – 2026-08-24

Erste Veröffentlichung als eigenes Repository: Firmware in deutscher und
englischer Fassung, Handbücher als PDF, MIT-Lizenz.

First release as its own repository: firmware in a German and an English
edition, manuals as PDF, MIT licence.
