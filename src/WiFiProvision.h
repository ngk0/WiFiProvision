/*!
 * @file WiFiProvision.h
 *
 * On-device WiFi setup for SAMD boards with an ATWINC1500.
 *
 * Scan list, on-screen keyboard, credential store and a connect flow, so a
 * sketch can join a network without the SSID and password being compiled into
 * it. ESP32 has WiFiManager; this fills the same hole for WiFi101.
 *
 * Touch input is supplied by the caller, so any controller works. Persistence
 * is supplied by the caller too, so the library has no opinion about
 * FlashStorage, EEPROM or SD.
 *
 * MIT licensed. Copyright (c) 2026 Nate Kocher.
 */

#ifndef WIFIPROVISION_H
#define WIFIPROVISION_H

#include <Arduino.h>
#include <Adafruit_GFX.h>

#define WP_MAX_NETWORKS 12
#define WP_SSID_LEN 33
#define WP_PASS_LEN 64
#define WP_CREDS_MAGIC 0x57503031UL  // "WP01"
#define WP_CREDS_VERSION 1

/*!
 * @brief Stored credentials.
 *
 * Plain old data with a magic number, a version and a CRC, so it can be
 * written straight to FlashStorage, EEPROM or a file and rejected cleanly when
 * it comes back wrong.
 */
struct WPCreds {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
  char ssid[WP_SSID_LEN];
  char pass[WP_PASS_LEN];
};

/*! @brief What the user is looking at. */
enum WPState {
  WP_IDLE,        //!< Nothing started yet.
  WP_PICK,        //!< Network list, waiting for a choice.
  WP_KEYBOARD,    //!< Entering a password.
  WP_CONNECTING,  //!< Association in progress.
  WP_CONNECTED,   //!< Joined.
  WP_FAILED       //!< Association refused or timed out.
};

/*! @brief One scan result. */
struct WPNetwork {
  char ssid[WP_SSID_LEN];
  int32_t rssi;
  uint8_t enc;
};

/*!
 * @brief On-screen WiFi provisioning.
 *
 * @code
 *   wp.begin(&tft);
 *   if (!wp.restore(loadCreds()) || !wp.connectStored())
 *     wp.start();                    // scan and show the picker
 *
 *   // in loop()
 *   if (touched) wp.touch(x, y);
 *   wp.handleSerial(Serial);
 *   if (wp.justConnected()) saveCreds(wp.credentials());
 * @endcode
 *
 * Be aware that WiFi101 scanning and association block. scan() takes a couple
 * of seconds and connect() up to its timeout, and neither can be made
 * cooperative without rewriting the driver. The library paints a status screen
 * before it blocks so the device does not look dead, but your loop stops for
 * the duration.
 */
class WiFiProvision {
public:
  /*! @brief Bind to a display. Required before anything draws. */
  void begin(Adafruit_GFX *gfx);

  /*! @brief Scan, then show the picker. Blocks for the scan. */
  void start();

  /*! @brief Scan for networks. Blocks. Returns how many were found. */
  uint8_t scan();

  /*! @brief Drop the network list, for tests that inject their own. */
  void clearNetworks() { _count = 0; }

  /*!
   * @brief Add a network to the list by hand.
   *
   * Intended for tests, so the picker and the keyboard can be exercised with
   * no radio present.
   *
   * @return false if the list is full or the ssid is empty.
   */
  bool addNetwork(const char *ssid, int32_t rssi, uint8_t enc);

  uint8_t networkCount() const { return _count; }
  const WPNetwork &network(uint8_t i) const { return _nets[i]; }

  /*! @brief Choose a network by index and open the password screen. */
  bool select(uint8_t index);

  /*! @brief Set the password for the selected network without typing it. */
  void setPassword(const char *pass);

  /*!
   * @brief Attempt to join the selected network with the entered password.
   *
   * Blocks until connected or the timeout expires.
   * @return true on success, at which point credentials() is worth storing.
   */
  bool connect();

  /*!
   * @brief Attempt to join using restored credentials.
   *
   * Blocks. Does not need a scan first.
   */
  bool connectStored();

  /*! @brief How long connect() waits. Default 20 seconds. */
  void setConnectTimeout(uint32_t ms) { _timeout = ms; }

  /*!
   * @brief Deliver a touch in screen coordinates.
   *
   * Feed this from whatever touch controller you have, ideally on the press
   * edge rather than continuously.
   */
  void touch(int16_t x, int16_t y);

  /*!
   * @brief Handle one line of serial input.
   *
   * Mirrors every on-screen action, which makes the flow testable without a
   * finger on the glass and is the way out when a panel is miscalibrated.
   * Commands: list, scan, pick N, pass TEXT, connect, forget, status, help.
   *
   * @return true if a complete line was consumed.
   */
  bool handleSerial(Stream &io);

  /*!
   * @brief True once, immediately after a successful connection.
   *
   * Clears when read, so it can drive a "save the credentials now" branch
   * without a flag of your own.
   */
  bool justConnected();

  WPState state() const { return _state; }
  const WPCreds &credentials() const { return _creds; }

  /*!
   * @brief Load stored credentials.
   * @return false if the magic, version or CRC does not check out.
   */
  bool restore(const WPCreds &c);

  /*! @brief Forget the stored credentials and disconnect. */
  void forget();

  /*! @brief Repaint whatever screen is current. */
  void redraw();

  /*! @brief Build a credential record with a correct CRC, for tests. */
  static WPCreds makeCreds(const char *ssid, const char *pass);

  /*! @brief CRC16-CCITT over the payload of a record. */
  static uint16_t crcOf(const WPCreds &c);

private:
  void drawPick();
  void drawKeyboard();
  void drawPasswordField();
  void drawStatus(const char *line1, const char *line2, uint16_t color);
  void touchPick(int16_t x, int16_t y);
  void touchKeyboard(int16_t x, int16_t y);
  const char *rowChars(uint8_t row) const;
  void layout();

  Adafruit_GFX *_gfx = nullptr;
  int16_t _w = 0, _h = 0;
  int16_t _kbTop = 0, _keyW = 0, _keyH = 0, _funcTop = 0, _funcH = 0;
  int16_t _rowH = 0, _listTop = 0;

  WPNetwork _nets[WP_MAX_NETWORKS];
  uint8_t _count = 0;
  uint8_t _selected = 0;
  uint8_t _layer = 0;   // 0 lower, 1 upper, 2 symbols

  char _pass[WP_PASS_LEN] = {0};
  uint8_t _passLen = 0;

  WPCreds _creds = {};
  WPState _state = WP_IDLE;
  uint32_t _timeout = 20000;
  bool _justConnected = false;

  char _line[96];
  uint8_t _lineLen = 0;
};

#endif // WIFIPROVISION_H
