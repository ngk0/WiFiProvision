/*!
 * @file WiFiProvision.cpp
 *
 * MIT licensed. Copyright (c) 2026 Nate Kocher.
 */

#include "WiFiProvision.h"
#include <WiFi101.h>
#include <string.h>

#define WP_BG 0x0000
#define WP_ACCENT 0x679F
#define WP_DIM 0x8410
#define WP_LINE 0x2124
#define WP_KEY 0x31A6
#define WP_OK 0x2648
#define WP_WARN 0x5900
#define WP_BAD 0xF9E7
#define WP_GOOD 0x4FEB
#define WP_WHITE 0xFFFF

// Four rows of ten. Three layers, because a WPA password with a symbol in it
// is common and a keyboard that cannot type one is decoration.
static const char *WP_LAYERS[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "!@#$%^&*()", "-_=+[]{}|\\", ";:'\",.<>/?"},
};

static const char *encName(uint8_t t) {
  switch (t) {
    case ENC_TYPE_WEP:  return "WEP";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_NONE: return "open";
    default:            return "?";
  }
}

void WiFiProvision::begin(Adafruit_GFX *gfx) {
  _gfx = gfx;
  layout();
}

void WiFiProvision::layout() {
  if (!_gfx) return;
  _w = _gfx->width();
  _h = _gfx->height();

  // Derived from the screen rather than hardcoded, so this is not silently
  // wrong on a panel that is not 320x240.
  _keyW = _w / 10;
  _funcH = _h / 9;
  _keyH = (_h / 8);
  _kbTop = _h - (4 * _keyH + _funcH + 4);
  _funcTop = _kbTop + 4 * _keyH + 2;

  _rowH = _h / 11;
  _listTop = _h / 7;
}

const char *WiFiProvision::rowChars(uint8_t row) const {
  return WP_LAYERS[_layer][row];
}

// ------------------------------------------------------------- scanning ----

bool WiFiProvision::addNetwork(const char *ssid, int32_t rssi, uint8_t enc) {
  if (_count >= WP_MAX_NETWORKS || !ssid || !*ssid) return false;
  WPNetwork &n = _nets[_count++];
  strncpy(n.ssid, ssid, WP_SSID_LEN - 1);
  n.ssid[WP_SSID_LEN - 1] = 0;
  n.rssi = rssi;
  n.enc = enc;
  return true;
}

uint8_t WiFiProvision::scan() {
  if (_gfx) drawStatus("Scanning", "", WP_ACCENT);
  const int found = WiFi.scanNetworks();
  _count = 0;
  for (int i = 0; i < found && _count < WP_MAX_NETWORKS; i++)
    addNetwork(WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i));
  return _count;
}

void WiFiProvision::start() {
  scan();
  _state = WP_PICK;
  drawPick();
}

bool WiFiProvision::select(uint8_t index) {
  if (index >= _count) return false;
  _selected = index;
  _passLen = 0;
  _pass[0] = 0;
  _layer = 0;
  _state = WP_KEYBOARD;
  drawKeyboard();
  return true;
}

void WiFiProvision::setPassword(const char *pass) {
  strncpy(_pass, pass ? pass : "", WP_PASS_LEN - 1);
  _pass[WP_PASS_LEN - 1] = 0;
  _passLen = strlen(_pass);
}

// ------------------------------------------------------------ credentials --

uint16_t WiFiProvision::crcOf(const WPCreds &c) {
  // Over the payload only. Stopping at the last member rather than running to
  // sizeof means trailing padding cannot make a good record fail to verify.
  const uint8_t *p = (const uint8_t *)c.ssid;
  const uint8_t *end = (const uint8_t *)c.pass + sizeof(c.pass);
  uint16_t v = 0xFFFF;
  while (p < end) {
    v ^= (uint16_t)(*p++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      v = (v & 0x8000) ? (uint16_t)((v << 1) ^ 0x1021) : (uint16_t)(v << 1);
  }
  return v;
}

WPCreds WiFiProvision::makeCreds(const char *ssid, const char *pass) {
  WPCreds c = {};
  c.magic = WP_CREDS_MAGIC;
  c.version = WP_CREDS_VERSION;
  strncpy(c.ssid, ssid ? ssid : "", WP_SSID_LEN - 1);
  strncpy(c.pass, pass ? pass : "", WP_PASS_LEN - 1);
  c.crc = crcOf(c);
  return c;
}

bool WiFiProvision::restore(const WPCreds &c) {
  if (c.magic != WP_CREDS_MAGIC || c.version != WP_CREDS_VERSION) return false;
  if (crcOf(c) != c.crc) return false;
  if (!c.ssid[0]) return false;
  _creds = c;
  return true;
}

void WiFiProvision::forget() {
  memset(&_creds, 0, sizeof(_creds));
  WiFi.disconnect();
  _state = WP_IDLE;
}

// -------------------------------------------------------------- connecting --

bool WiFiProvision::connect() {
  if (_selected >= _count) return false;

  _state = WP_CONNECTING;
  if (_gfx) drawStatus("Connecting to", _nets[_selected].ssid, WP_ACCENT);

  WiFi.begin(_nets[_selected].ssid, _pass);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < _timeout) delay(250);

  if (WiFi.status() != WL_CONNECTED) {
    _state = WP_FAILED;
    if (_gfx)
      drawStatus("Failed", "Wrong password, or the radio is 2.4GHz only", WP_BAD);
    return false;
  }

  _creds = makeCreds(_nets[_selected].ssid, _pass);
  _state = WP_CONNECTED;
  _justConnected = true;
  if (_gfx) drawStatus("Connected", _creds.ssid, WP_GOOD);
  return true;
}

bool WiFiProvision::connectStored() {
  if (_creds.magic != WP_CREDS_MAGIC || !_creds.ssid[0]) return false;

  _state = WP_CONNECTING;
  if (_gfx) drawStatus("Connecting to", _creds.ssid, WP_ACCENT);

  WiFi.begin(_creds.ssid, _creds.pass);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < _timeout) delay(250);

  if (WiFi.status() != WL_CONNECTED) {
    _state = WP_FAILED;
    return false;
  }
  _state = WP_CONNECTED;
  return true;
}

bool WiFiProvision::justConnected() {
  const bool v = _justConnected;
  _justConnected = false;
  return v;
}

// ------------------------------------------------------------------ drawing --

void WiFiProvision::drawStatus(const char *l1, const char *l2, uint16_t color) {
  if (!_gfx) return;
  _gfx->fillScreen(WP_BG);
  _gfx->setTextSize(2);
  _gfx->setTextColor(color);
  _gfx->setCursor(10, _h / 2 - 24);
  _gfx->print(l1);
  if (l2 && *l2) {
    _gfx->setTextSize(1);
    _gfx->setTextColor(WP_WHITE);
    _gfx->setCursor(10, _h / 2 + 4);
    _gfx->print(l2);
  }
}

void WiFiProvision::drawPick() {
  if (!_gfx) return;
  _gfx->fillScreen(WP_BG);
  _gfx->setTextSize(2);
  _gfx->setTextColor(WP_ACCENT);
  _gfx->setCursor(6, 6);
  _gfx->print(F("Select network"));

  _gfx->setTextSize(1);
  const uint8_t rows = (_h - _listTop - 30) / _rowH;
  for (uint8_t i = 0; i < _count && i < rows; i++) {
    const int16_t y = _listTop + i * _rowH;
    _gfx->drawRect(4, y, _w - 8, _rowH - 3, WP_LINE);
    _gfx->setTextColor(WP_WHITE);
    _gfx->setCursor(10, y + 6);
    _gfx->print(_nets[i].ssid);
    _gfx->setTextColor(WP_DIM);
    _gfx->setCursor(_w - 96, y + 6);
    _gfx->print(_nets[i].rssi);
    _gfx->print(F(" dBm "));
    _gfx->print(encName(_nets[i].enc));
  }

  if (!_count) {
    _gfx->setTextColor(WP_DIM);
    _gfx->setCursor(10, _listTop + 10);
    _gfx->print(F("No networks found. The radio is 2.4GHz only."));
  }

  _gfx->fillRect(4, _h - 26, 90, 22, WP_OK);
  _gfx->setTextColor(WP_WHITE);
  _gfx->setCursor(24, _h - 19);
  _gfx->print(F("Rescan"));
}

void WiFiProvision::drawPasswordField() {
  if (!_gfx) return;
  const int16_t y = _kbTop - 30;
  _gfx->fillRect(6, y, _w - 12, 24, WP_BG);
  _gfx->drawRect(4, y - 2, _w - 8, 26, WP_LINE);
  _gfx->setTextSize(2);
  _gfx->setTextColor(WP_WHITE);
  _gfx->setCursor(10, y + 4);
  // Shown in the clear. This is a device sitting on a bench being set up by
  // the person holding it, and a row of asterisks makes a typo on a resistive
  // panel impossible to find.
  _gfx->print(_pass);
}

void WiFiProvision::drawKeyboard() {
  if (!_gfx) return;
  _gfx->fillScreen(WP_BG);

  _gfx->setTextSize(1);
  _gfx->setTextColor(WP_DIM);
  _gfx->setCursor(6, 6);
  _gfx->print(F("Password for"));
  _gfx->setTextSize(2);
  _gfx->setTextColor(WP_ACCENT);
  _gfx->setCursor(6, 20);
  _gfx->print(_nets[_selected].ssid);

  drawPasswordField();

  for (uint8_t r = 0; r < 4; r++) {
    const char *row = rowChars(r);
    const uint8_t n = strlen(row);
    for (uint8_t c = 0; c < n; c++) {
      const int16_t kx = c * _keyW + 1, ky = _kbTop + r * _keyH;
      _gfx->drawRect(kx, ky, _keyW - 2, _keyH - 2, WP_KEY);
      _gfx->setTextSize(2);
      _gfx->setTextColor(WP_WHITE);
      _gfx->setCursor(kx + _keyW / 2 - 5, ky + _keyH / 2 - 7);
      _gfx->print(row[c]);
    }
  }

  struct { int16_t x, w; const char *label; uint16_t col; } btn[] = {
      {2, 56, _layer == 2 ? "abc" : "SHIFT", WP_KEY},
      {60, 46, "?123", WP_KEY},
      {110, 56, "SPACE", WP_KEY},
      {170, 44, "DEL", WP_WARN},
      {218, 40, "OK", WP_OK},
      {262, 54, "BACK", WP_BAD},
  };
  for (uint8_t i = 0; i < 6; i++) {
    if (btn[i].x + btn[i].w > _w) continue;
    _gfx->fillRect(btn[i].x, _funcTop, btn[i].w, _funcH, btn[i].col);
    _gfx->setTextSize(1);
    _gfx->setTextColor(WP_WHITE);
    _gfx->setCursor(btn[i].x + 6, _funcTop + _funcH / 2 - 3);
    _gfx->print(btn[i].label);
  }
}

void WiFiProvision::redraw() {
  switch (_state) {
    case WP_PICK:     drawPick(); break;
    case WP_KEYBOARD: drawKeyboard(); break;
    default: break;
  }
}

// ------------------------------------------------------------------- touch --

void WiFiProvision::touch(int16_t x, int16_t y) {
  switch (_state) {
    case WP_PICK:     touchPick(x, y); break;
    case WP_KEYBOARD: touchKeyboard(x, y); break;
    case WP_FAILED:   start(); break;
    default: break;
  }
}

void WiFiProvision::touchPick(int16_t x, int16_t y) {
  if (y > _h - 30 && x < 100) { scan(); drawPick(); return; }
  if (y < _listTop) return;
  const uint8_t i = (y - _listTop) / _rowH;
  if (i < _count) select(i);
}

void WiFiProvision::touchKeyboard(int16_t x, int16_t y) {
  if (y >= _funcTop) {
    if (x < 58) {
      _layer = (_layer == 0) ? 1 : 0;
      drawKeyboard();
    } else if (x < 108) {
      _layer = (_layer == 2) ? 0 : 2;
      drawKeyboard();
    } else if (x < 168) {
      if (_passLen < WP_PASS_LEN - 2) {
        _pass[_passLen++] = ' ';
        _pass[_passLen] = 0;
        drawPasswordField();
      }
    } else if (x < 214) {
      if (_passLen) { _pass[--_passLen] = 0; drawPasswordField(); }
    } else if (x < 256) {
      connect();
    } else {
      _state = WP_PICK;
      drawPick();
    }
    return;
  }

  if (y < _kbTop) return;
  const uint8_t r = (y - _kbTop) / _keyH;
  if (r > 3) return;
  const char *row = rowChars(r);
  const uint8_t c = x / _keyW;
  if (c >= strlen(row)) return;

  if (_passLen < WP_PASS_LEN - 2) {
    _pass[_passLen++] = row[c];
    _pass[_passLen] = 0;
    // A shift layer that stays up after one letter is a keyboard nobody can
    // type a password on, so it falls back the way a phone's does.
    if (_layer == 1) { _layer = 0; drawKeyboard(); }
    else drawPasswordField();
  }
}

// ------------------------------------------------------------------ serial --

bool WiFiProvision::handleSerial(Stream &io) {
  bool consumed = false;
  while (io.available()) {
    const char ch = io.read();
    if (ch != '\n' && ch != '\r') {
      if (_lineLen < sizeof(_line) - 1) _line[_lineLen++] = ch;
      continue;
    }
    if (!_lineLen) continue;
    _line[_lineLen] = 0;
    _lineLen = 0;
    consumed = true;

    if (!strcmp(_line, "list")) {
      io.print(_count);
      io.println(F(" networks"));
      for (uint8_t i = 0; i < _count; i++) {
        io.print(F("  [")); io.print(i); io.print(F("] "));
        io.print(_nets[i].ssid);
        io.print(F("  ")); io.print(_nets[i].rssi);
        io.print(F(" dBm  ")); io.println(encName(_nets[i].enc));
      }
    } else if (!strcmp(_line, "scan")) {
      io.print(scan());
      io.println(F(" networks found"));
      if (_state == WP_PICK) drawPick();
    } else if (!strncmp(_line, "pick ", 5)) {
      const int i = atoi(_line + 5);
      if (i < 0 || !select((uint8_t)i)) io.println(F("no such network"));
      else { io.print(F("selected ")); io.println(_nets[_selected].ssid); }
    } else if (!strncmp(_line, "pass ", 5)) {
      setPassword(_line + 5);
      io.print(F("password set (")); io.print(_passLen);
      io.println(F(" chars)"));
    } else if (!strcmp(_line, "connect")) {
      io.println(connect() ? F("connected") : F("failed"));
    } else if (!strcmp(_line, "forget")) {
      forget();
      io.println(F("credentials cleared"));
    } else if (!strcmp(_line, "status")) {
      static const char *names[] = {"idle", "pick", "keyboard",
                                    "connecting", "connected", "failed"};
      io.print(F("state ")); io.print(names[_state]);
      if (WiFi.status() == WL_CONNECTED) {
        const IPAddress ip = WiFi.localIP();
        io.print(F("   ssid ")); io.print(_creds.ssid);
        io.print(F("   ip ")); io.print(ip);
        io.print(F("   rssi ")); io.print(WiFi.RSSI());
      } else {
        io.print(F("   not associated"));
      }
      io.println();
    } else {
      io.println(F("commands: list | scan | pick N | pass TEXT | connect | "
                   "forget | status"));
    }
  }
  return consumed;
}
