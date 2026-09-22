// ============================================================================
//  C64uRemote  -  M5Dial (StampS3)  +  eingebauter RFID/NFC-Leser (WS1850S)
//                                   +  nachgeruestete microSD (SPI)
// ----------------------------------------------------------------------------
//  Fernbedienung fuer den Commodore 64 Ultimate (c64u) / Ultimate64 Elite-II
//  ueber die ReST-API der Ultimate-Firmware (ab 3.11).
//
//  Basiert auf der Originalidee von Karl Prosser (@klumsy)
//      https://github.com/ReadyOS-C64/C64uRemote
//  sowie auf den Versionen fuer M5StickC Plus2, M5Dial und M5Stack Core von
//      Martin Oswald (@mad) - https://1MHz.de
//
//  Lizenz: MIT - siehe LICENSE im Projektstamm.
//    Copyright (c) 2026 Karl Prosser  (Originalprojekt C64uRemote,
//                                      https://github.com/ReadyOS-C64/C64uRemote)
//    Copyright (c) 2026 Martin Oswald (Portierung und Erweiterungen)
//
//  Diese Fassung bringt den vollen Funktionsumfang der Core-Version auf den
//  M5Dial und passt die Bedienung an Drehgeber + Taste + Touch an:
//
//    * Runde Oberflaeche fuer 240x240 (GC9A01), alles ueber einen
//      Offscreen-Puffer (M5Canvas) - dadurch flimmerfreie Vollbilder.
//    * Ring-Menue mit zehn Piktogrammen, Auswahl per Drehgeber oder Touch.
//    * RFID/NFC ueber den im M5Dial VERBAUTEN Leser (WS1850S, interner
//      I2C-Bus G11/G12, Adresse 0x28) - Bedienung und Kartenformat sind
//      identisch zur Core-Version mit Unit RFID2.
//        - Pfad einer Programmdatei auf eine NFC-Karte SCHREIBEN
//        - Pfad von der Karte LESEN, Datei von der microSD holen und per
//          ReST-API an den c64u senden + starten
//        - Karten-Info, Karte auf SD sichern (Dump) und zurueckschreiben
//    * SD-Browser (Datei auch ohne Karte direkt starten)
//    * Streaming-Upload: auch grosse .d64 passen ohne PSRAM durch den RAM
//
//  Hardware-Erweiterung microSD:  siehe README.md, Abschnitt "Verkabelung".
//      SCK  = G15 (Port A, weisse Ader)      MOSI = G13 (Port A, gelbe Ader)
//      MISO = G2  (Port B, gelbe Ader)       CS   = G1  (Port B, weisse Ader)
//  Die Pins stehen als Konstanten weiter unten und lassen sich dort aendern.
//
//  Speicherhinweis: der StampS3 hat kein PSRAM. Der Offscreen-Puffer belegt
//  240*240*2 = 115 kB. Schlaegt das Anlegen fehl, zeichnet der Code direkt
//  ins Display weiter - langsamer, aber voll funktionsfaehig.
// ============================================================================

#include <M5Dial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>

#if __has_include("build_env.h")
#include "build_env.h"
#else
#define C64U_WIFI_SSID ""
#define C64U_WIFI_PASSWORD ""
#define C64U_TARGET_HOST "192.168.0.64"
#define C64U_TARGET_PASSWORD ""
#endif

#include "1MHz_logo_rgb565.h"

namespace {

// ---------------------------------------------------------------------------
// Zeiten und Grenzwerte
// ---------------------------------------------------------------------------
constexpr uint32_t kModalMs           = 1500;
constexpr uint32_t kApiRetryDelayMs   = 250;   // Pause vor dem zweiten Versuch
constexpr uint32_t kHttpTimeoutMs     = 3000;
constexpr uint32_t kWiFiRetryMs       = 10000;
constexpr uint32_t kConnectionProbeMs = 15000;
// So lange wartet das Geraet nach einer aufgelegten WLAN-Karte auf die
// Verbindung, bevor es "Netz nicht da" meldet. Danach laeuft der Versuch
// ueber serviceWiFi ganz normal weiter.
constexpr uint32_t kWifiCardConnectMs = 8000;
constexpr uint32_t kFrameMs           = 33;     // ~30 fps
constexpr uint32_t kLongPressMs       = 600;
constexpr uint32_t kDialOffHoldMs     = 1500;  // lang auf "c64u Power Off" = M5Dial aus
constexpr uint32_t kRfidPollMs        = 250;
// Steht "Home Timeout" auf Off, faellt eine von der Hintergrundabfrage
// geoeffnete Leseseite trotzdem nach dieser Zeit zum Startbild zurueck.
constexpr uint32_t kAutoRfidHoldMs    = 20000;
constexpr uint8_t  kPowerOffConfirmDefDs = 15;  // 1,5 s (Zehntelsekunden)
constexpr size_t   kMaxCpuChoices     = 16;
constexpr size_t   kMaxJoyChoices     = 6;
constexpr size_t   kMaxDirEntries     = 160;
constexpr size_t   kUploadChunk       = 1024;   // Bytes pro TCP-Write

// Mount-Modus fuer hochgeladene Disk-Images. "readwrite" ist die in der
// API-Doku dokumentierte Kombination fuer Uploads.
constexpr const char* kMountMode = "readwrite";

// ---------------------------------------------------------------------------
// WLAN-Einrichtung
//
// Die Zugangsdaten stehen im NVS und lassen sich zur Laufzeit aendern -
// build_env.h liefert nur noch die Startwerte beim allerersten Start.
// Drei Wege fuehren zu neuen Zugangsdaten:
//   1. NFC-Karte mit einem NDEF-Text-Record
//   2. Datei /wifi.txt auf der SD-Karte
//   3. Setup-Portal: der M5Dial macht einen eigenen Accesspoint auf
// Umgekehrt schreibt "Auf SD sichern" die gespeicherten Netze wieder als
// /wifi.txt heraus - so wandern sie ohne Tipperei auf das naechste Geraet.
// ---------------------------------------------------------------------------
constexpr size_t   kWifiProfileMax  = 4;        // gespeicherte Netze
constexpr size_t   kWifiScanMax     = 16;       // angezeigte Scan-Treffer
constexpr uint32_t kPortalIdleMs    = 300000;   // Portal nach 5 min beenden
constexpr uint32_t kPortalCloseMs   = 2500;     // Nachlauf nach dem Speichern

// Der Accesspoint des Setup-Portals. WPA2 verlangt mindestens acht Zeichen;
// das Passwort steht waehrend des Betriebs gross auf dem Display.
constexpr const char* kPortalSsid   = "C64uRemote-Setup";
constexpr const char* kPortalPass   = "c64ultimate";
constexpr uint8_t     kPortalDnsPort = 53;

// Konfigurationsdatei auf der SD-Karte
constexpr const char* kWifiFileSd   = "/wifi.txt";

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
// RFID/NFC ist im M5Dial fest verbaut und haengt am INTERNEN I2C-Bus
// (G11 = SDA, G12 = SCL). Die Bibliothek M5Dial kuemmert sich darum,
// wir sprechen den Leser ueber M5Dial.Rfid an.
constexpr uint8_t kRfidAddr = 0x28;

// microSD am SPI-Bus. Der M5Dial hat nur zwei Grove-Buchsen mit insgesamt
// vier freien GPIOs - deshalb werden beide Ports benutzt.
//   Port A (rot,   HY2.0-4P):  G13 / G15
//   Port B (schwarz, HY2.0-4P): G2 (gelb) / G1 (weiss)
constexpr int kSdSckPin  = 15;   // Port A, Pin "SCL"
constexpr int kSdMosiPin = 13;   // Port A, Pin "SDA"
constexpr int kSdMisoPin = 2;    // Port B, Pin 3 (gelbe Ader)
constexpr int kSdCsPin   = 1;    // Port B, Pin 4 (weisse Ader)

// SPI3 (HSPI) ist frei; SPI2 gehoert dem Display.
SPIClass sdSpi(HSPI);

// ---------------------------------------------------------------------------
// Displaygeometrie (240 x 240, rund)
// ---------------------------------------------------------------------------
constexpr int kScrW   = 240;
constexpr int kScrH   = 240;
constexpr int kCx     = 120;
constexpr int kCy     = 120;
constexpr int kRadius = 119;      // sichtbarer Radius
constexpr int kRing   = 116;      // Innenkreis fuer Menue- und Listenseiten

// Das Logo ist genau 240 x 135 gross und wird deshalb 1:1 dargestellt.
constexpr int kLogoY = (kScrH - kLogoSrcH) / 2;   // = 52

// Listen
constexpr int kListY0   = 58;
constexpr int kRowH     = 24;
constexpr int kListRows = 5;
constexpr int kTitleY   = 32;
constexpr int kSubY     = 48;
constexpr int kHintY    = 208;

// ---------------------------------------------------------------------------
// Menue und Einstellungen
// ---------------------------------------------------------------------------
enum MenuId : uint8_t {
  kMenuPowerOff = 0,
  kMenuReset,
  kMenuReboot,
  kMenuUltiMenu,
  kMenuCpu,
  kMenuRfidRun,
  kMenuSdBrowse,
  kMenuJoySwap,
  kMenuStatus,
  kMenuSetup,
  kMenuCount
};

constexpr const char* kMenuLabels[kMenuCount] = {
    "c64u Power Off", "Reset", "Reboot", "Ultimate Menu", "CPU Speed",
    "RFID / NFC", "SD-Karte", "Joystick Swap", "Status", "Settings",
};

// Die Reihenfolge der Einstellungen steckt an mehreren Stellen (Text, Wert,
// Aktion). Damit beim Einfuegen nichts verrutscht, gibt es dafuer Namen.
enum SettingsId : uint8_t {
  kSetNfcWrite = 0,
  kSetNfcInfo,
  kSetNfcDump,
  kSetNfcRestore,
  kSetNfcRandom,       // Zufallskarte fuer ein Verzeichnis
  kSetNfcCmd,          // letzte NFC-Aktion; danach kommen Schalter
  kSetCardConfirm,     // Abfragezeit der PowerOff-Befehlskarte (NFC-Cmd)
  kSetWifi,            // Aktion, wird in activateSetting() vorab behandelt
  kSetShutdown,        // M5Dial ausschalten, ebenfalls vorab behandelt
  kSetShortcutBtn,
  kSetShortcutTouch,
  kSetAutoNfc,
  kSetPowerOffAsk,
  kSetPowerOffTime,
  kSetAnimations,
  kSetEffect,
  kSetAnimSpeed,
  kSetEffectTime,
  kSetStaticTime,
  kSetHomeTimeout,
  kSetEncoderSteps,
  kSetBrightness,
  kSetDiskAction,
  kSetDiskDrive,
  kSetJoystick,        // Portbelegung im c64u (Config "Joystick Swapper")
  kSetBeep,
  kSetFactoryReset,
  kSetItemCount
};

// Bis hier (einschliesslich) sind es NFC-Aktionen, keine Schalter. Der
// WLAN-Punkt ist ebenfalls eine Aktion, braucht aber keinen NFC-Leser und
// wird deshalb in activateSetting() vorab behandelt.
constexpr uint8_t kSetLastAction = kSetNfcCmd;

constexpr const char* kSettingsItems[] = {
    "NFC-Write",
    "NFC-Info",
    "NFC-Dump",
    "NFC-Restore",
    "NFC-Zufall",
    "NFC-Cmd",
    "NFC-Cmd PowOff",
    "WLAN",
    "M5Dial Power Off",
    "Taste lang",
    "Touch lang",
    "Auto-NFC",
    "PowerOff Abfrage",
    "PowerOff Zeit",
    "Animations",
    "Effect",
    "Anim Speed",
    "Effect Time",
    "Static Time",
    "Home Timeout",
    "Encoder Steps",
    "Brightness",
    "Disk Action",
    "Disk Drive",
    "Joystick",
    "Beep",
    "Factory Reset",
};
constexpr size_t kSettingsCount = sizeof(kSettingsItems) / sizeof(kSettingsItems[0]);
static_assert(kSettingsCount == static_cast<size_t>(kSetItemCount),
              "kSettingsItems und SettingsId sind aus dem Tritt geraten");

// Untermenue der WLAN-Einrichtung
enum WifiMenuId : uint8_t {
  kWifiScanNow = 0,
  kWifiFromSd,
  kWifiPortal,
  kWifiConnectSaved,
  kWifiToCard,
  kWifiToSd,
  kWifiDeleteOne,
  kWifiDeleteAll,
  kWifiMenuCount
};

constexpr const char* kWifiMenuItems[kWifiMenuCount] = {
    "Netz suchen",
    "Von SD laden",
    "Setup-Portal",
    "Gespeichert",
    "Auf NFC-Karte",
    "Auf SD sichern",
    "Netz loeschen",
    "Alle loeschen",
};

enum class ScreenMode : uint8_t {
  Home,          // Logo / Bildschirmschoner
  Menu,          // Ring-Menue
  CpuMenu,
  Status,
  Settings,
  SdBrowser,     // Datei auswaehlen (starten oder auf Karte schreiben)
  CmdPick,       // Befehl auswaehlen, der auf eine Karte geschrieben wird
  RfidRun,       // Karte auflegen -> Pfad lesen -> starten
  RfidWrite,     // Karte auflegen -> Pfad schreiben
  RfidInfo,      // Karte auflegen -> alle Infos anzeigen
  RfidDump,      // Karte auflegen -> Inhalt auf die SD sichern
  RfidRestore,   // Karte auflegen -> Dump von der SD zurueckschreiben
  WifiMenu,      // Untermenue der WLAN-Einrichtung
  WifiScan,      // Liste der gefundenen Netze
  WifiCard,      // Karte auflegen -> WLAN-Passwort lesen
  WifiPortal,    // Setup-Accesspoint laeuft
  WifiSaved,     // gespeicherte Netze: verbinden oder loeschen
  Busy,          // Upload laeuft, eigener Fortschrittsbildschirm
};

enum class HomeMode : uint8_t { Static, Water, RotoZoom, SineWave, Ripple, Raster };
enum class DisplayEffectMode : uint8_t { Auto, Static, Water, RotoZoom, SineWave, Ripple, Raster };
enum class AnimationSpeedMode : uint8_t { Slow, Normal, Fast };
enum class EffectDurationMode : uint8_t { Short, Normal, Long };
enum class StaticDurationMode : uint8_t { Short, Normal, Long };
enum class HomeTimeoutMode : uint8_t { Off, Short, Normal, Long };
enum class EncoderStepsMode : uint8_t { Steps1, Steps2, Steps4, Steps6 };
// Hintergrundabfrage des NFC-Lesers auf Startbild und Ring-Menue
enum class AutoNfcMode : uint8_t { Off, Slow, Normal, Fast };
enum class DiskActionMode : uint8_t { Mount, MountReset, MountRun };
enum class UploadDriveMode : uint8_t { AutoBus8, DriveA, DriveB };

// Befehle, die statt eines Dateipfads auf einer NFC-Karte stehen koennen.
// Auf der Karte steht dann z. B. "CMD:RESET" oder "CMD:CPU=10".
enum class CardCmd : uint8_t {
  None,
  Reset,
  Reboot,
  UltiMenu,
  PowerOff,        // Argument = Bestaetigungszeit in Sekunden, 0 = sofort
  CpuSpeed,        // Argument = gewuenschter Wert, z. B. "10"
  DialOff,         // M5Dial selbst ausschalten ("CMD:M5OFF")
  JoySwap,         // Argument = Zielwert; ohne Argument wird umgeschaltet
};

struct CardCommand {
  CardCmd cmd    = CardCmd::None;
  String  arg;                 // PowerOff: Sekunden, CpuSpeed: MHz, JoySwap: Ziel
  bool    hasArg = false;
};

// Frei belegbare Aktionen fuer lange Tasten- bzw. Touch-Druecke
enum class ShortcutAction : uint8_t { None, Reset, Reboot, UltiMenu, PowerOff, JoySwap };

// ---------------------------------------------------------------------------
// Datenstrukturen
// ---------------------------------------------------------------------------
struct ApiResponse {
  bool   transportOk = false;
  bool   jsonOk      = false;
  bool   apiOk       = false;
  int    httpCode    = -1;
  String body;
  String errors;
};

struct ConnectionState {
  bool   wifiConnected   = false;
  bool   targetReachable = false;
  bool   authOk          = false;
  String detail          = "Not tested";
};

struct SettingsState {
  bool               animationsEnabled = true;
  DisplayEffectMode  effectMode        = DisplayEffectMode::Auto;
  AnimationSpeedMode animationSpeed    = AnimationSpeedMode::Normal;
  EffectDurationMode effectDuration    = EffectDurationMode::Normal;
  StaticDurationMode staticDuration    = StaticDurationMode::Normal;
  HomeTimeoutMode    homeTimeout       = HomeTimeoutMode::Normal;
  EncoderStepsMode   encoderSteps      = EncoderStepsMode::Steps2;
  AutoNfcMode        autoNfc           = AutoNfcMode::Normal;
  uint8_t            brightness        = 160;
  DiskActionMode     diskAction        = DiskActionMode::MountRun;
  UploadDriveMode    uploadDrive       = UploadDriveMode::AutoBus8;
  bool               beepEnabled       = true;
  // Sicherheitsabfrage fuer PowerOff per Tastenkuerzel. Der Menuepunkt
  // PowerOff fragt immer nach, unabhaengig von dieser Einstellung.
  bool               powerOffComboAsk  = true;
  uint8_t            powerOffConfirmDs = kPowerOffConfirmDefDs;
  // Wie lange nach einem PowerOff-Kartenbefehl auf die Bestaetigung
  // gewartet wird (Sekunden). Die Karte muss so lange wieder aufgelegt
  // oder die Taste gedrueckt werden.
  uint8_t            cardConfirmS      = 8;

  ShortcutAction     shortcutButton    = ShortcutAction::Reset;      // Taste lang auf Home
  // Touch lang wirkt auf dem Startbild und im Ring-Menue
  ShortcutAction     shortcutTouch     = ShortcutAction::UltiMenu;
};

struct HomeDemoState {
  HomeMode mode            = HomeMode::Static;
  uint8_t  nextEffectIndex = 0;
  uint32_t startedAtMs     = 0;
};

struct DirEntryInfo {
  String name;
  bool   isDir = false;
};

// Ein gespeichertes WLAN. Das Passwort bleibt im NVS und verlaesst das
// Geraet nur ueber das Setup-Portal (dort maskiert).
struct WifiProfile {
  String ssid;
  String pass;
};

// Ein Treffer aus dem Netzsuchlauf
struct WifiScanEntry {
  String  ssid;
  int32_t rssi = 0;
  bool    open = false;
};

struct AppState {
  ScreenMode screen        = ScreenMode::Home;
  ScreenMode returnScreen  = ScreenMode::Home;   // wohin nach Busy
  int        menuIndex     = 0;
  int        cpuIndex      = 0;
  int        settingsIndex = 0;

  // ---- c64u ----
  String cpuCategory;
  String cpuItem;
  String currentCpuValue = "Unknown";
  bool   cpuPathKnown    = false;
  String cpuWireOptions[kMaxCpuChoices];
  String cpuDisplayOptions[kMaxCpuChoices];
  size_t cpuChoiceCount = 0;

  String joyCategory;
  String joyItem;
  String joyValue;
  bool   joyPathKnown   = false;
  String joyOptions[kMaxJoyChoices];
  size_t joyChoiceCount = 0;

  bool            configReady = false;
  ConnectionState connection  = {};
  SettingsState   settings    = {};
  HomeDemoState   home        = {};

  // ---- WLAN-Einrichtung ----
  int      wifiMenuIndex   = 0;
  int      wifiScanIndex   = 0;
  int      wifiSavedIndex  = 0;
  size_t   wifiScanCount   = 0;
  bool     wifiSavedDelete = false;   // Liste im Loeschmodus
  bool     wifiSavedToCard = false;   // Liste schreibt das Netz auf eine Karte
  String   wifiPendingSsid;           // gewaehltes Netz, wartet auf Passwort
  String   wifiHint = "Karte auflegen...";
  bool     portalActive    = false;
  uint32_t portalTouchedMs = 0;       // letzter Zugriff (Leerlauf-Abschaltung)
  uint32_t portalCloseAtMs = 0;       // 0 = kein Abschaltwunsch

  // ---- Overlay ----
  String   modalText;
  uint16_t modalColor   = TFT_WHITE;
  uint32_t modalUntilMs = 0;

  String   menuLabel;               // kurzer Hinweis im Ring-Menue
  uint32_t menuLabelUntilMs = 0;

  // ---- Timing ----
  uint32_t lastWiFiAttemptMs     = 0;
  uint32_t lastConnectionProbeMs = 0;
  uint32_t lastRfidPollMs        = 0;
  uint32_t lastInteractionMs     = 0;

  bool     pendingPowerOff     = false;    // Menue: zweites Druecken bestaetigt
  uint32_t pendingPowerOffAtMs = 0;
  bool     pendingShutdown     = false;    // "M5Dial aus": zweites Druecken schaltet ab
  uint32_t pendingShutdownAtMs = 0;
  bool     comboPowerOff       = false;    // Tastenkuerzel: Rueckfrage laeuft
  uint32_t comboPowerOffAtMs   = 0;

  // ---- Eingabe ----
  long encoderBase     = 0;
  int  encoderResidual = 0;
  bool buttonHandled   = false;   // langer Druck bereits ausgewertet
  bool touchLatch      = false;
  bool touchLongDone   = false;
  uint32_t touchStartMs = 0;
  int  touchStartX = 0;
  int  touchStartY = 0;

  // ---- SD ----
  bool         sdReady = false;
  String       sdPath  = "/";
  DirEntryInfo sdEntries[kMaxDirEntries];
  size_t       sdCount = 0;
  int          sdIndex = 0;
  uint8_t      sdPickMode = 0;          // 0 starten, 1 auf Karte schreiben, 2 Dump zurueckspielen

  // ---- RFID ----
  bool   rfidReady   = false;
  String pendingPath;                   // Pfad, der auf die Karte soll
  String pendingCardText;               // fertiger Kartentext (Befehlskarten)
  int    cmdIndex = 0;                  // Auswahl im Befehlsmenue

  // Offene Rueckfrage eines PowerOff-Kartenbefehls
  bool     cardPowerOffPending = false;
  String   cardPowerOffUid;
  uint32_t cardPowerOffUntilMs = 0;
  String pendingDump;                   // Dump-Datei, die zurueckgeschrieben wird
  String lastCardPath;                  // zuletzt von Karte gelesener Pfad
  String rfidHint = "Karte auflegen...";
  // true, wenn die Leseseite durch die Hintergrundabfrage geoeffnet wurde.
  // Dann darf sie nach Ablauf des Home-Timeouts wieder verschwinden.
  bool   autoRfidActive = false;

  static constexpr size_t kMaxInfoLines = 16;
  String infoLines[kMaxInfoLines];      // Zusammenfassung
  size_t infoCount = 0;
  String infoLines2[kMaxInfoLines];     // Rohdaten und Technik
  size_t infoCount2 = 0;
  int    infoScroll = 0;
  String infoUid;                       // UID der bereits eingelesenen Karte

  // ---- Upload ----
  String   busyTitle;
  String   busyDetail;
  size_t   busySent  = 0;
  size_t   busyTotal = 0;
} app;

Preferences prefs;

// Offscreen-Puffer. Klappt das Anlegen nicht, wird direkt ins Display
// gezeichnet - dann zeigt gDraw auf M5Dial.Display.
M5Canvas          canvas(&M5Dial.Display);
lgfx::LovyanGFX*  gDraw     = nullptr;
bool              gUseCanvas = false;

uint16_t rowBuf[kScrW] = {};

// Bequemer Zugriff auf den eingebauten Leser
#define RFID (M5Dial.Rfid)

// ---------------------------------------------------------------------------
// Kleine Helfer
// ---------------------------------------------------------------------------
String trimCopy(const String& value) { String r = value; r.trim(); return r; }
String configString(const char* v)   { return trimCopy(v == nullptr ? "" : v); }

// ---------------------------------------------------------------------------
// Netzkonfiguration zur Laufzeit
//
// Frueher kamen SSID, Passwort und Zieladresse fest aus build_env.h. Jetzt
// stehen sie im NVS und build_env.h liefert nur noch die Startwerte, solange
// nichts gespeichert ist. Damit muss fuer ein neues WLAN nichts mehr neu
// uebersetzt werden.
// ---------------------------------------------------------------------------
WifiProfile gWifiProfiles[kWifiProfileMax];
size_t      gWifiCount = 0;
size_t      gWifiTry   = 0;      // Profil fuer den naechsten Verbindungsversuch
String      gTargetHost;
String      gTargetPass;

WifiScanEntry gWifiScan[kWifiScanMax];

// Werte aus build_env.h (nur Startwerte)
const String& buildWifiSsid()  { static const String v = configString(C64U_WIFI_SSID); return v; }
const String& buildWifiPass()  { static const String v = configString(C64U_WIFI_PASSWORD); return v; }
const String& buildHost()      { static const String v = configString(C64U_TARGET_HOST); return v; }
const String& buildHostPass()  { static const String v = configString(C64U_TARGET_PASSWORD); return v; }

const String& targetHost()     { return gTargetHost; }
const String& targetPassword() { return gTargetPass; }

bool hasWiFiConfig()   { return gWifiCount > 0; }
bool hasTargetConfig() { return !gTargetHost.isEmpty(); }
bool configReady()     { return hasWiFiConfig() && hasTargetConfig(); }

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

uint16_t blend565(uint16_t from, uint16_t to, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  const uint8_t fr = ((from >> 11) & 0x1Fu) << 3;
  const uint8_t fg = ((from >> 5) & 0x3Fu) << 2;
  const uint8_t fb = (from & 0x1Fu) << 3;
  const uint8_t tr = ((to >> 11) & 0x1Fu) << 3;
  const uint8_t tg = ((to >> 5) & 0x3Fu) << 2;
  const uint8_t tb = (to & 0x1Fu) << 3;
  return rgb565(static_cast<uint8_t>(fr + (tr - fr) * t),
                static_cast<uint8_t>(fg + (tg - fg) * t),
                static_cast<uint8_t>(fb + (tb - fb) * t));
}

// Farbpalette (identisch zur Core-Version, damit beide Geraete gleich wirken)
const uint16_t kColBg      = rgb565(6, 10, 18);
const uint16_t kColPanel   = rgb565(16, 25, 42);
const uint16_t kColPanelHi = rgb565(48, 94, 164);
const uint16_t kColLine    = rgb565(66, 96, 132);
const uint16_t kColLineHi  = rgb565(184, 228, 255);
const uint16_t kColText    = rgb565(212, 226, 248);
const uint16_t kColLabel   = rgb565(156, 190, 228);
const uint16_t kColOk      = rgb565(80, 255, 0);
const uint16_t kColWarn    = rgb565(255, 190, 84);
const uint16_t kColErr     = rgb565(255, 120, 96);
const uint16_t kColInfo    = rgb565(120, 220, 255);

void beep(uint16_t freq = 2000, uint32_t ms = 40) {
  if (!app.settings.beepEnabled) return;
  M5Dial.Speaker.tone(freq, ms);
}

void noteInteraction(uint32_t now) { app.lastInteractionMs = now; }

void setModal(const String& text, uint16_t color, uint32_t now, uint32_t durationMs = kModalMs) {
  app.modalText    = text;
  app.modalColor   = color;
  app.modalUntilMs = now + durationMs;
}

bool modalVisible(uint32_t now) {
  return !app.modalText.isEmpty() && now <= app.modalUntilMs;
}

void showMenuLabel(const String& text, uint32_t now) {
  app.menuLabel        = text;
  app.menuLabelUntilMs = now + 1600;
}

// Halbe Sehnenbreite des Kreises auf Hoehe y - damit kein Text am runden
// Rand abgeschnitten wird.
int chordHalfWidth(int y) {
  const int dy = y - kCy;
  const int inner = kRadius * kRadius - dy * dy;
  if (inner <= 0) return 0;
  return static_cast<int>(sqrtf(static_cast<float>(inner)));
}

// Vorwaertsdeklarationen
void drawBusyScreen();
void openSdBrowser(uint8_t forCard, uint32_t now);
void setScreen(ScreenMode next, uint32_t now);
void refreshCpuValue();
void beginWiFi(uint32_t now);
ApiResponse sendApiRequest(const char* method, const String& path, bool authenticated);

// ---------------------------------------------------------------------------
// Labels fuer die Einstellungen
// ---------------------------------------------------------------------------
const char* effectLabel(DisplayEffectMode m) {
  switch (m) {
    case DisplayEffectMode::Auto:     return "Auto";
    case DisplayEffectMode::Static:   return "Static";
    case DisplayEffectMode::Water:    return "Water";
    case DisplayEffectMode::RotoZoom: return "RotoZoom";
    case DisplayEffectMode::SineWave: return "SineWave";
    case DisplayEffectMode::Ripple:   return "Ripple";
    case DisplayEffectMode::Raster:   return "Raster";
  }
  return "Auto";
}
const char* animationSpeedLabel(AnimationSpeedMode m) {
  switch (m) {
    case AnimationSpeedMode::Slow: return "Slow";
    case AnimationSpeedMode::Fast: return "Fast";
    default:                       return "Normal";
  }
}
const char* effectDurationLabel(EffectDurationMode m) {
  switch (m) {
    case EffectDurationMode::Short: return "Short";
    case EffectDurationMode::Long:  return "Long";
    default:                        return "Normal";
  }
}
const char* staticDurationLabel(StaticDurationMode m) {
  switch (m) {
    case StaticDurationMode::Short: return "Short";
    case StaticDurationMode::Long:  return "Long";
    default:                        return "Normal";
  }
}
const char* homeTimeoutLabel(HomeTimeoutMode m) {
  switch (m) {
    case HomeTimeoutMode::Off:    return "Off";
    case HomeTimeoutMode::Short:  return "10s";
    case HomeTimeoutMode::Long:   return "45s";
    default:                      return "20s";
  }
}
uint32_t homeTimeoutMs(HomeTimeoutMode m) {
  switch (m) {
    case HomeTimeoutMode::Off:   return 0;
    case HomeTimeoutMode::Short: return 10000;
    case HomeTimeoutMode::Long:  return 45000;
    default:                     return 20000;
  }
}
const char* encoderStepsLabel(EncoderStepsMode m) {
  switch (m) {
    case EncoderStepsMode::Steps1: return "1";
    case EncoderStepsMode::Steps4: return "4";
    case EncoderStepsMode::Steps6: return "6";
    default:                       return "2";
  }
}
const char* autoNfcLabel(AutoNfcMode m) {
  switch (m) {
    case AutoNfcMode::Off:    return "Off";
    case AutoNfcMode::Slow:   return "1.5s";
    case AutoNfcMode::Fast:   return "0.3s";
    default:                  return "0.7s";
  }
}
// Abstand zwischen zwei Hintergrundabfragen des NFC-Lesers
uint32_t autoNfcIntervalMs(AutoNfcMode m) {
  switch (m) {
    case AutoNfcMode::Off:    return 0;
    case AutoNfcMode::Slow:   return 1500;
    case AutoNfcMode::Fast:   return 300;
    default:                  return 700;
  }
}

int encoderStepsValue(EncoderStepsMode m) {
  switch (m) {
    case EncoderStepsMode::Steps1: return 1;
    case EncoderStepsMode::Steps4: return 4;
    case EncoderStepsMode::Steps6: return 6;
    default:                       return 2;
  }
}
const char* diskActionLabel(DiskActionMode m) {
  switch (m) {
    case DiskActionMode::Mount:      return "Mount";
    case DiskActionMode::MountReset: return "Mnt+Reset";
    default:                         return "Mnt+Run";
  }
}
const char* uploadDriveLabel(UploadDriveMode m) {
  switch (m) {
    case UploadDriveMode::DriveA: return "A fest";
    case UploadDriveMode::DriveB: return "B fest";
    default:                      return "Auto (8)";
  }
}
const char* shortcutLabel(ShortcutAction a) {
  switch (a) {
    case ShortcutAction::Reset:    return "Reset";
    case ShortcutAction::Reboot:   return "Reboot";
    case ShortcutAction::UltiMenu: return "Menu";
    case ShortcutAction::PowerOff: return "c64u Off";
    case ShortcutAction::JoySwap:  return "Joy Swap";
    default:                       return "Off";
  }
}
ShortcutAction nextShortcutAction(ShortcutAction a) {
  uint8_t next = static_cast<uint8_t>(a) + 1;
  if (next > static_cast<uint8_t>(ShortcutAction::JoySwap)) next = 0;
  return static_cast<ShortcutAction>(next);
}

// Waehlbare Zeitfenster in Zehntelsekunden: 0,5 bis 3,0 s
constexpr uint8_t kTimeSteps[]  = {5, 7, 10, 15, 20, 25, 30};
constexpr size_t  kTimeStepCount = sizeof(kTimeSteps) / sizeof(kTimeSteps[0]);

uint8_t nextTimeStep(uint8_t current) {
  for (size_t i = 0; i < kTimeStepCount; ++i) {
    if (current < kTimeSteps[i]) return kTimeSteps[i];
    if (current == kTimeSteps[i]) return kTimeSteps[(i + 1) % kTimeStepCount];
  }
  return kTimeSteps[0];
}
String timeStepLabel(uint8_t ds) { return String(ds / 10) + "." + String(ds % 10) + "s"; }

// Bestaetigungsfenster fuer PowerOff-Kartenbefehle. Deutlich groesser als die
// Tastenabfrage, weil die Karte erst weggenommen und wieder aufgelegt wird.
constexpr uint8_t kCardConfirmSteps[] = {3, 5, 8, 15};
constexpr size_t  kCardConfirmCount = sizeof(kCardConfirmSteps) / sizeof(kCardConfirmSteps[0]);

uint8_t nextCardConfirm(uint8_t current) {
  for (size_t i = 0; i < kCardConfirmCount; ++i) {
    if (current < kCardConfirmSteps[i]) return kCardConfirmSteps[i];
    if (current == kCardConfirmSteps[i]) return kCardConfirmSteps[(i + 1) % kCardConfirmCount];
  }
  return kCardConfirmSteps[0];
}
String cardConfirmLabel(uint8_t s) { return String(s) + "s"; }
uint32_t cardConfirmMs() { return app.settings.cardConfirmS * 1000u; }
uint32_t powerOffConfirmMs() { return app.settings.powerOffConfirmDs * 100u; }

float animationSpeedFactor(AnimationSpeedMode m) {
  switch (m) {
    case AnimationSpeedMode::Slow: return 0.65f;
    case AnimationSpeedMode::Fast: return 1.45f;
    default: return 1.0f;
  }
}
float effectDurationFactor(EffectDurationMode m) {
  switch (m) {
    case EffectDurationMode::Short: return 0.60f;
    case EffectDurationMode::Long:  return 1.60f;
    default: return 1.0f;
  }
}
float staticDurationFactor(StaticDurationMode m) {
  switch (m) {
    case StaticDurationMode::Short: return 0.60f;
    case StaticDurationMode::Long:  return 3.20f;
    default: return 1.0f;
  }
}

uint8_t nextBrightnessValue(uint8_t current) {
  static const uint8_t levels[] = {32, 64, 96, 128, 160, 192, 224, 255};
  const size_t count = sizeof(levels) / sizeof(levels[0]);
  for (size_t i = 0; i < count; ++i) {
    if (current < levels[i]) return levels[i];
    if (current == levels[i]) return levels[(i + 1) % count];
  }
  return levels[0];
}

void applyBrightness() { M5Dial.Display.setBrightness(app.settings.brightness); }

// ---------------------------------------------------------------------------
// Einstellungen laden / speichern (NVS)
// ---------------------------------------------------------------------------
void loadDefaultSettings() { app.settings = SettingsState(); }

void saveSettings() {
  prefs.begin("c64udial", false);
  prefs.putBool ("anim_on",  app.settings.animationsEnabled);
  prefs.putUChar("fx_mode",  static_cast<uint8_t>(app.settings.effectMode));
  prefs.putUChar("anim_spd", static_cast<uint8_t>(app.settings.animationSpeed));
  prefs.putUChar("fx_time",  static_cast<uint8_t>(app.settings.effectDuration));
  prefs.putUChar("st_time",  static_cast<uint8_t>(app.settings.staticDuration));
  prefs.putUChar("home_to",  static_cast<uint8_t>(app.settings.homeTimeout));
  prefs.putUChar("enc_step", static_cast<uint8_t>(app.settings.encoderSteps));
  prefs.putUChar("auto_nfc", static_cast<uint8_t>(app.settings.autoNfc));
  prefs.putUChar("bright",   app.settings.brightness);
  prefs.putUChar("disk_act", static_cast<uint8_t>(app.settings.diskAction));
  prefs.putUChar("updrive",  static_cast<uint8_t>(app.settings.uploadDrive));
  prefs.putBool ("beep",     app.settings.beepEnabled);
  prefs.putBool ("po_combo", app.settings.powerOffComboAsk);
  prefs.putUChar("po_time",  app.settings.powerOffConfirmDs);
  prefs.putUChar("card_cnf", app.settings.cardConfirmS);
  prefs.putUChar("sc_btn",   static_cast<uint8_t>(app.settings.shortcutButton));
  prefs.putUChar("sc_tch",   static_cast<uint8_t>(app.settings.shortcutTouch));
  prefs.end();
}

void loadSettings() {
  loadDefaultSettings();
  prefs.begin("c64udial", true);
  app.settings.animationsEnabled = prefs.getBool("anim_on", app.settings.animationsEnabled);
  const uint8_t fxMode  = prefs.getUChar("fx_mode",  static_cast<uint8_t>(app.settings.effectMode));
  const uint8_t animSpd = prefs.getUChar("anim_spd", static_cast<uint8_t>(app.settings.animationSpeed));
  const uint8_t fxTime  = prefs.getUChar("fx_time",  static_cast<uint8_t>(app.settings.effectDuration));
  const uint8_t stTime  = prefs.getUChar("st_time",  static_cast<uint8_t>(app.settings.staticDuration));
  const uint8_t homeTo  = prefs.getUChar("home_to",  static_cast<uint8_t>(app.settings.homeTimeout));
  const uint8_t encStep = prefs.getUChar("enc_step", static_cast<uint8_t>(app.settings.encoderSteps));
  const uint8_t autoNfc = prefs.getUChar("auto_nfc", static_cast<uint8_t>(app.settings.autoNfc));
  const uint8_t bright  = prefs.getUChar("bright",   app.settings.brightness);
  const uint8_t diskAct = prefs.getUChar("disk_act", static_cast<uint8_t>(app.settings.diskAction));
  const uint8_t upDrive = prefs.getUChar("updrive",  static_cast<uint8_t>(app.settings.uploadDrive));
  app.settings.beepEnabled       = prefs.getBool ("beep",     app.settings.beepEnabled);
  app.settings.powerOffComboAsk  = prefs.getBool ("po_combo", app.settings.powerOffComboAsk);
  app.settings.powerOffConfirmDs = prefs.getUChar("po_time",  app.settings.powerOffConfirmDs);
  app.settings.cardConfirmS      = prefs.getUChar("card_cnf", app.settings.cardConfirmS);
  const uint8_t scBtn = prefs.getUChar("sc_btn", static_cast<uint8_t>(app.settings.shortcutButton));
  const uint8_t scTch = prefs.getUChar("sc_tch", static_cast<uint8_t>(app.settings.shortcutTouch));
  prefs.end();

  const uint8_t scMax = static_cast<uint8_t>(ShortcutAction::JoySwap);
  if (scBtn <= scMax) app.settings.shortcutButton = static_cast<ShortcutAction>(scBtn);
  if (scTch <= scMax) app.settings.shortcutTouch  = static_cast<ShortcutAction>(scTch);

  if (fxMode  <= static_cast<uint8_t>(DisplayEffectMode::Raster))  app.settings.effectMode     = static_cast<DisplayEffectMode>(fxMode);
  if (animSpd <= static_cast<uint8_t>(AnimationSpeedMode::Fast))   app.settings.animationSpeed = static_cast<AnimationSpeedMode>(animSpd);
  if (fxTime  <= static_cast<uint8_t>(EffectDurationMode::Long))   app.settings.effectDuration = static_cast<EffectDurationMode>(fxTime);
  if (stTime  <= static_cast<uint8_t>(StaticDurationMode::Long))   app.settings.staticDuration = static_cast<StaticDurationMode>(stTime);
  if (homeTo  <= static_cast<uint8_t>(HomeTimeoutMode::Long))      app.settings.homeTimeout    = static_cast<HomeTimeoutMode>(homeTo);
  if (encStep <= static_cast<uint8_t>(EncoderStepsMode::Steps6))   app.settings.encoderSteps   = static_cast<EncoderStepsMode>(encStep);
  if (autoNfc <= static_cast<uint8_t>(AutoNfcMode::Fast))           app.settings.autoNfc        = static_cast<AutoNfcMode>(autoNfc);
  if (diskAct <= static_cast<uint8_t>(DiskActionMode::MountRun))   app.settings.diskAction     = static_cast<DiskActionMode>(diskAct);
  if (upDrive <= static_cast<uint8_t>(UploadDriveMode::DriveB))    app.settings.uploadDrive    = static_cast<UploadDriveMode>(upDrive);
  app.settings.brightness = std::max<uint8_t>(32, bright);

  if (app.settings.powerOffConfirmDs < kTimeSteps[0] ||
      app.settings.powerOffConfirmDs > kTimeSteps[kTimeStepCount - 1]) {
    app.settings.powerOffConfirmDs = kPowerOffConfirmDefDs;
  }
  if (app.settings.cardConfirmS < kCardConfirmSteps[0] ||
      app.settings.cardConfirmS > kCardConfirmSteps[kCardConfirmCount - 1]) {
    app.settings.cardConfirmS = 8;
  }
}

// ---------------------------------------------------------------------------
// Logo direkt aus dem Flash lesen (kein RAM-Cache, der StampS3 hat kein PSRAM)
// Das Logo ist 240 x 135 Pixel gross und passt damit 1:1 auf die Breite.
// ---------------------------------------------------------------------------
inline uint16_t logoPixel(int x, int y) {
  if (x < 0) x = 0;
  if (x >= kLogoSrcW) x = kLogoSrcW - 1;
  if (y < 0) y = 0;
  if (y >= kLogoSrcH) y = kLogoSrcH - 1;
  return pgm_read_word(&commodore_logo_rgb565[y * kLogoSrcW + x]);
}

inline int wrapCoord(int value, int limit) {
  if (limit <= 0) return 0;
  value %= limit;
  if (value < 0) value += limit;
  return value;
}

// Schiebt eine fertige Logozeile in den Zeichenpuffer und maskiert dabei
// alles ausserhalb des runden Displays.
inline void pushLogoRow(int y) {
  const int screenY = kLogoY + y;
  const int half    = chordHalfWidth(screenY);
  const int left    = kCx - half;
  const int right   = kCx + half;
  for (int x = 0; x < kScrW; ++x) {
    if (x < left || x > right) rowBuf[x] = TFT_BLACK;
  }
  gDraw->pushImage(0, screenY, kScrW, 1, rowBuf);
}

void drawStaticLogo() {
  for (int y = 0; y < kLogoSrcH; ++y) {
    for (int x = 0; x < kScrW; ++x) rowBuf[x] = logoPixel(x, y);
    pushLogoRow(y);
  }
}

// Wasser- / Sinus-Verzerrung (zeilenweise Verschiebung)
void drawDistortedRows(uint32_t tickMs, bool waterMode) {
  const float t  = static_cast<float>(tickMs) * 0.001f;
  const float cx = kScrW * 0.5f;

  for (int y = 0; y < kLogoSrcH; ++y) {
    const float baseWave   = sinf(y * 0.10f + t * (waterMode ? 8.2f : 3.2f));
    const float secondWave = sinf(y * 0.035f - t * (waterMode ? 4.9f : 2.1f));
    const float xOffset    = waterMode ? (baseWave * 11.0f + secondWave * 6.0f)
                                       : (baseWave * 8.0f + secondWave * 9.0f);
    const float yOffset    = waterMode ? (secondWave * 3.5f) : (baseWave * 3.5f);
    const float shimmer    = waterMode ? std::max(0.0f, sinf(y * 0.07f + t * 10.8f)) : 0.0f;
    const int   srcY       = static_cast<int>(y + yOffset);

    for (int x = 0; x < kScrW; ++x) {
      const int sx = static_cast<int>(cx + (static_cast<float>(x) - cx) + xOffset);
      uint16_t color = logoPixel(sx, srcY);
      if (waterMode) color = blend565(color, rgb565(180, 240, 255), shimmer * 0.28f);
      else           color = blend565(color, rgb565(82, 180, 255), 0.08f);
      rowBuf[x] = color;
    }
    pushLogoRow(y);
  }
}

void drawRotoZoom(uint32_t tickMs) {
  const float t     = static_cast<float>(tickMs) * 0.001f;
  const float srcCx = kScrW * 0.5f;
  const float srcCy = kLogoSrcH * 0.5f;
  const float angle = t * 1.8f;
  const float zoom  = 1.33f + 0.65f * sinf(t * 1.25f);
  const float cs    = cosf(angle) / zoom;
  const float sn    = sinf(angle) / zoom;

  for (int y = 0; y < kLogoSrcH; ++y) {
    const float py = static_cast<float>(y) - srcCy;
    for (int x = 0; x < kScrW; ++x) {
      const float px = static_cast<float>(x) - srcCx;
      const int sx = wrapCoord(static_cast<int>(srcCx + px * cs - py * sn), kScrW);
      const int sy = wrapCoord(static_cast<int>(srcCy + px * sn + py * cs), kLogoSrcH);
      rowBuf[x] = logoPixel(sx, sy);
    }
    pushLogoRow(y);
  }
}

void drawRipple(uint32_t tickMs) {
  const float t  = static_cast<float>(tickMs) * 0.001f;
  const float cx = kScrW * 0.5f;
  const float cy = kLogoSrcH * 0.5f;
  const float lightX = -0.58f;
  const float lightY = -0.42f;
  const float maxRadius = sqrtf(static_cast<float>(kScrW * kScrW + kLogoSrcH * kLogoSrcH)) * 0.5f;
  const float travel = t * 118.0f;
  const float span   = maxRadius * 2.0f;
  const float waveFreq = 0.17f;
  const float timeFreq = 15.8f;
  const float displacementScale = 22.0f;
  const float sourceX[3] = {cx, cx - 48.0f, cx + 42.0f};
  const float sourceY[3] = {cy, cy + 26.0f, cy - 22.0f};
  const float sourceWeight[3] = {1.0f, 0.46f, 0.38f};

  for (int y = 0; y < kLogoSrcH; ++y) {
    const float fy = static_cast<float>(y);
    for (int x = 0; x < kScrW; ++x) {
      const float fx = static_cast<float>(x);
      float gradX = 0.0f, gradY = 0.0f, waveMix = 0.0f;

      for (int i = 0; i < 3; ++i) {
        const float dx = fx - sourceX[i];
        const float dy = fy - sourceY[i];
        const float radius = sqrtf(dx * dx + dy * dy) + 0.0001f;

        float reflected = fmodf(radius + travel * (0.94f + 0.08f * i), span);
        float bounceDir = 1.0f;
        if (reflected > maxRadius) {
          reflected = span - reflected;
          bounceDir = -1.0f;
        }
        const float normR   = std::min(reflected / maxRadius, 1.0f);
        const float damping = 1.0f - normR * 0.55f;
        const float wave    = reflected * waveFreq - t * (timeFreq + i * 0.8f);
        const float slope   = cosf(wave) * waveFreq * damping * bounceDir * sourceWeight[i];

        gradX   += slope * (dx / radius);
        gradY   += slope * (dy / radius);
        waveMix += sinf(wave) * damping * sourceWeight[i];
      }

      const int sx = static_cast<int>(fx - gradX * displacementScale);
      const int sy = static_cast<int>(fy - gradY * displacementScale);
      uint16_t color = logoPixel(sx, sy);

      const float shade     = std::max(0.0f, (-gradX * lightX - gradY * lightY) * 0.85f);
      const float highlight = std::min(0.48f, shade * 0.52f);
      const float shadow    = std::min(0.26f, std::max(0.0f, (gradX * lightX + gradY * lightY) * 0.36f));

      color = blend565(color, rgb565(215, 244, 255), highlight);
      color = blend565(color, rgb565(18, 44, 76), shadow);
      color = blend565(color, rgb565(120, 196, 255), std::min(0.24f, fabsf(waveMix) * 0.10f));
      rowBuf[x] = color;
    }
    pushLogoRow(y);
  }
}

// Rasterbalken rund um einen Kasten mit dem Logo
void drawRasterBars(uint32_t tickMs) {
  const float t = static_cast<float>(tickMs) * 0.0014f;
  const uint16_t palette[] = {rgb565(255, 92, 164), rgb565(255, 190, 94),
                              rgb565(132, 255, 184), rgb565(96, 196, 255)};

  const int innerX = 34;
  const int innerY = 14;
  const int innerW = kScrW - 2 * innerX;
  const int innerH = kLogoSrcH - 2 * innerY;

  for (int y = 0; y < kLogoSrcH; ++y) {
    const int band = static_cast<int>(fmodf(y + t * 140.0f, 56.0f) / 14.0f) & 3;
    const uint16_t color = palette[band];
    for (int x = 0; x < kScrW; ++x) rowBuf[x] = color;

    if (y >= innerY && y < innerY + innerH) {
      const int srcY = ((y - innerY) * kLogoSrcH) / innerH;
      for (int x = 0; x < innerW; ++x) {
        const int srcX = (x * kLogoSrcW) / innerW;
        rowBuf[innerX + x] = logoPixel(srcX, srcY);
      }
      const bool edge = (y == innerY) || (y == innerY + innerH - 1);
      if (edge) {
        for (int x = 0; x < innerW; ++x) rowBuf[innerX + x] = rgb565(132, 170, 255);
      } else {
        rowBuf[innerX] = rgb565(132, 170, 255);
        rowBuf[innerX + innerW - 1] = rgb565(132, 170, 255);
      }
    }
    pushLogoRow(y);
  }
}

// ---------------------------------------------------------------------------
// Effekt-Ablaufsteuerung
// ---------------------------------------------------------------------------
HomeMode homeModeFromEffect(DisplayEffectMode m) {
  switch (m) {
    case DisplayEffectMode::Water:    return HomeMode::Water;
    case DisplayEffectMode::RotoZoom: return HomeMode::RotoZoom;
    case DisplayEffectMode::SineWave: return HomeMode::SineWave;
    case DisplayEffectMode::Ripple:   return HomeMode::Ripple;
    case DisplayEffectMode::Raster:   return HomeMode::Raster;
    default:                          return HomeMode::Static;
  }
}

HomeMode cycleEffectByIndex(uint8_t index) {
  switch (index % 5u) {
    case 0: return HomeMode::Water;
    case 1: return HomeMode::RotoZoom;
    case 2: return HomeMode::SineWave;
    case 3: return HomeMode::Ripple;
    default:return HomeMode::Raster;
  }
}

HomeMode selectedCycleEffect() {
  if (app.settings.effectMode == DisplayEffectMode::Auto) {
    return cycleEffectByIndex(app.home.nextEffectIndex);
  }
  return homeModeFromEffect(app.settings.effectMode);
}

HomeMode currentHomeMode() {
  if (!app.settings.animationsEnabled) return HomeMode::Static;
  if (app.settings.effectMode == DisplayEffectMode::Static) return HomeMode::Static;
  return app.home.mode;
}

uint32_t homeModeDuration(HomeMode mode) {
  uint32_t baseMs;
  switch (mode) {
    case HomeMode::Static:
      return static_cast<uint32_t>(1200.0f * staticDurationFactor(app.settings.staticDuration));
    case HomeMode::RotoZoom:
    case HomeMode::Ripple:
      baseMs = 7000;
      break;
    default:
      baseMs = 5000;
      break;
  }
  return static_cast<uint32_t>(baseMs * effectDurationFactor(app.settings.effectDuration));
}

void enterHomeMode(HomeMode mode, uint32_t now) {
  app.home.mode        = mode;
  app.home.startedAtMs = now;
}

void resetHomeAnimation(uint32_t now) {
  app.home.nextEffectIndex = 0;
  enterHomeMode(HomeMode::Static, now);
}

void updateHomeDemo(uint32_t now) {
  if (!app.settings.animationsEnabled || app.settings.effectMode == DisplayEffectMode::Static) {
    if (app.home.mode != HomeMode::Static || app.home.startedAtMs == 0) enterHomeMode(HomeMode::Static, now);
    return;
  }
  if (app.home.startedAtMs == 0) { enterHomeMode(HomeMode::Static, now); return; }
  if (now - app.home.startedAtMs < homeModeDuration(app.home.mode)) return;

  if (app.home.mode == HomeMode::Static) {
    enterHomeMode(selectedCycleEffect(), now);
  } else {
    if (app.settings.effectMode == DisplayEffectMode::Auto) {
      app.home.nextEffectIndex = static_cast<uint8_t>((app.home.nextEffectIndex + 1) % 5u);
    }
    enterHomeMode(HomeMode::Static, now);
  }
}

void drawHomeVisual(uint32_t now) {
  const HomeMode mode  = currentHomeMode();
  const uint32_t phase = now - app.home.startedAtMs;
  const uint32_t tick  = static_cast<uint32_t>(static_cast<float>(phase) *
                                               animationSpeedFactor(app.settings.animationSpeed));
  switch (mode) {
    case HomeMode::Static:   drawStaticLogo();               break;
    case HomeMode::Water:    drawDistortedRows(tick, true);  break;
    case HomeMode::SineWave: drawDistortedRows(tick, false); break;
    case HomeMode::RotoZoom: drawRotoZoom(tick);             break;
    case HomeMode::Ripple:   drawRipple(tick);               break;
    case HomeMode::Raster:   drawRasterBars(tick);           break;
  }
}

// ===========================================================================
//  ReST-API des c64u
// ===========================================================================
String urlEncode(const String& value) {
  static const char* hex = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    const bool safe = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String apiBaseUrl() { return String("http://") + targetHost(); }

String extractErrors(DynamicJsonDocument& doc) {
  if (!doc.containsKey("errors")) return "";
  String text;
  JsonArray errors = doc["errors"].as<JsonArray>();
  for (JsonVariant value : errors) {
    if (!text.isEmpty()) text += ", ";
    text += value.as<const char*>();
  }
  return text;
}

String jsonValueToString(JsonVariantConst value) {
  if (value.is<const char*>()) return String(value.as<const char*>());
  if (value.is<String>())      return value.as<String>();
  if (value.is<long>())        return String(value.as<long>());
  if (value.is<int>())         return String(value.as<int>());
  if (value.is<bool>())        return value.as<bool>() ? "Yes" : "No";
  String text;
  serializeJson(value, text);
  return text;
}

String extractDigits(const String& value) {
  String digits;
  for (size_t i = 0; i < value.length(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(value[i]))) digits += value[i];
  }
  return digits;
}

ApiResponse sendApiRequestOnce(const char* method, const String& path, bool authenticated) {
  ApiResponse result;
  if (!hasTargetConfig()) { result.errors = "Target host missing"; return result; }

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  const String url = apiBaseUrl() + path;
  if (!http.begin(url)) { result.errors = "HTTP begin failed"; return result; }

  if (authenticated && !targetPassword().isEmpty()) {
    http.addHeader("X-Password", targetPassword());
  }

  if (strcmp(method, "GET") == 0) {
    result.httpCode = http.GET();
  } else if (strcmp(method, "PUT") == 0) {
    result.httpCode = http.sendRequest("PUT", "");
  } else {
    http.end();
    result.errors = "Unsupported method";
    return result;
  }

  result.transportOk = result.httpCode > 0;
  if (result.transportOk) {
    result.body = http.getString();
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, result.body) == DeserializationError::Ok) {
      result.jsonOk = true;
      result.errors = extractErrors(doc);
      result.apiOk  = result.httpCode >= 200 && result.httpCode < 300 && result.errors.isEmpty();
    } else {
      result.apiOk = result.httpCode >= 200 && result.httpCode < 300;
    }
  } else {
    result.errors = http.errorToString(result.httpCode);
  }
  http.end();
  return result;
}

// Der HTTP-Server im c64u nimmt jeweils nur eine Verbindung an. Haengt ein
// zweites Geraet im Netz und fragt zufaellig im selben Moment, kommt
// "connection refused" zurueck, obwohl mit Funk und Adresse alles stimmt.
// Ein Transportfehler heisst, dass beim c64u nichts angekommen ist - ein
// zweiter Versuch ist deshalb gefahrlos und rettet genau diesen Fall.
ApiResponse sendApiRequest(const char* method, const String& path, bool authenticated) {
  ApiResponse result = sendApiRequestOnce(method, path, authenticated);
  if (!result.transportOk && hasTargetConfig()) {
    delay(kApiRetryDelayMs);
    result = sendApiRequestOnce(method, path, authenticated);
  }
  return result;
}

// ---------------------------------------------------------------------------
// CPU-Speed: Pfad in der Konfiguration suchen und Auswahl einlesen
// ---------------------------------------------------------------------------
String cpuLabelFromValue(const String& rawValue) {
  const String trimmed = trimCopy(rawValue);
  const String digits  = extractDigits(trimmed);
  for (size_t i = 0; i < app.cpuChoiceCount; ++i) {
    if (trimCopy(app.cpuWireOptions[i]) == trimmed ||
        extractDigits(app.cpuWireOptions[i]) == digits ||
        trimCopy(app.cpuDisplayOptions[i]).equalsIgnoreCase(trimmed)) {
      return app.cpuDisplayOptions[i];
    }
  }
  if (!digits.isEmpty()) return digits + " MHz";
  return trimmed.isEmpty() ? "Unknown" : trimmed;
}

int cpuIndexFromValue(const String& rawValue) {
  const String trimmed = trimCopy(rawValue);
  const String digits  = extractDigits(trimmed);
  for (size_t i = 0; i < app.cpuChoiceCount; ++i) {
    if (trimCopy(app.cpuWireOptions[i]) == trimmed ||
        extractDigits(app.cpuWireOptions[i]) == digits ||
        trimCopy(app.cpuDisplayOptions[i]).equalsIgnoreCase(trimmed)) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void setFallbackCpuChoices() {
  static const char* fallback[] = {" 1", " 2", " 3", " 4", " 6", " 8", "10", "12",
                                   "14", "16", "20", "24", "32", "40", "48", "64"};
  app.cpuChoiceCount = std::min(kMaxCpuChoices, sizeof(fallback) / sizeof(fallback[0]));
  for (size_t i = 0; i < app.cpuChoiceCount; ++i) {
    app.cpuWireOptions[i]    = fallback[i];
    app.cpuDisplayOptions[i] = trimCopy(fallback[i]) + " MHz";
  }
}

bool inspectCpuCategory(const String& category, String* itemOut, String* valueOut) {
  const ApiResponse response = sendApiRequest("GET", "/v1/configs/" + urlEncode(category), true);
  if (!response.apiOk) return false;

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, response.body) != DeserializationError::Ok) return false;

  JsonVariant categoryObject = doc[category];
  if (categoryObject.isNull()) {
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (String(kv.key().c_str()) != "errors" && kv.value().is<JsonObject>()) {
        categoryObject = kv.value();
        break;
      }
    }
  }
  if (categoryObject.isNull() || !categoryObject.is<JsonObject>()) return false;

  for (JsonPair kv : categoryObject.as<JsonObject>()) {
    const String key = kv.key().c_str();
    if (key.indexOf("CPU") >= 0 && key.indexOf("Speed") >= 0) {
      *itemOut  = key;
      *valueOut = jsonValueToString(kv.value());
      return true;
    }
  }
  return false;
}

bool refreshCpuChoices() {
  if (app.cpuCategory.isEmpty() || app.cpuItem.isEmpty()) { setFallbackCpuChoices(); return false; }

  const ApiResponse response = sendApiRequest(
      "GET", "/v1/configs/" + urlEncode(app.cpuCategory) + "/" + urlEncode(app.cpuItem), true);
  if (!response.apiOk) { setFallbackCpuChoices(); return false; }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, response.body) != DeserializationError::Ok) { setFallbackCpuChoices(); return false; }

  JsonVariant itemObject = doc[app.cpuCategory][app.cpuItem];
  if (itemObject.isNull()) { setFallbackCpuChoices(); return false; }

  app.cpuChoiceCount = 0;
  JsonArray values = itemObject["values"].as<JsonArray>();
  for (JsonVariant value : values) {
    if (app.cpuChoiceCount >= kMaxCpuChoices) break;
    const String wire   = jsonValueToString(value);
    const String digits = extractDigits(wire);
    app.cpuWireOptions[app.cpuChoiceCount]    = wire;
    app.cpuDisplayOptions[app.cpuChoiceCount] = digits.isEmpty() ? trimCopy(wire) : digits + " MHz";
    app.cpuChoiceCount += 1;
  }
  if (app.cpuChoiceCount == 0) { setFallbackCpuChoices(); return false; }

  app.currentCpuValue = cpuLabelFromValue(jsonValueToString(itemObject["current"]));
  return true;
}

bool resolveCpuPath(String* detailOut = nullptr) {
  if (app.cpuPathKnown) {
    if (app.cpuChoiceCount == 0) refreshCpuChoices();
    return true;
  }

  String item, value;
  if (inspectCpuCategory("U64 Specific Settings", &item, &value)) {
    app.cpuCategory     = "U64 Specific Settings";
    app.cpuItem         = item;
    app.currentCpuValue = cpuLabelFromValue(value);
    app.cpuPathKnown    = true;
    refreshCpuChoices();
    return true;
  }

  const ApiResponse listResponse = sendApiRequest("GET", "/v1/configs", true);
  if (!listResponse.apiOk) {
    if (detailOut) *detailOut = listResponse.errors.isEmpty() ? "Config list failed" : listResponse.errors;
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, listResponse.body) != DeserializationError::Ok) {
    if (detailOut) *detailOut = "Config list parse failed";
    return false;
  }

  JsonArray categories = doc["categories"].as<JsonArray>();
  for (JsonVariant valueVariant : categories) {
    const String category = valueVariant.as<const char*>();
    if (inspectCpuCategory(category, &item, &value)) {
      app.cpuCategory     = category;
      app.cpuItem         = item;
      app.currentCpuValue = cpuLabelFromValue(value);
      app.cpuPathKnown    = true;
      refreshCpuChoices();
      return true;
    }
  }
  if (detailOut) *detailOut = "CPU speed item not found";
  return false;
}

void refreshCpuValue() {
  String detail;
  if (!resolveCpuPath(&detail)) { app.currentCpuValue = detail; return; }
  refreshCpuChoices();
}

// ---------------------------------------------------------------------------
// WLAN und Verbindungsstatus
// ---------------------------------------------------------------------------
// Verbindet mit dem naechsten gespeicherten Netz. Sind mehrere Profile
// hinterlegt, wandert der Versuch bei jedem Aufruf eins weiter - so werden
// nacheinander alle bekannten Netze durchprobiert.
void beginWiFi(uint32_t now) {
  if (!hasWiFiConfig()) return;
  if (gWifiTry >= gWifiCount) gWifiTry = 0;

  const WifiProfile& profile = gWifiProfiles[gWifiTry];

  // Waehrend das Setup-Portal laeuft, bleibt der Accesspoint bestehen.
  if (!app.portalActive) WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(profile.ssid.c_str(), profile.pass.c_str());
  app.lastWiFiAttemptMs = now;

  if (gWifiCount > 1) gWifiTry = (gWifiTry + 1) % gWifiCount;
}

int wifiProfileIndex(const String& ssid);

// Merkt sich, mit welchem Netz die Verbindung zustande kam. Nach einem
// Aussetzer wird dann zuerst wieder dieses versucht statt blind das naechste.
// Sind zwei Netze gespeichert und nur eines ist erreichbar, kostet das sonst
// jedes zweite Mal einen kompletten Wiederholungstakt.
bool gWifiNoted = false;

void serviceWiFi(uint32_t now) {
  if (app.portalActive) return;          // Portal hat Vorrang
  if (!hasWiFiConfig()) return;
  if (WiFi.status() == WL_CONNECTED) {
    if (!gWifiNoted) {
      const int index = wifiProfileIndex(WiFi.SSID());
      if (index >= 0) gWifiTry = static_cast<size_t>(index);
      gWifiNoted = true;
    }
    return;
  }
  gWifiNoted = false;
  if (app.lastWiFiAttemptMs == 0 || now - app.lastWiFiAttemptMs >= kWiFiRetryMs) beginWiFi(now);
}

void refreshConnectionStatus(uint32_t now, bool force = false) {
  app.connection.wifiConnected = WiFi.status() == WL_CONNECTED;

  if (!configReady()) {
    app.connection.targetReachable = false;
    app.connection.authOk          = false;
    app.connection.detail          = hasWiFiConfig() ? "c64u-Adresse fehlt"
                                                     : "Settings > WLAN einrichten";
  } else if (!app.connection.wifiConnected) {
    app.connection.targetReachable = false;
    app.connection.authOk          = false;
    app.connection.detail          = "WiFi disconnected";
  } else if (force ||
             ((app.screen == ScreenMode::Home || app.screen == ScreenMode::Status) &&
              (app.lastConnectionProbeMs == 0 ||
               now - app.lastConnectionProbeMs >= kConnectionProbeMs))) {
    // Der Statustest kostet zwei HTTP-Aufrufe und blockiert die Schleife bis
    // zu zwei Sekunden. Waehrend dieser Zeit wird der Touchscreen nicht
    // abgefragt - Tipper gingen dadurch verloren. Deshalb laeuft der
    // regelmaessige Test nur im Leerlauf (Home-Screen bzw. Statusseite),
    // waehrend der Bedienung nur noch auf ausdrueckliche Anforderung.
    app.lastConnectionProbeMs = now;

    const ApiResponse reach = sendApiRequest("GET", "/v1/version", false);
    app.connection.targetReachable = reach.transportOk;
    if (!reach.transportOk) {
      app.connection.authOk = false;
      app.connection.detail = reach.errors.isEmpty() ? "Target unreachable" : reach.errors;
    } else if (targetPassword().isEmpty()) {
      // Ohne hinterlegtes Passwort waere die zweite Anfrage byte-gleich mit
      // der ersten - der Header X-Password wird ja nur gesetzt, wenn eines da
      // ist. Jede gesparte Anfrage macht auf dem c64u Platz fuer ein zweites
      // Geraet im Netz.
      app.connection.authOk = reach.apiOk;
      app.connection.detail = reach.apiOk ? "Reachable + auth ok"
                                          : (reach.errors.isEmpty() ? "Auth failed" : reach.errors);
    } else {
      const ApiResponse auth = sendApiRequest("GET", "/v1/version", true);
      app.connection.authOk = auth.apiOk;
      app.connection.detail = auth.apiOk ? "Reachable + auth ok"
                                         : (auth.errors.isEmpty() ? "Auth failed" : auth.errors);
    }
  }
}

bool requireNetwork(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) return true;
  beginWiFi(now);
  setModal("NO WIFI", kColWarn, now);
  return false;
}

// ---------------------------------------------------------------------------
// Einfache Maschinen-Kommandos
// ---------------------------------------------------------------------------
void clearPendingPowerOff() { app.pendingPowerOff = false; }

void simpleCommand(const char* path, const String& okText, const String& failText,
                   uint16_t okColor, uint32_t now) {
  if (!requireNetwork(now)) return;
  const ApiResponse response = sendApiRequest("PUT", path, true);
  if (response.apiOk) {
    beep(2400, 40);
    setModal(okText, okColor, now);
  } else {
    beep(500, 120);
    setModal(response.errors.isEmpty() ? failText : response.errors, kColErr, now, 2000);
  }
}

void performReset(uint32_t now)      { clearPendingPowerOff(); simpleCommand("/v1/machine:reset", "RESET", "RESET FAILED", kColOk, now); }
void performHardReset(uint32_t now)  { clearPendingPowerOff(); simpleCommand("/v1/machine:reboot", "REBOOT", "REBOOT FAILED", kColWarn, now); }
void performMenuButton(uint32_t now) { clearPendingPowerOff(); simpleCommand("/v1/machine:menu_button", "ULTIMATE MENU", "MENU FAILED", kColInfo, now); }

void performPowerOff(uint32_t now) {
  clearPendingPowerOff();
  simpleCommand("/v1/machine:poweroff", "c64u POWER OFF", "POWEROFF FAILED", kColWarn, now);
}

void requestPowerOff(uint32_t now) {
  if (app.pendingPowerOff && (now - app.pendingPowerOffAtMs <= powerOffConfirmMs())) {
    performPowerOff(now);
    return;
  }
  app.pendingPowerOff     = true;
  app.pendingPowerOffAtMs = now;
  beep(900, 60);
  setModal("c64u OFF? NOCHMAL!", kColWarn, now, powerOffConfirmMs());
}

void setCpuSpeed(int cpuIndex, uint32_t now) {
  clearPendingPowerOff();
  if (!requireNetwork(now)) return;

  String detail;
  if (!resolveCpuPath(&detail)) { setModal(detail, kColErr, now, 2000); return; }
  if (app.cpuChoiceCount == 0) refreshCpuChoices();

  const int idx = std::max(0, std::min(cpuIndex, static_cast<int>(app.cpuChoiceCount) - 1));
  const String displayValue = app.cpuDisplayOptions[idx];
  const String path = "/v1/configs/" + urlEncode(app.cpuCategory) + "/" + urlEncode(app.cpuItem)
                    + "?value=" + urlEncode(app.cpuWireOptions[idx]);

  const ApiResponse response = sendApiRequest("PUT", path, true);
  if (response.apiOk) {
    refreshCpuValue();
    beep(2600, 40);
    setModal(displayValue, kColOk, now);
  } else {
    beep(500, 120);
    setModal(response.errors.isEmpty() ? "CPU SET FAILED" : response.errors, kColErr, now, 2000);
  }
}

// ---------------------------------------------------------------------------
// Joystick-Ports
//
// Einen eigenen "machine:"-Befehl fuer das Tauschen der Ports gibt es in der
// ReST-API nicht. Der c64u fuehrt die Belegung als Konfigurationseintrag
// "Joystick Swapper" in der Kategorie "U64 Specific Settings"; gesetzt wird
// also ueber /v1/configs, genau wie die CPU-Stufe. Die Werteliste kommt vom
// Geraet und heisst je nach Firmware "Normal", "Swapped", "WASD Port 1",
// "WASD Port 2".
// ---------------------------------------------------------------------------

// Kurzform fuer die Karte: "Swapped" -> "SWAPPED", "WASD Port 1" -> "WASD1".
String joyTokenFromValue(const String& value) {
  String upper = trimCopy(value);
  upper.toUpperCase();
  if (upper.indexOf("WASD") >= 0) {
    const String digits = extractDigits(upper);
    return digits.isEmpty() ? String("WASD") : ("WASD" + digits);
  }
  if (upper.indexOf("SWAP")   >= 0) return "SWAPPED";
  if (upper.indexOf("NORMAL") >= 0) return "NORMAL";
  upper.replace(" ", "");
  return upper;
}

// Sucht zu einer Kurzform den Wert, den der c64u erwartet. Ist die Liste noch
// unbekannt, wird die Kurzform unveraendert durchgereicht.
String joyValueFromToken(const String& token) {
  const String want = joyTokenFromValue(token);
  for (size_t i = 0; i < app.joyChoiceCount; ++i) {
    if (joyTokenFromValue(app.joyOptions[i]) == want) return app.joyOptions[i];
  }
  return trimCopy(token);
}

// Kurzer Text fuer Liste und Meldung.
String joyLabelFromToken(const String& token) {
  const String t = joyTokenFromValue(token);
  if (t == "NORMAL")  return "Normal";
  if (t == "SWAPPED") return "Swapped";
  if (t == "WASD")    return "WASD";
  if (t.startsWith("WASD")) return "WASD P" + t.substring(4);
  return trimCopy(token);
}

// Sucht in einer Kategorie den Eintrag mit "Joystick" im Namen.
bool inspectJoyCategory(const String& category, String* itemOut, String* valueOut) {
  const ApiResponse response = sendApiRequest("GET", "/v1/configs/" + urlEncode(category), true);
  if (!response.apiOk) return false;

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, response.body) != DeserializationError::Ok) return false;

  JsonVariant categoryObject = doc[category];
  if (categoryObject.isNull()) {
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (String(kv.key().c_str()) != "errors" && kv.value().is<JsonObject>()) {
        categoryObject = kv.value();
        break;
      }
    }
  }
  if (categoryObject.isNull() || !categoryObject.is<JsonObject>()) return false;

  for (JsonPair kv : categoryObject.as<JsonObject>()) {
    const String key = kv.key().c_str();
    if (key.indexOf("Joystick") >= 0) {
      *itemOut  = key;
      *valueOut = jsonValueToString(kv.value());
      return true;
    }
  }
  return false;
}

// Holt Werteliste und aktuellen Stand des gefundenen Eintrags.
bool refreshJoyChoices() {
  if (app.joyCategory.isEmpty() || app.joyItem.isEmpty()) return false;

  const ApiResponse response = sendApiRequest(
      "GET", "/v1/configs/" + urlEncode(app.joyCategory) + "/" + urlEncode(app.joyItem), true);
  if (!response.apiOk) return false;

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, response.body) != DeserializationError::Ok) return false;

  JsonVariant itemObject = doc[app.joyCategory][app.joyItem];
  if (itemObject.isNull()) return false;

  app.joyChoiceCount = 0;
  JsonArray values = itemObject["values"].as<JsonArray>();
  for (JsonVariant value : values) {
    if (app.joyChoiceCount >= kMaxJoyChoices) break;
    app.joyOptions[app.joyChoiceCount] = trimCopy(jsonValueToString(value));
    app.joyChoiceCount += 1;
  }
  if (app.joyChoiceCount == 0) return false;

  app.joyValue = trimCopy(jsonValueToString(itemObject["current"]));
  return true;
}

// Findet Kategorie und Eintragsnamen einmalig heraus und merkt sie sich.
bool resolveJoyPath(String* detailOut = nullptr) {
  if (app.joyPathKnown) {
    if (app.joyChoiceCount == 0) refreshJoyChoices();
    return true;
  }

  String item, value;
  if (inspectJoyCategory("U64 Specific Settings", &item, &value)) {
    app.joyCategory  = "U64 Specific Settings";
    app.joyItem      = item;
    app.joyValue     = trimCopy(value);
    app.joyPathKnown = true;
    refreshJoyChoices();
    return true;
  }

  const ApiResponse listResponse = sendApiRequest("GET", "/v1/configs", true);
  if (!listResponse.apiOk) {
    if (detailOut) *detailOut = listResponse.errors.isEmpty() ? "Config list failed" : listResponse.errors;
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, listResponse.body) != DeserializationError::Ok) {
    if (detailOut) *detailOut = "Config list parse failed";
    return false;
  }

  JsonArray categories = doc["categories"].as<JsonArray>();
  for (JsonVariant valueVariant : categories) {
    const String category = valueVariant.as<const char*>();
    if (inspectJoyCategory(category, &item, &value)) {
      app.joyCategory  = category;
      app.joyItem      = item;
      app.joyValue     = trimCopy(value);
      app.joyPathKnown = true;
      refreshJoyChoices();
      return true;
    }
  }
  if (detailOut) *detailOut = "Joystick item not found";
  return false;
}

// Setzt die Portbelegung auf einen festen Wert.
void applyJoystickValue(const String& wanted, uint32_t now) {
  clearPendingPowerOff();
  if (!requireNetwork(now)) return;

  String detail;
  if (!resolveJoyPath(&detail)) { setModal(detail, kColErr, now, 2000); return; }

  const String target = joyValueFromToken(wanted);
  const String path   = "/v1/configs/" + urlEncode(app.joyCategory) + "/" + urlEncode(app.joyItem)
                      + "?value=" + urlEncode(target);

  const ApiResponse response = sendApiRequest("PUT", path, true);
  if (response.apiOk) {
    app.joyValue = target;
    beep(2600, 40);
    setModal("JOY " + joyLabelFromToken(target), kColOk, now);
  } else {
    beep(500, 120);
    setModal(response.errors.isEmpty() ? "JOY SET FAILED" : response.errors, kColErr, now, 2000);
  }
}

// Schaltet zwischen "Normal" und "Swapped" hin und her. Steht der c64u auf
// einem WASD-Modus, geht es zurueck auf "Normal".
void toggleJoystickSwap(uint32_t now) {
  clearPendingPowerOff();
  if (!requireNetwork(now)) return;

  String detail;
  if (!resolveJoyPath(&detail)) { setModal(detail, kColErr, now, 2000); return; }
  refreshJoyChoices();

  applyJoystickValue(joyTokenFromValue(app.joyValue) == "NORMAL" ? "SWAPPED" : "NORMAL", now);
}

// Schaltet im Settings-Menue durch alle Werte, die der c64u anbietet.
void cycleJoystickValue(uint32_t now) {
  clearPendingPowerOff();
  if (!requireNetwork(now)) return;

  String detail;
  if (!resolveJoyPath(&detail)) { setModal(detail, kColErr, now, 2000); return; }
  refreshJoyChoices();
  if (app.joyChoiceCount == 0) { setModal("JOY?", kColErr, now, 2000); return; }

  size_t index = 0;
  for (size_t i = 0; i < app.joyChoiceCount; ++i) {
    if (joyTokenFromValue(app.joyOptions[i]) == joyTokenFromValue(app.joyValue)) { index = i; break; }
  }
  index = (index + 1) % app.joyChoiceCount;
  applyJoystickValue(app.joyOptions[index], now);
}

void runConnectionTest(uint32_t now) {
  clearPendingPowerOff();
  refreshConnectionStatus(now, true);

  if (!configReady()) {
    setModal("CONFIG MISSING", kColWarn, now, 1800);
  } else if (!app.connection.wifiConnected) {
    beginWiFi(now);
    setModal("WIFI NOT READY", kColWarn, now, 1800);
  } else if (app.connection.authOk) {
    refreshCpuValue();
    setModal("AUTH OK", kColOk, now, 1400);
  } else if (app.connection.targetReachable) {
    setModal("AUTH FAILED", kColWarn, now, 1800);
  } else {
    setModal("TARGET OFFLINE", kColErr, now, 1800);
  }
}

// ===========================================================================
//  PETSCII-Tastatureingabe (fuer Autostart nach dem Mounten)
// ===========================================================================
String toHex(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

// Schreibt bis zu 10 PETSCII-Zeichen in den Tastaturpuffer des C64
// ($0277..$0280) und setzt die Anzahl in NDX ($C6).
bool typeToC64(const uint8_t* petscii, size_t len) {
  if (len == 0 || len > 10) return false;
  if (!sendApiRequest("PUT", "/v1/machine:writemem?address=C5&data=0000", true).apiOk) return false;
  if (!sendApiRequest("PUT", "/v1/machine:writemem?address=277&data=" + toHex(petscii, len), true).apiOk) return false;
  const uint8_t count = static_cast<uint8_t>(len);
  return sendApiRequest("PUT", "/v1/machine:writemem?address=C6&data=" + toHex(&count, 1), true).apiOk;
}

// Liest ein einzelnes Byte aus dem C64-Speicher (per DMA).
bool readC64Byte(uint16_t address, uint8_t* out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  char path[72];
  snprintf(path, sizeof(path), "/v1/machine:readmem?address=%X&length=1", address);

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(apiBaseUrl() + path)) return false;
  if (!targetPassword().isEmpty()) http.addHeader("X-Password", targetPassword());

  bool ok = false;
  if (http.GET() == 200) {
    WiFiClient* stream = http.getStreamPtr();
    const uint32_t deadline = millis() + 2000;
    while (millis() < deadline) {
      if (stream->available()) {
        *out = static_cast<uint8_t>(stream->read());
        ok = true;
        break;
      }
      delay(5);
    }
  }
  http.end();
  return ok;
}

// $CC (BLNSW) ist 0, solange der Cursor blinkt - also genau dann, wenn BASIC
// auf Eingaben wartet. Waehrend LOAD/RUN ist der Wert ungleich 0.
constexpr uint16_t kAddrCursorBlink = 0x00CC;

bool waitCursorBlinking(bool wantBlinking, uint32_t timeoutMs, const char* label) {
  const uint32_t start = millis();
  uint8_t stable = 0;
  uint32_t lastUi = 0;

  while (millis() - start < timeoutMs) {
    uint8_t value = 0xFF;
    if (readC64Byte(kAddrCursorBlink, &value)) {
      const bool blinking = (value == 0);
      if (blinking == wantBlinking) {
        if (++stable >= 2) return true;
      } else {
        stable = 0;
      }
    } else {
      stable = 0;
    }

    const uint32_t now = millis();
    if (now - lastUi > 400) {
      lastUi = now;
      app.busyDetail = String(label) + " (" + String((timeoutMs - (now - start)) / 1000) + "s)";
      drawBusyScreen();
    }
    delay(250);
  }
  return false;
}

// Ermittelt das Laufwerk am IEC-Bus 8.
String resolveTargetDrive() {
  if (app.settings.uploadDrive == UploadDriveMode::DriveA) return "a";
  if (app.settings.uploadDrive == UploadDriveMode::DriveB) return "b";

  const ApiResponse response = sendApiRequest("GET", "/v1/drives", true);
  if (response.apiOk) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, response.body) == DeserializationError::Ok) {
      JsonArray drives = doc["drives"].as<JsonArray>();
      for (JsonVariant item : drives) {
        if (!item.is<JsonObject>()) continue;
        for (JsonPair kv : item.as<JsonObject>()) {
          const String key = kv.key().c_str();
          if (key != "a" && key != "b") continue;          // softiec ueberspringen
          if (!kv.value().is<JsonObject>()) continue;
          JsonObject drive = kv.value().as<JsonObject>();
          if (drive["bus_id"].as<int>() == 8) return key;
        }
      }
    }
  }
  return "a";   // Werkseinstellung von Laufwerk A ist Bus 8
}

// lO"*",8,1<CR>   ("lO" = abgekuerztes LOAD, passt so in 10 Zeichen)
bool typeLoadFirstFile() {
  static const uint8_t seq[] = {0x4C, 0xCF, 0x22, 0x2A, 0x22, 0x2C, 0x38, 0x2C, 0x31, 0x0D};
  return typeToC64(seq, sizeof(seq));
}

// rU<CR>          ("rU" = abgekuerztes RUN)
bool typeRun() {
  static const uint8_t seq[] = {0x52, 0xD5, 0x0D};
  return typeToC64(seq, sizeof(seq));
}

// ===========================================================================
//  Streaming-Upload von der microSD an den c64u
// ===========================================================================
struct UploadResult {
  bool   ok       = false;
  int    httpCode = -1;
  String message;
};

void publishProgress(size_t sent, size_t total) {
  app.busySent  = sent;
  app.busyTotal = total;
  drawBusyScreen();
}

UploadResult readHttpResponse(WiFiClient& client) {
  UploadResult result;
  const uint32_t deadline = millis() + 15000;

  String statusLine;
  while (client.connected() || client.available()) {
    if (millis() > deadline) { result.message = "Timeout"; return result; }
    if (!client.available()) { delay(5); continue; }
    statusLine = client.readStringUntil('\n');
    break;
  }
  statusLine.trim();
  const int firstSpace = statusLine.indexOf(' ');
  if (firstSpace > 0) result.httpCode = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();

  while (client.connected() || client.available()) {
    if (millis() > deadline) break;
    if (!client.available()) { delay(5); continue; }
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) break;
  }

  String body;
  while ((client.connected() || client.available()) && body.length() < 1024) {
    if (millis() > deadline) break;
    if (!client.available()) { delay(5); continue; }
    body += static_cast<char>(client.read());
  }

  result.ok = result.httpCode >= 200 && result.httpCode < 300;

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, body) == DeserializationError::Ok) {
    const String errors = extractErrors(doc);
    if (!errors.isEmpty()) { result.ok = false; result.message = errors; }
  }
  if (result.message.isEmpty()) {
    result.message = result.ok ? "OK" : (result.httpCode == 403 ? "Forbidden (Passwort?)"
                                                                : String("HTTP ") + result.httpCode);
  }
  return result;
}

// urlPath        z. B. "/v1/runners:run_prg"
// multipartName  leer  -> Datei als reiner Body (octet-stream)
//                sonst -> multipart/form-data mit genau diesem einen Dateifeld
UploadResult uploadFile(const String& urlPath, File& file, const String& fileName,
                        const String& multipartName) {
  UploadResult result;
  const size_t fileSize = file.size();

  WiFiClient client;
  client.setTimeout(10);   // Sekunden (WiFiClient)
  if (!client.connect(targetHost().c_str(), 80)) {
    result.message = "Verbindung fehlgeschlagen";
    return result;
  }

  const bool multipart = !multipartName.isEmpty();
  const String boundary = "----C64uRemoteBoundary7A31";

  String head;
  String tail;
  size_t contentLength = fileSize;

  if (multipart) {
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"" + multipartName +
            "\"; filename=\"" + fileName + "\"\r\n";
    head += "Content-Type: application/octet-stream\r\n\r\n";
    tail  = "\r\n--" + boundary + "--\r\n";
    contentLength = head.length() + fileSize + tail.length();
  }

  String request;
  request.reserve(320);
  request += "POST " + urlPath + " HTTP/1.1\r\n";
  request += "Host: " + targetHost() + "\r\n";
  if (!targetPassword().isEmpty()) request += "X-Password: " + targetPassword() + "\r\n";
  request += "Content-Type: ";
  request += multipart ? ("multipart/form-data; boundary=" + boundary) : String("application/octet-stream");
  request += "\r\n";
  request += "Content-Length: " + String(contentLength) + "\r\n";
  request += "Connection: close\r\n\r\n";

  client.print(request);
  if (multipart && !head.isEmpty()) client.print(head);

  static uint8_t buffer[kUploadChunk];
  size_t sent = 0;
  uint32_t lastUi = 0;
  while (sent < fileSize) {
    const int chunk = file.read(buffer, kUploadChunk);
    if (chunk <= 0) break;
    const size_t written = client.write(buffer, static_cast<size_t>(chunk));
    if (written != static_cast<size_t>(chunk)) {
      client.stop();
      result.message = "Upload abgebrochen";
      return result;
    }
    sent += written;

    const uint32_t now = millis();
    if (now - lastUi > 150) {
      lastUi = now;
      publishProgress(sent, fileSize);
    }
    if (!client.connected()) break;
  }
  if (multipart && !tail.isEmpty()) client.print(tail);
  client.flush();
  publishProgress(fileSize, fileSize);

  result = readHttpResponse(client);
  client.stop();
  return result;
}

// ===========================================================================
//  microSD (nachgeruestetes Modul an Port A + Port B)
// ===========================================================================
bool initSd() {
  static bool spiStarted = false;
  if (!spiStarted) {
    sdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    spiStarted = true;
  }
  // Erst schnell versuchen, dann langsam - lange Grove-Kabel vertragen
  // nicht immer 20 MHz.
  app.sdReady = SD.begin(kSdCsPin, sdSpi, 20000000);
  if (!app.sdReady) {
    delay(50);
    app.sdReady = SD.begin(kSdCsPin, sdSpi, 4000000);
  }
  if (!app.sdReady) {
    delay(50);
    app.sdReady = SD.begin(kSdCsPin, sdSpi, 1000000);
  }
  return app.sdReady;
}

String lowerExt(const String& name) {
  const int dot = name.lastIndexOf('.');
  if (dot < 0) return "";
  String ext = name.substring(dot + 1);
  ext.toLowerCase();
  return ext;
}

bool isSupportedExt(const String& ext) {
  return ext == "prg" || ext == "crt" || ext == "sid" || ext == "mod" ||
         ext == "d64" || ext == "d71" || ext == "d81" || ext == "g64" || ext == "g71";
}

bool isDiskImageExt(const String& ext) {
  return ext == "d64" || ext == "d71" || ext == "d81" || ext == "g64" || ext == "g71";
}

String joinPath(const String& dir, const String& name) {
  if (dir.endsWith("/")) return dir + name;
  return dir + "/" + name;
}

String parentPath(const String& path) {
  if (path == "/" || path.isEmpty()) return "/";
  String p = path;
  if (p.endsWith("/")) p.remove(p.length() - 1);
  const int slash = p.lastIndexOf('/');
  if (slash <= 0) return "/";
  return p.substring(0, slash);
}

String baseName(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash < 0 ? path : path.substring(slash + 1);
}

void sortDirEntries() {
  // Ordner zuerst, danach alphabetisch (Insertion Sort, n <= 160)
  for (size_t i = 1; i < app.sdCount; ++i) {
    DirEntryInfo key = app.sdEntries[i];
    size_t j = i;
    while (j > 0) {
      const DirEntryInfo& prev = app.sdEntries[j - 1];
      const bool keyFirst = (key.isDir != prev.isDir) ? key.isDir
                                                      : (strcasecmp(key.name.c_str(), prev.name.c_str()) < 0);
      if (!keyFirst) break;
      app.sdEntries[j] = prev;
      --j;
    }
    app.sdEntries[j] = key;
  }
}

bool readDirectory(const String& path) {
  app.sdCount = 0;
  app.sdIndex = 0;

  if (!app.sdReady && !initSd()) return false;

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }

  while (app.sdCount < kMaxDirEntries) {
    File entry = dir.openNextFile();
    if (!entry) break;

    String name = baseName(String(entry.name()));
    const bool isDir = entry.isDirectory();
    entry.close();

    if (name.isEmpty() || name.startsWith(".")) continue;
    const String ext = lowerExt(name);
    if (!isDir) {
      // Zufallskarte: nur Ordner anbieten
      if (app.sdPickMode == 3) continue;
      else if (app.sdPickMode == 2) { if (ext != "nfc") continue; }
      else if (!isSupportedExt(ext)) continue;
    }

    app.sdEntries[app.sdCount].name  = name;
    app.sdEntries[app.sdCount].isDir = isDir;
    app.sdCount++;
  }
  dir.close();
  sortDirEntries();
  app.sdPath = path;
  return true;
}

// ===========================================================================
//  WLAN-Einrichtung
//
//  Speicher:  NVS-Namensraum "c64unet"
//               wn        Anzahl der Profile (0..kWifiProfileMax)
//               s0..s3    SSID
//               p0..p3    Passwort
//               host      Adresse des c64u
//               hpass     Passwort des c64u
//             Ist noch nichts gespeichert, kommen die Werte aus build_env.h.
//
//  Eingabewege: NFC-Karte, /wifi.txt auf der SD-Karte, Setup-Portal.
// ===========================================================================
constexpr const char* kNetNamespace = "c64unet";

void saveNetConfig() {
  prefs.begin(kNetNamespace, false);
  prefs.putUChar("wn", static_cast<uint8_t>(gWifiCount));
  for (size_t i = 0; i < kWifiProfileMax; ++i) {
    char keySsid[6];
    char keyPass[6];
    snprintf(keySsid, sizeof(keySsid), "s%u", static_cast<unsigned>(i));
    snprintf(keyPass, sizeof(keyPass), "p%u", static_cast<unsigned>(i));
    if (i < gWifiCount) {
      prefs.putString(keySsid, gWifiProfiles[i].ssid);
      prefs.putString(keyPass, gWifiProfiles[i].pass);
    } else {
      prefs.remove(keySsid);
      prefs.remove(keyPass);
    }
  }
  prefs.putString("host",  gTargetHost);
  prefs.putString("hpass", gTargetPass);
  prefs.end();
}

void loadNetConfig() {
  gWifiCount = 0;
  gWifiTry   = 0;

  prefs.begin(kNetNamespace, true);
  const uint8_t stored = prefs.getUChar("wn", 0);
  for (uint8_t i = 0; i < stored && gWifiCount < kWifiProfileMax; ++i) {
    char keySsid[6];
    char keyPass[6];
    snprintf(keySsid, sizeof(keySsid), "s%u", static_cast<unsigned>(i));
    snprintf(keyPass, sizeof(keyPass), "p%u", static_cast<unsigned>(i));
    const String ssid = prefs.getString(keySsid, "");
    if (ssid.isEmpty()) continue;
    gWifiProfiles[gWifiCount].ssid = ssid;
    gWifiProfiles[gWifiCount].pass = prefs.getString(keyPass, "");
    ++gWifiCount;
  }
  gTargetHost = prefs.getString("host",  "");
  gTargetPass = prefs.getString("hpass", "");
  prefs.end();

  // Leere Felder werden aus build_env.h aufgefuellt. Ein dort eingetragenes
  // c64u-Passwort laesst sich damit nicht auf "leer" setzen - dafuer den
  // Eintrag in build_env.h loeschen und neu uebersetzen.
  if (gTargetHost.isEmpty()) gTargetHost = buildHost();
  if (gTargetPass.isEmpty()) gTargetPass = buildHostPass();

  if (gWifiCount == 0 && !buildWifiSsid().isEmpty()) {
    gWifiProfiles[0].ssid = buildWifiSsid();
    gWifiProfiles[0].pass = buildWifiPass();
    gWifiCount = 1;
  }
}

// Neues Netz vorne einsortieren. Ein bereits bekanntes Netz bekommt nur ein
// neues Passwort, das aelteste faellt bei Bedarf hinten heraus.
bool wifiAddProfile(const String& ssidRaw, const String& pass, bool store = true) {
  const String ssid = trimCopy(ssidRaw);
  if (ssid.isEmpty() || ssid.length() > 32) return false;
  if (pass.length() > 63) return false;

  size_t found = kWifiProfileMax;
  for (size_t i = 0; i < gWifiCount; ++i) {
    if (gWifiProfiles[i].ssid == ssid) { found = i; break; }
  }

  if (found < gWifiCount) {
    gWifiProfiles[found].pass = pass;
    for (size_t i = found; i > 0; --i) {
      const WifiProfile tmp = gWifiProfiles[i];
      gWifiProfiles[i]      = gWifiProfiles[i - 1];
      gWifiProfiles[i - 1]  = tmp;
    }
  } else {
    if (gWifiCount < kWifiProfileMax) ++gWifiCount;
    for (size_t i = gWifiCount - 1; i > 0; --i) gWifiProfiles[i] = gWifiProfiles[i - 1];
    gWifiProfiles[0].ssid = ssid;
    gWifiProfiles[0].pass = pass;
  }

  gWifiTry = 0;
  if (store) saveNetConfig();
  return true;
}

bool wifiRemoveProfile(size_t index) {
  if (index >= gWifiCount) return false;
  for (size_t i = index; i + 1 < gWifiCount; ++i) gWifiProfiles[i] = gWifiProfiles[i + 1];
  --gWifiCount;
  gWifiProfiles[gWifiCount] = WifiProfile();
  gWifiTry = 0;
  saveNetConfig();
  return true;
}

void wifiClearProfiles() {
  for (size_t i = 0; i < kWifiProfileMax; ++i) gWifiProfiles[i] = WifiProfile();
  gWifiCount = 0;
  gWifiTry   = 0;
  saveNetConfig();
}

int wifiProfileIndex(const String& ssid) {
  for (size_t i = 0; i < gWifiCount; ++i) {
    if (gWifiProfiles[i].ssid == ssid) return static_cast<int>(i);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Kartentext im WLAN-Schema lesen
//
//     WIFI:S:<ssid>;T:WPA;P:<passwort>;;
//
// Das ist dasselbe Format, das WLAN-QR-Codes verwenden. Jede Handy-App, die
// einen NDEF-Text-Record schreiben kann, ist damit einsetzbar. Als Kurzform
// wird auch "WIFI:<ssid>;<passwort>" akzeptiert.
// ---------------------------------------------------------------------------
bool parseWifiText(const String& raw, String* ssidOut, String* passOut) {
  String text = trimCopy(raw);
  if (text.length() < 6) return false;

  String head = text.substring(0, 5);
  head.toUpperCase();
  if (head != "WIFI:") return false;

  String ssid;
  String pass;
  bool   sawField = false;

  unsigned i = 5;
  while (i < text.length()) {
    String key;
    while (i < text.length() && text[i] != ':' && text[i] != ';') key += text[i++];
    if (i >= text.length()) break;
    if (text[i] == ';') { ++i; continue; }      // Feld ohne Wert
    ++i;                                        // Doppelpunkt ueberspringen

    String value;
    while (i < text.length() && text[i] != ';') {
      if (text[i] == '\\' && i + 1 < text.length()) { value += text[i + 1]; i += 2; continue; }
      value += text[i++];
    }
    if (i < text.length()) ++i;                 // Semikolon ueberspringen

    key.toUpperCase();
    if (key == "S")      { ssid = value; sawField = true; }
    else if (key == "P") { pass = value; sawField = true; }
  }

  // Kurzform ohne Feldnamen: WIFI:<ssid>;<passwort>
  if (!sawField) {
    String rest = text.substring(5);
    const int sep = rest.indexOf(';');
    if (sep < 0) { ssid = trimCopy(rest); }
    else         { ssid = trimCopy(rest.substring(0, sep)); pass = rest.substring(sep + 1); }
    // Ein abschliessendes Semikolon gehoert nicht zum Passwort
    while (pass.endsWith(";")) pass.remove(pass.length() - 1);
  }

  if (ssid.isEmpty()) return false;
  if (ssidOut) *ssidOut = ssid;
  if (passOut) *passOut = pass;
  return true;
}

// Gegenstueck zu parseWifiText: aus SSID und Passwort einen Kartentext bauen.
// Sonderzeichen werden wie im QR-Schema mit Backslash geschuetzt.
String wifiCardText(const String& ssid, const String& pass) {
  auto escape = [](const String& in) {
    String out;
    for (unsigned i = 0; i < in.length(); ++i) {
      const char c = in[i];
      if (c == '\\' || c == ';' || c == ':' || c == ',' || c == '"') out += '\\';
      out += c;
    }
    return out;
  };
  String text = "WIFI:S:" + escape(ssid) + ";T:";
  text += pass.isEmpty() ? "nopass" : "WPA";
  text += ";P:" + escape(pass) + ";;";
  return text;
}

// Steht eine WLAN-Karte auf dem Leser, ohne dass gerade danach gefragt wurde?
bool textLooksLikeWifi(const String& text) {
  String head = trimCopy(text).substring(0, 5);
  head.toUpperCase();
  return head == "WIFI:";
}

// ---------------------------------------------------------------------------
// /wifi.txt von der SD-Karte
//
//     # Kommentar
//     ssid = MeinWLAN
//     pass = geheim
//
//     ssid = Zweitnetz
//     pass = auchgeheim
//
//     host     = 192.168.0.64
//     hostpass =
//
// Jede neue Zeile "ssid" beginnt einen neuen Eintrag. Die Datei darf bis zu
// kWifiProfileMax Netze enthalten.
// ---------------------------------------------------------------------------
String stripQuotes(const String& value) {
  String v = trimCopy(value);
  if (v.length() >= 2 &&
      ((v.startsWith("\"") && v.endsWith("\"")) || (v.startsWith("'") && v.endsWith("'")))) {
    v = v.substring(1, v.length() - 1);
  }
  return v;
}

size_t loadWifiFromSd(String* errorOut) {
  if (!app.sdReady && !initSd()) {
    if (errorOut) *errorOut = "keine SD-Karte";
    return 0;
  }
  File file = SD.open(kWifiFileSd, FILE_READ);
  if (!file) {
    if (errorOut) *errorOut = "wifi.txt fehlt";
    return 0;
  }

  size_t added = 0;
  String ssid;
  String pass;

  auto flush = [&]() {
    if (!ssid.isEmpty() && wifiAddProfile(ssid, pass, false)) ++added;
    ssid = "";
    pass = "";
  };

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue;

    const int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq);
    key.trim();
    key.toLowerCase();
    const String value = stripQuotes(line.substring(eq + 1));

    if (key == "ssid")                              { flush(); ssid = value; }
    else if (key == "pass" || key == "password")    { pass = value; }
    else if (key == "host")                         { if (!value.isEmpty()) gTargetHost = value; }
    else if (key == "hostpass" || key == "hostpassword") { gTargetPass = value; }
  }
  flush();
  file.close();

  saveNetConfig();
  if (added == 0 && errorOut) *errorOut = "keine SSID in wifi.txt";
  return added;
}

// ---------------------------------------------------------------------------
// Gegenstueck zu loadWifiFromSd: alle gespeicherten Netze samt Adresse und
// Passwort des c64u als /wifi.txt auf die SD-Karte schreiben.
//
// Eine bereits vorhandene Datei wird vorher nach /wifi.bak umbenannt, es geht
// also nichts verloren. Die Passwoerter stehen im Klartext in der Datei -
// darauf weist der Kopf der Datei noch einmal hin.
//
// Rueckgabe: Anzahl der geschriebenen Netze, 0 bei einem Fehler.
// ---------------------------------------------------------------------------
constexpr const char* kWifiFileSdBak = "/wifi.bak";

size_t saveWifiToSd(String* errorOut) {
  if (gWifiCount == 0) {
    if (errorOut) *errorOut = "nichts gespeichert";
    return 0;
  }
  if (!app.sdReady && !initSd()) {
    if (errorOut) *errorOut = "keine SD-Karte";
    return 0;
  }

  // Vorhandene Datei zur Seite legen. Ein alter Sicherungsstand muss dafuer
  // weichen, sonst schlaegt SD.rename() fehl.
  if (SD.exists(kWifiFileSd)) {
    if (SD.exists(kWifiFileSdBak)) SD.remove(kWifiFileSdBak);
    if (!SD.rename(kWifiFileSd, kWifiFileSdBak)) SD.remove(kWifiFileSd);
  }

  File file = SD.open(kWifiFileSd, FILE_WRITE);
  if (!file) {
    if (errorOut) *errorOut = "schreiben ging nicht";
    return 0;
  }

  file.println("# ---------------------------------------------------------------------------");
  file.println("# C64uRemote - WLAN-Zugangsdaten fuer den M5Dial");
  file.println("#");
  file.println("# Vom Geraet geschrieben ueber  Settings > WLAN > Auf SD sichern.");
  file.println("# Zurueckgelesen wird die Datei ueber  Settings > WLAN > Von SD laden");
  file.println("# oder beim Start, solange noch kein Netz im Geraet gespeichert ist.");
  file.println("#");
  file.println("# Achtung: die Passwoerter stehen hier im Klartext.");
  file.println("# ---------------------------------------------------------------------------");
  file.println();

  size_t written = 0;
  for (size_t i = 0; i < gWifiCount; ++i) {
    if (gWifiProfiles[i].ssid.isEmpty()) continue;
    file.print("ssid = ");
    file.println(gWifiProfiles[i].ssid);
    file.print("pass = ");
    file.println(gWifiProfiles[i].pass);
    file.println();
    ++written;
  }

  file.println("# Adresse und Passwort des Ultimate 64 / 1541 Ultimate II+");
  file.print("host     = ");
  file.println(gTargetHost);
  file.print("hostpass = ");
  file.println(gTargetPass);

  file.flush();
  file.close();

  if (written == 0) {
    if (errorOut) *errorOut = "kein Netz zu sichern";
    SD.remove(kWifiFileSd);
    return 0;
  }
  return written;
}

// ---------------------------------------------------------------------------
// Netzsuchlauf. Der Aufruf blockiert einige Sekunden - der Aufrufer zeigt
// vorher einen Hinweis an.
// ---------------------------------------------------------------------------
void wifiRunScan() {
  app.wifiScanCount = 0;
  app.wifiScanIndex = 0;

  if (!app.portalActive && WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);

  const int found = WiFi.scanNetworks(false, false);
  for (int i = 0; i < found && app.wifiScanCount < kWifiScanMax; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;

    bool duplicate = false;
    for (size_t k = 0; k < app.wifiScanCount; ++k) {
      if (gWifiScan[k].ssid == ssid) { duplicate = true; break; }
    }
    if (duplicate) continue;

    gWifiScan[app.wifiScanCount].ssid = ssid;
    gWifiScan[app.wifiScanCount].rssi = WiFi.RSSI(i);
    gWifiScan[app.wifiScanCount].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    ++app.wifiScanCount;
  }
  WiFi.scanDelete();
}

// Vor dem ersten Verbindungsversuch das staerkste bekannte Netz heraussuchen.
// Nur sinnvoll, wenn mehr als ein Profil gespeichert ist.
void wifiPickBestProfile() {
  if (gWifiCount < 2) return;

  WiFi.mode(WIFI_STA);
  const int found = WiFi.scanNetworks(false, false);
  int     bestProfile = -1;
  int32_t bestRssi    = -1000;
  for (int i = 0; i < found; ++i) {
    const int profile = wifiProfileIndex(WiFi.SSID(i));
    if (profile >= 0 && WiFi.RSSI(i) > bestRssi) {
      bestRssi    = WiFi.RSSI(i);
      bestProfile = profile;
    }
  }
  WiFi.scanDelete();
  if (bestProfile >= 0) gWifiTry = static_cast<size_t>(bestProfile);
}

// ===========================================================================
//  Setup-Portal: eigener Accesspoint mit kleiner Weboberflaeche
// ===========================================================================
WebServer gPortal(80);
DNSServer gPortalDns;
bool      gPortalRoutesReady = false;

String htmlEscape(const String& raw) {
  String out;
  out.reserve(raw.length() + 8);
  for (unsigned i = 0; i < raw.length(); ++i) {
    const char c = raw[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

const char* kPortalStyle =
    "<style>body{background:#0b1220;color:#dce6f8;font-family:system-ui,sans-serif;"
    "margin:0;padding:18px;}h1{font-size:20px;color:#8ce4ff;margin:0 0 4px;}"
    "p{color:#9cbee4;font-size:13px;margin:4px 0 14px;}"
    "label{display:block;margin:12px 0 4px;font-size:13px;color:#9cbee4;}"
    "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;"
    "border:1px solid #426084;background:#101a2c;color:#dce6f8;font-size:16px;}"
    "button{margin-top:18px;width:100%;padding:12px;border:0;border-radius:8px;"
    "background:#2f6bb0;color:#fff;font-size:16px;}"
    "ul{padding-left:18px;color:#9cbee4;font-size:13px;}"
    "hr{border:0;border-top:1px solid #24344f;margin:20px 0;}</style>";

void portalSendRoot() {
  app.portalTouchedMs = millis();

  String page;
  page.reserve(3000);
  page += "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<title>C64uRemote WLAN</title>";
  page += kPortalStyle;
  page += "</head><body><h1>C64uRemote</h1>";
  page += "<p>WLAN-Zugangsdaten eintragen. Sie werden im Geraet gespeichert - "
          "der Quelltext muss dafuer nicht mehr uebersetzt werden.</p>";

  page += "<form method=\"POST\" action=\"/save\">";

  page += "<label for=\"ssid\">Gefundene Netze</label>";
  page += "<select id=\"ssid\" name=\"ssid\">";
  page += "<option value=\"\">-- bitte waehlen --</option>";
  for (size_t i = 0; i < app.wifiScanCount; ++i) {
    page += "<option value=\"" + htmlEscape(gWifiScan[i].ssid) + "\">";
    page += htmlEscape(gWifiScan[i].ssid);
    page += " (" + String(static_cast<int>(gWifiScan[i].rssi)) + " dBm)";
    page += "</option>";
  }
  page += "</select>";

  page += "<label for=\"ssid2\">oder SSID von Hand</label>";
  page += "<input id=\"ssid2\" name=\"ssid2\" maxlength=\"32\" autocomplete=\"off\">";

  page += "<label for=\"pass\">WLAN-Passwort</label>";
  page += "<input id=\"pass\" name=\"pass\" type=\"password\" maxlength=\"63\">";

  page += "<hr><label for=\"host\">c64u-Adresse (optional)</label>";
  page += "<input id=\"host\" name=\"host\" value=\"" + htmlEscape(gTargetHost) + "\">";
  page += "<label for=\"hpass\">c64u-Passwort (optional)</label>";
  page += "<input id=\"hpass\" name=\"hpass\" type=\"password\" maxlength=\"63\">";

  page += "<button type=\"submit\">Speichern und verbinden</button></form>";

  page += "<hr><p>Gespeicherte Netze (" + String(static_cast<unsigned>(gWifiCount)) + "/" +
          String(static_cast<unsigned>(kWifiProfileMax)) + "):</p><ul>";
  if (gWifiCount == 0) page += "<li>noch keins</li>";
  for (size_t i = 0; i < gWifiCount; ++i) page += "<li>" + htmlEscape(gWifiProfiles[i].ssid) + "</li>";
  page += "</ul></body></html>";

  gPortal.send(200, "text/html; charset=utf-8", page);
}

void portalSendSaved() {
  String ssid = trimCopy(gPortal.arg("ssid2"));
  if (ssid.isEmpty()) ssid = trimCopy(gPortal.arg("ssid"));
  const String pass  = gPortal.arg("pass");
  const String host  = trimCopy(gPortal.arg("host"));
  const String hpass = gPortal.arg("hpass");

  bool ok = false;
  if (!ssid.isEmpty()) ok = wifiAddProfile(ssid, pass, false);
  if (!host.isEmpty())  gTargetHost = host;
  if (!hpass.isEmpty()) gTargetPass = hpass;
  saveNetConfig();

  String page;
  page.reserve(900);
  page += "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += kPortalStyle;
  page += "</head><body><h1>";
  page += ok ? "Gespeichert" : "Nichts gespeichert";
  page += "</h1><p>";
  if (ok) {
    page += "Das Geraet schaltet den Accesspoint jetzt ab und verbindet sich mit \"";
    page += htmlEscape(ssid);
    page += "\". Diese Verbindung bricht dabei ab - das ist normal.";
  } else {
    page += "Es war keine SSID angegeben. <a style=\"color:#8ce4ff\" href=\"/\">Zurueck</a>";
  }
  page += "</p></body></html>";

  gPortal.send(200, "text/html; charset=utf-8", page);

  app.portalTouchedMs = millis();
  if (ok) app.portalCloseAtMs = millis() + kPortalCloseMs;
}

void portalRedirect() {
  app.portalTouchedMs = millis();
  gPortal.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  gPortal.send(302, "text/plain", "");
}

void startPortal(uint32_t now) {
  if (app.portalActive) return;

  // Der Accesspoint laeuft bewusst ohne Station-Teil. Bleibt die STA aktiv,
  // sucht sie im Hintergrund weiter nach dem gespeicherten Netz - dabei
  // wechselt der Funkkanal, und angemeldete Handys fliegen nach wenigen
  // Sekunden wieder raus ("Portal beendet sich von selbst").
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(60);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);                       // kein Modem-Sleep im AP-Betrieb
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  // Fester Kanal 1, damit der AP nicht mehr umgeschaltet werden kann.
  WiFi.softAP(kPortalSsid, kPortalPass, 1 /*Kanal*/, 0 /*sichtbar*/, 4 /*Clients*/);
  delay(120);

  if (!gPortalRoutesReady) {
    gPortal.on("/", HTTP_GET, portalSendRoot);
    gPortal.on("/save", HTTP_POST, portalSendSaved);
    gPortal.onNotFound(portalRedirect);
    gPortalRoutesReady = true;
  }
  gPortalDns.setErrorReplyCode(DNSReplyCode::NoError);
  gPortalDns.start(kPortalDnsPort, "*", WiFi.softAPIP());
  gPortal.begin();

  app.portalActive    = true;
  app.portalTouchedMs = now;
  app.portalCloseAtMs = 0;
  Serial.printf("Setup-Portal aktiv: SSID %s, IP %s, Heap %u\n", kPortalSsid,
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned>(ESP.getFreeHeap()));
}

void beginWiFi(uint32_t now);

void stopPortal(uint32_t now) {
  if (!app.portalActive) return;
  gPortal.stop();
  gPortalDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);    // beim Portalstart abgeschaltet
  app.portalActive    = false;
  app.portalCloseAtMs = 0;
  app.lastWiFiAttemptMs = 0;      // sofort neu verbinden
  beginWiFi(now);
  Serial.println("Setup-Portal beendet");
}

void servicePortal(uint32_t now) {
  if (!app.portalActive) return;

  gPortalDns.processNextRequest();
  gPortal.handleClient();

  // Solange ein Geraet angemeldet ist, laeuft die Leerlaufuhr nicht weiter.
  if (WiFi.softAPgetStationNum() > 0) app.portalTouchedMs = now;

  if (app.portalCloseAtMs != 0 &&
      static_cast<int32_t>(now - app.portalCloseAtMs) >= 0) {
    stopPortal(now);
    // Ohne Wechsel bliebe die Portalseite mit SSID und Passwort stehen,
    // obwohl der Accesspoint schon abgeschaltet ist.
    if (app.screen == ScreenMode::WifiPortal) setScreen(ScreenMode::WifiMenu, now);
    setModal("WLAN GESPEICHERT", kColOk, now, 2000);
    return;
  }
  if (now - app.portalTouchedMs >= kPortalIdleMs) {
    stopPortal(now);
    if (app.screen == ScreenMode::WifiPortal) setScreen(ScreenMode::WifiMenu, now);
    setModal("PORTAL BEENDET", kColWarn, now, 1800);
  }
}

// ===========================================================================
//  Datei an den c64u schicken und starten
// ===========================================================================
void startFileOnC64(const String& fullPath, uint32_t now) {
  const String ext  = lowerExt(fullPath);
  const String name = baseName(fullPath);

  if (!requireNetwork(now)) return;
  if (!isSupportedExt(ext)) {
    Serial.printf("Nicht unterstuetzt: '%s'  (Endung '%s')\n", fullPath.c_str(), ext.c_str());
    app.rfidHint = name.isEmpty() ? String("Pfad leer") : name;
    setModal(ext.isEmpty() ? String("KEINE ENDUNG") : ("TYP ? " + ext), kColErr, now, 2600);
    return;
  }

  if (!app.sdReady && !initSd()) { setModal("KEINE SD-KARTE", kColErr, now, 2000); return; }

  File file = SD.open(fullPath, FILE_READ);
  if (!file) { setModal("DATEI FEHLT", kColErr, now, 2200); return; }
  if (file.isDirectory()) { file.close(); setModal("IST EIN ORDNER", kColErr, now, 2000); return; }

  app.returnScreen = app.screen;
  app.screen       = ScreenMode::Busy;
  app.busyTitle    = "Upload";
  app.busyDetail   = name;
  app.busySent     = 0;
  app.busyTotal    = file.size();
  drawBusyScreen();

  UploadResult result;
  const bool diskImage = isDiskImageExt(ext);
  String targetDrive;

  if (diskImage) {
    targetDrive = resolveTargetDrive();
    app.busyDetail = name + "  -> LW " + targetDrive;
    drawBusyScreen();
    // type und mode als Query-Argumente, die Datei als einziger Multipart-Teil
    const String mountUrl = "/v1/drives/" + targetDrive + ":mount"
                            + "?type=" + ext + "&mode=" + kMountMode;
    Serial.println(mountUrl.c_str());
    result = uploadFile(mountUrl, file, name, "file");
  } else if (ext == "prg") {
    result = uploadFile("/v1/runners:run_prg", file, name, "");
  } else if (ext == "crt") {
    result = uploadFile("/v1/runners:run_crt", file, name, "");
  } else if (ext == "sid") {
    result = uploadFile("/v1/runners:sidplay", file, name, "");
  } else {  // mod
    result = uploadFile("/v1/runners:modplay", file, name, "");
  }
  file.close();

  if (!result.ok) {
    beep(500, 160);
    app.screen = app.returnScreen;
    setModal(result.message, kColErr, millis(), 2600);
    return;
  }

  // ---- Disk-Image: Laufwerk aktivieren und erstes Programm starten ----
  if (diskImage) {
    app.busyTitle  = "Laufwerk 8";
    app.busySent   = 0;
    app.busyTotal  = 0;
    app.busyDetail = "Laufwerk an";
    drawBusyScreen();

    sendApiRequest("PUT", "/v1/drives/" + targetDrive + ":on", true);

    if (app.settings.diskAction != DiskActionMode::Mount) {
      app.busyDetail = "Reset";
      drawBusyScreen();
      sendApiRequest("PUT", "/v1/machine:reset", true);

      if (app.settings.diskAction == DiskActionMode::MountRun) {
        waitCursorBlinking(true, 15000, "warte READY");
        app.busyDetail = "LOAD \"*\",8,1";
        drawBusyScreen();
        typeLoadFirstFile();

        waitCursorBlinking(false, 6000, "starte LOAD");
        if (waitCursorBlinking(true, 180000, "lade Diskette")) {
          app.busyDetail = "RUN";
          drawBusyScreen();
          typeRun();
        } else {
          beep(700, 140);
          app.screen = app.returnScreen;
          setModal("LADEN ZU LANG", kColWarn, millis(), 2600);
          return;
        }
      }
    }
  }

  beep(2800, 60);
  app.screen = app.returnScreen;
  setModal(String(diskImage ? "LAEUFT: " : "START: ") + name, kColOk, millis(), 2000);
}

// ===========================================================================
//  RFID / NFC  -  eingebauter WS1850S des M5Dial
// ---------------------------------------------------------------------------
//  Das Kartenformat ist bewusst identisch zu dem des TeensyROM NFC-Loaders
//  bzw. des Zaparoo/TapTo-Projekts und zur Core-Version dieses Projekts,
//  damit dieselbe Karte an allen Geraeten funktioniert:
//
//      Ein einzelner NDEF-Record, Typ "Text" (Well Known, UTF-8),
//      Inhalt = Pfad zur Programmdatei, z. B.  SD:OneLoad v5/Bubble Bobble.crt
//
//  Erlaubte Praefixe: "SD:", "USB:", "TR:" oder gar keins (dann gilt SD).
//  Ein "?" als Dateiname startet eine zufaellige Datei aus dem Verzeichnis.
//
//  Ablage auf der Karte:
//
//  A) NTAG213/215/216, MIFARE Ultralight (SAK 0x00, 4-Byte-Seiten, kein
//     Schluessel): NDEF-TLV ab Seite 4. Seiten 0..3 bleiben unberuehrt.
//
//  B) MIFARE Classic 1K/4K/Mini (16-Byte-Bloecke): NDEF-TLV in den
//     Datenbloecken ab Block 4 (Sektor-Trailer werden uebersprungen).
//     Authentifiziert wird zuerst mit dem NDEF-Schluessel D3F7D3F7D3F7,
//     ersatzweise mit dem Werksschluessel FFFFFFFFFFFF.
//
//  Zusaetzlich wird beim Lesen weiterhin das alte Rohformat "C64UPATH"
//  erkannt, damit bereits beschriebene Karten weiter funktionieren.
// ===========================================================================
constexpr size_t  kMaxTextLen  = 246;   // wie TeensyROM
constexpr size_t  kNdefBufSize = 288;   // TLV + Record + Text + Reserve
constexpr uint8_t kUlDataPage  = 4;     // NDEF beginnt auf Seite 4
constexpr uint8_t kMagic[8]    = {'C', '6', '4', 'U', 'P', 'A', 'T', 'H'};

enum class CardKind : uint8_t { None, Classic, Ultralight };

MFRC522::MIFARE_Key kKeyNdef    = {{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}};
MFRC522::MIFARE_Key kKeyFactory = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
bool gClassicNdefFormatted = false;
bool gClassicTryNdefFirst  = true;

uint8_t trailerForBlock(uint8_t block) { return static_cast<uint8_t>((block / 4) * 4 + 3); }

// Linearer Index -> Datenblock, Sektor-Trailer werden ausgelassen.
uint8_t classicDataBlock(size_t index) {
  const size_t sector = 1 + index / 3;
  return static_cast<uint8_t>(sector * 4 + (index % 3));
}

// Nach einem fehlgeschlagenen Auth ist die Karte im HALT-Zustand und
// antwortet nur noch auf WUPA.
bool reselectCard() {
  uint8_t atqa[2];
  uint8_t size = sizeof(atqa);
  if (RFID.PICC_WakeupA(atqa, &size) != MFRC522::STATUS_OK) return false;
  return RFID.PICC_Select(&(RFID.uid), 0) == MFRC522::STATUS_OK;
}

bool classicAuth(uint8_t block) {
  const uint8_t trailer = trailerForBlock(block);

  MFRC522::MIFARE_Key* keys[2] = {
      gClassicTryNdefFirst ? &kKeyNdef : &kKeyFactory,
      gClassicTryNdefFirst ? &kKeyFactory : &kKeyNdef};

  for (int i = 0; i < 2; ++i) {
    if (RFID.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailer, keys[i],
                              &(RFID.uid)) == MFRC522::STATUS_OK) {
      gClassicNdefFormatted = (keys[i] == &kKeyNdef);
      gClassicTryNdefFirst  = gClassicNdefFormatted;
      return true;
    }
    RFID.PCD_StopCrypto1();
    if (!reselectCard()) return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Schluessel-Woerterbuch fuer das Klonen fremder MIFARE-Classic-Karten
// ---------------------------------------------------------------------------
const uint8_t kKeyDict[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},   // Werksschluessel
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},   // MAD / NDEF Sektor 0
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},   // NDEF-Daten
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},
    {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD},
    {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A},
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
    {0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97},
    {0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F},
    {0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91},
    {0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6},
    {0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9},
};
constexpr size_t kKeyDictCount = sizeof(kKeyDict) / sizeof(kKeyDict[0]);

uint8_t blockTrailer(uint16_t block) {
  if (block < 128) return static_cast<uint8_t>((block / 4) * 4 + 3);
  const uint16_t b = block - 128;
  return static_cast<uint8_t>(128 + (b / 16) * 16 + 15);
}
bool blockIsTrailer(uint16_t block) { return blockTrailer(block) == block; }

// Ein ungueltiges Zugriffsbit-Muster wuerde den Sektor unwiderruflich sperren.
bool validAccessBits(uint8_t b6, uint8_t b7, uint8_t b8) {
  const uint8_t c1i = b6 & 0x0F;
  const uint8_t c2i = b6 >> 4;
  const uint8_t c1  = b7 >> 4;
  const uint8_t c3i = b7 & 0x0F;
  const uint8_t c3  = b8 >> 4;
  const uint8_t c2  = b8 & 0x0F;
  return c1i == static_cast<uint8_t>(~c1 & 0x0F) &&
         c2i == static_cast<uint8_t>(~c2 & 0x0F) &&
         c3i == static_cast<uint8_t>(~c3 & 0x0F);
}

bool classicAuthDict(uint8_t trailer, uint8_t* keyOut, char* typeOut) {
  const uint8_t cmds[2] = {MFRC522::PICC_CMD_MF_AUTH_KEY_A, MFRC522::PICC_CMD_MF_AUTH_KEY_B};
  const char letters[2] = {'A', 'B'};

  for (size_t i = 0; i < kKeyDictCount; ++i) {
    MFRC522::MIFARE_Key key;
    memcpy(key.keyByte, kKeyDict[i], 6);
    for (int t = 0; t < 2; ++t) {
      if (RFID.PCD_Authenticate(cmds[t], trailer, &key, &(RFID.uid)) == MFRC522::STATUS_OK) {
        if (keyOut) memcpy(keyOut, kKeyDict[i], 6);
        if (typeOut) *typeOut = letters[t];
        return true;
      }
      RFID.PCD_StopCrypto1();
      if (!reselectCard()) return false;   // Karte weg
    }
  }
  return false;
}

bool rfidReadBlock(uint8_t block, uint8_t* out16) {
  if (!classicAuth(block)) return false;
  uint8_t buffer[18];
  uint8_t size = sizeof(buffer);
  if (RFID.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK) return false;
  memcpy(out16, buffer, 16);
  return true;
}

bool rfidWriteBlock(uint8_t block, const uint8_t* data16) {
  if (!classicAuth(block)) return false;
  return RFID.MIFARE_Write(block, const_cast<uint8_t*>(data16), 16) == MFRC522::STATUS_OK;
}

void rfidRelease() {
  RFID.PICC_HaltA();
  RFID.PCD_StopCrypto1();
}

bool cardPresent() {
  const bool found = RFID.PICC_IsNewCardPresent() && RFID.PICC_ReadCardSerial();
  if (found) gClassicTryNdefFirst = true;
  return found;
}

// ---------------------------------------------------------------------------
// Schnelle Anwesenheitsprobe fuer die Hintergrundabfrage
//
// Liegt keine Karte auf, wartet der MFRC522 nach dem REQA-Kommando so lange,
// bis sein interner Timer ablaeuft - ab Werk rund 25 ms. Genau so lange
// haengt die Hauptschleife und das Bild ruckelt sichtbar.
//
// Eine Karte antwortet aber innerhalb von deutlich unter einer Millisekunde
// (Frame Delay Time bei 106 kBit/s: ~86 us). Fuer die reine Probe wird der
// Timer deshalb auf ~2 ms verkuerzt und danach sofort wieder auf den
// Ausgangswert gesetzt - Auswahl, Authentifizierung und Schreibvorgaenge
// laufen also unveraendert mit dem vollen Zeitfenster.
//
// Der Timer zaehlt in Schritten von 25 us (TPrescaler aus PCD_Init).
// ---------------------------------------------------------------------------
uint16_t gRfidTimerReload = 0x03E8;      // in initRfid() vom Chip gelesen
constexpr uint16_t kRfidProbeReload = 80;   // 80 * 25 us = 2 ms

void setRfidTimerReload(uint16_t ticks) {
  RFID.PCD_WriteRegister(MFRC522::TReloadRegH, static_cast<uint8_t>(ticks >> 8));
  RFID.PCD_WriteRegister(MFRC522::TReloadRegL, static_cast<uint8_t>(ticks & 0xFF));
}

bool cardPresentQuick() {
  setRfidTimerReload(kRfidProbeReload);
  const bool present = RFID.PICC_IsNewCardPresent();
  setRfidTimerReload(gRfidTimerReload);      // vor der Auswahl zurueckstellen
  if (!present) return false;
  if (!RFID.PICC_ReadCardSerial()) return false;
  gClassicTryNdefFirst = true;
  return true;
}

CardKind cardKind() {
  const uint8_t type = RFID.PICC_GetType(RFID.uid.sak);
  if (type == MFRC522::PICC_TYPE_MIFARE_MINI ||
      type == MFRC522::PICC_TYPE_MIFARE_1K ||
      type == MFRC522::PICC_TYPE_MIFARE_4K) {
    return CardKind::Classic;
  }
  if (type == MFRC522::PICC_TYPE_MIFARE_UL) return CardKind::Ultralight;
  return CardKind::None;
}

const char* cardKindLabel(CardKind kind) {
  switch (kind) {
    case CardKind::Classic:    return "MIFARE Classic";
    case CardKind::Ultralight: return "NTAG / Ultralight";
    default:                   return "unbekannt";
  }
}

// ---- Ultralight / NTAG: 4 Byte pro Seite --------------------------------
bool ulRead16(uint8_t page, uint8_t* out16) {
  uint8_t buffer[18];
  uint8_t size = sizeof(buffer);
  if (RFID.MIFARE_Read(page, buffer, &size) != MFRC522::STATUS_OK) return false;
  memcpy(out16, buffer, 16);
  return true;
}

bool ulWritePage(uint8_t page, const uint8_t* data4) {
  return RFID.MIFARE_Ultralight_Write(page, const_cast<uint8_t*>(data4), 4) == MFRC522::STATUS_OK;
}

bool ulWrite16(uint8_t firstPage, const uint8_t* data16) {
  for (uint8_t i = 0; i < 4; ++i) {
    if (!ulWritePage(static_cast<uint8_t>(firstPage + i), data16 + i * 4)) return false;
  }
  return true;
}

String cardUidString() {
  String uid;
  char buf[4];
  for (uint8_t i = 0; i < RFID.uid.size; ++i) {
    snprintf(buf, sizeof(buf), "%02X", RFID.uid.uidByte[i]);
    uid += buf;
  }
  return uid;
}

// GET_VERSION (0x60) - NTAG21x liefert acht Bytes mit Typ und Speichergroesse.
bool ulGetVersion(uint8_t* out8) {
  uint8_t cmd[3] = {0x60, 0, 0};
  if (RFID.PCD_CalculateCRC(cmd, 1, &cmd[1]) != MFRC522::STATUS_OK) return false;

  uint8_t back[10];
  uint8_t backLen = sizeof(back);
  if (RFID.PCD_TransceiveData(cmd, 3, back, &backLen, nullptr, 0, true) != MFRC522::STATUS_OK) {
    return false;
  }
  if (backLen < 8) return false;
  memcpy(out8, back, 8);
  return true;
}

const char* ntagNameFromStorage(uint8_t storage) {
  switch (storage) {
    case 0x0B: return "NTAG210";
    case 0x0E: return "NTAG212";
    case 0x0F: return "NTAG213";
    case 0x11: return "NTAG215";
    case 0x13: return "NTAG216";
    default:   return "NTAG/UL";
  }
}

uint16_t ntagBytesFromStorage(uint8_t storage) {
  switch (storage) {
    case 0x0B: return 48;
    case 0x0E: return 128;
    case 0x0F: return 144;
    case 0x11: return 504;
    case 0x13: return 888;
    default:   return 0;
  }
}

String hexBytes(const uint8_t* data, size_t len, size_t maxLen = 16) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  const size_t n = std::min(len, maxLen);
  out.reserve(n * 3);
  for (size_t i = 0; i < n; ++i) {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
    if (i + 1 < n) out += ' ';
  }
  if (len > n) out += " ...";
  return out;
}

// ---------------------------------------------------------------------------
// Zugriff auf den NDEF-Datenbereich, 16 Byte am Stueck
// ---------------------------------------------------------------------------
bool cardReadChunk(CardKind kind, size_t chunk, uint8_t* out16) {
  if (kind == CardKind::Classic) return rfidReadBlock(classicDataBlock(chunk), out16);
  return ulRead16(static_cast<uint8_t>(kUlDataPage + chunk * 4), out16);
}

bool cardWriteChunk(CardKind kind, size_t chunk, const uint8_t* data16) {
  if (kind == CardKind::Classic) return rfidWriteBlock(classicDataBlock(chunk), data16);
  return ulWrite16(static_cast<uint8_t>(kUlDataPage + chunk * 4), data16);
}

// ---------------------------------------------------------------------------
// NDEF: ein einzelner Text-Record (Well Known, UTF-8, Sprache "en")
// ---------------------------------------------------------------------------
size_t buildNdefText(const String& text, uint8_t* out, size_t cap) {
  const size_t textLen    = text.length();
  const size_t payloadLen = 3 + textLen;          // Status + "en" + Text
  const size_t recordLen  = 4 + payloadLen;
  const size_t total      = 2 + recordLen + 1;
  if (payloadLen > 255 || total > cap) return 0;

  size_t i = 0;
  out[i++] = 0x03;                                       // TLV: NDEF-Nachricht
  out[i++] = static_cast<uint8_t>(recordLen);
  out[i++] = 0xD1;                                       // MB|ME|SR|TNF=Well Known
  out[i++] = 0x01;                                       // Typlaenge
  out[i++] = static_cast<uint8_t>(payloadLen);
  out[i++] = 'T';                                        // Typ "Text"
  out[i++] = 0x02;                                       // UTF-8, Sprachcode 2 Zeichen
  out[i++] = 'e';
  out[i++] = 'n';
  for (size_t k = 0; k < textLen; ++k) out[i++] = static_cast<uint8_t>(text[k]);
  out[i++] = 0xFE;                                       // TLV: Ende

  while (i % 16 != 0 && i < cap) out[i++] = 0x00;
  return i;
}

bool parseNdefText(const uint8_t* data, size_t len, String* out) {
  size_t i = 0;
  size_t msgStart = 0;
  size_t msgLen   = 0;
  while (i < len) {
    const uint8_t tag = data[i++];
    if (tag == 0x00) continue;                     // NULL-TLV
    if (tag == 0xFE) return false;                 // Ende ohne Nachricht
    if (i >= len) return false;

    size_t tlvLen = data[i++];
    if (tlvLen == 0xFF) {                          // 3-Byte-Laenge
      if (i + 1 >= len) return false;
      tlvLen = (static_cast<size_t>(data[i]) << 8) | data[i + 1];
      i += 2;
    }
    if (tag == 0x03) { msgStart = i; msgLen = tlvLen; break; }
    i += tlvLen;
  }
  if (msgLen == 0 || msgStart + msgLen > len) return false;

  size_t p = msgStart;
  const size_t end = msgStart + msgLen;
  while (p < end) {
    const uint8_t header = data[p++];
    const bool shortRec = (header & 0x10) != 0;
    const bool hasId    = (header & 0x08) != 0;
    const uint8_t tnf   = header & 0x07;
    if (p >= end) return false;

    const uint8_t typeLen = data[p++];
    size_t payloadLen = 0;
    if (shortRec) {
      if (p >= end) return false;
      payloadLen = data[p++];
    } else {
      if (p + 3 >= end) return false;
      payloadLen = (static_cast<size_t>(data[p]) << 24) | (static_cast<size_t>(data[p + 1]) << 16) |
                   (static_cast<size_t>(data[p + 2]) << 8) | data[p + 3];
      p += 4;
    }
    uint8_t idLen = 0;
    if (hasId) {
      if (p >= end) return false;
      idLen = data[p++];
    }

    const size_t typePos    = p;
    const size_t payloadPos = p + typeLen + idLen;

    // Beim letzten Record hat die TLV-Laenge Vorrang: manche Schreiber
    // (u. a. TeensyROM) tragen dort eine falsche Payload-Laenge ein.
    if ((header & 0x40) != 0 && payloadPos < end) {
      const size_t fromTlv = end - payloadPos;
      if (fromTlv != payloadLen) payloadLen = fromTlv;
    }
    if (payloadPos + payloadLen > len) {
      if (payloadPos >= len) return false;
      payloadLen = len - payloadPos;
    }

    if (tnf == 0x01 && typeLen == 1 && data[typePos] == 'T' && payloadLen >= 1) {
      const uint8_t status  = data[payloadPos];
      const uint8_t langLen = status & 0x3F;
      if (payloadLen > static_cast<size_t>(1 + langLen)) {
        const size_t textPos = payloadPos + 1 + langLen;
        const size_t textLen = payloadLen - 1 - langLen;
        String text;
        text.reserve(textLen + 1);
        for (size_t k = 0; k < textLen; ++k) {
          const uint8_t b = data[textPos + k];
          if (b == 0x00 || b == 0xFE) break;
          text += static_cast<char>(b);
        }
        *out = text;
        return true;
      }
    }

    p = payloadPos + payloadLen;
    if ((header & 0x40) != 0) break;               // ME: letzter Record
  }
  return false;
}

// ---------------------------------------------------------------------------
// Karteninhalt lesen: erst NDEF, ersatzweise das alte Rohformat "C64UPATH"
// ---------------------------------------------------------------------------
struct CardContent {
  bool   ok       = false;
  bool   isNdef   = false;
  bool   isLegacy = false;
  String text;
  String error;
  uint8_t raw[16]  = {0};
  uint8_t raw2[16] = {0};
};

CardContent readCardContent(CardKind kind) {
  CardContent result;
  if (kind == CardKind::None) {
    result.error = "Kartentyp nicht unterstuetzt";
    return result;
  }

  static uint8_t buffer[kNdefBufSize];
  memset(buffer, 0, sizeof(buffer));

  if (!cardReadChunk(kind, 0, buffer)) {
    result.error = (kind == CardKind::Classic) ? "Block 4 nicht lesbar (Schluessel?)"
                                               : "Seite 4 nicht lesbar";
    return result;
  }
  memcpy(result.raw, buffer, 16);
  if (cardReadChunk(kind, 1, buffer + 16)) memcpy(result.raw2, buffer + 16, 16);

  // Altes Rohformat?
  if (memcmp(buffer, kMagic, sizeof(kMagic)) == 0) {
    const uint8_t len = buffer[9];
    if (len == 0 || len > 128) {
      result.error = "Laenge ungueltig";
      return result;
    }
    String path;
    path.reserve(len + 1);
    static const uint8_t legacyBlocks[] = {5, 6, 8, 9, 10, 12, 13, 14};
    size_t remaining = len;
    for (size_t i = 0; i < 8 && remaining > 0; ++i) {
      uint8_t data[16] = {0};
      const bool ok = (kind == CardKind::Classic)
                          ? rfidReadBlock(legacyBlocks[i], data)
                          : ulRead16(static_cast<uint8_t>(8 + i * 4), data);
      if (!ok) {
        result.error = "Lesefehler (Altformat)";
        return result;
      }
      const size_t take = std::min<size_t>(16, remaining);
      for (size_t k = 0; k < take; ++k) path += static_cast<char>(data[k]);
      remaining -= take;
    }
    result.ok       = true;
    result.isLegacy = true;
    result.text     = path;
    return result;
  }

  // NDEF: Laenge aus dem TLV holen und nur so viel nachladen wie noetig
  size_t needed = sizeof(buffer);
  if (buffer[0] == 0x03) {
    needed = (buffer[1] == 0xFF)
                 ? 4 + ((static_cast<size_t>(buffer[2]) << 8) | buffer[3])
                 : 2 + static_cast<size_t>(buffer[1]) + 1;
    needed = std::min(needed, sizeof(buffer));
  }

  size_t have = 16;
  for (size_t chunk = 1; have < needed; ++chunk) {
    if (!cardReadChunk(kind, chunk, buffer + have)) break;
    have += 16;
    if (have + 16 > sizeof(buffer)) break;
  }

  String text;
  if (parseNdefText(buffer, have, &text)) {
    result.ok     = true;
    result.isNdef = true;
    result.text   = text;
    return result;
  }

  result.error = (buffer[0] == 0x03) ? "NDEF ohne Text-Record" : "kein NDEF-Text";
  return result;
}

// ---------------------------------------------------------------------------
// Karte beschreiben: immer als NDEF-Text-Record
// ---------------------------------------------------------------------------
bool writeCardText(CardKind kind, const String& text, String* errorOut) {
  if (kind == CardKind::None) {
    if (errorOut) *errorOut = "Kartentyp nicht unterstuetzt";
    return false;
  }
  if (text.isEmpty() || text.length() > kMaxTextLen) {
    if (errorOut) *errorOut = "Text zu lang (max " + String(kMaxTextLen) + ")";
    return false;
  }

  static uint8_t buffer[kNdefBufSize];
  const size_t total = buildNdefText(text, buffer, sizeof(buffer));
  if (total == 0) {
    if (errorOut) *errorOut = "NDEF passt nicht";
    return false;
  }

  const size_t chunks = total / 16;
  for (size_t chunk = 0; chunk < chunks; ++chunk) {
    if (!cardWriteChunk(kind, chunk, buffer + chunk * 16)) {
      if (errorOut) {
        *errorOut = (chunk == 0) ? "Karte nicht beschreibbar"
                                 : "Karte zu klein ab Block " + String(chunk);
      }
      return false;
    }
  }

  const CardContent check = readCardContent(kind);
  if (!check.ok || check.text != text) {
    if (errorOut) *errorOut = check.ok ? "Kontrolle abweichend" : check.error;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Karte auf die SD sichern und von dort wieder zurueckschreiben
//
//  Dateiformat (Textdatei, /NFC-DUMPS/<uid>.nfc):
//      # C64uRemote NFC-Dump
//      type NTAG215
//      uid  04 01 A1 01 C1 47 03
//      sak  00
//      P4   03 27 D1 01            <- Ultralight: 4 Byte je Seite
//      B4   00 11 ... (16 Byte)    <- Classic: 16 Byte je Block
// ---------------------------------------------------------------------------
constexpr const char* kDumpDir = "/NFC-DUMPS";

uint16_t ntagLastUserPage(uint8_t storage) {
  switch (storage) {
    case 0x0F: return 39;    // NTAG213
    case 0x11: return 129;   // NTAG215
    case 0x13: return 225;   // NTAG216
    default:   return 39;
  }
}

bool dumpCardToSd(CardKind kind, String* fileOut, String* errorOut) {
  if (!app.sdReady && !initSd()) {
    if (errorOut) *errorOut = "keine SD-Karte";
    return false;
  }
  if (!SD.exists(kDumpDir) && !SD.mkdir(kDumpDir)) {
    if (errorOut) *errorOut = "Ordner NFC-DUMPS fehlt";
    return false;
  }

  const String uid  = cardUidString();
  const String path = String(kDumpDir) + "/" + uid + ".nfc";

  File out = SD.open(path, FILE_WRITE);
  if (!out) {
    if (errorOut) *errorOut = "Datei nicht anlegbar";
    return false;
  }

  uint8_t version[8] = {0};
  const bool haveVersion = (kind == CardKind::Ultralight) && ulGetVersion(version);
  if (kind == CardKind::Ultralight && !haveVersion) reselectCard();

  out.println("# C64uRemote NFC-Dump");
  out.print("type ");
  out.println(kind == CardKind::Classic ? cardKindLabel(kind)
                                        : (haveVersion ? ntagNameFromStorage(version[6]) : "NTAG/UL"));
  out.print("uid  ");
  out.println(hexBytes(RFID.uid.uidByte, RFID.uid.size, 10).c_str());
  out.print("sak  ");
  out.println(hexBytes(&RFID.uid.sak, 1).c_str());

  size_t written = 0;
  char label[8];

  if (kind == CardKind::Ultralight) {
    const uint16_t lastPage = haveVersion ? ntagLastUserPage(version[6]) : 39;
    for (uint16_t page = 0; page <= lastPage; page += 4) {
      uint8_t data[16] = {0};
      if (!ulRead16(static_cast<uint8_t>(page), data)) break;
      for (uint16_t k = 0; k < 4 && (page + k) <= lastPage; ++k) {
        snprintf(label, sizeof(label), "P%-4u", static_cast<unsigned>(page + k));
        out.print(label);
        out.println(hexBytes(data + k * 4, 4).c_str());
        written++;
      }
    }
  } else {
    const uint8_t  type = RFID.PICC_GetType(RFID.uid.sak);
    const uint16_t lastBlock = (type == MFRC522::PICC_TYPE_MIFARE_4K) ? 255
                             : (type == MFRC522::PICC_TYPE_MIFARE_MINI) ? 19 : 63;

    uint8_t curTrailer = 0xFF;
    bool    authed     = false;
    uint8_t sectorKey[6] = {0};
    for (uint16_t block = 0; block <= lastBlock; ++block) {
      const uint8_t tr = blockTrailer(block);
      if (tr != curTrailer) {
        curTrailer = tr;
        char keyType = '?';
        authed = classicAuthDict(tr, sectorKey, &keyType);
        if (authed) {
          out.print("# Sektor ");
          out.print(String(block / 4).c_str());
          out.print("  Key ");
          out.print(String(keyType).c_str());
          out.print(" ");
          out.println(hexBytes(sectorKey, 6).c_str());
        } else {
          out.print("# Sektor ");
          out.print(String(block / 4).c_str());
          out.println("  kein Schluessel gefunden");
        }
      }
      if (!authed) continue;

      uint8_t buf[18] = {0};
      uint8_t sz = sizeof(buf);
      if (RFID.MIFARE_Read(static_cast<uint8_t>(block), buf, &sz) != MFRC522::STATUS_OK) continue;

      // Schluessel A ist nie lesbar - den gefundenen Schluessel eintragen,
      // damit der Dump wieder brauchbar ist.
      if (blockIsTrailer(block)) memcpy(buf, sectorKey, 6);

      snprintf(label, sizeof(label), "B%-4u", static_cast<unsigned>(block));
      out.print(label);
      out.println(hexBytes(buf, 16).c_str());
      written++;
    }
  }

  out.close();

  if (written == 0) {
    SD.remove(path);
    if (errorOut) *errorOut = "nichts lesbar";
    return false;
  }
  if (fileOut) *fileOut = path;
  Serial.printf("Dump geschrieben: %s (%u Eintraege)\n", path.c_str(), static_cast<unsigned>(written));
  return true;
}

// Eine Zeile "P12  AA BB CC DD" zerlegen
bool parseDumpLine(const String& line, char* kindOut, uint16_t* indexOut,
                   uint8_t* data, size_t* lenOut) {
  if (line.length() < 4) return false;
  const char tag = line[0];
  if (tag != 'P' && tag != 'B') return false;

  size_t i = 1;
  uint16_t index = 0;
  bool haveDigit = false;
  while (i < line.length() && isdigit(static_cast<unsigned char>(line[i]))) {
    index = static_cast<uint16_t>(index * 10 + (line[i] - '0'));
    haveDigit = true;
    ++i;
  }
  if (!haveDigit) return false;

  size_t len = 0;
  while (i < line.length() && len < 16) {
    while (i < line.length() && line[i] == ' ') ++i;
    if (i + 1 >= line.length()) break;
    const char hi = line[i], lo = line[i + 1];
    if (!isxdigit(static_cast<unsigned char>(hi)) || !isxdigit(static_cast<unsigned char>(lo))) break;
    auto nib = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
      return static_cast<uint8_t>((c | 0x20) - 'a' + 10);
    };
    data[len++] = static_cast<uint8_t>((nib(hi) << 4) | nib(lo));
    i += 2;
  }
  if (len == 0) return false;

  *kindOut  = tag;
  *indexOut = index;
  *lenOut   = len;
  return true;
}

bool restoreDumpToCard(CardKind kind, const String& file, String* errorOut) {
  if (!app.sdReady && !initSd()) {
    if (errorOut) *errorOut = "keine SD-Karte";
    return false;
  }
  File in = SD.open(file, FILE_READ);
  if (!in) {
    if (errorOut) *errorOut = "Dump nicht lesbar";
    return false;
  }

  uint16_t lastPage = 39;
  if (kind == CardKind::Ultralight) {
    uint8_t version[8] = {0};
    if (ulGetVersion(version)) lastPage = ntagLastUserPage(version[6]);
    else                       reselectCard();
  }

  size_t written = 0, skipped = 0;
  uint8_t destTrailer = 0xFF;
  bool    destAuthed  = false;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line[0] == '#') continue;

    char tag = 0;
    uint16_t index = 0;
    uint8_t data[16] = {0};
    size_t len = 0;
    if (!parseDumpLine(line, &tag, &index, data, &len)) continue;

    if (kind == CardKind::Ultralight && tag == 'P') {
      if (index < 4 || index > lastPage) { skipped++; continue; }   // Kopf und Konfig tabu
      if (len < 4) continue;
      if (!ulWritePage(static_cast<uint8_t>(index), data)) {
        in.close();
        if (errorOut) *errorOut = "Seite " + String(index) + " nicht schreibbar";
        return false;
      }
      written++;
    } else if (kind == CardKind::Classic && tag == 'B') {
      if (index == 0 || index > 255) { skipped++; continue; }
      if (len < 16) continue;

      const bool trailer = blockIsTrailer(index);
      if (trailer && !validAccessBits(data[6], data[7], data[8])) { skipped++; continue; }

      const uint8_t tr = blockTrailer(index);
      if (tr != destTrailer) {
        destTrailer = tr;
        destAuthed  = classicAuthDict(tr, nullptr, nullptr);
      }
      if (!destAuthed) { skipped++; continue; }

      if (RFID.MIFARE_Write(static_cast<uint8_t>(index), data, 16) != MFRC522::STATUS_OK) {
        in.close();
        if (errorOut) *errorOut = String(trailer ? "Trailer " : "Block ") + String(index) +
                                  " nicht schreibbar";
        return false;
      }
      written++;
    } else {
      skipped++;
    }
  }
  in.close();

  Serial.printf("Restore: %u geschrieben, %u uebersprungen\n",
                static_cast<unsigned>(written), static_cast<unsigned>(skipped));
  if (written == 0) {
    if (errorOut) *errorOut = "Dump passt nicht zur Karte";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Umrechnung zwischen dem Kartentext und einem Pfad auf unserer microSD
//   Karte:  SD:OneLoad v5/Bubble Bobble.crt
//   intern: /OneLoad v5/Bubble Bobble.crt
// ---------------------------------------------------------------------------
String sanitizeCardText(const String& text) {
  String out;
  out.reserve(text.length() + 1);
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (static_cast<uint8_t>(c) >= 0x20) out += c;
  }
  out.trim();
  return out;
}

// ---------------------------------------------------------------------------
// Kommandokarten
//
// Statt eines Dateipfads kann auf einer Karte auch ein Befehl fuer den c64u
// stehen. Format: das Praefix "CMD:" gefolgt vom Schluesselwort, optional mit
// einem Argument hinter einem Gleichheitszeichen.
//
//     CMD:RESET
//     CMD:REBOOT
//     CMD:MENU
//     CMD:POWEROFF=0      sofort ausschalten
//     CMD:POWEROFF=8      nachfragen, 8 s Zeit fuer die Bestaetigung
//     CMD:POWEROFF        nachfragen mit der am Geraet eingestellten Zeit
//     CMD:M5OFF           den M5Dial selbst ausschalten (auch CMD:DIALOFF)
//     CMD:CPU=10          CPU auf 10 MHz stellen
//     CMD:JOY             Joystickports umschalten (Normal <-> Swapped)
//     CMD:JOY=SWAPPED     Ports fest setzen; auch NORMAL, WASD1, WASD2
//
// Gross-/Kleinschreibung und Leerzeichen sind egal. Der Inhalt bleibt ein
// gewoehnlicher NDEF-Textrecord, jede NFC-App kann so eine Karte lesen.
// ---------------------------------------------------------------------------
constexpr const char* kCardCmdPrefix = "CMD:";

bool parseCardCommand(const String& text, CardCommand* out) {
  String t = text;
  t.trim();
  String head = t.substring(0, 4);
  head.toUpperCase();
  if (head != kCardCmdPrefix) return false;

  String body = t.substring(4);
  body.trim();

  String arg;
  const int eq = body.indexOf('=');
  if (eq >= 0) {
    arg  = body.substring(eq + 1);
    body = body.substring(0, eq);
    arg.trim();
    body.trim();
  }
  body.toUpperCase();

  CardCommand cmd;
  cmd.arg    = arg;
  cmd.hasArg = (eq >= 0);

  if      (body == "RESET")    cmd.cmd = CardCmd::Reset;
  else if (body == "REBOOT")   cmd.cmd = CardCmd::Reboot;
  else if (body == "MENU")     cmd.cmd = CardCmd::UltiMenu;
  else if (body == "POWEROFF") cmd.cmd = CardCmd::PowerOff;
  else if (body == "M5OFF" || body == "DIALOFF") cmd.cmd = CardCmd::DialOff;
  else if (body == "CPU")      cmd.cmd = CardCmd::CpuSpeed;
  else if (body == "JOY" || body == "JOYSTICK") cmd.cmd = CardCmd::JoySwap;
  else return false;

  // CPU ohne Wert ergibt keinen Sinn
  if (cmd.cmd == CardCmd::CpuSpeed && arg.isEmpty()) return false;

  *out = cmd;
  return true;
}

// Bestaetigungszeit eines PowerOff-Befehls in Sekunden.
// Ohne Argument gilt die Geraeteeinstellung, 0 bedeutet "ohne Nachfrage".
uint8_t cardPowerOffSeconds(const CardCommand& c) {
  if (!c.hasArg) return app.settings.cardConfirmS;
  const long v = c.arg.toInt();
  if (v <= 0) return 0;
  return static_cast<uint8_t>(std::min<long>(v, 60));
}

String cardCommandText(const CardCommand& c) {
  switch (c.cmd) {
    case CardCmd::Reset:    return "CMD:RESET";
    case CardCmd::Reboot:   return "CMD:REBOOT";
    case CardCmd::UltiMenu: return "CMD:MENU";
    case CardCmd::PowerOff: return "CMD:POWEROFF=" + String(cardPowerOffSeconds(c));
    case CardCmd::DialOff:  return "CMD:M5OFF";
    case CardCmd::CpuSpeed: return "CMD:CPU=" + c.arg;
    case CardCmd::JoySwap:  return (c.hasArg && !c.arg.isEmpty())
                                   ? ("CMD:JOY=" + joyTokenFromValue(c.arg))
                                   : String("CMD:JOY");
    default:                return "";
  }
}

String cardCommandLabel(const CardCommand& c) {
  switch (c.cmd) {
    case CardCmd::Reset:    return "Reset";
    case CardCmd::Reboot:   return "Reboot";
    case CardCmd::UltiMenu: return "Ultimate Menu";
    case CardCmd::PowerOff: {
      const uint8_t sec = cardPowerOffSeconds(c);
      return sec == 0 ? String("c64u Off direkt")
                      : ("c64u Off, " + String(sec) + "s Abfrage");
    }
    case CardCmd::DialOff:  return "M5Dial Power Off";
    case CardCmd::CpuSpeed: return "CPU " + c.arg + " MHz";
    case CardCmd::JoySwap:  return (c.hasArg && !c.arg.isEmpty())
                                   ? ("Joystick " + joyLabelFromToken(c.arg))
                                   : String("Joystick tauschen");
    default:                return "?";
  }
}

// ---- Auswahlliste zum Beschreiben einer Karte -----------------------------
// Feste Befehle zuerst, danach die Joystickwerte und die CPU-Stufen, die
// der c64u anbietet.
constexpr size_t kCmdFixedCount = 7;

size_t cmdListCount() { return kCmdFixedCount + app.joyChoiceCount + app.cpuChoiceCount; }

CardCommand cmdListAt(size_t index) {
  CardCommand c;
  switch (index) {
    case 0: c.cmd = CardCmd::Reset;    return c;
    case 1: c.cmd = CardCmd::Reboot;   return c;
    case 2: c.cmd = CardCmd::UltiMenu; return c;
    case 3: c.cmd = CardCmd::PowerOff; c.arg = "0"; c.hasArg = true; return c;
    case 4:
      c.cmd    = CardCmd::PowerOff;
      c.arg    = String(app.settings.cardConfirmS);
      c.hasArg = true;
      return c;
    case 5: c.cmd = CardCmd::DialOff; return c;
    case 6: c.cmd = CardCmd::JoySwap; return c;   // umschalten, ohne Argument
    default: break;
  }
  size_t rest = index - kCmdFixedCount;
  if (rest < app.joyChoiceCount) {
    c.cmd    = CardCmd::JoySwap;
    c.arg    = joyTokenFromValue(app.joyOptions[rest]);
    c.hasArg = true;
    return c;
  }
  rest -= app.joyChoiceCount;
  if (rest < app.cpuChoiceCount) {
    c.cmd    = CardCmd::CpuSpeed;
    c.arg    = extractDigits(app.cpuDisplayOptions[rest]);
    c.hasArg = true;
  }
  return c;
}

// Kuerzel rechts neben dem Listeneintrag.
const char* cmdListTag(size_t index) {
  if (index < kCmdFixedCount) return "";
  if (index - kCmdFixedCount < app.joyChoiceCount) return "JOY";
  return "CPU";
}

String stripSourcePrefix(const String& text, String* sourceOut) {
  String t = sanitizeCardText(text);
  const int colon = t.indexOf(':');
  if (colon > 0 && colon <= 3) {
    String prefix = t.substring(0, colon);
    prefix.toUpperCase();
    if (prefix == "SD" || prefix == "USB" || prefix == "TR") {
      if (sourceOut) *sourceOut = prefix;
      t = t.substring(colon + 1);
    }
  }
  return t;
}

String cardTextToPath(const String& text, String* sourceOut = nullptr) {
  String t = stripSourcePrefix(text, sourceOut);

  String clean;
  clean.reserve(t.length() + 1);
  for (size_t i = 0; i < t.length(); ++i) {
    const char c = t[i];
    if (c == '/' && !clean.isEmpty() && clean[clean.length() - 1] == '/') continue;
    clean += c;
  }
  if (!clean.startsWith("/")) clean = "/" + clean;
  return clean;
}

String pathToCardText(const String& path) {
  String p = path;
  while (p.startsWith("/")) p.remove(0, 1);
  return "SD:" + p;
}

bool pathIsRandom(const String& path) {
  return path.endsWith("/?") || path == "?" || path.endsWith("/");
}

bool pathIsDirectory(const String& path) {
  // Ohne diesen Versuch waere eine Verzeichniskarte direkt nach dem
  // Einschalten wertlos: die SD ist dann noch nicht angemeldet, der Pfad
  // gaebe kein Verzeichnis her und landete als Datei ohne Endung im
  // Upload.
  if (!app.sdReady && !initSd()) return false;
  File probe = SD.open(path);
  const bool isDir = probe && probe.isDirectory();
  if (probe) probe.close();
  return isDir;
}

// ---------------------------------------------------------------------------
// Zufallskarte: Verzeichnis statt Datei auswaehlen (app.sdPickMode == 3)
//
// Im Browser steht dafuer eine zusaetzliche Zeile ueber den Ordnern. Sie
// waehlt das gerade geoeffnete Verzeichnis selbst aus - ohne sie kaeme man
// nie an einen Ordner heran, weil ein Tipper darauf hineinwechselt.
// ---------------------------------------------------------------------------
bool sdPickHereRow() { return app.sdPickMode == 3; }

int sdBrowserCount() {
  return static_cast<int>(app.sdCount) + (app.sdPath != "/" ? 1 : 0) +
         (sdPickHereRow() ? 1 : 0);
}

// Zeile -> Bedeutung:  -1 = "..",  -2 = dieses Verzeichnis,  >= 0 = Eintrag
int sdBrowserSlot(int row) {
  if (app.sdPath != "/") { if (row == 0) return -1; row--; }
  if (sdPickHereRow())   { if (row == 0) return -2; row--; }
  return row;
}

// Verzeichnis als Zufallspfad schreiben:  "/Games" -> "/Games/?"
// Das Fragezeichen ist eindeutig und wird beim Auflegen auch dann erkannt,
// wenn die SD noch gar nicht gelesen wurde.
String randomCardPath(const String& dir) {
  String d = dir;
  if (!d.endsWith("/")) d += "/";
  return d + "?";
}

String resolveRandomFile(const String& path) {
  String dir = path;
  if (dir.endsWith("/?"))     dir = dir.substring(0, dir.length() - 2);
  else if (dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
  if (dir.isEmpty()) dir = "/";

  // readDirectory() filtert je nach Auswahlmodus. Steht der noch auf "Dump"
  // oder "Ordner" vom letzten Besuch im Browser, bliebe die Liste hier leer
  // und die Zufallskarte faende nichts.
  const uint8_t savedPick = app.sdPickMode;
  app.sdPickMode = 0;
  const bool listed = readDirectory(dir);
  app.sdPickMode = savedPick;
  if (!listed) return "";

  size_t files = 0;
  for (size_t i = 0; i < app.sdCount; ++i) {
    if (!app.sdEntries[i].isDir) files++;
  }
  if (files == 0) return "";

  size_t pick = static_cast<size_t>(random(static_cast<long>(files)));
  for (size_t i = 0; i < app.sdCount; ++i) {
    if (app.sdEntries[i].isDir) continue;
    if (pick == 0) return joinPath(dir, app.sdEntries[i].name);
    --pick;
  }
  return "";
}

// ---------------------------------------------------------------------------
// Alle verfuegbaren Informationen der aufliegenden Karte einsammeln
// ---------------------------------------------------------------------------
void addInfoLine(const String& text) {
  if (app.infoCount < AppState::kMaxInfoLines) app.infoLines[app.infoCount++] = text;
}

void addRawLine(const String& text) {
  if (app.infoCount2 < AppState::kMaxInfoLines) app.infoLines2[app.infoCount2++] = text;
}

uint8_t ntagConfigPage(uint8_t storage) {
  switch (storage) {
    case 0x0F: return 0x29;   // NTAG213
    case 0x11: return 0x83;   // NTAG215
    case 0x13: return 0xE3;   // NTAG216
    default:   return 0;
  }
}

// NFC-Zaehler (READ_CNT 0x39) - nur aktiv, wenn im Chip freigeschaltet
bool ulReadCounter(uint32_t* out) {
  uint8_t cmd[4] = {0x39, 0x02, 0, 0};
  if (RFID.PCD_CalculateCRC(cmd, 2, &cmd[2]) != MFRC522::STATUS_OK) return false;
  uint8_t back[8];
  uint8_t backLen = sizeof(back);
  if (RFID.PCD_TransceiveData(cmd, 4, back, &backLen, nullptr, 0, true) != MFRC522::STATUS_OK) {
    return false;
  }
  if (backLen < 3) return false;
  *out = static_cast<uint32_t>(back[0]) | (static_cast<uint32_t>(back[1]) << 8) |
         (static_cast<uint32_t>(back[2]) << 16);
  return true;
}

uint8_t gInfoStorage     = 0;
bool    gInfoHaveVersion = false;

void collectCardRaw(CardKind kind, uint8_t storage, bool haveVersion) {
  app.infoCount2 = 0;

  if (kind == CardKind::Ultralight) {
    for (uint8_t base = 0; base < 16; base += 4) {
      uint8_t data[16] = {0};
      char label[12];
      if (!ulRead16(base, data)) {
        snprintf(label, sizeof(label), "S %2u-%-2u", base, base + 3);
        addRawLine(String(label) + "   nicht lesbar");
        continue;
      }
      snprintf(label, sizeof(label), "S %2u-%-2u", base, base + 1);
      addRawLine(String(label) + "   " + hexBytes(data, 8));
      snprintf(label, sizeof(label), "S %2u-%-2u", base + 2, base + 3);
      addRawLine(String(label) + "   " + hexBytes(data + 8, 8));
    }

    uint8_t head[16] = {0};
    if (ulRead16(0, head)) {
      addRawLine("Lock      " + hexBytes(head + 10, 2) +
                 String((head[10] || head[11]) ? "  (gesperrt)" : "  (frei)"));
      const uint8_t* cc = head + 12;
      if (cc[0] == 0xE1) {
        addRawLine("CC        NDEF " + String(cc[1] >> 4) + "." + String(cc[1] & 0x0F) +
                   ", " + String(static_cast<uint16_t>(cc[2]) * 8) + " Byte, " +
                   String(cc[3] == 0x00 ? "les-/schreibbar" : "nur lesbar"));
      } else {
        addRawLine("CC        " + hexBytes(cc, 4) + "  (nicht NDEF)");
      }
    }

    const uint8_t cfgPage = haveVersion ? ntagConfigPage(storage) : 0;
    if (cfgPage != 0) {
      uint8_t cfg[16] = {0};
      if (ulRead16(cfgPage, cfg)) {
        const uint8_t auth0 = cfg[3];
        const uint8_t prot  = (cfg[4] & 0x80) ? 1 : 0;
        addRawLine(String("Schutz    ") +
                   (auth0 >= 0xFF ? String("aus")
                                  : ("ab Seite " + String(auth0) +
                                     (prot ? ", Lesen+Schreiben" : ", nur Schreiben"))));
      }
    }
    uint32_t counter = 0;
    if (ulReadCounter(&counter)) addRawLine("Zaehler   " + String(counter) + " Lesevorgaenge");
    else                         addRawLine("Zaehler   nicht aktiviert");

  } else if (kind == CardKind::Classic) {
    static const uint8_t blocks[] = {0, 4, 5, 6, 8};
    for (size_t i = 0; i < sizeof(blocks); ++i) {
      uint8_t data[16] = {0};
      const String label = "Block " + String(blocks[i]) + (blocks[i] < 10 ? "   " : "  ");
      if (rfidReadBlock(blocks[i], data)) addRawLine(label + hexBytes(data, 16));
      else                                addRawLine(label + "nicht lesbar");
    }
    addRawLine(String("Schluessel") + (gClassicNdefFormatted ? " D3F7D3F7D3F7 (NDEF)"
                                                             : " FFFFFFFFFFFF (Werk)"));
  }
}

void collectCardInfo(CardKind kind) {
  app.infoCount    = 0;
  app.infoCount2   = 0;
  app.infoScroll   = 0;
  gInfoStorage     = 0;
  gInfoHaveVersion = false;

  addInfoLine("UID       " + hexBytes(RFID.uid.uidByte, RFID.uid.size, 10) +
              "  (" + String(RFID.uid.size) + " Byte)");
  addInfoLine("SAK       0x" + hexBytes(&RFID.uid.sak, 1));

  uint16_t userBytes = 0;
  if (kind == CardKind::Classic) {
    const uint8_t type = RFID.PICC_GetType(RFID.uid.sak);
    const char* name = "MIFARE Classic";
    if (type == MFRC522::PICC_TYPE_MIFARE_1K)   { name = "MIFARE Classic 1K";   userBytes = 1024; }
    if (type == MFRC522::PICC_TYPE_MIFARE_4K)   { name = "MIFARE Classic 4K";   userBytes = 4096; }
    if (type == MFRC522::PICC_TYPE_MIFARE_MINI) { name = "MIFARE Classic Mini"; userBytes = 320;  }
    addInfoLine(String("Typ       ") + name + ", " + String(userBytes) + " Byte, " +
                String(userBytes / 64) + " Sektoren");
  } else {
    uint8_t version[8] = {0};
    uint8_t cc[16] = {0};
    if (ulRead16(3, cc) && cc[2] != 0) userBytes = static_cast<uint16_t>(cc[2]) * 8;

    if (ulGetVersion(version)) {
      gInfoStorage     = version[6];
      gInfoHaveVersion = true;
      const uint16_t fromVer = ntagBytesFromStorage(version[6]);
      if (fromVer) userBytes = fromVer;
      addInfoLine(String("Typ       ") + ntagNameFromStorage(version[6]) + ", " +
                  String(userBytes) + " Byte");
      addInfoLine(String("Version   ") + (version[1] == 0x04 ? "NXP  " : "") + hexBytes(version, 8));
    } else {
      reselectCard();
      addInfoLine(String("Typ       NTAG / Ultralight (Type 2)") +
                  (userBytes ? (", " + String(userBytes) + " Byte") : String("")));
      addInfoLine("Version   GET_VERSION fehlt");
    }
  }

  const CardContent content = readCardContent(kind);

  if (kind == CardKind::Classic) {
    addInfoLine(String("Format    ") + (gClassicNdefFormatted ? "NDEF-Key D3F7.."
                                                              : "Werks-Key FFFF.."));
  }

  if (!content.ok) {
    addInfoLine("Inhalt    " + content.error);
    collectCardRaw(kind, gInfoStorage, gInfoHaveVersion);
    return;
  }

  addInfoLine(String("Inhalt    ") +
              (content.isNdef ? "NDEF Text-Record" : "Altformat C64UPATH") + ", " +
              String(content.text.length()) + " Zeichen");
  addInfoLine("Text      " + content.text);

  String source;
  const String path = cardTextToPath(content.text, &source);

  if (pathIsRandom(path) || pathIsDirectory(path)) {
    addInfoLine("Pfad      " + path);
    addInfoLine("Datei     Verzeichnis - Zufallsstart");
    collectCardRaw(kind, gInfoStorage, gInfoHaveVersion);
    return;
  }

  addInfoLine("Pfad      " + parentPath(path));
  addInfoLine("Datei     " + baseName(path));

  String state = "keine SD-Karte";
  if (app.sdReady) {
    File probe = SD.open(path, FILE_READ);
    if (probe && !probe.isDirectory()) {
      state = "auf SD, " + String(static_cast<uint32_t>(probe.size() / 1024)) + " kB";
    } else {
      state = "NICHT auf SD";
    }
    if (probe) probe.close();
  }
  const String ext = lowerExt(path);
  addInfoLine("Typ/SD    " + (ext.isEmpty() ? String("ohne Endung") : ext) + ", " + state);

  collectCardRaw(kind, gInfoStorage, gInfoHaveVersion);
}

// ===========================================================================
//  Benutzeroberflaeche  (240 x 240, rund)
// ===========================================================================
// Kleine Font-Schalter, damit im restlichen Code kein Font-Typ auftaucht.
inline void fontSmall() { gDraw->setFont(&fonts::Font0); }
inline void fontText()  { gDraw->setFont(&fonts::Font2); }
inline void fontBig()   { gDraw->setFont(&fonts::Font4); }

using DatumT = decltype(middle_center);

// Zeichnet Text und kuerzt ihn, falls er breiter als maxW waere.
void drawClipped(const String& text, int x, int y, int maxW, uint16_t color, DatumT datum) {
  gDraw->setTextColor(color);
  gDraw->setTextDatum(datum);
  String out = text;
  while (out.length() > 1 && gDraw->textWidth(out) > maxW) out.remove(out.length() - 1);
  gDraw->drawString(out, x, y);
}

// Zentrierter Text, der automatisch an der Rundung des Displays endet.
void drawCentered(const String& text, int y, uint16_t color) {
  drawClipped(text, kCx, y, std::max(40, 2 * chordHalfWidth(y) - 10), color, middle_center);
}

// ---------------------------------------------------------------------------
// 24x24-Piktogramme fuer das Ring-Menue (1-Bit-Masken)
// ---------------------------------------------------------------------------
static const uint8_t kIconMaskPowerOff[72] PROGMEM = {0x00, 0x18, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x3C, 0x00, 0x02, 0x3C, 0x40, 0x07, 0x3C, 0xE0, 0x0F, 0xBD, 0xF0, 0x1F, 0x3C, 0xF8, 0x3E, 0x3C, 0x7C, 0x3C, 0x3C, 0x3C, 0x7C, 0x3C, 0x3E, 0x78, 0x3C, 0x1E, 0x78, 0x3C, 0x1E, 0x78, 0x18, 0x1E, 0x78, 0x00, 0x1E, 0x78, 0x00, 0x1E, 0x78, 0x00, 0x1E, 0x7C, 0x00, 0x3E, 0x3E, 0x00, 0x7C, 0x3F, 0x00, 0xFC, 0x1F, 0x81, 0xF8, 0x0F, 0xFF, 0xF0, 0x07, 0xFF, 0xE0, 0x03, 0xFF, 0xC0, 0x00, 0xFF, 0x00};
static const uint8_t kIconMaskReset[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF1, 0xF8, 0x03, 0xF1, 0xF0, 0x07, 0xC1, 0xE0, 0x0F, 0x01, 0xF0, 0x1C, 0x01, 0xB8, 0x1C, 0x01, 0x38, 0x38, 0x00, 0x1C, 0x38, 0x00, 0x1C, 0x30, 0x00, 0x0C, 0x30, 0x00, 0x0C, 0x30, 0x00, 0x0C, 0x30, 0x00, 0x0C, 0x38, 0x00, 0x1C, 0x38, 0x00, 0x1C, 0x1C, 0x00, 0x38, 0x1C, 0x00, 0x78, 0x0F, 0x00, 0xF0, 0x07, 0xC3, 0xE0, 0x03, 0xFF, 0xC0, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskReboot[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xC7, 0x00, 0x1F, 0xC7, 0x80, 0x03, 0xC3, 0xC0, 0x07, 0xC0, 0xE0, 0x0E, 0xC0, 0x70, 0x1C, 0xC0, 0x38, 0x1C, 0xC0, 0x38, 0x18, 0x00, 0x18, 0x18, 0x00, 0xD8, 0x18, 0x02, 0xD0, 0x18, 0x06, 0xD8, 0x1C, 0x0E, 0xDC, 0x1C, 0x0E, 0xDC, 0x0E, 0x0C, 0xCC, 0x07, 0x0C, 0x0C, 0x03, 0xCE, 0x1C, 0x01, 0xFF, 0x3C, 0x00, 0xF7, 0xF8, 0x00, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskUltimateMenu[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x03, 0xFF, 0xC0, 0x07, 0xC3, 0xE0, 0x0F, 0x00, 0xF0, 0x1C, 0x00, 0x38, 0x1D, 0xFF, 0xB8, 0x39, 0xFF, 0x9C, 0x38, 0x00, 0x1C, 0x30, 0x00, 0x0C, 0x31, 0xFF, 0x8C, 0x31, 0xFF, 0x8C, 0x30, 0x00, 0x0C, 0x38, 0x00, 0x1C, 0x39, 0xFF, 0x9C, 0x1D, 0xFF, 0xB8, 0x1C, 0x00, 0x78, 0x0F, 0x00, 0xF0, 0x07, 0xC3, 0xE0, 0x03, 0xFF, 0xC0, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskCpuSpeed[72] PROGMEM = {0x00, 0x3C, 0x00, 0x01, 0xFF, 0x80, 0x07, 0xFF, 0xE0, 0x0F, 0x18, 0xF0, 0x1E, 0x18, 0x78, 0x3C, 0x00, 0x3C, 0x3E, 0x00, 0x7C, 0x76, 0x00, 0x6E, 0x60, 0x03, 0x06, 0x60, 0x0F, 0x06, 0xE0, 0x3F, 0x07, 0xE0, 0x7F, 0x07, 0xFC, 0xE6, 0x3F, 0xFC, 0xEE, 0x3F, 0x70, 0xFC, 0x0E, 0x60, 0x7C, 0x06, 0x70, 0x10, 0x0E, 0x38, 0x00, 0x1C, 0x38, 0x00, 0x1C, 0x1C, 0x00, 0x38, 0x08, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskJoySwap[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xFF, 0x00, 0x01, 0xFF, 0x80, 0x01, 0xFF, 0x80, 0x00, 0xFF, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x7E, 0x00, 0x03, 0xFF, 0xC0, 0x0F, 0xFF, 0xF0, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x0F, 0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x18, 0x3F, 0xFF, 0xFC, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskSettings[72] PROGMEM = {0x00, 0x3C, 0x00, 0x00, 0x7E, 0x00, 0x08, 0x66, 0x30, 0x1F, 0xE7, 0xF8, 0x3F, 0xC3, 0xFC, 0x39, 0x00, 0x1C, 0x18, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0xFF, 0x18, 0x39, 0xC3, 0x9E, 0xF1, 0x81, 0x8F, 0xC1, 0x81, 0xC3, 0xC1, 0x81, 0xC3, 0xF1, 0x81, 0x8F, 0x79, 0xC3, 0x9E, 0x18, 0xFF, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x18, 0x39, 0x00, 0x0C, 0x3F, 0xC3, 0xFC, 0x1F, 0xE7, 0xF8, 0x00, 0x66, 0x30, 0x00, 0x3E, 0x00, 0x00, 0x3C, 0x00};
static const uint8_t kIconMaskRfid[72] PROGMEM = {0x00, 0x78, 0x00, 0x00, 0x7C, 0x00, 0x00, 0x1E, 0x00, 0x00, 0xCF, 0x00, 0x01, 0xE7, 0x00, 0x00, 0xF3, 0x80, 0x02, 0x7B, 0x80, 0x03, 0xB9, 0xC0, 0x07, 0x9D, 0xC0, 0x01, 0xDC, 0xC0, 0x01, 0xCC, 0xC0, 0x00, 0xCC, 0xC0, 0x00, 0xEE, 0xE0, 0x00, 0xCC, 0xC0, 0x01, 0xCC, 0xC0, 0x01, 0xDC, 0xC0, 0x07, 0x9D, 0xC0, 0x03, 0xB9, 0xC0, 0x02, 0x7B, 0x80, 0x00, 0xF3, 0x80, 0x01, 0xE7, 0x00, 0x00, 0xCF, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x7C, 0x00};
static const uint8_t kIconMaskSd[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFE, 0x00, 0x04, 0x03, 0x00, 0x04, 0x01, 0x80, 0x04, 0xDB, 0xE0, 0x04, 0xDB, 0x60, 0x04, 0xDB, 0x70, 0x04, 0xDB, 0x70, 0x04, 0xDB, 0x70, 0x04, 0xDB, 0x70, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x04, 0x00, 0x10, 0x07, 0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kIconMaskStatus[72] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x80, 0x03, 0xFF, 0xE0, 0x07, 0xC1, 0xF0, 0x0F, 0x00, 0x78, 0x1C, 0x1C, 0x1C, 0x38, 0x1C, 0x0E, 0x38, 0x1C, 0x0E, 0x70, 0x00, 0x07, 0x70, 0x00, 0x07, 0x60, 0x1C, 0x03, 0x60, 0x1C, 0x03, 0x60, 0x1C, 0x03, 0x60, 0x1C, 0x03, 0x60, 0x1C, 0x03, 0x70, 0x1C, 0x07, 0x70, 0x1C, 0x07, 0x38, 0x1C, 0x0E, 0x38, 0x1C, 0x0E, 0x1C, 0x00, 0x1C, 0x0F, 0x00, 0x78, 0x07, 0xC1, 0xF0, 0x03, 0xFF, 0xE0, 0x00, 0xFF, 0x80};

const uint8_t* menuIconMask(size_t index) {
  switch (index) {
    case kMenuPowerOff:  return kIconMaskPowerOff;
    case kMenuReset:     return kIconMaskReset;
    case kMenuReboot:    return kIconMaskReboot;
    case kMenuUltiMenu:  return kIconMaskUltimateMenu;
    case kMenuCpu:       return kIconMaskCpuSpeed;
    case kMenuRfidRun:   return kIconMaskRfid;
    case kMenuSdBrowse:  return kIconMaskSd;
    case kMenuJoySwap:   return kIconMaskJoySwap;
    case kMenuStatus:    return kIconMaskStatus;
    default:             return kIconMaskSettings;
  }
}

void drawMaskIcon24(const uint8_t* mask, int cx, int cy, uint16_t color) {
  constexpr int kSize = 24;
  constexpr int kBytesPerRow = 3;
  const int x0 = cx - kSize / 2;
  const int y0 = cy - kSize / 2;
  for (int y = 0; y < kSize; ++y) {
    for (int bx = 0; bx < kBytesPerRow; ++bx) {
      const uint8_t byte = pgm_read_byte(mask + y * kBytesPerRow + bx);
      if (byte == 0) continue;
      for (int bit = 0; bit < 8; ++bit) {
        if (byte & (0x80 >> bit)) gDraw->drawPixel(x0 + bx * 8 + bit, y0 + y, color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Grundgeruest der runden Seiten
// ---------------------------------------------------------------------------
void beginFrame() { gDraw->fillScreen(TFT_BLACK); }

void endFrame() { if (gUseCanvas) canvas.pushSprite(0, 0); }

uint16_t connectionColor() {
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  if (wifiOk && app.connection.authOk)        return kColOk;
  if (wifiOk && app.connection.targetReachable) return kColWarn;
  if (wifiOk)                                 return kColInfo;
  return kColErr;
}

const char* connectionText() {
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  if (wifiOk && app.connection.authOk)          return "C64U OK";
  if (wifiOk && app.connection.targetReachable) return "AUTH?";
  if (wifiOk)                                   return "NO C64U";
  return "NO WIFI";
}

// Runder Rahmen mit kleinem Statuspunkt oben und Hardware-Kuerzeln unten.
void drawRoundFrame(bool withStatus = true, bool withDot = true) {
  gDraw->fillCircle(kCx, kCy, kRing, kColBg);
  gDraw->drawCircle(kCx, kCy, kRing, kColLine);
  gDraw->drawCircle(kCx, kCy, kRing - 1, rgb565(40, 62, 92));
  if (!withStatus) return;

  if (withDot) gDraw->fillCircle(kCx, 14, 4, connectionColor());
  fontSmall();
  drawClipped(app.rfidReady ? "NFC" : "-", kCx - 26, 226, 30, app.rfidReady ? kColOk : kColLine, middle_center);
  drawClipped(app.sdReady ? "SD" : "-", kCx + 26, 226, 30, app.sdReady ? kColOk : kColLine, middle_center);
}

void drawTitle(const char* title) {
  fontText();
  drawCentered(title, kTitleY, kColLineHi);
}

void drawHint(const String& text) {
  fontSmall();
  drawCentered(text, kHintY, kColLabel);
}

// ---------------------------------------------------------------------------
// Home: Logo im Vollbild (Bildschirmschoner)
// ---------------------------------------------------------------------------
void drawHome(uint32_t now) {
  beginFrame();
  drawHomeVisual(now);
  gDraw->fillCircle(kCx, 14, 3, connectionColor());
}

// ---------------------------------------------------------------------------
// Ring-Menue
// ---------------------------------------------------------------------------
void drawMenuCenterLogo() {
  constexpr int dstW = 112;
  constexpr int dstH = (dstW * kLogoSrcH) / kLogoSrcW;
  constexpr int dstX = (kScrW - dstW) / 2;
  constexpr int dstY = kCy - (dstH / 2);
  for (int y = 0; y < dstH; ++y) {
    const int srcY = (y * kLogoSrcH) / dstH;
    for (int x = 0; x < dstW; ++x) {
      const int srcX = (x * kLogoSrcW) / dstW;
      rowBuf[x] = logoPixel(srcX, srcY);
    }
    gDraw->pushImage(dstX, dstY + y, dstW, 1, rowBuf);
  }
}

constexpr float kMenuStep       = 6.2831853f / static_cast<float>(kMenuCount);
constexpr float kMenuStartAngle = -1.5707963f;   // erster Eintrag oben
constexpr int   kMenuRingRadius = 92;

// Trefferflaeche pro Icon. Bewusst groesser als das gezeichnete Kaestchen
// (36 px), damit der Finger nicht exakt sitzen muss.
constexpr int   kMenuHitRadius  = 30;
// Ab diesem Abstand von der Mitte gilt ein Tipper als "auf dem Ring" und
// wird dem naechstgelegenen Icon zugeordnet.
constexpr int   kMenuRingInner  = 50;
// Innerhalb dieses Radius ist die Mitte gemeint (Logo -> zurueck zum Home).
constexpr int   kMenuCenterRadius = 50;

void menuIconCenter(size_t index, int* outX, int* outY) {
  const float angle = kMenuStartAngle + static_cast<float>(index) * kMenuStep;
  *outX = kCx + static_cast<int>(cosf(angle) * kMenuRingRadius);
  *outY = kCy + static_cast<int>(sinf(angle) * kMenuRingRadius);
}

// Liefert den Menuepunkt zu einer Touch-Position, oder -1.
//
// Zwei Stufen, damit auch ein ungenauer Tipper sitzt:
//   1. direkter Treffer im Umkreis eines Icons
//   2. sonst: Winkel zur Mitte ausrechnen und das naechstgelegene Icon
//      nehmen - der komplette Ring ausserhalb des Logos ist damit aktiv.
int menuIndexFromTouch(int tx, int ty) {
  const int dx = tx - kCx;
  const int dy = ty - kCy;
  const int dist2 = dx * dx + dy * dy;

  for (size_t i = 0; i < static_cast<size_t>(kMenuCount); ++i) {
    int cx = 0, cy = 0;
    menuIconCenter(i, &cx, &cy);
    const int ddx = tx - cx;
    const int ddy = ty - cy;
    if (ddx * ddx + ddy * ddy <= kMenuHitRadius * kMenuHitRadius) return static_cast<int>(i);
  }

  if (dist2 < kMenuRingInner * kMenuRingInner) return -1;   // Mitte, kein Ring

  // atan2 liefert -PI..PI; auf den Winkel des ersten Eintrags beziehen und
  // auf 0..2PI normieren, dann durch die Schrittweite teilen.
  float rel = atan2f(static_cast<float>(dy), static_cast<float>(dx)) - kMenuStartAngle;
  const float twoPi = 6.2831853f;
  while (rel < 0.0f)      rel += twoPi;
  while (rel >= twoPi)    rel -= twoPi;

  int index = static_cast<int>(rel / kMenuStep + 0.5f);
  if (index >= static_cast<int>(kMenuCount)) index = 0;
  return index;
}

void drawMainMenu(uint32_t now) {
  beginFrame();
  // Oben sitzt das PowerOff-Symbol und verdeckt den Statuspunkt. Im Ring-
  // Menue zeigt deshalb das Status-Symbol (i) die Verbindungsfarbe.
  drawRoundFrame(true, false);
  drawMenuCenterLogo();

  for (size_t i = 0; i < static_cast<size_t>(kMenuCount); ++i) {
    int cx = 0, cy = 0;
    menuIconCenter(i, &cx, &cy);
    const bool selected = static_cast<int>(i) == app.menuIndex;
    const int  size     = selected ? 40 : 34;

    uint16_t fill   = selected ? kColPanelHi : kColPanel;
    uint16_t border = selected ? kColLineHi  : kColLine;
    uint16_t icon   = selected ? TFT_WHITE   : kColText;

    if (i == kMenuPowerOff) {
      border = selected ? kColWarn : rgb565(120, 80, 40);
      if (app.pendingPowerOff) fill = rgb565(140, 70, 30);
    }
    if (i == kMenuStatus) {
      icon = connectionColor();
      if (!selected) border = icon;
    }
    if (i == kMenuRfidRun  && !app.rfidReady) icon = kColLine;
    if (i == kMenuSdBrowse && !app.sdReady)   icon = kColLine;

    gDraw->fillRoundRect(cx - size / 2, cy - size / 2, size, size, 6, fill);
    gDraw->drawRoundRect(cx - size / 2, cy - size / 2, size, size, 6, border);
    drawMaskIcon24(menuIconMask(i), cx, cy, icon);
  }

  if (!app.menuLabel.isEmpty() && now <= app.menuLabelUntilMs) {
    gDraw->fillRoundRect(40, 74, 160, 24, 6, rgb565(10, 16, 28));
    gDraw->drawRoundRect(40, 74, 160, 24, 6, kColLineHi);
    fontText();
    drawClipped(app.menuLabel, kCx, 86, 150, kColText, middle_center);
  }
}

// ---------------------------------------------------------------------------
// Listenzeile
// ---------------------------------------------------------------------------
void drawListRow(int row, const String& left, const String& right, bool selected) {
  const int y = kListY0 + row * kRowH;
  const int h = kRowH - 3;
  const int half = chordHalfWidth(y + h / 2) - 4;
  const int w = std::min(200, 2 * half);
  const int x = kCx - w / 2;

  gDraw->fillRoundRect(x, y, w, h, 6, selected ? kColPanelHi : kColPanel);
  gDraw->drawRoundRect(x, y, w, h, 6, selected ? kColLineHi : kColLine);

  fontSmall();
  const int rightW = right.isEmpty() ? 0 : 58;
  drawClipped(left, x + 8, y + h / 2, w - 16 - rightW, selected ? TFT_WHITE : kColText, middle_left);
  if (!right.isEmpty()) {
    drawClipped(right, x + w - 8, y + h / 2, rightW, selected ? kColOk : kColLabel, middle_right);
  }
}

int listWindowStart(int selected, int count) {
  if (count <= kListRows) return 0;
  int start = selected - kListRows / 2;
  start = std::max(0, std::min(start, count - kListRows));
  return start;
}

void drawScrollMarks(int start, int count) {
  fontSmall();
  gDraw->setTextDatum(middle_center);
  if (start > 0) {
    gDraw->setTextColor(kColLabel);
    gDraw->drawString("^", kCx, kListY0 - 8);
  }
  if (start + kListRows < count) {
    gDraw->setTextColor(kColLabel);
    gDraw->drawString("v", kCx, kListY0 + kListRows * kRowH + 2);
  }
}

// Zeilenindex aus einer Touch-Position; -1 wenn daneben getippt wurde.
// Der Zwischenraum zwischen zwei Zeilen wird bewusst der oberen Zeile
// zugeschlagen - so gibt es keine "toten" Streifen, in denen ein Tipper
// wirkungslos bleibt.
int listRowFromTouch(int ty) {
  if (ty < kListY0) return -1;
  const int row = (ty - kListY0) / kRowH;
  if (row < 0 || row >= kListRows) return -1;
  return row;
}

// ---------------------------------------------------------------------------
// CPU-Menue
// ---------------------------------------------------------------------------
void drawCpuMenu() {
  beginFrame();
  drawRoundFrame();
  drawTitle("CPU SPEED");
  fontSmall();
  drawCentered(String("aktuell: ") + app.currentCpuValue, kSubY, kColOk);

  const int count    = static_cast<int>(app.cpuChoiceCount);
  const int selected = std::max(0, std::min(app.cpuIndex, count - 1));
  const int start    = listWindowStart(selected, count);

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    drawListRow(row, app.cpuDisplayOptions[index],
                app.cpuDisplayOptions[index] == app.currentCpuValue ? "aktiv" : "",
                index == selected);
  }
  drawScrollMarks(start, count);
  drawHint("Druecken = setzen");
}

// ---------------------------------------------------------------------------
// Statusseite
// ---------------------------------------------------------------------------
void drawStatusScreen() {
  beginFrame();
  drawRoundFrame();
  drawTitle("STATUS");

  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  auto line = [](int row, const char* label, const String& value, uint16_t color) {
    const int y    = 56 + row * 17;
    const int half = chordHalfWidth(y) - 6;
    fontSmall();
    drawClipped(label, kCx - half, y, 64, kColLabel, middle_left);
    drawClipped(value, kCx + half, y, 2 * half - 66, color, middle_right);
  };

  line(0, "WiFi",   wifiOk ? WiFi.SSID() : String(app.portalActive ? "Setup-Portal" : "disconnected"),
       wifiOk ? kColOk : kColWarn);
  line(1, "IP",     wifiOk ? WiFi.localIP().toString() : String("---"), kColText);
  line(2, "RSSI",   wifiOk ? String(WiFi.RSSI()) + " dBm" : String("---"), kColText);
  line(3, "c64u",   targetHost(), kColText);
  line(4, "Target", app.connection.targetReachable ? "reachable" : "not reached",
       app.connection.targetReachable ? kColOk : kColWarn);
  line(5, "Auth",   app.connection.authOk ? "ok" : "not verified",
       app.connection.authOk ? kColOk : kColWarn);
  line(6, "CPU",    app.currentCpuValue, kColText);
  line(7, "NFC/SD", String(app.rfidReady ? "ok" : "-") + " / " + (app.sdReady ? "ok" : "-"),
       kColText);
  line(8, "Heap",   String(ESP.getFreeHeap() / 1024) + " kB", kColText);

  fontSmall();
  drawCentered(app.connection.detail, 194, kColInfo);
  drawHint("Druecken = Test");
}

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------
String settingsValue(size_t index) {
  switch (index) {
    case kSetNfcWrite:      return app.rfidReady ? "Karte" : "kein NFC";
    case kSetNfcInfo:       return app.rfidReady ? "lesen" : "kein NFC";
    case kSetNfcDump:       return app.rfidReady ? "sichern" : "kein NFC";
    case kSetNfcRestore:    return app.rfidReady ? "zurueck" : "kein NFC";
    case kSetNfcRandom:     return app.rfidReady ? "Ordner" : "kein NFC";
    case kSetNfcCmd:        return app.rfidReady ? "Befehl" : String("kein NFC");
    case kSetCardConfirm:   return cardConfirmLabel(app.settings.cardConfirmS);
    case kSetWifi: {
      if (app.portalActive) return "Portal";
      if (gWifiCount == 0)  return "nicht gesetzt";
      String value(static_cast<unsigned>(gWifiCount));
      value += (gWifiCount == 1) ? " Netz" : " Netze";
      return value;
    }
    case kSetShutdown:      return app.pendingShutdown ? "NOCHMAL" : "Jetzt";
    case kSetShortcutBtn:   return shortcutLabel(app.settings.shortcutButton);
    case kSetShortcutTouch: return shortcutLabel(app.settings.shortcutTouch);
    case kSetAutoNfc:       return app.rfidReady ? autoNfcLabel(app.settings.autoNfc) : String("kein NFC");
    case kSetPowerOffAsk:   return app.settings.powerOffComboAsk ? "Abfrage" : "Direkt";
    case kSetPowerOffTime:  return timeStepLabel(app.settings.powerOffConfirmDs);
    case kSetAnimations:    return app.settings.animationsEnabled ? "On" : "Off";
    case kSetEffect:        return effectLabel(app.settings.effectMode);
    case kSetAnimSpeed:     return animationSpeedLabel(app.settings.animationSpeed);
    case kSetEffectTime:    return effectDurationLabel(app.settings.effectDuration);
    case kSetStaticTime:    return staticDurationLabel(app.settings.staticDuration);
    case kSetHomeTimeout:   return homeTimeoutLabel(app.settings.homeTimeout);
    case kSetEncoderSteps:  return encoderStepsLabel(app.settings.encoderSteps);
    case kSetBrightness:    return String(app.settings.brightness);
    case kSetDiskAction:    return diskActionLabel(app.settings.diskAction);
    case kSetDiskDrive:     return uploadDriveLabel(app.settings.uploadDrive);
    case kSetJoystick:      return app.joyValue.isEmpty() ? String("?")
                                                          : joyLabelFromToken(app.joyValue);
    case kSetBeep:          return app.settings.beepEnabled ? "On" : "Off";
    case kSetFactoryReset:  return "Now";
  }
  return "";
}

void drawSettings() {
  beginFrame();
  drawRoundFrame();
  drawTitle("SETTINGS");

  const int count    = static_cast<int>(kSettingsCount);
  const int selected = std::max(0, std::min(app.settingsIndex, count - 1));
  const int start    = listWindowStart(selected, count);

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    drawListRow(row, kSettingsItems[index], settingsValue(index), index == selected);
  }
  drawScrollMarks(start, count);
  drawHint(String(selected + 1) + "/" + String(count) + "   Druecken = aendern");
}

// ---------------------------------------------------------------------------
// WLAN-Einrichtung
// ---------------------------------------------------------------------------
String wifiMenuValue(size_t index) {
  switch (index) {
    case kWifiScanNow:
      return app.wifiScanCount == 0 ? String("suchen")
                                    : String(static_cast<unsigned>(app.wifiScanCount));
    case kWifiFromSd:       return app.sdReady ? "wifi.txt" : "keine SD";
    case kWifiPortal:       return app.portalActive ? "an" : "aus";
    case kWifiConnectSaved: return String(static_cast<unsigned>(gWifiCount));
    case kWifiToCard:       return gWifiCount == 0 ? "leer" : "schreiben";
    case kWifiToSd:         return !app.sdReady  ? "keine SD"
                                 : gWifiCount == 0 ? "leer"
                                                   : "wifi.txt";
    case kWifiDeleteOne:    return gWifiCount == 0 ? "leer" : "waehlen";
    case kWifiDeleteAll:    return "Reset";
  }
  return "";
}

void drawWifiMenu() {
  beginFrame();
  drawRoundFrame();
  drawTitle("WLAN");

  fontSmall();
  const bool online = WiFi.status() == WL_CONNECTED;
  drawCentered(online ? WiFi.SSID() : String(gWifiCount == 0 ? "kein Netz gespeichert"
                                                             : "nicht verbunden"),
               kSubY, online ? kColOk : kColWarn);

  const int count    = static_cast<int>(kWifiMenuCount);
  const int selected = std::max(0, std::min(app.wifiMenuIndex, count - 1));
  const int start    = listWindowStart(selected, count);

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    drawListRow(row, kWifiMenuItems[index], wifiMenuValue(index), index == selected);
  }
  drawScrollMarks(start, count);
  drawHint("Druecken = waehlen");
}

void drawWifiScan() {
  beginFrame();
  drawRoundFrame();
  drawTitle("NETZ WAEHLEN");

  const int count = static_cast<int>(app.wifiScanCount);
  if (count == 0) {
    fontSmall();
    drawCentered("kein Netz gefunden", 110, kColWarn);
    drawCentered("Druecken = zurueck", 130, kColLabel);
    drawHint("");
    return;
  }

  const int selected = std::max(0, std::min(app.wifiScanIndex, count - 1));
  const int start    = listWindowStart(selected, count);

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    String    info  = String(static_cast<int>(gWifiScan[index].rssi));
    if (wifiProfileIndex(gWifiScan[index].ssid) >= 0) info = "bekannt";
    else if (gWifiScan[index].open)                   info = "offen";
    drawListRow(row, gWifiScan[index].ssid, info, index == selected);
  }
  drawScrollMarks(start, count);
  drawHint(String(selected + 1) + "/" + String(count) + "   Druecken = waehlen");
}

void drawWifiSaved() {
  beginFrame();
  drawRoundFrame();
  drawTitle(app.wifiSavedDelete ? "NETZ LOESCHEN"
                                : (app.wifiSavedToCard ? "AUF NFC-KARTE" : "GESPEICHERT"));

  const int count = static_cast<int>(gWifiCount);
  if (count == 0) {
    fontSmall();
    drawCentered("noch kein Netz gespeichert", 110, kColWarn);
    drawCentered("Druecken = zurueck", 130, kColLabel);
    drawHint("");
    return;
  }

  const int selected = std::max(0, std::min(app.wifiSavedIndex, count - 1));
  const int start    = listWindowStart(selected, count);
  const bool online  = WiFi.status() == WL_CONNECTED;

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    const bool active = online && WiFi.SSID() == gWifiProfiles[index].ssid;
    drawListRow(row, gWifiProfiles[index].ssid,
                active ? "aktiv" : (gWifiProfiles[index].pass.isEmpty() ? "offen" : ""),
                index == selected);
  }
  drawScrollMarks(start, count);
  drawHint(app.wifiSavedDelete ? "Druecken = loeschen"
                               : (app.wifiSavedToCard ? "Druecken = auf Karte"
                                                      : "Druecken = verbinden"));
}

void drawWifiCard() {
  beginFrame();
  drawRoundFrame();
  drawTitle("WLAN-PASSWORT");

  gDraw->fillRoundRect(28, 62, 184, 86, 10, kColPanel);
  gDraw->drawRoundRect(28, 62, 184, 86, 10, kColInfo);

  fontSmall();
  drawClipped("Netz:", kCx, 78, 172, kColLabel, middle_center);
  drawClipped(app.wifiPendingSsid.isEmpty() ? String("von der Karte") : app.wifiPendingSsid,
              kCx, 94, 172, kColOk, middle_center);
  drawClipped(app.rfidReady ? "Karte mit Passwort auflegen" : "RFID-Leser nicht gefunden",
              kCx, 118, 172, app.rfidReady ? kColText : kColErr, middle_center);
  drawClipped("Textkarte: WIFI:S:..;P:..;;", kCx, 134, 172, kColLabel, middle_center);

  fontSmall();
  drawCentered(app.wifiHint, 168, kColInfo);
  drawHint("Druecken = zurueck");
}

void drawWifiPortal() {
  beginFrame();
  drawRoundFrame();
  drawTitle("SETUP-PORTAL");

  fontSmall();
  drawCentered("Mit diesem Netz verbinden:", 62, kColLabel);
  fontText();
  drawCentered(kPortalSsid, 82, kColOk);

  fontSmall();
  drawCentered("Passwort:", 104, kColLabel);
  fontText();
  drawCentered(kPortalPass, 122, kColOk);

  fontSmall();
  drawCentered("dann im Browser oeffnen:", 146, kColLabel);
  drawCentered(app.portalActive ? WiFi.softAPIP().toString() : String("---"), 162, kColInfo);

  const uint32_t leftMs = (millis() - app.portalTouchedMs >= kPortalIdleMs)
                              ? 0
                              : (kPortalIdleMs - (millis() - app.portalTouchedMs));
  drawCentered(String("endet in ") + String(leftMs / 1000) + "s", 182, kColLabel);
  drawHint("Druecken = beenden");
}

// ---------------------------------------------------------------------------
// SD-Browser
// ---------------------------------------------------------------------------
void drawSdBrowser() {
  beginFrame();
  drawRoundFrame();
  drawTitle(app.sdPickMode == 3 ? "ZUFALLS-ORDNER"
            : app.sdPickMode == 1 ? "AUF KARTE"
            : app.sdPickMode == 2 ? "DUMP WAEHLEN" : "SD-KARTE");
  fontSmall();
  drawCentered(app.sdPath, kSubY, kColInfo);

  const int count = sdBrowserCount();

  if (count == 0) {
    fontSmall();
    drawCentered("keine passenden Dateien", 110, kColWarn);
  } else {
    const int selected = std::max(0, std::min(app.sdIndex, count - 1));
    const int start    = listWindowStart(selected, count);

    for (int row = 0; row < kListRows && start + row < count; ++row) {
      const int index = start + row;
      String left, right;
      const int slot = sdBrowserSlot(index);
      if (slot == -1) {
        left  = "..";
        right = "up";
      } else if (slot == -2) {
        left  = "[dieser Ordner]";
        right = "Zufall";
      } else {
        const DirEntryInfo& e = app.sdEntries[slot];
        left  = e.name;
        right = e.isDir ? "dir" : lowerExt(e.name);
      }
      drawListRow(row, left, right, index == selected);
    }
    drawScrollMarks(start, count);
  }
  drawHint(app.sdPickMode ? "lang = zurueck" : "Druecken = starten");
}

// ---------------------------------------------------------------------------
// Befehl fuer eine Karte auswaehlen
// ---------------------------------------------------------------------------
void drawCmdPick() {
  beginFrame();
  drawRoundFrame();
  drawTitle("KARTEN-BEFEHL");
  fontSmall();
  drawCentered("auf Karte schreiben", kSubY, kColInfo);

  const int count    = static_cast<int>(cmdListCount());
  const int selected = std::max(0, std::min(app.cmdIndex, count - 1));
  const int start    = listWindowStart(selected, count);

  for (int row = 0; row < kListRows && start + row < count; ++row) {
    const int index = start + row;
    const CardCommand c = cmdListAt(static_cast<size_t>(index));
    drawListRow(row, cardCommandLabel(c), cmdListTag(static_cast<size_t>(index)),
                index == selected);
  }
  drawScrollMarks(start, count);
  drawHint(String(selected + 1) + "/" + String(count) + "   Druecken = waehlen");
}

// ---------------------------------------------------------------------------
// RFID-Seiten
// ---------------------------------------------------------------------------
void drawRfidScreen() {
  const bool writeMode = (app.screen == ScreenMode::RfidWrite ||
                          app.screen == ScreenMode::RfidRestore);
  beginFrame();
  drawRoundFrame();

  const char* title = "KARTE LESEN";
  if (app.screen == ScreenMode::RfidWrite)   title = "KARTE SCHREIBEN";
  if (app.screen == ScreenMode::RfidDump)    title = "KARTE SICHERN";
  if (app.screen == ScreenMode::RfidRestore) title = "DUMP ZURUECK";
  drawTitle(title);

  gDraw->fillRoundRect(28, 62, 184, 86, 10, kColPanel);
  gDraw->drawRoundRect(28, 62, 184, 86, 10, writeMode ? kColWarn : kColInfo);

  fontSmall();
  drawClipped(app.rfidReady ? "Karte auf den M5Dial legen" : "RFID-Leser nicht gefunden",
              kCx, 78, 172, app.rfidReady ? kColText : kColErr, middle_center);
  if (app.rfidReady) {
    drawClipped("MIFARE Classic / NTAG213-216", kCx, 92, 172, kColLabel, middle_center);
  }

  if (writeMode) {
    const bool restore = (app.screen == ScreenMode::RfidRestore);
    const bool command = !restore && !app.pendingCardText.isEmpty();
    // WLAN-Karten zeigen nur die SSID - das Passwort steht sonst gross auf
    // dem Display.
    const bool wifiCard = command && textLooksLikeWifi(app.pendingCardText);
    drawClipped(restore ? "Dump:" : (wifiCard ? "WLAN:" : (command ? "Befehl:" : "Pfad:")),
                kCx, 110, 172, kColLabel, middle_center);
    drawClipped(restore ? app.pendingDump
                        : (wifiCard ? app.rfidHint
                                    : (command ? app.pendingCardText : app.pendingPath)),
                kCx, 126, 172, kColOk, middle_center);
  } else {
    drawClipped(app.lastCardPath.isEmpty() ? String("noch keine Karte gelesen") : app.lastCardPath,
                kCx, 118, 172, kColLabel, middle_center);
  }

  fontSmall();
  drawCentered(app.rfidHint, 168, kColInfo);

  if (app.cardPowerOffPending) {
    const int32_t left = static_cast<int32_t>(app.cardPowerOffUntilMs - millis());
    fontText();
    drawCentered(String("c64u OFF? ") + String(std::max<int32_t>(0, left) / 1000 + 1) + "s",
                 188, kColWarn);
    drawHint("Karte nochmal oder Taste");
  } else {
    drawHint("Druecken = zurueck");
  }
}

// ---------------------------------------------------------------------------
// Karten-Info: alle Zeilen werden in eine scrollbare Ansicht umgebrochen
// ---------------------------------------------------------------------------
constexpr size_t kInfoViewMax  = 80;
constexpr int    kInfoRowStep  = 13;
constexpr int    kInfoRows     = 11;
constexpr size_t kInfoCharsRow = 30;

String   gInfoView[kInfoViewMax];
uint16_t gInfoViewColor[kInfoViewMax];
size_t   gInfoViewCount = 0;

void addViewLine(const String& text, uint16_t color) {
  if (gInfoViewCount < kInfoViewMax) {
    gInfoView[gInfoViewCount]      = text;
    gInfoViewColor[gInfoViewCount] = color;
    gInfoViewCount++;
  }
}

void addViewEntry(const String& line) {
  // Die ersten zehn Zeichen sind die Beschriftung, danach folgt der Wert.
  String label = line.substring(0, 10);
  label.trim();
  String value = line.length() > 10 ? line.substring(10) : String("");
  value.trim();

  addViewLine(label, kColLabel);
  while (!value.isEmpty()) {
    size_t cut = std::min(kInfoCharsRow, static_cast<size_t>(value.length()));
    if (cut < static_cast<size_t>(value.length())) {
      const int slash = value.lastIndexOf('/', static_cast<int>(cut) - 1);
      if (slash > static_cast<int>(cut) / 2) cut = static_cast<size_t>(slash) + 1;
    }
    addViewLine("  " + value.substring(0, cut), kColText);
    value = value.substring(cut);
  }
}

void buildInfoView() {
  gInfoViewCount = 0;
  for (size_t i = 0; i < app.infoCount; ++i) addViewEntry(app.infoLines[i]);
  if (app.infoCount2 > 0) {
    addViewLine("--- Rohdaten ---", kColInfo);
    for (size_t i = 0; i < app.infoCount2; ++i) addViewEntry(app.infoLines2[i]);
  }
  app.infoScroll = 0;
}

int infoMaxScroll() {
  const int over = static_cast<int>(gInfoViewCount) - kInfoRows;
  return over > 0 ? over : 0;
}

void drawRfidInfoScreen() {
  beginFrame();
  drawRoundFrame();

  if (gInfoViewCount == 0) {
    drawTitle("KARTEN-INFO");
    fontSmall();
    drawCentered(app.rfidReady ? "Karte auf den M5Dial legen" : "RFID-Leser nicht gefunden",
                 110, app.rfidReady ? kColText : kColErr);
    drawCentered("MIFARE Classic / NTAG213-216", 128, kColLabel);
    drawHint("Druecken = zurueck");
    return;
  }

  const int start = std::max(0, std::min(app.infoScroll, infoMaxScroll()));
  fontSmall();
  int y = 46;
  for (int row = 0; row < kInfoRows; ++row) {
    const size_t index = static_cast<size_t>(start + row);
    if (index >= gInfoViewCount) break;
    const int half = chordHalfWidth(y) - 6;
    drawClipped(gInfoView[index], kCx - half, y, 2 * half, gInfoViewColor[index], middle_left);
    y += kInfoRowStep;
  }

  fontSmall();
  drawClipped(String(start + 1) + "-" +
                  String(std::min(gInfoViewCount, static_cast<size_t>(start + kInfoRows))) +
                  "/" + String(gInfoViewCount),
              kCx, 200, 120, kColLabel, middle_center);
  drawHint("drehen = scrollen");
}

// ---------------------------------------------------------------------------
// Fortschritt (Upload)
// ---------------------------------------------------------------------------
void drawBusyScreen() {
  beginFrame();
  drawRoundFrame();
  drawTitle(app.busyTitle.c_str());

  fontSmall();
  drawCentered(app.busyDetail, 96, kColText);

  const int barX = 40;
  const int barW = kScrW - 2 * barX;
  const int barY = 124;

  gDraw->drawRoundRect(barX, barY, barW, 18, 5, kColLine);
  if (app.busyTotal > 0) {
    const int filled = static_cast<int>((static_cast<uint64_t>(barW - 4) * app.busySent) / app.busyTotal);
    gDraw->fillRect(barX + 2, barY + 2, std::max(0, filled), 14, kColOk);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u / %u kB",
             static_cast<unsigned>(app.busySent / 1024), static_cast<unsigned>(app.busyTotal / 1024));
    fontSmall();
    drawCentered(buf, 158, kColLabel);
  } else {
    fontSmall();
    drawCentered("bitte warten...", 158, kColLabel);
  }
  drawHint("UPLOAD");
  endFrame();
}

// ---------------------------------------------------------------------------
// Modal-Overlay und Gesamtausgabe
// ---------------------------------------------------------------------------
void drawModalOverlay(uint32_t now) {
  if (!modalVisible(now)) return;

  const int boxW = 196;
  const int boxH = 74;
  const int x = (kScrW - boxW) / 2;
  const int y = (kScrH - boxH) / 2;

  gDraw->fillRoundRect(x, y, boxW, boxH, 12, rgb565(6, 10, 18));
  gDraw->drawRoundRect(x, y, boxW, boxH, 12, app.modalColor);
  gDraw->fillRoundRect(x + 8, y + 8, boxW - 16, 5, 2, app.modalColor);

  fontBig();
  if (gDraw->textWidth(app.modalText) > (boxW - 20)) fontText();
  if (gDraw->textWidth(app.modalText) > (boxW - 20)) fontSmall();
  drawClipped(app.modalText, kCx, y + 44, boxW - 16, app.modalColor, middle_center);
}

void render(uint32_t now) {
  switch (app.screen) {
    case ScreenMode::Home:        drawHome(now);         break;
    case ScreenMode::Menu:        drawMainMenu(now);     break;
    case ScreenMode::CpuMenu:     drawCpuMenu();         break;
    case ScreenMode::Status:      drawStatusScreen();    break;
    case ScreenMode::Settings:    drawSettings();        break;
    case ScreenMode::SdBrowser:   drawSdBrowser();       break;
    case ScreenMode::CmdPick:     drawCmdPick();         break;
    case ScreenMode::RfidRun:
    case ScreenMode::RfidWrite:
    case ScreenMode::RfidDump:
    case ScreenMode::RfidRestore: drawRfidScreen();      break;
    case ScreenMode::RfidInfo:    drawRfidInfoScreen();  break;
    case ScreenMode::WifiMenu:    drawWifiMenu();        break;
    case ScreenMode::WifiScan:    drawWifiScan();        break;
    case ScreenMode::WifiCard:    drawWifiCard();        break;
    case ScreenMode::WifiPortal:  drawWifiPortal();      break;
    case ScreenMode::WifiSaved:   drawWifiSaved();       break;
    case ScreenMode::Busy:        drawBusyScreen();      return;   // schiebt selbst
  }
  drawModalOverlay(now);
  endFrame();
}

void setScreen(ScreenMode next, uint32_t now) {
  if (app.screen == next) return;
  // Sobald der Benutzer selbst navigiert, gilt die Leseseite nicht mehr als
  // automatisch geoeffnet.
  if (next != ScreenMode::RfidRun) app.autoRfidActive = false;
  app.screen = next;
  app.encoderBase     = M5Dial.Encoder.read();
  app.encoderResidual = 0;
  if (next == ScreenMode::Home) resetHomeAnimation(now);
  if (next == ScreenMode::Menu) showMenuLabel(kMenuLabels[app.menuIndex], now);
}

// ===========================================================================
//  Aktionen
// ===========================================================================
void openSdBrowser(uint8_t forCard, uint32_t now) {
  app.sdPickMode = forCard;
  if (!app.sdReady && !initSd()) {
    setModal("KEINE SD-KARTE", kColErr, now, 2000);
    return;
  }
  if (forCard == 2) app.sdPath = kDumpDir;   // Dumps liegen im eigenen Ordner
  if (!readDirectory(app.sdPath)) {
    app.sdPath = "/";
    if (!readDirectory("/")) {
      setModal("SD NICHT LESBAR", kColErr, now, 2000);
      return;
    }
  }
  setScreen(ScreenMode::SdBrowser, now);
}

// ---------------------------------------------------------------------------
// WLAN-Einrichtung: Aktionen
// ---------------------------------------------------------------------------
void openWifiMenu(uint32_t now) {
  app.wifiMenuIndex = 0;
  setScreen(app.portalActive ? ScreenMode::WifiPortal : ScreenMode::WifiMenu, now);
}

void wifiConnectProfile(size_t index, uint32_t now) {
  if (index >= gWifiCount) return;
  gWifiTry              = index;
  app.lastWiFiAttemptMs = 0;
  if (app.portalActive) stopPortal(now);      // beendet und verbindet selbst
  else                  beginWiFi(now);
  setModal("VERBINDE...", kColInfo, now, 1800);
  setScreen(ScreenMode::WifiMenu, now);
}

// Der Suchlauf blockiert einige Sekunden. Damit der M5Dial nicht eingefroren
// wirkt, wird der Hinweis vorher noch einmal ausgegeben.
void wifiStartScan(uint32_t now) {
  setModal("SUCHE NETZE", kColInfo, now, 8000);
  render(now);
  wifiRunScan();
  app.modalText = "";

  if (app.wifiScanCount == 0) {
    setModal("NICHTS GEFUNDEN", kColWarn, now, 1800);
    beep(500, 120);
    return;
  }
  beep(2400, 30);
  setScreen(ScreenMode::WifiScan, now);
}

void wifiChooseNetwork(int index, uint32_t now) {
  if (index < 0 || index >= static_cast<int>(app.wifiScanCount)) return;
  const WifiScanEntry entry = gWifiScan[index];

  // Bereits gespeichert? Dann reicht ein Verbindungsversuch.
  const int known = wifiProfileIndex(entry.ssid);
  if (known >= 0) {
    wifiConnectProfile(static_cast<size_t>(known), now);
    return;
  }

  // Offenes Netz braucht kein Passwort.
  if (entry.open) {
    wifiAddProfile(entry.ssid, "");
    app.lastWiFiAttemptMs = 0;
    beginWiFi(now);
    setModal("GESPEICHERT", kColOk, now, 1600);
    setScreen(ScreenMode::WifiMenu, now);
    return;
  }

  app.wifiPendingSsid = entry.ssid;
  app.wifiHint = app.rfidReady ? "Karte auflegen..." : "kein NFC - Portal nutzen";
  setScreen(ScreenMode::WifiCard, now);
}

void wifiMenuSelect(uint32_t now) {
  const int index = std::max(0, std::min(app.wifiMenuIndex, static_cast<int>(kWifiMenuCount) - 1));
  beep(2200, 25);

  switch (index) {
    case kWifiScanNow:
      wifiStartScan(now);
      break;

    case kWifiFromSd: {
      String error;
      const size_t added = loadWifiFromSd(&error);
      if (added > 0) {
        app.lastWiFiAttemptMs = 0;
        gWifiTry = 0;
        beginWiFi(now);
        setModal(String(static_cast<unsigned>(added)) + " NETZ(E) GELADEN", kColOk, now, 2000);
      } else {
        beep(500, 120);
        setModal(error.isEmpty() ? String("SD-FEHLER") : error, kColErr, now, 2400);
      }
      break;
    }

    case kWifiPortal:
      // Vor dem Portal einmal suchen, damit die Weboberflaeche eine Netzliste
      // anbieten kann.
      setModal("PORTAL STARTET", kColInfo, now, 8000);
      render(now);
      wifiRunScan();
      app.modalText = "";
      startPortal(now);
      setScreen(ScreenMode::WifiPortal, now);
      break;

    case kWifiConnectSaved:
      app.wifiSavedDelete = false;
      app.wifiSavedToCard = false;
      app.wifiSavedIndex  = 0;
      setScreen(ScreenMode::WifiSaved, now);
      break;

    case kWifiToCard:
      // Ein gespeichertes Netz (z.B. gerade ueber das Portal eingegeben) auf
      // eine NFC-Karte schreiben - damit laesst sich ein weiteres Geraet ohne
      // Tipperei einrichten.
      if (gWifiCount == 0) { setModal("NICHTS GESPEICHERT", kColWarn, now, 1600); break; }
      if (!app.rfidReady)  { setModal("KEIN NFC-LESER", kColErr, now, 1800); break; }
      app.wifiSavedDelete = false;
      app.wifiSavedToCard = true;
      app.wifiSavedIndex  = 0;
      setScreen(ScreenMode::WifiSaved, now);
      break;

    case kWifiToSd: {
      // Alle gespeicherten Netze als /wifi.txt auf die SD-Karte schreiben -
      // das Gegenstueck zu "Von SD laden" und damit ein einfacher Weg, die
      // Zugangsdaten auf ein zweites Geraet zu bringen.
      if (gWifiCount == 0) { setModal("NICHTS GESPEICHERT", kColWarn, now, 1600); break; }
      String error;
      const size_t written = saveWifiToSd(&error);
      if (written > 0) {
        setModal(String(static_cast<unsigned>(written)) + " NETZ(E) AUF SD", kColOk, now, 2000);
      } else {
        beep(500, 120);
        setModal(error.isEmpty() ? String("SD-FEHLER") : error, kColErr, now, 2400);
      }
      break;
    }

    case kWifiDeleteOne:
      if (gWifiCount == 0) { setModal("NICHTS GESPEICHERT", kColWarn, now, 1600); break; }
      app.wifiSavedDelete = true;
      app.wifiSavedToCard = false;
      app.wifiSavedIndex  = 0;
      setScreen(ScreenMode::WifiSaved, now);
      break;

    case kWifiDeleteAll:
      wifiClearProfiles();
      WiFi.disconnect(false, true);
      setModal("ALLE GELOESCHT", kColWarn, now, 1800);
      break;
  }
}

void wifiSavedSelect(uint32_t now) {
  const int count = static_cast<int>(gWifiCount);
  if (count == 0) { setScreen(ScreenMode::WifiMenu, now); return; }

  const int index = std::max(0, std::min(app.wifiSavedIndex, count - 1));
  beep(2200, 25);

  if (app.wifiSavedDelete) {
    const String ssid = gWifiProfiles[index].ssid;
    wifiRemoveProfile(static_cast<size_t>(index));
    app.wifiSavedIndex = std::max(0, index - 1);
    setModal("GELOESCHT", kColWarn, now, 1400);
    app.wifiHint = ssid;
    if (gWifiCount == 0) setScreen(ScreenMode::WifiMenu, now);
    return;
  }

  if (app.wifiSavedToCard) {
    const WifiProfile& profile = gWifiProfiles[index];
    app.pendingCardText = wifiCardText(profile.ssid, profile.pass);
    app.pendingPath     = "";
    app.rfidHint        = profile.ssid;
    app.wifiSavedToCard = false;
    setScreen(ScreenMode::RfidWrite, now);
    return;
  }

  wifiConnectProfile(static_cast<size_t>(index), now);
}

// ---------------------------------------------------------------------------
// M5Dial selbst ausschalten (Einstellungen -> "M5Dial aus")
// ---------------------------------------------------------------------------
// Im Akkubetrieb haelt sich der M5Dial ueber G46 (HOLD) selbst eingeschaltet;
// M5Unified setzt den Pin in begin() auf HIGH. Geht er auf LOW, trennt die
// Selbsthaltung den Akku - laut M5Stack bleiben dann rund 2 uA Ruhestrom.
// Eingeschaltet wird wieder mit der Taste (WAKE).
//
// Solange die Taste gedrueckt ist, ueberbrueckt sie die Selbsthaltung.
// Deshalb wird erst nach dem Loslassen abgeschaltet.
//
// Haengt USB oder eine andere externe Quelle dran, laeuft der ESP32 trotz
// HOLD = LOW weiter. Dann gehen Display und Funk aus, und das Geraet schlaeft
// (Light-Sleep), bis die Taste gedrueckt wird; danach folgt ein Neustart.
// HOLD bleibt dabei LOW: wird das Kabel im Schlaf abgezogen, ist der M5Dial
// sofort ganz aus.
constexpr int      kPowerHoldPin    = 46;     // Selbsthaltung (HOLD)
constexpr int      kWakeButtonPin   = 42;     // Taste, LOW = gedrueckt
constexpr uint32_t kShutdownAskMs   = 3000;
constexpr uint32_t kDialOffBootGuardMs = 8000;

void shutdownDevice() {
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(kColWarn, TFT_BLACK);
  M5Dial.Display.setTextSize(3);
  M5Dial.Display.drawString("AUS", kCx, kCy);
  beep(1200, 60);
  delay(90);
  beep(700, 120);
  delay(150);

  const uint32_t t0 = millis();
  while (digitalRead(kWakeButtonPin) == LOW && millis() - t0 < 5000) delay(10);
  delay(300);

  if (app.sdReady) SD.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  pinMode(kPowerHoldPin, OUTPUT);
  digitalWrite(kPowerHoldPin, LOW);
  delay(1500);

  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextSize(2);
  M5Dial.Display.drawString("USB: Schlaf", kCx, kCy);
  delay(1200);
  M5Dial.Display.setBrightness(0);
  M5Dial.Display.sleep();

  while (digitalRead(kWakeButtonPin) == LOW) delay(10);
  delay(50);
  gpio_wakeup_enable(static_cast<gpio_num_t>(kWakeButtonPin), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  esp_restart();
}

void requestShutdown(uint32_t now) {
  if (app.pendingShutdown && (now - app.pendingShutdownAtMs <= kShutdownAskMs)) {
    app.pendingShutdown = false;
    shutdownDevice();
    return;
  }
  app.pendingShutdown     = true;
  app.pendingShutdownAtMs = now;
  beep(900, 60);
  setModal("M5DIAL OFF? NOCHMAL!", kColWarn, now, kShutdownAskMs);
}

void activateSetting(uint32_t now) {
  // Die ersten Eintraege sind Aktionen, keine Einstellungen. Der WLAN-Punkt
  // braucht keinen NFC-Leser und wird deshalb vorab behandelt.
  if (app.settingsIndex == kSetWifi) {
    beep(2200, 25);
    openWifiMenu(now);
    return;
  }

  if (app.settingsIndex == kSetShutdown) {
    requestShutdown(now);
    return;
  }

  if (app.settingsIndex <= kSetLastAction) {
    if (!app.rfidReady) {
      setModal("KEIN NFC-LESER", kColErr, now, 1800);
      return;
    }
    beep(2200, 25);
    switch (app.settingsIndex) {
      case kSetNfcWrite:
        app.pendingCardText = "";         // NFC-Write: Datei waehlen
        openSdBrowser(1, now);
        break;
      case kSetNfcInfo:
        app.infoCount  = 0;               // NFC-Info
        app.infoCount2 = 0;
        gInfoViewCount = 0;
        app.infoScroll = 0;
        app.infoUid    = "";
        app.rfidHint   = "warte auf Karte";
        setScreen(ScreenMode::RfidInfo, now);
        break;
      case kSetNfcDump:
        app.rfidHint = "Karte auflegen zum Sichern";
        setScreen(ScreenMode::RfidDump, now);
        break;
      case kSetNfcRestore:
        app.pendingCardText = "";
        openSdBrowser(2, now);            // NFC-Restore: Dump waehlen
        break;
      case kSetNfcCmd:                    // NFC-Cmd: Befehl waehlen
        // Die CPU-Stufen kommen vom c64u - einmal nachladen, damit die Liste
        // die tatsaechlich moeglichen Werte anbietet.
        if (!app.cpuPathKnown) refreshCpuValue();
        if (!app.joyPathKnown) resolveJoyPath();
        app.cmdIndex = 0;
        setScreen(ScreenMode::CmdPick, now);
        break;
      case kSetNfcRandom:
        // NFC-Zufall: Verzeichnis waehlen
        app.pendingCardText = "";
        openSdBrowser(3, now);
        break;
      default:
        break;
    }
    return;
  }

  switch (app.settingsIndex) {
    case kSetShortcutBtn:   app.settings.shortcutButton = nextShortcutAction(app.settings.shortcutButton); break;
    case kSetShortcutTouch: app.settings.shortcutTouch  = nextShortcutAction(app.settings.shortcutTouch);  break;
    case kSetAutoNfc: {
      uint8_t next = static_cast<uint8_t>(app.settings.autoNfc) + 1;
      if (next > static_cast<uint8_t>(AutoNfcMode::Fast)) next = 0;
      app.settings.autoNfc = static_cast<AutoNfcMode>(next);
      break;
    }
    case kSetCardConfirm:  app.settings.cardConfirmS = nextCardConfirm(app.settings.cardConfirmS);       break;
    case kSetPowerOffAsk:  app.settings.powerOffComboAsk = !app.settings.powerOffComboAsk;               break;
    case kSetPowerOffTime: app.settings.powerOffConfirmDs = nextTimeStep(app.settings.powerOffConfirmDs); break;
    case kSetAnimations:   app.settings.animationsEnabled = !app.settings.animationsEnabled;             break;
    case kSetEffect: {
      uint8_t next = static_cast<uint8_t>(app.settings.effectMode) + 1;
      if (next > static_cast<uint8_t>(DisplayEffectMode::Raster)) next = 0;
      app.settings.effectMode = static_cast<DisplayEffectMode>(next);
      break;
    }
    case kSetAnimSpeed: {
      uint8_t next = static_cast<uint8_t>(app.settings.animationSpeed) + 1;
      if (next > static_cast<uint8_t>(AnimationSpeedMode::Fast)) next = 0;
      app.settings.animationSpeed = static_cast<AnimationSpeedMode>(next);
      break;
    }
    case kSetEffectTime: {
      uint8_t next = static_cast<uint8_t>(app.settings.effectDuration) + 1;
      if (next > static_cast<uint8_t>(EffectDurationMode::Long)) next = 0;
      app.settings.effectDuration = static_cast<EffectDurationMode>(next);
      break;
    }
    case kSetStaticTime: {
      uint8_t next = static_cast<uint8_t>(app.settings.staticDuration) + 1;
      if (next > static_cast<uint8_t>(StaticDurationMode::Long)) next = 0;
      app.settings.staticDuration = static_cast<StaticDurationMode>(next);
      break;
    }
    case kSetHomeTimeout: {
      uint8_t next = static_cast<uint8_t>(app.settings.homeTimeout) + 1;
      if (next > static_cast<uint8_t>(HomeTimeoutMode::Long)) next = 0;
      app.settings.homeTimeout = static_cast<HomeTimeoutMode>(next);
      break;
    }
    case kSetEncoderSteps: {
      uint8_t next = static_cast<uint8_t>(app.settings.encoderSteps) + 1;
      if (next > static_cast<uint8_t>(EncoderStepsMode::Steps6)) next = 0;
      app.settings.encoderSteps = static_cast<EncoderStepsMode>(next);
      break;
    }
    case kSetBrightness:
      app.settings.brightness = nextBrightnessValue(app.settings.brightness);
      applyBrightness();
      break;
    case kSetDiskAction: {
      uint8_t next = static_cast<uint8_t>(app.settings.diskAction) + 1;
      if (next > static_cast<uint8_t>(DiskActionMode::MountRun)) next = 0;
      app.settings.diskAction = static_cast<DiskActionMode>(next);
      break;
    }
    case kSetDiskDrive: {
      uint8_t next = static_cast<uint8_t>(app.settings.uploadDrive) + 1;
      if (next > static_cast<uint8_t>(UploadDriveMode::DriveB)) next = 0;
      app.settings.uploadDrive = static_cast<UploadDriveMode>(next);
      break;
    }
    case kSetJoystick:
      cycleJoystickValue(now);
      break;
    case kSetBeep:
      app.settings.beepEnabled = !app.settings.beepEnabled;
      break;
    case kSetFactoryReset:
      loadDefaultSettings();
      applyBrightness();
      setModal("FACTORY RESET", kColOk, now, 1200);
      break;
  }
  saveSettings();
  resetHomeAnimation(now);
  beep(2200, 25);
}

void handleMenuSelect(uint32_t now) {
  noteInteraction(now);
  beep(1900, 18);        // Tastendruck sofort bestaetigen, das Ergebnis kommt danach
  switch (app.menuIndex) {
    case kMenuPowerOff:  requestPowerOff(now);  break;   // Menue fragt immer nach
    case kMenuReset:     performReset(now);     break;
    case kMenuReboot:    performHardReset(now); break;
    case kMenuUltiMenu:  performMenuButton(now);break;

    case kMenuCpu:
      clearPendingPowerOff();
      refreshCpuValue();
      app.cpuIndex = cpuIndexFromValue(app.currentCpuValue);
      setScreen(ScreenMode::CpuMenu, now);
      break;

    case kMenuRfidRun:
      clearPendingPowerOff();
      if (!app.rfidReady) { setModal("KEIN NFC-LESER", kColErr, now, 1800); break; }
      app.rfidHint = "warte auf Karte";
      setScreen(ScreenMode::RfidRun, now);
      break;

    case kMenuSdBrowse:
      clearPendingPowerOff();
      openSdBrowser(0, now);
      break;

    case kMenuJoySwap:
      toggleJoystickSwap(now);
      break;

    case kMenuStatus:
      clearPendingPowerOff();
      refreshConnectionStatus(now, true);
      setScreen(ScreenMode::Status, now);
      break;

    case kMenuSetup:
      clearPendingPowerOff();
      app.settingsIndex = 0;
      setScreen(ScreenMode::Settings, now);
      break;
  }
}

void sdBrowserSelect(uint32_t now) {
  const int count = sdBrowserCount();
  if (count == 0) return;

  const int index = std::max(0, std::min(app.sdIndex, count - 1));
  const int slot  = sdBrowserSlot(index);

  if (slot == -1) {
    readDirectory(parentPath(app.sdPath));
    return;
  }

  // Zufallskarte: das gerade geoeffnete Verzeichnis selbst nehmen
  if (slot == -2) {
    const String randomPath = randomCardPath(app.sdPath);
    if (pathToCardText(randomPath).length() > kMaxTextLen) {
      setModal("PFAD ZU LANG", kColErr, now, 2200);
      return;
    }
    app.pendingCardText = "";
    app.pendingPath     = randomPath;
    app.rfidHint        = "Karte auflegen";
    setScreen(ScreenMode::RfidWrite, now);
    return;
  }

  const DirEntryInfo& entry = app.sdEntries[slot];
  const String full = joinPath(app.sdPath, entry.name);

  if (entry.isDir) {
    readDirectory(full);
    return;
  }

  if (app.sdPickMode == 2) {
    app.pendingDump = full;
    app.rfidHint    = "Karte auflegen";
    setScreen(ScreenMode::RfidRestore, now);
    return;
  }

  if (app.sdPickMode == 1) {
    const String cardText = pathToCardText(full);
    if (cardText.length() > kMaxTextLen) {
      setModal("PFAD ZU LANG", kColErr, now, 2200);
      return;
    }
    app.pendingPath = full;
    app.rfidHint    = "Karte auflegen";
    setScreen(ScreenMode::RfidWrite, now);
  } else {
    startFileOnC64(full, now);
  }
}

// Fuehrt die im Setup zugeordnete Aktion eines langen Druckes aus.
void runShortcut(ShortcutAction action, uint32_t now) {
  switch (action) {
    case ShortcutAction::Reset:    beep(2600, 50); performReset(now);      break;
    case ShortcutAction::Reboot:   beep(2400, 50); performHardReset(now);  break;
    case ShortcutAction::UltiMenu: beep(2200, 50); performMenuButton(now); break;
    case ShortcutAction::PowerOff:
      beep(1800, 60);
      if (app.settings.powerOffComboAsk) {
        app.comboPowerOff     = true;
        app.comboPowerOffAtMs = now;
        setModal("c64u OFF?  TASTE = JA", kColWarn, now, powerOffConfirmMs());
      } else {
        performPowerOff(now);
      }
      break;
    case ShortcutAction::JoySwap:  beep(2400, 50); toggleJoystickSwap(now);  break;
    case ShortcutAction::None:
      beep(900, 60);
      setModal("NICHT BELEGT", kColLabel, now, 900);
      break;
  }
}

// ===========================================================================
//  RFID-Polling
// ===========================================================================
// ---------------------------------------------------------------------------
// Kartenbefehl ausfuehren
//
// PowerOff mit Bestaetigung laeuft ueber cardPowerOffPending: die Karte wird
// weggenommen und innerhalb des Zeitfensters erneut aufgelegt. Ein Druck auf
// die Taste bestaetigt ebenfalls, ein Abbruch geschieht durch Abwarten.
// ---------------------------------------------------------------------------
void runCardCommand(const CardCommand& cmd, const String& uid, uint32_t now) {
  app.rfidHint = cardCommandLabel(cmd);

  switch (cmd.cmd) {
    case CardCmd::Reset:
      beep(2600, 50);
      performReset(now);
      return;

    case CardCmd::Reboot:
      beep(2400, 50);
      performHardReset(now);
      return;

    case CardCmd::UltiMenu:
      beep(2200, 50);
      performMenuButton(now);
      return;

    case CardCmd::PowerOff: {
      const uint8_t sec = cardPowerOffSeconds(cmd);

      // Zweites Auflegen derselben Karte innerhalb des Fensters bestaetigt.
      if (app.cardPowerOffPending && uid == app.cardPowerOffUid &&
          static_cast<int32_t>(now - app.cardPowerOffUntilMs) < 0) {
        app.cardPowerOffPending = false;
        beep(1800, 60);
        performPowerOff(now);
        return;
      }

      if (sec == 0) {                       // "CMD:POWEROFF=0" - ohne Nachfrage
        beep(1800, 60);
        performPowerOff(now);
        return;
      }

      app.cardPowerOffPending = true;
      app.cardPowerOffUid     = uid;
      app.cardPowerOffUntilMs = now + sec * 1000u;
      beep(900, 60);
      app.rfidHint = "Karte nochmal auflegen";
      setModal("c64u OFF? NOCHMAL!", kColWarn, now, sec * 1000u);
      return;
    }

    case CardCmd::DialOff:
      // Eine Karte, die beim Einschalten noch aufliegt, wuerde das Geraet
      // sofort wieder abschalten - deshalb in den ersten Sekunden ignorieren.
      if (millis() < kDialOffBootGuardMs) {
        beep(500, 120);
        setModal("KARTE ABNEHMEN", kColWarn, now, 1500);
        return;
      }
      rfidRelease();
      shutdownDevice();
      return;

    case CardCmd::JoySwap:
      beep(2400, 40);
      if (cmd.hasArg && !cmd.arg.isEmpty()) applyJoystickValue(cmd.arg, now);
      else                                  toggleJoystickSwap(now);
      return;

    case CardCmd::CpuSpeed: {
      // Die Stufenliste des c64u kennen wir vielleicht noch nicht.
      if (!app.cpuPathKnown) refreshCpuValue();
      const int index = cpuIndexFromValue(cmd.arg);
      beep(2400, 40);
      setCpuSpeed(index, now);
      return;
    }

    default:
      beep(500, 140);
      setModal("BEFEHL UNBEKANNT", kColErr, now, 2000);
      return;
  }
}

// Verarbeitet eine bereits ausgewaehlte Karte passend zum aktuellen Bildschirm.
void processCard(uint32_t now) {
  const CardKind kind = cardKind();
  if (kind == CardKind::None) {
    rfidRelease();
    app.rfidHint = "Kartentyp nicht unterstuetzt";
    beep(500, 120);
    return;
  }

  const String uid = cardUidString();

  if (app.screen == ScreenMode::RfidInfo) {
    // Einmal einlesen und stehen lassen; eine andere Karte loest neu aus.
    if (app.infoCount > 0 && uid == app.infoUid) {
      rfidRelease();
      return;
    }
    collectCardInfo(kind);
    buildInfoView();
    app.infoUid = uid;
    rfidRelease();
    beep(2400, 40);
    return;
  }

  if (app.screen == ScreenMode::RfidDump) {
    String error, file;
    const bool ok = dumpCardToSd(kind, &file, &error);
    rfidRelease();
    beep(ok ? 2800 : 500, ok ? 60 : 160);
    app.rfidHint = ok ? ("gesichert: " + baseName(file)) : error;
    setModal(ok ? "AUF SD GESICHERT" : "SICHERN FEHLER", ok ? kColOk : kColErr, now, 2200);
    return;
  }

  if (app.screen == ScreenMode::RfidRestore) {
    String error;
    const bool ok = restoreDumpToCard(kind, app.pendingDump, &error);
    rfidRelease();
    beep(ok ? 2800 : 500, ok ? 60 : 160);
    app.rfidHint = ok ? (String(cardKindLabel(kind)) + "  " + uid) : error;
    setModal(ok ? "KARTE BESCHRIEBEN" : "SCHREIBFEHLER", ok ? kColOk : kColErr, now, 2200);
    return;
  }

  if (app.screen == ScreenMode::RfidWrite) {
    String error;
    // Befehlskarten bringen ihren Text fertig mit, sonst wird der Dateipfad
    // in das Kartenformat uebersetzt.
    const String text = app.pendingCardText.isEmpty() ? pathToCardText(app.pendingPath)
                                                      : app.pendingCardText;
    const bool ok = writeCardText(kind, text, &error);
    rfidRelease();
    beep(ok ? 2800 : 500, ok ? 60 : 160);
    app.rfidHint = ok ? (String(cardKindLabel(kind)) + "  " + uid) : error;
    setModal(ok ? "KARTE OK" : "SCHREIBFEHLER", ok ? kColOk : kColErr, now, ok ? 1800 : 2200);
    return;
  }

  // ---- Lesen und starten ----
  const CardContent content = readCardContent(kind);
  rfidRelease();

  if (!content.ok) {
    beep(500, 140);
    app.rfidHint = content.error;
    app.wifiHint = content.error;
    setModal("KARTE LEER?", kColWarn, now, 2000);
    return;
  }

  // ---- WLAN-Passwortkarte auf der Einrichtungsseite ----
  if (app.screen == ScreenMode::WifiCard) {
    String ssid = app.wifiPendingSsid;
    String pass = trimCopy(content.text);
    // Karten im WLAN-Schema bringen die SSID selbst mit. Alles andere gilt
    // als reines Passwort fuer das vorher gewaehlte Netz.
    parseWifiText(content.text, &ssid, &pass);

    if (ssid.isEmpty()) {
      beep(500, 140);
      app.wifiHint = "Karte ohne SSID";
      setModal("KEIN NETZ", kColErr, now, 2000);
      return;
    }
    // Programm- oder Befehlskarten sind hier mit Sicherheit ein Versehen.
    CardCommand strayCommand;
    if (!textLooksLikeWifi(content.text) &&
        (pass.startsWith("/") || parseCardCommand(content.text, &strayCommand))) {
      beep(500, 140);
      app.wifiHint = "das ist keine WLAN-Karte";
      setModal("FALSCHE KARTE", kColErr, now, 2200);
      return;
    }
    if (!wifiAddProfile(ssid, pass)) {
      beep(500, 140);
      app.wifiHint = "SSID oder Passwort zu lang";
      setModal("KARTE UNGUELTIG", kColErr, now, 2200);
      return;
    }

    beep(2800, 60);
    app.wifiPendingSsid   = "";
    app.wifiHint          = ssid;
    app.lastWiFiAttemptMs = 0;
    gWifiTry              = 0;
    beginWiFi(now);
    setModal("WLAN GESPEICHERT", kColOk, now, 2000);
    setScreen(ScreenMode::WifiMenu, now);
    return;
  }

  // ---- WLAN-Karte ausserhalb der Einrichtung ----
  //
  // Die Karte ist kein Dateipfad. Statt nur darauf hinzuweisen, wird das Netz
  // gleich uebernommen und eine Verbindung aufgebaut: Karte auflegen genuegt,
  // der Umweg ueber Settings > WLAN entfaellt.
  if (textLooksLikeWifi(content.text)) {
    String ssid;
    String pass;
    if (!parseWifiText(content.text, &ssid, &pass) || ssid.isEmpty()) {
      beep(500, 140);
      app.rfidHint = "WLAN-Karte ohne SSID";
      setModal("KEIN NETZ", kColErr, now, 2200);
      return;
    }

    // Laeuft die Verbindung bereits, ist nichts zu tun. Das faengt auch den
    // Fall ab, dass die Karte liegen bleibt und die Hintergrundabfrage sie
    // immer wieder erkennt.
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
      beep(2400, 40);
      app.rfidHint = ssid;
      setModal("SCHON VERBUNDEN", kColOk, now, 1800);
      return;
    }

    if (!wifiAddProfile(ssid, pass)) {
      beep(500, 140);
      app.rfidHint = "SSID oder Passwort zu lang";
      setModal("KARTE UNGUELTIG", kColErr, now, 2200);
      return;
    }

    // wifiAddProfile sortiert das Netz nach vorne; von dort aus wird gezielt
    // dieses eine Netz versucht und nicht die ganze Liste durchgegangen.
    const int index = wifiProfileIndex(ssid);
    gWifiTry              = index >= 0 ? static_cast<size_t>(index) : 0;
    app.lastWiFiAttemptMs = 0;
    beep(2600, 50);

    app.rfidHint = ssid;
    setModal("VERBINDE...", kColInfo, now, kWifiCardConnectMs + 1500);
    render(now);

    if (app.portalActive) stopPortal(now);    // beendet den AP und verbindet selbst
    else                  beginWiFi(now);

    // Kurz auf das Ergebnis warten, damit die Rueckmeldung etwas taugt. Laenger
    // als kWifiCardConnectMs wird nicht gewartet - der Rest laeuft ueber die
    // regelmaessigen Versuche in serviceWiFi weiter.
    const uint32_t deadline = millis() + kWifiCardConnectMs;
    while (millis() < deadline && WiFi.status() != WL_CONNECTED) delay(100);

    const bool connected = WiFi.status() == WL_CONNECTED;
    app.modalText = "";
    beep(connected ? 2800 : 500, connected ? 60 : 140);
    app.rfidHint = connected ? (ssid + "  " + WiFi.localIP().toString())
                             : (ssid + " nicht erreichbar");
    setModal(connected ? "WLAN AKTIV" : "NETZ NICHT DA",
             connected ? kColOk : kColWarn, millis(), 2400);
    return;
  }

  Serial.printf("Karte gelesen (%s): '%s'\n",
                content.isNdef ? "NDEF-Text" : "Altformat", content.text.c_str());

  // ---- Befehlskarte? Dann ist weder SD noch Datei noetig ----
  CardCommand command;
  if (parseCardCommand(content.text, &command)) {
    runCardCommand(command, uid, now);
    return;
  }
  {
    // Karte traegt zwar das Praefix, aber kein bekanntes Schluesselwort -
    // dann ist es sicher kein Dateipfad.
    String head = content.text.substring(0, 4);
    head.toUpperCase();
    if (head == kCardCmdPrefix) {
      beep(500, 140);
      app.rfidHint = content.text;
      setModal("BEFEHL UNBEKANNT", kColErr, now, 2200);
      return;
    }
  }
  if (app.cardPowerOffPending) app.cardPowerOffPending = false;   // andere Karte bricht ab

  String path = cardTextToPath(content.text);

  if (pathIsRandom(path) || pathIsDirectory(path)) {
    const String picked = resolveRandomFile(path);
    if (picked.isEmpty()) {
      beep(500, 140);
      app.rfidHint = "Verzeichnis leer: " + path;
      setModal("NICHTS GEFUNDEN", kColWarn, now, 2200);
      return;
    }
    path = picked;
  }

  app.lastCardPath = path;
  app.rfidHint     = path;
  beep(2600, 50);
  startFileOnC64(path, millis());
}

// ---------------------------------------------------------------------------
// RFID-Ablauf
//
// Zwei Betriebsarten:
//
//   1. Auf den RFID-Seiten wird alle 250 ms voll abgefragt - dort wartet der
//      Benutzer ohnehin auf die Karte.
//   2. Auf dem Startbild und im Ring-Menue laeuft je nach Einstellung
//      "Auto-NFC" eine schnelle Probe im Hintergrund. Wird dabei eine Karte
//      erkannt, wechselt die Anzeige selbsttaetig in den Lesemodus und das
//      hinterlegte Programm startet - man muss also nicht erst den Menuepunkt
//      RFID/NFC aufrufen.
//
// Die Probe kostet dank des verkuerzten Zeitfensters (siehe cardPresentQuick)
// nur wenige Millisekunden. Bei 0,7 s Abstand liegt die Grundlast damit im
// niedrigen einstelligen Promillebereich, ein Ruckeln ist nicht sichtbar.
// ---------------------------------------------------------------------------
bool onRfidScreen() {
  return app.screen == ScreenMode::RfidRun   || app.screen == ScreenMode::RfidWrite ||
         app.screen == ScreenMode::RfidInfo  || app.screen == ScreenMode::RfidDump  ||
         app.screen == ScreenMode::RfidRestore || app.screen == ScreenMode::WifiCard;
}

void serviceRfid(uint32_t now) {
  if (!app.rfidReady) return;

  // ---- 1. Reguläre Abfrage auf den RFID-Seiten ----
  if (onRfidScreen()) {
    if (now - app.lastRfidPollMs < kRfidPollMs) return;
    app.lastRfidPollMs = now;
    if (!cardPresent()) return;
    processCard(now);
    return;
  }

  // ---- 2. Hintergrundabfrage ----
  if (app.settings.autoNfc == AutoNfcMode::Off) return;
  if (app.screen != ScreenMode::Home && app.screen != ScreenMode::Menu) return;
  if (now - app.lastRfidPollMs < autoNfcIntervalMs(app.settings.autoNfc)) return;
  app.lastRfidPollMs = now;

  if (!cardPresentQuick()) return;

  // Karte liegt auf: in den Lesemodus wechseln, Bild sofort zeigen und
  // dieselbe Verarbeitung wie auf der RFID-Seite anstossen.
  beep(2200, 30);
  app.rfidHint       = "Karte erkannt";
  app.autoRfidActive = true;          // danach wieder zum Startbild
  setScreen(ScreenMode::RfidRun, now);
  render(millis());
  processCard(millis());

  // Nach dem Start bleibt die Leseseite noch fuer die Dauer des Home-Timeouts
  // stehen - so laesst sich gleich die naechste Karte auflegen.
  noteInteraction(millis());
}

// ===========================================================================
//  Eingabe: Drehgeber, Taste, Touch
// ===========================================================================
void moveSelection(int delta, uint32_t now) {
  switch (app.screen) {
    case ScreenMode::Menu: {
      const int count = static_cast<int>(kMenuCount);
      app.menuIndex = ((app.menuIndex + delta) % count + count) % count;
      clearPendingPowerOff();
      showMenuLabel(kMenuLabels[app.menuIndex], now);
      break;
    }
    case ScreenMode::CpuMenu: {
      const int count = std::max(1, static_cast<int>(app.cpuChoiceCount));
      app.cpuIndex = ((app.cpuIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::Settings: {
      const int count = static_cast<int>(kSettingsCount);
      app.settingsIndex = ((app.settingsIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::SdBrowser: {
      const int count = sdBrowserCount();
      if (count > 0) app.sdIndex = ((app.sdIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::CmdPick: {
      const int count = static_cast<int>(cmdListCount());
      if (count > 0) app.cmdIndex = ((app.cmdIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::WifiMenu: {
      const int count = static_cast<int>(kWifiMenuCount);
      app.wifiMenuIndex = ((app.wifiMenuIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::WifiScan: {
      const int count = static_cast<int>(app.wifiScanCount);
      if (count > 0) app.wifiScanIndex = ((app.wifiScanIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::WifiSaved: {
      const int count = static_cast<int>(gWifiCount);
      if (count > 0) app.wifiSavedIndex = ((app.wifiSavedIndex + delta) % count + count) % count;
      break;
    }
    case ScreenMode::RfidInfo:
      app.infoScroll = std::max(0, std::min(app.infoScroll + delta, infoMaxScroll()));
      break;
    default:
      break;
  }
}

void handleBack(uint32_t now) {
  clearPendingPowerOff();
  switch (app.screen) {
    case ScreenMode::Home:
      break;
    case ScreenMode::Menu:
      setScreen(ScreenMode::Home, now);
      break;
    case ScreenMode::SdBrowser:
      if (app.sdPath != "/") readDirectory(parentPath(app.sdPath));
      else                   setScreen(ScreenMode::Menu, now);
      break;
    case ScreenMode::CmdPick:
    case ScreenMode::WifiMenu:
      setScreen(ScreenMode::Settings, now);
      break;
    case ScreenMode::WifiScan:
    case ScreenMode::WifiSaved:
    case ScreenMode::WifiCard:
      app.wifiPendingSsid = "";
      app.wifiSavedToCard = false;
      setScreen(ScreenMode::WifiMenu, now);
      break;
    case ScreenMode::WifiPortal:
      stopPortal(now);
      setScreen(ScreenMode::WifiMenu, now);
      break;
    default:
      setScreen(ScreenMode::Menu, now);
      break;
  }
}

void handleSelect(uint32_t now) {
  // Laufende PowerOff-Rueckfrage zuerst beantworten
  if (app.comboPowerOff) {
    app.comboPowerOff = false;
    if (now - app.comboPowerOffAtMs <= powerOffConfirmMs()) {
      beep(1800, 60);
      performPowerOff(now);
      return;
    }
  }

  switch (app.screen) {
    case ScreenMode::Home:
      setScreen(ScreenMode::Menu, now);
      break;
    case ScreenMode::Menu:
      handleMenuSelect(now);
      break;
    case ScreenMode::CpuMenu:
      setCpuSpeed(app.cpuIndex, now);
      break;
    case ScreenMode::Settings:
      activateSetting(now);
      break;
    case ScreenMode::SdBrowser:
      sdBrowserSelect(now);
      break;
    case ScreenMode::CmdPick: {
      const int count = static_cast<int>(cmdListCount());
      if (count <= 0) break;
      const int index = std::max(0, std::min(app.cmdIndex, count - 1));
      const CardCommand c = cmdListAt(static_cast<size_t>(index));
      app.pendingCardText = cardCommandText(c);
      app.pendingPath     = "";
      app.rfidHint        = "Karte auflegen";
      setScreen(ScreenMode::RfidWrite, now);
      break;
    }
    case ScreenMode::Status:
      runConnectionTest(now);
      break;
    case ScreenMode::RfidRun:
      // Laeuft eine PowerOff-Rueckfrage von der Karte, bestaetigt die Taste sie.
      if (app.cardPowerOffPending &&
          static_cast<int32_t>(now - app.cardPowerOffUntilMs) < 0) {
        app.cardPowerOffPending = false;
        beep(1800, 60);
        performPowerOff(now);
        break;
      }
      setScreen(ScreenMode::Menu, now);
      break;
    case ScreenMode::RfidWrite:
    case ScreenMode::RfidDump:
    case ScreenMode::RfidRestore:
    case ScreenMode::RfidInfo:
      setScreen(ScreenMode::Menu, now);
      break;

    case ScreenMode::WifiMenu:
      wifiMenuSelect(now);
      break;
    case ScreenMode::WifiScan:
      if (app.wifiScanCount == 0) setScreen(ScreenMode::WifiMenu, now);
      else                        wifiChooseNetwork(app.wifiScanIndex, now);
      break;
    case ScreenMode::WifiSaved:
      wifiSavedSelect(now);
      break;
    case ScreenMode::WifiCard:
      app.wifiPendingSsid = "";
      setScreen(ScreenMode::WifiMenu, now);
      break;
    case ScreenMode::WifiPortal:
      stopPortal(now);
      setScreen(ScreenMode::WifiMenu, now);
      break;

    case ScreenMode::Busy:
      break;
  }
}

void handleEncoder(uint32_t now) {
  const long raw = M5Dial.Encoder.read();
  const int rawDelta = static_cast<int>(raw - app.encoderBase);
  if (rawDelta == 0) return;

  app.encoderBase = raw;
  noteInteraction(now);

  // Der M5Dial liefert sehr feine Tickfolgen - erst nach der eingestellten
  // Anzahl Ticks gibt es einen echten Menueschritt.
  app.encoderResidual += rawDelta;
  const int stepTicks = encoderStepsValue(app.settings.encoderSteps);

  int delta = 0;
  while (app.encoderResidual >= stepTicks)  { delta += 1; app.encoderResidual -= stepTicks; }
  while (app.encoderResidual <= -stepTicks) { delta -= 1; app.encoderResidual += stepTicks; }
  if (delta == 0) return;

  if (app.screen == ScreenMode::Home) {
    setScreen(ScreenMode::Menu, now);
    return;                       // erste Drehung oeffnet nur das Menue
  }
  beep(1700, 12);
  moveSelection(delta, now);
}

void handleButton(uint32_t now) {
  if (app.screen == ScreenMode::Busy) return;

  if (M5Dial.BtnA.wasPressed()) {
    app.buttonHandled = false;
    noteInteraction(now);
  }

  // Im Ring-Menue auf "c64u Power Off": langer Druck (1,5 s) schaltet den
  // M5Dial selbst aus. Kuerzer losgelassen bleibt es der normale Menuepunkt.
  if (app.screen == ScreenMode::Menu && app.menuIndex == kMenuPowerOff) {
    if (!app.buttonHandled && M5Dial.BtnA.pressedFor(kDialOffHoldMs)) {
      app.buttonHandled = true;
      clearPendingPowerOff();
      shutdownDevice();
    }
    if (M5Dial.BtnA.wasReleased() && !app.buttonHandled) {
      noteInteraction(now);
      beep(2000, 20);
      handleSelect(now);
    }
    return;
  }

  if (!app.buttonHandled && M5Dial.BtnA.pressedFor(kLongPressMs)) {
    app.buttonHandled = true;
    noteInteraction(now);
    beep(1400, 30);
    if (app.screen == ScreenMode::Home) runShortcut(app.settings.shortcutButton, now);
    else                                handleBack(now);
    return;
  }

  if (M5Dial.BtnA.wasReleased() && !app.buttonHandled) {
    noteInteraction(now);
    beep(2000, 20);
    handleSelect(now);
  }
}

// Kurzer Tipper auf das Display
void handleTouchTap(int tx, int ty, uint32_t now) {
  const int dx = tx - kCx;
  const int dy = ty - kCy;

  switch (app.screen) {
    case ScreenMode::Home:
      setScreen(ScreenMode::Menu, now);
      return;

    case ScreenMode::Menu: {
      // Mitte (das Logo) fuehrt zurueck zum Home-Screen.
      if (dx * dx + dy * dy <= kMenuCenterRadius * kMenuCenterRadius) {
        setScreen(ScreenMode::Home, now);
        return;
      }
      const int index = menuIndexFromTouch(tx, ty);
      if (index < 0) return;
      app.menuIndex = index;
      showMenuLabel(kMenuLabels[app.menuIndex], now);
      beep(2000, 20);
      handleMenuSelect(now);
      return;
    }

    case ScreenMode::CpuMenu:
    case ScreenMode::Settings:
    case ScreenMode::SdBrowser:
    case ScreenMode::CmdPick:
    case ScreenMode::WifiMenu:
    case ScreenMode::WifiScan:
    case ScreenMode::WifiSaved: {
      if (ty < kListY0 - 4) { handleBack(now); return; }   // Titelzeile = zurueck
      const int row = listRowFromTouch(ty);
      if (row < 0) return;

      int count = 0;
      int* target = nullptr;
      if (app.screen == ScreenMode::CpuMenu) {
        count = static_cast<int>(app.cpuChoiceCount);
        target = &app.cpuIndex;
      } else if (app.screen == ScreenMode::Settings) {
        count = static_cast<int>(kSettingsCount);
        target = &app.settingsIndex;
      } else if (app.screen == ScreenMode::CmdPick) {
        count = static_cast<int>(cmdListCount());
        target = &app.cmdIndex;
      } else if (app.screen == ScreenMode::WifiMenu) {
        count = static_cast<int>(kWifiMenuCount);
        target = &app.wifiMenuIndex;
      } else if (app.screen == ScreenMode::WifiScan) {
        count = static_cast<int>(app.wifiScanCount);
        target = &app.wifiScanIndex;
      } else if (app.screen == ScreenMode::WifiSaved) {
        count = static_cast<int>(gWifiCount);
        target = &app.wifiSavedIndex;
      } else {
        count = sdBrowserCount();
        target = &app.sdIndex;
      }
      if (count <= 0) return;

      const int selected = std::max(0, std::min(*target, count - 1));
      const int start    = listWindowStart(selected, count);
      const int index    = start + row;
      if (index >= count) return;

      beep(2000, 20);
      if (index == selected) {
        handleSelect(now);          // zweiter Tipper auf dieselbe Zeile fuehrt sie aus
      } else {
        *target = index;
      }
      return;
    }

    case ScreenMode::RfidInfo:
      if (ty < 40)       { handleBack(now); return; }
      if (ty < kCy)      { moveSelection(-3, now); return; }
      moveSelection(+3, now);
      return;

    case ScreenMode::Status:
    case ScreenMode::RfidRun:
    case ScreenMode::RfidWrite:
    case ScreenMode::RfidDump:
    case ScreenMode::RfidRestore:
    case ScreenMode::WifiCard:
      handleBack(now);
      return;

    case ScreenMode::WifiPortal:
      // Kein Abbruch per Tipper: ein versehentlicher Kontakt (Anfassen,
      // Ablegen) haette sonst den Accesspoint mitten in der Eingabe beendet.
      // Beendet wird nur mit dem Knopf.
      app.portalTouchedMs = now;
      return;

    case ScreenMode::Busy:
      return;
  }
}

void handleTouch(uint32_t now) {
  if (app.screen == ScreenMode::Busy) return;

  auto touch = M5Dial.Touch.getDetail();

  if (touch.isPressed()) {
    if (!app.touchLatch) {
      app.touchLatch    = true;
      app.touchLongDone = false;
      app.touchStartMs  = now;
      app.touchStartX   = touch.x;
      app.touchStartY   = touch.y;
      noteInteraction(now);
    } else if (!app.touchLongDone && app.screen == ScreenMode::Menu &&
               menuIndexFromTouch(app.touchStartX, app.touchStartY) == kMenuPowerOff) {
      // Langes Beruehren von "c64u Power Off" schaltet den M5Dial aus.
      if (now - app.touchStartMs >= kDialOffHoldMs) {
        app.touchLongDone = true;
        clearPendingPowerOff();
        shutdownDevice();
      }
    } else if (!app.touchLongDone &&
               (app.screen == ScreenMode::Home || app.screen == ScreenMode::Menu) &&
               now - app.touchStartMs >= kLongPressMs) {
      app.touchLongDone = true;
      beep(1400, 30);
      runShortcut(app.settings.shortcutTouch, now);
    }
    return;
  }

  if (!app.touchLatch) return;
  app.touchLatch = false;
  if (app.touchLongDone) return;
  if (app.touchStartX < 0 || app.touchStartY < 0) return;

  noteInteraction(now);
  handleTouchTap(app.touchStartX, app.touchStartY, now);
}

// ===========================================================================
//  Hardware-Erkennung
// ===========================================================================
bool initRfid() {
  // Der WS1850S sitzt fest am internen I2C-Bus. Ist er ansprechbar, liefert
  // das Versionsregister einen Wert ungleich 0x00 / 0xFF.
  const uint8_t version = RFID.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("RFID (I2C 0x%02X) VersionReg = 0x%02X\n", kRfidAddr, version);
  if (version == 0x00 || version == 0xFF) return false;

  // Das vom Treiber gesetzte Zeitfenster merken, damit die schnelle Probe es
  // hinterher exakt wiederherstellen kann.
  const uint16_t reload =
      static_cast<uint16_t>(RFID.PCD_ReadRegister(MFRC522::TReloadRegH) << 8) |
      RFID.PCD_ReadRegister(MFRC522::TReloadRegL);
  if (reload > kRfidProbeReload) gRfidTimerReload = reload;
  Serial.printf("RFID Zeitfenster = %u x 25 us\n", static_cast<unsigned>(gRfidTimerReload));
  return true;
}

}  // namespace

// ===========================================================================
//  setup()
// ===========================================================================
void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.internal_spk  = true;
  cfg.internal_mic  = false;

  // true / true = Drehgeber UND eingebauter RFID-Leser einschalten
  M5Dial.begin(cfg, true, true);

  Serial.begin(115200);

  M5Dial.Display.setRotation(0);
  M5Dial.Display.setSwapBytes(true);
  M5Dial.Display.setTextWrap(false);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Speaker.setVolume(64);

  // Offscreen-Puffer anlegen. Ohne PSRAM kann das fehlschlagen - dann wird
  // direkt ins Display gezeichnet.
  canvas.setColorDepth(16);
  gUseCanvas = (canvas.createSprite(kScrW, kScrH) != nullptr);
  if (gUseCanvas) {
    canvas.setSwapBytes(true);
    canvas.setTextWrap(false);
    gDraw = &canvas;
  } else {
    gDraw = &M5Dial.Display;
    Serial.println("Kein Speicher fuer den Offscreen-Puffer - zeichne direkt.");
  }

  randomSeed(micros());   // fuer die Zufallsauswahl per "?" auf der Karte
  setFallbackCpuChoices();
  loadSettings();
  applyBrightness();

  app.rfidReady = initRfid();
  app.sdReady   = initSd();

  // Netzkonfiguration aus dem NVS; build_env.h liefert nur die Startwerte.
  loadNetConfig();

  // Steht noch nichts im NVS, darf /wifi.txt von der SD-Karte einspringen.
  if (gWifiCount == 0 && app.sdReady) {
    String error;
    const size_t added = loadWifiFromSd(&error);
    if (added > 0) Serial.printf("wifi.txt: %u Netz(e) uebernommen\n", static_cast<unsigned>(added));
    else           Serial.printf("wifi.txt: %s\n", error.c_str());
  }

  app.configReady = configReady();
  app.encoderBase = M5Dial.Encoder.read();
  app.encoderResidual = 0;
  app.lastInteractionMs = millis();

  resetHomeAnimation(millis());
  render(millis());

  // Sind mehrere Netze gespeichert, einmal suchen und mit dem staerksten
  // bekannten Netz starten.
  wifiPickBestProfile();
  beginWiFi(millis());
  refreshConnectionStatus(millis(), true);

  if (!hasWiFiConfig()) {
    setModal("SETTINGS > WLAN", kColWarn, millis(), 2600);
  } else if (!hasTargetConfig()) {
    setModal("c64u-ADRESSE FEHLT", kColWarn, millis(), 2600);
  } else if (!app.rfidReady) {
    setModal("KEIN NFC-LESER", kColWarn, millis(), 1800);
  } else if (!app.sdReady) {
    setModal("KEINE SD-KARTE", kColWarn, millis(), 1800);
  }

  Serial.printf("C64uRemote M5Dial  RFID:%d  SD:%d  Canvas:%d  Heap:%u\n",
                app.rfidReady ? 1 : 0, app.sdReady ? 1 : 0, gUseCanvas ? 1 : 0,
                static_cast<unsigned>(ESP.getFreeHeap()));
}

// ===========================================================================
//  loop()
// ===========================================================================
void loop() {
  static uint32_t nextFrameMs = 0;

  M5Dial.update();
  const uint32_t now = millis();

  servicePortal(now);
  serviceWiFi(now);
  refreshConnectionStatus(now);

  // PowerOff-Bestaetigungen verfallen nach dem Zeitfenster
  if (app.pendingPowerOff && (now - app.pendingPowerOffAtMs > powerOffConfirmMs())) {
    app.pendingPowerOff = false;
  }
  if (app.comboPowerOff && (now - app.comboPowerOffAtMs > powerOffConfirmMs())) {
    app.comboPowerOff = false;
  }
  if (app.pendingShutdown && (now - app.pendingShutdownAtMs > kShutdownAskMs)) {
    app.pendingShutdown = false;
  }
  if (app.cardPowerOffPending &&
      static_cast<int32_t>(now - app.cardPowerOffUntilMs) >= 0) {
    app.cardPowerOffPending = false;
    app.rfidHint = "Abfrage abgelaufen";
  }

  handleEncoder(now);
  handleButton(now);
  handleTouch(now);
  serviceRfid(now);

  if (app.screen == ScreenMode::Home) {
    updateHomeDemo(now);
  } else {
    // Der Bildschirmschoner darf uebernehmen, wenn der Benutzer nicht gerade
    // auf eine Karte wartet. Eine Leseseite, die die Hintergrundabfrage selbst
    // geoeffnet hat, zaehlt nicht als Warten und faellt wieder zurueck.
    // Die Portalseite bleibt stehen, solange der Accesspoint laeuft - sonst
    // waeren SSID und Passwort nicht mehr ablesbar.
    const bool waitingForCard = (onRfidScreen() && !app.autoRfidActive) ||
                                app.screen == ScreenMode::WifiPortal;
    if (app.screen != ScreenMode::Busy && !waitingForCard) {
      uint32_t timeoutMs = homeTimeoutMs(app.settings.homeTimeout);
      // Eine automatisch geoeffnete Leseseite verschwindet auch dann wieder,
      // wenn der Bildschirmschoner sonst abgeschaltet ist.
      if (app.autoRfidActive && timeoutMs == 0) timeoutMs = kAutoRfidHoldMs;
      if (timeoutMs > 0 && (now - app.lastInteractionMs >= timeoutMs)) {
        setScreen(ScreenMode::Home, now);
      }
    }
  }

  // CPU-Wert einmalig nachladen, sobald das WLAN steht
  if (WiFi.status() == WL_CONNECTED && app.currentCpuValue == "Unknown" &&
      app.screen == ScreenMode::Home) {
    refreshCpuValue();
  }

  if (nextFrameMs == 0 || static_cast<int32_t>(now - nextFrameMs) >= 0) {
    render(now);
    nextFrameMs = millis() + kFrameMs;
  }

  delay(2);
}
