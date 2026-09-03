# Änderungen / Changelog

C64uRemote für den **M5Dial**. Neueste Version zuerst.

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
