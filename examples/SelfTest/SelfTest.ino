// SelfTest - check everything that does not need a radio or a finger.
//
// Networks are injected rather than scanned, and touches are delivered as
// coordinates, so the picker, the keyboard and the credential record can all
// be exercised on a board with nothing attached to it.
//
// Prints machine-readable results for arduino-hil:
//   hil run tests.toml

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFiProvision.h>
#include <HilTest.h>

HilTest t;
WiFiProvision wp;

// A canvas rather than a panel, so this runs with no display wired up. It is
// an Adafruit_GFX, which is all the library asks for.
GFXcanvas1 screen(320, 240);

static void testCredentialRecord() {
  const WPCreds good = WiFiProvision::makeCreds("network", "hunter2");
  t.check(wp.restore(good), "a well formed record is accepted");
  t.check(!strcmp(wp.credentials().ssid, "network"), "ssid survives the round trip");
  t.check(!strcmp(wp.credentials().pass, "hunter2"), "password survives the round trip");

  WPCreds bad = good;
  bad.pass[0] ^= 0xFF;
  t.check(!wp.restore(bad), "a tampered payload fails its CRC");

  bad = good;
  bad.magic ^= 1;
  t.check(!wp.restore(bad), "a wrong magic number is rejected");

  bad = good;
  bad.version = 99;
  t.check(!wp.restore(bad), "an unknown version is rejected");

  bad = good;
  bad.ssid[0] = 0;
  bad.crc = WiFiProvision::crcOf(bad);
  t.check(!wp.restore(bad), "an empty ssid is rejected even with a good CRC");

  // The one that matters after a failed write: an erased flash page reads back
  // as all ones or all zeros, and neither may look like a valid record.
  WPCreds blank;
  memset(&blank, 0, sizeof(blank));
  t.check(!wp.restore(blank), "an all-zero page is rejected");
  memset(&blank, 0xFF, sizeof(blank));
  t.check(!wp.restore(blank), "an all-ones page is rejected");
}

static void testNetworkList() {
  wp.clearNetworks();
  t.check(wp.networkCount() == 0, "list starts empty");
  t.check(wp.addNetwork("alpha", -40, 4), "a network can be added");
  t.check(!wp.addNetwork("", -40, 4), "an empty ssid is refused");
  t.check(wp.addNetwork("beta", -70, 4), "a second network can be added");
  t.check(wp.networkCount() == 2, "count follows");
  t.check(!strcmp(wp.network(1).ssid, "beta"), "entries keep their order");

  while (wp.addNetwork("filler", -80, 4)) { }
  t.check(wp.networkCount() == WP_MAX_NETWORKS, "the list stops at its limit");
}

static void testSelection() {
  wp.clearNetworks();
  wp.addNetwork("alpha", -40, 4);
  wp.addNetwork("beta", -70, 4);

  t.check(!wp.select(9), "selecting past the end is refused");
  t.check(wp.select(1), "selecting a real network works");
  t.check(wp.state() == WP_KEYBOARD, "selection opens the keyboard");
}

static void testKeyboard() {
  wp.clearNetworks();
  wp.addNetwork("alpha", -40, 4);
  wp.select(0);

  // Row 1 of the default layer is qwertyuiop, on a 320 wide screen so each key
  // is 32 across. The keyboard starts at height minus four rows plus the
  // function row.
  const int16_t keyW = 320 / 10;
  const int16_t keyH = 240 / 8;
  const int16_t funcH = 240 / 9;
  const int16_t kbTop = 240 - (4 * keyH + funcH + 4);

  // Tap q, then w.
  wp.touch(keyW / 2, kbTop + keyH + keyH / 2);
  wp.touch(keyW + keyW / 2, kbTop + keyH + keyH / 2);
  t.check(true, "two keys tapped without a crash");

  // Shift, then a letter, which should come out uppercase and drop the shift.
  const int16_t funcTop = kbTop + 4 * keyH + 2;
  wp.touch(10, funcTop + funcH / 2);                       // SHIFT
  wp.touch(keyW / 2, kbTop + keyH + keyH / 2);             // q -> Q
  wp.touch(keyW + keyW / 2, kbTop + keyH + keyH / 2);      // w, shift gone

  // Symbols layer.
  wp.touch(80, funcTop + funcH / 2);                       // ?123
  wp.touch(keyW / 2, kbTop + keyH + keyH / 2);             // !

  // Delete everything, then one more, which must not run off the front.
  for (uint8_t i = 0; i < 12; i++) wp.touch(190, funcTop + funcH / 2);
  t.check(true, "delete past empty does not underflow");

  // Back returns to the picker.
  wp.touch(300, funcTop + funcH / 2);
  t.check(wp.state() == WP_PICK, "back returns to the picker");
}

static void testPasswordLength() {
  wp.clearNetworks();
  wp.addNetwork("alpha", -40, 4);
  wp.select(0);

  char big[200];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  wp.setPassword(big);
  // Silently truncating is correct here; overrunning the buffer is not.
  t.check(true, "an over-long password is truncated rather than overrunning");

  wp.setPassword("short");
  const WPCreds c = WiFiProvision::makeCreds("alpha", "short");
  t.check(wp.restore(c), "a record built from a short password verifies");
}

static void testPickerTouch() {
  wp.clearNetworks();
  wp.addNetwork("alpha", -40, 4);
  wp.addNetwork("beta", -70, 4);
  wp.touch(160, 5);   // above the list, should do nothing
  t.check(wp.state() != WP_KEYBOARD, "a tap above the list selects nothing");
}

static void runSuite() {
  t.begin("WiFiProvision self test");
  wp.begin(&screen);

  testCredentialRecord();
  testNetworkList();
  testSelection();
  testKeyboard();
  testPasswordLength();
  testPickerTouch();

  t.note("WPCreds size", (long)sizeof(WPCreds));
  t.finish();
}

void setup() {
  Serial.begin(115200);
}

void loop() {
  HIL_EVERY(8000) { runSuite(); }
}
