% C64uRemote für M5Dial
% Kurzanleitung – in zehn Minuten einsatzbereit
% Firmware 1.3.1

# Was du brauchst

- den M5Dial mit eingesteckter microSD-Karte und die NFC-Karten aus dem Paket
- einen **Commodore 64 Ultimate (c64u)** oder Ultimate64 Elite-II
- dein WLAN: Name und Passwort. Der M5Dial funkt **nur im 2,4-GHz-Band** – ein
  reines 5-GHz-Netz sieht er nicht.
- ein Handy oder einen Laptop mit Browser für die Einrichtung

Der M5Dial und der c64u müssen im **selben Netz** hängen. Ein Gäste-WLAN geht
meist nicht, weil der Router die Geräte darin voneinander abschottet.

# Schritt 1: Den c64u ins Netz bringen

Das machst du einmal am C64 selbst, im **Ultimate-Menü**. Du öffnest es mit der
Menütaste am Gerät oder mit **C= und RESTORE** gleichzeitig.

1. **Netzwerk verbinden.**
   *Per WLAN:* **Wi-Fi Network Setup** öffnen, die Liste der Netze anzeigen
   lassen, dein Netz wählen und das Passwort eingeben.
   *Per Kabel:* Netzwerkkabel einstecken, **Wired Network Setup** öffnen.
2. **IP-Adresse notieren.** Im selben Menü steht nach dem Verbinden die aktive
   IP-Adresse, zum Beispiel `192.168.178.47`. **Diese Adresse brauchst du in
   Schritt 3.**
3. **Fernsteuerung einschalten.** Unter **Network Services & Timezone** den
   Eintrag **Web Remote Control Service** auf **Enabled** stellen. Ohne diesen
   Dienst kann der M5Dial den c64u nicht erreichen.
4. **Netzwerkpasswort (optional).** Hast du in den Einstellungen des c64u ein
   *Network Password* vergeben, notiere es ebenfalls. Ohne Passwort bleibt das
   Feld in Schritt 3 einfach leer.

Je nach Firmware-Stand des c64u können die Menüpunkte leicht anders heißen – im
Zweifel hilft Kapitel 11 („Networking and Wi-Fi") im Handbuch des c64u.

> **Tipp: feste Adresse im Router.** Damit der c64u nicht irgendwann eine neue
> Adresse bekommt und der M5Dial ihn nicht mehr findet, lege sie im Router fest.
> Bei einer FRITZ!Box: *Heimnetz → Netzwerk →* beim c64u auf *Bearbeiten* →
> **„Diesem Netzwerkgerät immer die gleiche IPv4-Adresse zuweisen"**.

# Schritt 2: M5Dial einschalten

USB-C-Kabel anstecken oder – mit Akku – auf den Drehring drücken. Beim ersten
Start meldet das Gerät **SETTINGS > WLAN**: Es kennt noch kein Netz. Der kleine
Punkt ganz oben im Display ist rot.

Kurz zur Bedienung: **Drehen** bewegt die Auswahl, **Drücken** auf den Ring wählt
aus, **lang drücken** geht eine Ebene zurück.

# Schritt 3: M5Dial einrichten (Setup-Portal)

1. Am Drehring drehen – das Ring-Menü öffnet sich. **Settings**
   auswählen, dann weiterdrehen bis **WLAN** und drücken.
2. **Setup-Portal** wählen und drücken. Das Display zeigt jetzt:

   | | |
   |---|---|
   | Netzname | `C64uRemote-Setup` |
   | Passwort | `c64ultimate` |
   | Adresse  | `192.168.4.1` |

3. Mit dem Handy oder Laptop in dieses Netz **C64uRemote-Setup** gehen. Meist
   öffnet sich die Einrichtungsseite von selbst; sonst im Browser
   **http://192.168.4.1** aufrufen.
4. Auf der Seite ausfüllen:
   - **Gefundene Netze:** dein WLAN auswählen (oder den Namen von Hand eintragen)
   - **WLAN-Passwort:** das Passwort deines WLANs
   - **c64u-Adresse:** die IP-Adresse aus Schritt 1, z. B. `192.168.178.47`
   - **c64u-Passwort:** nur, wenn du am c64u eines vergeben hast
5. **Speichern und verbinden** antippen. Der M5Dial beendet das Setup-Netz und
   verbindet sich mit deinem WLAN. Dass das Handy dabei die Verbindung verliert,
   ist normal – es geht von selbst zurück in dein eigenes WLAN.

Das Setup-Portal schaltet sich nach fünf Minuten ohne Benutzung von selbst ab.
Dann einfach erneut starten.

# Schritt 4: Prüfen

Nach ein paar Sekunden zeigt der Punkt oben im Display die Verbindung an:

| Punkt | Bedeutung | Was tun? |
|---|---|---|
| **grün** | alles verbunden | fertig! |
| **blau** | WLAN steht, der c64u antwortet nicht | c64u an? Adresse richtig? *Web Remote Control Service* eingeschaltet? |
| **gelb** | c64u erreichbar, Passwort falsch | c64u-Passwort im Setup-Portal korrigieren |
| **rot** | kein WLAN | WLAN-Passwort prüfen, 2,4 GHz eingeschaltet? |

Im Ring-Menü zeigt das Symbol **Status (i)** dieselbe Farbe. Auf der Seite
*Status* stehen alle Einzelheiten: WLAN, IP-Adresse, eingetragene c64u-Adresse,
Erreichbarkeit – und ganz unten die Firmware-Version.

# Losspielen

Eine NFC-Karte flach auf den M5Dial legen, solange das Startbild oder das
Ring-Menü zu sehen ist. Das Spiel wird von der microSD-Karte zum c64u übertragen
und startet von selbst. Die Befehlskarten lösen Reset, Ultimate-Menü und Co. aus.

Alles Weitere – Karten selbst beschreiben, Einstellungen, Ausschalten, Akku –
steht im ausführlichen **Benutzerhandbuch**.

# Andere Wege ins WLAN

Statt des Setup-Portals geht es auch so:

- **Datei auf der microSD:** eine Textdatei `wifi.txt` ins Hauptverzeichnis der
  Karte legen und den M5Dial neu starten (oder *Settings → WLAN → Von SD laden*):

  ```
  ssid     = MeinWLAN
  pass     = MeinWlanPasswort
  host     = 192.168.178.47
  hostpass =
  ```

- **NFC-Karte:** mit einer Handy-App wie *NFC Tools* einen Text
  `WIFI:S:MeinWLAN;T:WPA;P:MeinPasswort;;` auf eine freie Karte schreiben und
  auflegen. Die c64u-Adresse muss dann noch über das Setup-Portal oder die
  `wifi.txt` eingetragen werden.

# Ohne Router: Direktmodus

Auf einem Treffen ohne WLAN spannt der M5Dial selbst ein Netz auf:
*Settings → WLAN → Direktmodus*. Am c64u trägst du einmal das WLAN
`C64uRemote-Direct` mit dem Passwort `c64ultimate` ein; er bekommt dann die
Adresse `192.168.4.64`. Ausführlich steht das im **Benutzerhandbuch**, Kapitel
*Direktmodus*.
