% C64uRemote for M5Dial
% User Manual
% Version 1.0

# Welcome

C64uRemote turns an **M5Dial** into a convenient remote control for the
**Commodore 64 Ultimate (c64u)** and the **Ultimate64 Elite-II**. Over Wi-Fi you
can reset and reboot the machine, power it off, open the Ultimate menu and change
the CPU speed.

The M5Dial ships with an **NFC reader built in**. Together with a retrofitted
**microSD card** it becomes a tap-to-play console: hold an NFC tag against the
device and the matching game starts on the C64. The tags use the TeensyROM and
Zaparoo format, so the same tag works on those systems and on the M5Stack Core
version of this project.

This project builds on the original idea by Karl Prosser (@klumsy) and was
extended for the M5Dial by Martin Oswald (@mad, 1MHz.de).

# What you need

**Required:**

- An M5Dial
- A Commodore 64 Ultimate or Ultimate64 Elite-II on the same Wi-Fi network

**Optional, for launching games from storage:**

- A microSD module wired to Port A and Port B
  (installation is covered in the technical documentation)
- A microSD card formatted as FAT32
- NFC tags: NTAG215 recommended, NTAG213/216 and MIFARE Classic also work

Without an SD card every other function still works. The NFC reader is always
present, but without an SD card it has nothing to launch.

# First-time setup

For the M5Dial to find your C64, the Wi-Fi credentials and the address of the C64
have to be stored once. You can do that **right on the device** under
*Settings → WiFi* – the source code no longer has to be touched. The credentials
go into internal memory and survive every restart. The *Setting up Wi-Fi* chapter
walks through the details.

If you would rather supply the credentials while programming the device, put
them into `build_env.h` as before (see the technical documentation). They then
serve as the initial values for the very first start.

Connection status is always visible as a small **dot at the very top** of the
round display.

| Dot | Meaning |
|---|---|
| **green** | Everything connected, ready |
| **blue** | Wi-Fi up, but the C64 does not answer |
| **yellow** | C64 reachable, but the password is wrong |
| **red** | No Wi-Fi connection |

At the bottom edge of every menu page you see two labels: **NFC** and **SD**.
Green means detected, grey means not present. Full details are on the *Status*
page.

# Operation

The M5Dial has three inputs: the **rotary bezel**, the **button** (press the
bezel down) and the **touchscreen**.

| Input | Effect |
|---|---|
| **Rotate** | On the home screen: open the menu. Otherwise: move the selection or scroll |
| **Short press** | Select, confirm, execute |
| **Long press** (from 0.6 s) | Go back one level. On the home screen: shortcut action |
| **Touch the ring** | Run that menu entry directly |
| **Touch the centre** | Return from the menu to the home screen |
| **Touch a list row** | Select the row, a second tap runs it |
| **Touch the title line** | Go back |
| **Long touch** (home screen) | Second shortcut action |

You do not have to hit the icons precisely: the whole outer ring is active and
the device picks the icon closest to your finger. Only the centre with the logo
returns to the home screen.

# The home screen

The home screen shows the 1MHz logo, either still or with changing effects
(*Water*, *RotoZoom*, *SineWave*, *Ripple*, *Raster*). It doubles as the screen
saver: after the configured idle time the device returns here on its own.

A turn, a button press or a tap opens the menu.

**Two shortcuts that skip the menu:** on the home screen a long button press runs
a freely selectable action (*Reset* by default) and a long touch on the display
runs a second one (*Ultimate Menu* by default). Both are configured under
*Settings*. The long touch also works inside the ring menu, so you do not have to
return to the home screen first.

# The menu

Ten icons sit in a circle with the logo in the middle. The selected icon is
larger and brightly outlined; its name appears briefly above the logo.

| Icon | What it does |
|---|---|
| **PowerOff** | Powers the C64 down – press twice for safety |
| **Reset** | Resets the C64 (like the reset button) |
| **Reboot** | Restarts the C64 completely |
| **Ultimate Menu** | Opens or closes the Ultimate menu on the C64 |
| **CPU Speed** | View and change the CPU speed |
| **RFID / NFC** | Present a tag and launch the stored game |
| **SD card** | Pick a game straight from the SD card and launch it |
| **Joystick Swap** | Swaps the joystick ports on the C64 (Normal ↔ Swapped) |
| **Status** | Detailed connection information |
| **Settings** | Preferences and NFC tools |

**Powering off:** switching the machine off by accident would be annoying, so the
prompt *POWER OFF? NOCHMAL!* appears first. Only a second press within the time
window actually powers down.

# Changing the CPU speed

Choose **CPU Speed** from the menu. The current speed is shown at the top, the
list of options below. Turn the bezel to pick a speed, press to apply. The M5Dial
reads the available steps straight from the C64, so you get exactly the values
your machine supports.

# Swapping the joystick ports

Some games expect the joystick in port 1, others in port 2. Instead of moving the
cable, the mapping can be swapped inside the C64.

Choose **Joystick Swap** from the menu – every press toggles between *Normal* and
*Swapped*, and *JOY Swapped* or *JOY Normal* appears briefly.

*Settings → Joystick* shows the current state and steps through every value your
C64 offers: besides *Normal* and *Swapped* there may be *WASD P1* and *WASD P2*,
depending on the firmware – the keyboard then drives that port.

The Ultimate firmware has no dedicated remote command for this. The M5Dial sets
the *Joystick Swapper* item in the C64 configuration, exactly like the CPU speed.
The state therefore survives until it is changed again, a reset included.

# Launching games from a tag

Prerequisites: a microSD card with your games is installed, and the tag has been
written beforehand (see below).

## Just present the tag (automatic)

Normally you do not have to operate anything at all: while the home screen or the
menu is showing, the M5Dial checks in the background whether a tag is present. As
soon as it finds one it switches to reading mode on its own and launches the
stored program.

The reading screen then stays up, so you can present the next tag right away. If
nothing happens for a while, the device returns to the home screen.

How often it looks is set under *Settings → Auto-NFC*:

| Setting | Meaning |
|---|---|
| **Off** | no background polling, tags only via the menu entry |
| **1.5s** | very frugal |
| **0.7s** | factory default, a good compromise |
| **0.3s** | quickest to react |

The probe is short enough that even at *0.3s* it slows neither the animation nor
the controls noticeably.

## Through the menu entry

If you want to read a tag deliberately — or if *Auto-NFC* is switched off — the
manual route still works:

1. Choose **RFID / NFC** from the menu.
2. Hold the NFC tag against the front of the M5Dial.
3. The device reads the stored path, fetches the file from the SD card and sends
   it to the C64. A progress bar shows the upload.
4. Once *START: …* appears (or *LAEUFT: …* for disk images), the game is running.

Take the tag away, present the next one – the screen stays in reading mode, so
you can play several tags in a row.

## Supported file types

| Extension | What it is |
|---|---|
| `.prg` | Program (loaded and started) |
| `.crt` | Cartridge |
| `.sid` | Music file (played back) |
| `.mod` | Amiga music module |
| `.d64` `.d71` `.d81` | Disk image |
| `.g64` `.g71` | Disk image (GCR format) |

Disk images are mounted into the drive. What happens next is set under
*Settings → Disk Action*: mount only, mount and reset, or mount, reset and load
and run the first program.

## Random launch

If the tag holds a `?` instead of a file name, or just a folder path, the M5Dial
picks a random file from that folder every time. Handy for a "surprise tag".

# Command cards

A card does not have to point at a game – it can also carry a **command**.
Present it and the command runs immediately, with no menu and no SD card needed.

| Card | Effect |
|---|---|
| **Reset** | Resets the C64 |
| **Reboot** | Restarts the C64 completely |
| **Ultimate Menu** | Opens or closes the Ultimate menu |
| **PowerOff direct** | Powers off immediately |
| **PowerOff with prompt** | Asks first – present the same card a second time within the time window to confirm |
| **CPU x MHz** | Sets the CPU to the value stored on the card |
| **Swap Joystick** | Swaps the joystick ports (Normal ↔ Swapped) |
| **Joystick Normal / Swapped / WASD P1 / WASD P2** | Sets the port mapping to that fixed value |

## Creating a command card

1. Choose **NFC-Cmd** in the settings.
2. Pick the command from the list. After the fixed entries come the joystick
   mappings and then all CPU steps your C64 offers, so a "CPU 10 MHz" card is
   a single click. *JOY* or *CPU* on the right tells the two blocks apart.
3. Present the card; *KARTE OK* means written and verified.

## PowerOff with prompt

The waiting time lives **on the card**, not in the device. When writing, the
value comes from *NFC-Cmd PowOff* (3, 5, 8 or 15 seconds, 8 s by default).
Presenting such a card shows *POWER OFF? NOCHMAL!* with a countdown. To power
off:

- present the **same card** again, or
- press the **button**

If the countdown expires or a different card appears, nothing happens. A card
with a time of **0** powers off immediately without asking.

## What is stored on the card

The command is plain text in an NDEF record – you can inspect or write it with
any NFC app on your phone:

```
CMD:RESET
CMD:REBOOT
CMD:MENU
CMD:POWEROFF=0      power off immediately
CMD:POWEROFF=8      ask first, 8 seconds to confirm
CMD:CPU=10          set the CPU to 10 MHz
CMD:JOY             toggle the joystick ports
CMD:JOY=SWAPPED     set the ports fixed; also NORMAL, WASD1, WASD2
```

Case does not matter. The M5Dial and M5Stack Core editions understand the same
format, so one card works on both.

# Launching games straight from the SD card

It works the same way without a tag: choose **SD card** from the menu. You see
the contents of the card, filtered down to launchable files.

- **Rotate** scrolls the list
- **Short press** opens a folder or launches a file
- **Long press** goes up one folder, and back to the menu at the top level
- `..` in the list also goes up one folder

To the right of each entry you see the file extension, or `dir` for folders.

# Writing and managing NFC tags

The four NFC tools sit at the top of **Settings**.

## NFC-Write: put a game on a tag

1. Choose **Settings → NFC-Write**.
2. Pick the game in the file browser.
3. Hold the NFC tag against the M5Dial.
4. *KARTE OK* means: written and verified.

The path is stored as an NDEF text record, the same format TeensyROM and Zaparoo
use.

## NFC-Info: read a tag

Choose **Settings → NFC-Info** and present a tag. You get the UID, tag type,
memory size, the stored text, the path derived from it and whether that file
actually exists on the SD card. Below that follow the raw contents of the tag.

Turn the bezel to scroll the list; tapping the upper or lower half also jumps.

## NFC-Dump and NFC-Restore: copying tags

- **NFC-Dump** reads the tag and stores it as a text file at
  `/NFC-DUMPS/<UID>.nfc` on the SD card.
- **NFC-Restore** writes such a file back to a different tag. Pick the dump file
  first, then present the target tag.

**Important:** the UID of a tag is burned into the chip and cannot be copied. The
*content* is copied – which is what matters here. Areas that could lock a tag
permanently are deliberately left untouched.

# Setting up Wi-Fi

Everything for this sits under **Settings → WiFi**. The M5Dial remembers up to
**four networks** and tries them one after another when connecting – handy if
you move between home and a phone hotspot.

| Entry | Effect |
|---|---|
| **Scan networks** | Search the area and pick a network from the list |
| **Load from SD** | Read `wifi.txt` from the microSD |
| **Setup portal** | Access point of its own with a web interface |
| **Saved** | Pick a stored network and connect |
| **To NFC card** | Write a stored network to an NFC card |
| **Save to SD** | Write all stored networks to the microSD as `wifi.txt` |
| **Delete network** | Remove a single entry |
| **Delete all** | Discard all credentials |

## Route 1: scan, then hand over the password on a tag

*Scan networks* shows the networks found after a few seconds, along with their
signal strength. A network that is already stored is marked *known* and connects
straight away; an *open* network needs no password.

For everything else the M5Dial asks for the password and waits for an NFC tag.
The tag carries an ordinary text record in the same scheme that Wi-Fi QR codes
use:

```
WIFI:S:MyWiFi;T:WPA;P:MyPassword;;
```

Write the tag with a phone, for example with the **NFC Tools** app
(*Write → Add a record → Text*). If the tag holds only the password without the
`WIFI:` prefix, it is assigned to the network you picked beforehand.

> The password sits unencrypted on the tag. Once you are set up, overwrite the
> tag – the device keeps the password in internal memory anyway.

## Presenting a tag is enough

A complete `WIFI:` tag also works **outside** the setup: present it on the home
screen or in the ring menu and the M5Dial stores the network and connects to it
right away. It waits up to eight seconds and then reports *WIFI ACTIVE* with the
IP address, or *NETWORK NOT THERE* if the network is out of range. If that
network is already connected, nothing happens (*ALREADY CONNECTED*) – so the tag
may simply stay on the reader.

That gives a second device its credentials in a matter of seconds.

## Route 2: a file on the SD card

Put a text file `wifi.txt` in the root directory of the microSD:

```
ssid = MyWiFi
pass = MyWiFiPassword

ssid = Hotspot
pass = secret123

host     = 192.168.0.64
hostpass =
```

Every new `ssid` line begins a new entry; lines starting with `#` are comments.
The file is read at start-up (as long as no network is stored yet) and at any
time through *Load from SD*.

The other way round, **Save to SD** writes all stored networks together with
`host` and `hostpass` back out in exactly this format. An existing `wifi.txt` is
renamed to `wifi.bak` first, so nothing is lost. That makes setting up a second
device a no-typing affair – bear in mind that the passwords sit on the card in
plain text.

## Route 3: the setup portal

*Setup portal* turns the M5Dial into an access point of its own for five
minutes. The display shows the network name, the password and the address you
open in a browser. There you enter SSID and password – either from the list of
networks found or by hand – and optionally the address and password of the C64.

After saving, the M5Dial shuts the access point down and connects to the new
network. The browser connection breaking off in the process is normal.

# Settings at a glance

Every setting is saved immediately and survives a restart.

## NFC tools

| Entry | Meaning |
|---|---|
| **NFC-Write** | Pick a file and write it to a tag |
| **NFC-Info** | Read a tag and show everything about it |
| **NFC-Dump** | Save the tag contents to the SD card |
| **NFC-Restore** | Write saved contents back to a tag |
| **NFC-Random** | Create a random-pick card for a directory |
| **NFC-Cmd** | Create a command card (see the *Command cards* chapter) |
| **NFC-Cmd PowOff** | Default prompt time for a PowerOff command card: 3, 5, 8, 15 s |
| **WiFi** | Wi-Fi setup submenu (see the *Setting up Wi-Fi* chapter) |

## Operation

| Entry | Meaning |
|---|---|
| **Taste lang** | Action for a long button press on the home screen |
| **Touch lang** | Action for a long touch on the home screen and in the ring menu |
| **PowerOff Abfrage** | *Abfrage* = the PowerOff shortcut asks first, *Direkt* = powers off immediately |
| **PowerOff Zeit** | Confirmation time window (0.5 to 3.0 s) |
| **Auto-NFC** | Background polling interval (*Off*, 1.5s, 0.7s, 0.3s) |
| **Home Timeout** | Idle time before returning to the home screen (*Off*, 10 s, 20 s, 45 s) |
| **Encoder Steps** | How many detents make one menu step (1, 2, 4, 6) |

*Taste lang* and *Touch lang* can be set to *Off*, *Reset*, *Reboot*, *Menu*,
*PowerOff* or *Joy Swap*.

If the menu feels too twitchy when turning, raise *Encoder Steps*.

## Display

| Entry | Meaning |
|---|---|
| **Animations** | Turn the home screen effects on or off |
| **Effect** | *Auto* cycles through all of them, otherwise a fixed one |
| **Anim Speed** | Speed of the effects |
| **Effect Time** | How long an effect runs |
| **Static Time** | How long the still logo shows in between |
| **Brightness** | Brightness in eight steps |

## C64 options

| Entry | Meaning |
|---|---|
| **Disk Action** | *Mount* only, *Mnt+Reset* adds a reset, *Mnt+Run* also loads and runs |
| **Disk Drive** | *Auto (8)* looks for the drive on bus 8, otherwise fixed *A* or *B* |
| **Joystick** | Port mapping in the C64: *Normal*, *Swapped*, and depending on firmware *WASD P1* / *WASD P2* |

## Miscellaneous

| Entry | Meaning |
|---|---|
| **Beep** | Confirmation tones on or off |
| **Factory Reset** | All settings back to defaults (Wi-Fi credentials are kept) |

# Frequently asked questions

**Touch sometimes does not respond.**
The whole outer ring is active, so you do not need to hit the icons precisely. If
nothing happens at all, check on the *Status* page whether the connection is up –
during network trouble the device waits briefly for an answer.

**The tag is not detected on its own.**
Background polling only runs on the home screen and in the ring menu, not in the
settings or the file browser. Also check whether *Settings → Auto-NFC* is set to
*Off*.

**Turning on the home screen only opens the menu.**
That is intentional: the first turn opens the menu only, so you do not
accidentally skip past a menu entry.

**The SD card is not detected.**
Format the card as FAT32, check the wiring and especially the supply voltage of
the module (see the technical documentation), and keep the Grove cables short.

**Uploading a .d64 takes a while.**
A disk image is about 175 kB and travels over Wi-Fi; a few seconds are normal.
Loading inside the C64 can take a little longer afterwards – the progress is
shown on the display.

**A tag is read but nothing starts.**
Usually the path on the tag does not match the SD card. *NFC-Info* tells you
whether the file was found.

**Can I use a tag from my TeensyROM?**
Yes. The tag format is identical. The folder and file names on your SD card just
have to match.

**Are my settings kept?**
Yes, they live in the flash memory of the M5Dial. Only *Factory Reset* clears
them.

# License

C64uRemote is released under the **MIT License**. The full text is in the file
`LICENSE` in the project root.

It originates from **C64uRemote by Karl Prosser (@klumsy)**,
<https://github.com/ReadyOS-C64/C64uRemote>, which he published under the MIT
License. This version is a derivative work and is released under the same terms:

* Copyright (c) 2026 Karl Prosser – original project
* Copyright (c) 2026 Martin Oswald (@mad, <https://1MHz.de>) – port and extensions

The MIT License allows you to use, modify and redistribute the software,
including commercially. The only condition: **the copyright notice and the
license text must be kept** and included with every copy. There is no warranty
and no liability.

The libraries used carry their own licenses: M5Unified and M5GFX (MIT,
© M5Stack), ArduinoJson (MIT, © Benoit Blanchon) and MFRC522_I2C
(<https://github.com/kkloesener/MFRC522_I2C>). PlatformIO fetches them at build
time.

# How this was made

The port, the extensions and these manuals were written with the help of
Claude (Anthropic). Concept, idea, hardware decisions and every test on real
devices: Martin Oswald (@mad).
