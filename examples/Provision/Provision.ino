// Provision - join a network without compiling the password in.
//
// Feather M0 WiFi with a 2.4" TFT FeatherWing. On boot it tries the stored
// credentials; if there are none, or they no longer work, it scans and puts
// the picker up.
//
// FlashStorage keeps the credentials in a reserved flash page which is erased
// on every sketch upload, so they survive power cycles and resets but not
// reflashing. Move them to the SD card if you need more than that.
//
// Touch calibration here is a rough default. For an affine fit that does not
// require you to work out which axis is swapped, see
// https://github.com/ngk0/TouchTune

#include <SPI.h>
#include <WiFi101.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_STMPE610.h>
#include <FlashStorage.h>
#include <WiFiProvision.h>

#define TFT_CS 9
#define TFT_DC 10
#define STMPE_CS 6
#define SD_CS 5   // shares the SPI bus and must be deselected

#define WINC_CS 8
#define WINC_IRQ 7
#define WINC_RST 4
#define WINC_EN 2

#define TS_MINX 338
#define TS_MINY 249
#define TS_MAXX 3779
#define TS_MAXY 3550

Adafruit_ILI9341 tft(TFT_CS, TFT_DC);
Adafruit_STMPE610 ts(STMPE_CS);
WiFiProvision wp;

FlashStorage(credStore, WPCreds);

void setup() {
  Serial.begin(115200);

  // Left floating, the microSD card can drive MISO and corrupt every touch
  // controller read on the shared bus.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  tft.begin(24000000);
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  ts.begin();

  WiFi.setPins(WINC_CS, WINC_IRQ, WINC_RST, WINC_EN);
  if (WiFi.status() == WL_NO_SHIELD) {
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(10, 100);
    tft.print(F("No WiFi radio"));
    while (true) delay(1000);
  }

  wp.begin(&tft);

  if (wp.restore(credStore.read()) && wp.connectStored()) return;
  wp.start();
}

// Press edge detection against an empty-FIFO release counter. The STMPE keeps
// filling its FIFO while the panel is held, so a fixed time window either
// repeats on a slow tap or drops a fast one.
static bool pressEdge(int16_t *sx, int16_t *sy) {
  static uint8_t emptyPolls = 3;
  static bool down = false;

  if (ts.bufferEmpty()) {
    if (emptyPolls < 3 && ++emptyPolls >= 3) down = false;
    return false;
  }

  uint16_t rx = 0, ry = 0;
  uint8_t rz = 0;
  while (!ts.bufferEmpty()) ts.readData(&rx, &ry, &rz);
  if (rz < 10) return false;
  emptyPolls = 0;
  if (down) return false;
  down = true;

  *sx = map(ry, TS_MINY, TS_MAXY, 0, tft.width());
  *sy = map(rx, TS_MINX, TS_MAXX, 0, tft.height());
  return true;
}

void loop() {
  int16_t x, y;
  if (pressEdge(&x, &y)) wp.touch(x, y);

  wp.handleSerial(Serial);

  if (wp.justConnected()) {
    credStore.write(wp.credentials());
    Serial.print(F("saved credentials for "));
    Serial.println(wp.credentials().ssid);
  }

  delay(10);
}
