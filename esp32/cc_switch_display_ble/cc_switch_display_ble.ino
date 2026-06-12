#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "cc_switch_bg.h"

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 0
#endif

#if ARDUINO_USB_CDC_ON_BOOT
#define DebugSerial Serial0
#else
#define DebugSerial Serial
#endif

static const char *DEVICE_NAME = "CCSwitch";
static const char *SERVICE_UUID = "7b3d0001-7c2f-4f81-9f2f-2d5a91c3cc01";
static const char *STATUS_UUID = "7b3d0002-7c2f-4f81-9f2f-2d5a91c3cc01";
static const char *REQUEST_UUID = "7b3d0003-7c2f-4f81-9f2f-2d5a91c3cc01";
static const int32_t LCD_BUS_SPEED = 1000000;

static String lastPayload = "{}";
static String lastProviderName = "Unknown";
static String lastStatus = "using";
static String lastBalanceText = "";
static String lastCurrency = "USD";
static String lastResetAt = "";
static String lastRefreshedAtText = "";
static String lastAgeText = "";
static String shownStatusText = "";
static String shownBalanceText = "";
static String shownCurrency = "";
static String shownAgeText = "";
static String shownProviderName = "";
static String shownConnectionText = "";
static String shownResetText = "";
static unsigned long lastAgeBaseSeconds = 0;
static bool hasNewPayload = false;
static bool isConnected = false;
static bool displayReady = false;
static bool dashboardFrameDrawn = false;
static bool refreshRequestPending = false;
static unsigned long lastHeartbeatAt = 0;
static unsigned long lastStatusRenderedAt = 0;
static unsigned long lastAgeRefreshAt = 0;
static unsigned long lastLvglTickAt = 0;
static unsigned long lastRefreshRequestAt = 0;
static unsigned long refreshButtonMessageUntil = 0;
static bool lvglReady = false;
static bool connectionUiDirty = false;
static BLECharacteristic *requestCharacteristic = nullptr;

static lv_disp_draw_buf_t lvDrawBuffer;
static lv_disp_drv_t lvDisplayDriver;
static lv_color_t *lvDrawBufferPixels = nullptr;
static lv_obj_t *connectionLabel = nullptr;
static lv_obj_t *providerValueLabel = nullptr;
static lv_obj_t *statusValueLabel = nullptr;
static lv_obj_t *balanceDigitsBox = nullptr;
static lv_obj_t *balanceCurrencyLabel = nullptr;
static lv_obj_t *currencyValueLabel = nullptr;
static lv_obj_t *resetAtValueLabel = nullptr;
static lv_obj_t *refreshButton = nullptr;
static lv_obj_t *refreshButtonLabel = nullptr;

static const uint8_t XL9555_ADDR = 0x20;
static const uint8_t XL9555_REG_OUTPUT0 = 0x02;
static const uint8_t XL9555_REG_CONFIG0 = 0x06;
static const uint8_t XL9555_LCD_RST = 0x02; // IO0_1, LCD_RST in the board schematic
static const uint8_t XL9555_BL_CTR = 0x08;  // IO0_3, BL_CTR in the board schematic
static uint8_t xl9555Output0 = 0xFF;
static uint8_t xl9555Config0 = 0xFF;

class Arduino_ST7796_SplitAddress : public Arduino_ST7796 {
public:
  using Arduino_ST7796::Arduino_ST7796;

  void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override {
    if ((x != _currentX) || (w != _currentW)) {
      _currentX = x;
      _currentW = w;
      x += _xStart;
      _bus->writeC8D16D16Split(ST7796_CASET, x, x + w - 1);
    }

    if ((y != _currentY) || (h != _currentH)) {
      _currentY = y;
      _currentH = h;
      y += _yStart;
      _bus->writeC8D16D16Split(ST7796_RASET, y, y + h - 1);
    }

    _bus->writeCommand(ST7796_RAMWR);
  }
};

static Arduino_DataBus *bus = new Arduino_ESP32LCD16(
    38 /* DC */, 39 /* CS */, 45 /* WR */, 14 /* RD */,
    13 /* D0 */, 12 /* D1 */, 11 /* D2 */, 10 /* D3 */,
    9 /* D4 */, 46 /* D5 */, 3 /* D6 */, 8 /* D7 */,
    18 /* D8 */, 17 /* D9 */, 16 /* D10 */, 15 /* D11 */,
    7 /* D12 */, 6 /* D13 */, 5 /* D14 */, 4 /* D15 */);
static Arduino_GFX *gfx = new Arduino_ST7796_SplitAddress(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* portrait */, true /* IPS */);

static const int16_t SCREEN_W = 320;
static const int16_t SCREEN_H = 480;
static const uint16_t LVGL_BUFFER_LINES = 24;

static const uint16_t COLOR_BG = RGB565(10, 15, 25);
static const uint16_t COLOR_CARD = RGB565(15, 23, 42);
static const uint16_t COLOR_MUTED = RGB565(203, 213, 225);
static const uint16_t COLOR_LIGHT_MUTED = RGB565(248, 250, 252);
static const uint16_t COLOR_GREEN = RGB565(74, 222, 128);
static const uint16_t COLOR_PILL = RGB565(30, 41, 59);
static const uint16_t COLOR_WARN = RGB565(251, 191, 36);
static const uint16_t COLOR_ERROR = RGB565(248, 113, 113);

static const int16_t CARD_X = 0;
static const int16_t CARD_Y = 70;
static const int16_t CARD_W = 480;
static const int16_t CARD_H = 220;
static const int16_t HEADER_TITLE_X = 24;
static const int16_t HEADER_TITLE_Y = 26;
static const int16_t STATUS_PILL_X = 326;
static const int16_t STATUS_PILL_Y = 14;
static const int16_t STATUS_PILL_W = 130;
static const int16_t STATUS_PILL_H = 44;
static const int16_t BALANCE_LABEL_X = 28;
static const int16_t BALANCE_LABEL_Y = 96;
static const int16_t BALANCE_VALUE_X = 28;
static const int16_t BALANCE_VALUE_Y = 132;
static const int16_t CURRENCY_Y = 154;
static const int16_t BALANCE_FIELD_X = 24;
static const int16_t BALANCE_FIELD_Y = 128;
static const int16_t BALANCE_FIELD_W = 432;
static const int16_t BALANCE_FIELD_H = 64;
static const int16_t PROVIDER_X = 28;
static const int16_t PROVIDER_Y = 238;
static const int16_t AGE_TEXT_X = 346;
static const int16_t AGE_TEXT_Y = 238;

static const uint8_t ST7796_VENDOR_INIT[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x01, // software reset
    END_WRITE,
    DELAY, 150,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11, // sleep out
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D8, 0x36, 0x48,
    WRITE_C8_D8, 0x3A, 0x55,

    WRITE_C8_D8, 0xF0, 0xC3,
    WRITE_C8_D8, 0xF0, 0x96,
    WRITE_C8_D8, 0xB4, 0x01,
    WRITE_C8_D8, 0xB7, 0xC6,

    WRITE_COMMAND_8, 0xC0,
    WRITE_BYTES, 2, 0x80, 0x45,

    WRITE_C8_D8, 0xC1, 0x13,
    WRITE_C8_D8, 0xC2, 0xA7,
    WRITE_C8_D8, 0xC5, 0x0A,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 14, 0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30,
    0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 14, 0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F,
    0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32,

    WRITE_C8_D8, 0xF0, 0x3C,
    WRITE_C8_D8, 0xF0, 0x69,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x21, // display inversion on, required by this IPS panel
    WRITE_COMMAND_8, 0x29, // display on
    WRITE_COMMAND_8, 0x2C, // memory write, matches the vendor init script tail
    END_WRITE,
    DELAY, 20,
};

static void debugPrint(const String &message) {
  DebugSerial.print(message);
}

static void debugPrintln(const String &message = "") {
  DebugSerial.println(message);
}

static bool xl9555WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool xl9555ReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(XL9555_ADDR, static_cast<uint8_t>(1)) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

static bool xl9555SetPort0Outputs(uint8_t mask) {
  uint8_t config = xl9555Config0;
  if (xl9555ReadReg(XL9555_REG_CONFIG0, config)) {
    xl9555Config0 = config;
  }

  xl9555Config0 &= ~mask;
  return xl9555WriteReg(XL9555_REG_CONFIG0, xl9555Config0);
}

static bool xl9555WritePort0Bits(uint8_t mask, uint8_t value) {
  uint8_t output = xl9555Output0;
  if (xl9555ReadReg(XL9555_REG_OUTPUT0, output)) {
    xl9555Output0 = output;
  }

  xl9555Output0 = (xl9555Output0 & ~mask) | (value & mask);
  return xl9555WriteReg(XL9555_REG_OUTPUT0, xl9555Output0);
}

static void initDisplayExpander() {
  Wire.begin(2 /* SDA */, 1 /* SCL */);
  Wire.setClock(400000);

  if (!xl9555ReadReg(XL9555_REG_CONFIG0, xl9555Config0)) {
    debugPrintln("XL9555 not found; trying LCD init without hardware reset.");
    return;
  }
  xl9555ReadReg(XL9555_REG_OUTPUT0, xl9555Output0);

  if (!xl9555SetPort0Outputs(XL9555_LCD_RST | XL9555_BL_CTR)) {
    debugPrintln("XL9555 config failed; trying LCD init anyway.");
    return;
  }

  xl9555WritePort0Bits(XL9555_LCD_RST | XL9555_BL_CTR, XL9555_LCD_RST | XL9555_BL_CTR);
  delay(5);
  xl9555WritePort0Bits(XL9555_LCD_RST, 0);
  delay(20);
  xl9555WritePort0Bits(XL9555_LCD_RST | XL9555_BL_CTR, XL9555_LCD_RST | XL9555_BL_CTR);
  delay(150);
  debugPrintln("LCD reset via XL9555");
}

static String shortText(String value, int maxChars) {
  if (value.length() <= maxChars) {
    return value;
  }

  if (maxChars <= 3) {
    return value.substring(0, maxChars);
  }

  return value.substring(0, maxChars - 3) + "...";
}

static String syncAgeText() {
  if (lastStatusRenderedAt == 0) {
    return "WAITING";
  }

  unsigned long elapsedSeconds = lastAgeBaseSeconds + (millis() - lastStatusRenderedAt) / 1000;
  if (elapsedSeconds < 60) {
    return "NOW";
  }

  unsigned long elapsedMinutes = elapsedSeconds / 60;
  if (elapsedMinutes < 60) {
    return String(elapsedMinutes) + "m AGO";
  }

  unsigned long elapsedHours = elapsedMinutes / 60;
  if (elapsedHours < 24) {
    return String(elapsedHours) + "h AGO";
  }

  unsigned long elapsedDays = elapsedHours / 24;
  if (elapsedDays > 99) {
    return "99d+ AGO";
  }

  return String(elapsedDays) + "d AGO";
}

static String statusText(String status) {
  if (status == "using") {
    return "USING";
  }
  if (status == "error") {
    return "ERROR";
  }
  if (status == "inactive") {
    return "IDLE";
  }
  status.toUpperCase();
  return shortText(status, 8);
}

static String resetText(String value) {
  if (value.length() >= 16 && value[10] == 'T') {
    String formatted = value.substring(0, 10) + " " + value.substring(11, 16);
    if (value.endsWith("Z")) {
      formatted += " UTC";
    }
    return formatted;
  }

  return value.length() ? shortText(value, 22) : "--";
}

static void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorMap) {
  if (!displayReady) {
    lv_disp_flush_ready(disp);
    return;
  }

  int16_t w = area->x2 - area->x1 + 1;
  int16_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(colorMap), w, h);
  lv_disp_flush_ready(disp);
}

static lv_obj_t *makeLabel(int16_t x, int16_t y, int16_t w, const lv_font_t *font, lv_color_t color,
                           lv_text_align_t align) {
  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, w, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
  return label;
}

static lv_obj_t *makeOverlayRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  lv_obj_t *obj = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x000500), LV_PART_MAIN);
  return obj;
}

static lv_obj_t *makeTinyPixel(int16_t x, int16_t y, lv_color_t color) {
  lv_obj_t *pixel = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(pixel, x, y);
  lv_obj_set_size(pixel, 1, 1);
  lv_obj_clear_flag(pixel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(pixel, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(pixel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(pixel, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pixel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(pixel, color, LV_PART_MAIN);
  return pixel;
}

static const char *tinyGlyph(char c) {
  switch (c) {
    case 'A':
      return "01110"
             "10001"
             "10001"
             "11111"
             "10001"
             "10001"
             "10001";
    case 'C':
      return "01111"
             "10000"
             "10000"
             "10000"
             "10000"
             "10000"
             "01111";
    case 'D':
      return "11110"
             "10001"
             "10001"
             "10001"
             "10001"
             "10001"
             "11110";
    case 'E':
      return "11111"
             "10000"
             "10000"
             "11110"
             "10000"
             "10000"
             "11111";
    case 'I':
      return "11111"
             "00100"
             "00100"
             "00100"
             "00100"
             "00100"
             "11111";
    case 'N':
      return "10001"
             "11001"
             "10101"
             "10011"
             "10001"
             "10001"
             "10001";
    case 'O':
      return "01110"
             "10001"
             "10001"
             "10001"
             "10001"
             "10001"
             "01110";
    case 'P':
      return "11110"
             "10001"
             "10001"
             "11110"
             "10000"
             "10000"
             "10000";
    case 'R':
      return "11110"
             "10001"
             "10001"
             "11110"
             "10100"
             "10010"
             "10001";
    case 'S':
      return "01111"
             "10000"
             "10000"
             "01110"
             "00001"
             "00001"
             "11110";
    case 'T':
      return "11111"
             "00100"
             "00100"
             "00100"
             "00100"
             "00100"
             "00100";
    case 'U':
      return "10001"
             "10001"
             "10001"
             "10001"
             "10001"
             "10001"
             "01110";
    case 'V':
      return "10001"
             "10001"
             "10001"
             "10001"
             "10001"
             "01010"
             "00100";
    case 'Y':
      return "10001"
             "10001"
             "01010"
             "00100"
             "00100"
             "00100"
             "00100";
    default:
      return nullptr;
  }
}

static void drawTinyText(int16_t x, int16_t y, const char *text, lv_color_t color) {
  int16_t cursorX = x;
  for (const char *p = text; *p; p++) {
    if (*p == ' ') {
      cursorX += 4;
      continue;
    }

    const char *glyph = tinyGlyph(*p);
    if (!glyph) {
      cursorX += 6;
      continue;
    }

    for (uint8_t row = 0; row < 7; row++) {
      for (uint8_t col = 0; col < 5; col++) {
        if (glyph[row * 5 + col] == '1') {
          makeTinyPixel(cursorX + col, y + row, color);
        }
      }
    }
    cursorX += 6;
  }
}

static void setRefreshButtonText(const char *text) {
  if (refreshButtonLabel == nullptr) {
    return;
  }

  lv_label_set_text(refreshButtonLabel, text);
  lv_obj_center(refreshButtonLabel);
}

static void showRefreshButtonText(const char *text, unsigned long durationMs) {
  setRefreshButtonText(text);
  refreshButtonMessageUntil = durationMs > 0 ? millis() + durationMs : 0;
}

static void sendRefreshRequest() {
  unsigned long now = millis();
  if (now - lastRefreshRequestAt < 1200) {
    return;
  }
  lastRefreshRequestAt = now;

  if (!isConnected || requestCharacteristic == nullptr) {
    refreshRequestPending = false;
    showRefreshButtonText("NO MAC", 1200);
    return;
  }

  String message = String("{\"action\":\"refresh_usage\",\"source\":\"lvgl_button\",\"at\":") + String(now) + "}";
  requestCharacteristic->setValue(message.c_str());
  requestCharacteristic->notify();
  refreshRequestPending = true;
  showRefreshButtonText("SYNCING", 2500);
  debugPrint("Refresh request sent: ");
  debugPrintln(message);
}

static void refreshButtonEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    sendRefreshRequest();
  }
}

static void refreshButtonTick() {
  if (refreshButtonMessageUntil == 0) {
    return;
  }

  if (static_cast<long>(millis() - refreshButtonMessageUntil) >= 0) {
    refreshButtonMessageUntil = 0;
    setRefreshButtonText("SYNC PLAN");
  }
}

static lv_obj_t *makeRefreshButton() {
  lv_obj_t *button = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(button, 72, 421);
  lv_obj_set_size(button, 176, 36);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x001000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0x35e33a), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(button, lv_color_hex(0x21ff2e), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x063506), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(button, lv_color_hex(0x7dff85), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, refreshButtonEvent, LV_EVENT_CLICKED, nullptr);

  refreshButtonLabel = lv_label_create(button);
  lv_label_set_long_mode(refreshButtonLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(refreshButtonLabel, &lv_font_unscii_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(refreshButtonLabel, lv_color_hex(0x35e33a), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(refreshButtonLabel, 1, LV_PART_MAIN);
  setRefreshButtonText("SYNC PLAN");
  return button;
}

static lv_obj_t *makeSegment(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(obj, 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x35e33a), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 8, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(obj, lv_color_hex(0x21ff2e), LV_PART_MAIN);
  return obj;
}

static bool digitSegmentOn(char digit, uint8_t seg) {
  switch (digit) {
    case '0':
      return seg == 0 || seg == 1 || seg == 2 || seg == 3 || seg == 4 || seg == 5;
    case '1':
      return seg == 1 || seg == 2;
    case '2':
      return seg == 0 || seg == 1 || seg == 6 || seg == 4 || seg == 3;
    case '3':
      return seg == 0 || seg == 1 || seg == 2 || seg == 3 || seg == 6;
    case '4':
      return seg == 5 || seg == 6 || seg == 1 || seg == 2;
    case '5':
      return seg == 0 || seg == 5 || seg == 6 || seg == 2 || seg == 3;
    case '6':
      return seg == 0 || seg == 5 || seg == 4 || seg == 3 || seg == 2 || seg == 6;
    case '7':
      return seg == 0 || seg == 1 || seg == 2;
    case '8':
      return true;
    case '9':
      return seg == 0 || seg == 1 || seg == 2 || seg == 3 || seg == 5 || seg == 6;
    case '-':
      return seg == 6;
    default:
      return false;
  }
}

static int16_t charWidthForBalance(char c, int16_t digitW, int16_t dotW) {
  if (c == '.') {
    return dotW;
  }
  return digitW;
}

static void drawSegmentDigit(lv_obj_t *parent, char digit, int16_t x, int16_t y,
                             int16_t digitW, int16_t digitH, int16_t thick) {
  int16_t half = digitH / 2;
  int16_t hSeg = half - thick;
  const int16_t pos[7][4] = {
      {thick, 0, static_cast<int16_t>(digitW - 2 * thick), thick},
      {static_cast<int16_t>(digitW - thick), thick, thick, hSeg},
      {static_cast<int16_t>(digitW - thick), half, thick, hSeg},
      {thick, static_cast<int16_t>(digitH - thick), static_cast<int16_t>(digitW - 2 * thick), thick},
      {0, half, thick, hSeg},
      {0, thick, thick, hSeg},
      {thick, static_cast<int16_t>(half - thick / 2), static_cast<int16_t>(digitW - 2 * thick), thick},
  };

  for (uint8_t seg = 0; seg < 7; seg++) {
    if (digitSegmentOn(digit, seg)) {
      makeSegment(parent, x + pos[seg][0], y + pos[seg][1], pos[seg][2], pos[seg][3]);
    }
  }
}

static void drawBalanceDigits(const String &balanceText) {
  String value = balanceText.length() > 0 ? shortText(balanceText, 8) : "--";
  if (value == shownBalanceText) {
    return;
  }

  lv_obj_clean(balanceDigitsBox);

  int16_t digitW = 48;
  int16_t digitH = 82;
  int16_t thick = 10;
  int16_t dotW = 13;
  int16_t gap = 7;

  int16_t totalW = 0;
  for (uint16_t i = 0; i < value.length(); i++) {
    totalW += charWidthForBalance(value[i], digitW, dotW);
    if (i + 1 < value.length()) {
      totalW += gap;
    }
  }

  if (totalW > 270) {
    digitW = 40;
    digitH = 72;
    thick = 8;
    dotW = 10;
    gap = 5;
    totalW = 0;
    for (uint16_t i = 0; i < value.length(); i++) {
      totalW += charWidthForBalance(value[i], digitW, dotW);
      if (i + 1 < value.length()) {
        totalW += gap;
      }
    }
  }

  int16_t cursorX = (270 - totalW) / 2;
  if (cursorX < 0) {
    cursorX = 0;
  }
  int16_t digitY = digitH == 82 ? 4 : 10;
  for (uint16_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '.') {
      makeSegment(balanceDigitsBox, cursorX + 2, digitY + digitH - thick - 2, thick, thick);
      cursorX += dotW + gap;
    } else {
      drawSegmentDigit(balanceDigitsBox, c, cursorX, digitY, digitW, digitH, thick);
      cursorX += digitW + gap;
    }
  }

  shownBalanceText = value;
}

static void refreshConnectionField(bool force) {
  String text = isConnected ? "CONNECTED" : "BLE READY";
  if (!force && text == shownConnectionText) {
    return;
  }

  lv_label_set_text(connectionLabel, text.c_str());
  lv_obj_set_style_text_color(connectionLabel, isConnected ? lv_color_hex(0x35e33a) : lv_color_hex(0x668066),
                              LV_PART_MAIN);
  shownConnectionText = text;
}

static void refreshProviderField(bool force) {
  String provider = shortText(lastProviderName.length() ? lastProviderName : "Unknown", 12);
  if (!force && provider == shownProviderName) {
    return;
  }

  lv_label_set_text(providerValueLabel, provider.c_str());
  shownProviderName = provider;
}

static void refreshStatusField(bool force) {
  String text = statusText(lastStatus);
  if (!force && text == shownStatusText) {
    return;
  }

  lv_label_set_text(statusValueLabel, text.c_str());
  lv_color_t color = lv_color_hex(0x35e33a);
  if (lastStatus == "error") {
    color = lv_color_hex(0xff5050);
  } else if (lastStatus == "inactive") {
    color = lv_color_hex(0xf7c948);
  }
  lv_obj_set_style_text_color(statusValueLabel, color, LV_PART_MAIN);
  shownStatusText = text;
}

static void refreshBalanceField(const String &balanceText, const String &currency, bool force) {
  String unit = currency.length() > 0 ? shortText(currency, 3) : "USD";
  unit.toUpperCase();

  if (force) {
    shownBalanceText = "";
  }

  drawBalanceDigits(balanceText);

  if (force || unit != shownCurrency) {
    lv_label_set_text(balanceCurrencyLabel, unit.c_str());
    lv_label_set_text(currencyValueLabel, unit.c_str());
    shownCurrency = unit;
  }
}

static void refreshResetField(bool force) {
  String text = lastRefreshedAtText.length() ? shortText(lastRefreshedAtText, 19) : "--";
  if (!force && text == shownResetText) {
    return;
  }

  lv_label_set_text(resetAtValueLabel, text.c_str());
  shownResetText = text;
}

static void refreshAgeField(bool force) {
  refreshConnectionField(force);
}

static void refreshDashboard(bool force) {
  if (!lvglReady) {
    return;
  }

  refreshConnectionField(force);
  refreshProviderField(force);
  refreshStatusField(force);
  refreshBalanceField(lastBalanceText, lastCurrency, force);
  refreshResetField(force);
}

static void buildDashboardUi() {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bg = lv_img_create(lv_scr_act());
  lv_img_set_src(bg, &cc_switch_bg);
  lv_obj_set_pos(bg, 0, 0);

  lv_obj_t *headerTitleLabel = makeLabel(50, 31, 220, &lv_font_montserrat_14, lv_color_hex(0x35e33a), LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(headerTitleLabel, "BALANCE DISPLAY");

  makeOverlayRect(246, 112, 47, 18);
  makeOverlayRect(74, 102, 76, 16);
  drawTinyText(76, 106, "PROVIDER", lv_color_hex(0x808080));
  makeOverlayRect(68, 348, 74, 17);
  drawTinyText(70, 353, "CURRENCY", lv_color_hex(0x808080));
  makeOverlayRect(68, 388, 82, 24);
  drawTinyText(70, 402, "SYNC AT", lv_color_hex(0x808080));

  connectionLabel = makeLabel(116, 59, 116, &lv_font_unscii_16, lv_color_hex(0x668066), LV_TEXT_ALIGN_LEFT);
  providerValueLabel = makeLabel(76, 118, 146, &lv_font_montserrat_24, lv_color_hex(0xe8e8e8), LV_TEXT_ALIGN_LEFT);
  statusValueLabel = makeLabel(248, 113, 44, &lv_font_unscii_16, lv_color_hex(0x35e33a), LV_TEXT_ALIGN_CENTER);

  balanceDigitsBox = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(balanceDigitsBox, 25, 210);
  lv_obj_set_size(balanceDigitsBox, 270, 94);
  lv_obj_clear_flag(balanceDigitsBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(balanceDigitsBox, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(balanceDigitsBox, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(balanceDigitsBox, 0, LV_PART_MAIN);

  balanceCurrencyLabel = makeLabel(98, 300, 124, &lv_font_montserrat_24, lv_color_hex(0x35e33a), LV_TEXT_ALIGN_CENTER);
  currencyValueLabel = makeLabel(240, 352, 56, &lv_font_montserrat_14, lv_color_hex(0xe8e8e8), LV_TEXT_ALIGN_RIGHT);
  resetAtValueLabel = makeLabel(125, 397, 172, &lv_font_montserrat_14, lv_color_hex(0xe8e8e8), LV_TEXT_ALIGN_RIGHT);
  refreshButton = makeRefreshButton();

  shownStatusText = "";
  shownBalanceText = "";
  shownCurrency = "";
  shownAgeText = "";
  shownProviderName = "";
  shownConnectionText = "";
  shownResetText = "";
  dashboardFrameDrawn = true;
  refreshDashboard(true);
}

static bool initLvgl() {
  lv_init();

  lvDrawBufferPixels = static_cast<lv_color_t *>(
      heap_caps_malloc(SCREEN_W * LVGL_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!lvDrawBufferPixels) {
    debugPrintln("LVGL buffer allocation failed.");
    return false;
  }

  lv_disp_draw_buf_init(&lvDrawBuffer, lvDrawBufferPixels, nullptr, SCREEN_W * LVGL_BUFFER_LINES);
  lv_disp_drv_init(&lvDisplayDriver);
  lvDisplayDriver.hor_res = SCREEN_W;
  lvDisplayDriver.ver_res = SCREEN_H;
  lvDisplayDriver.flush_cb = lvglFlush;
  lvDisplayDriver.draw_buf = &lvDrawBuffer;
  lv_disp_drv_register(&lvDisplayDriver);

  lvglReady = true;
  lastLvglTickAt = millis();
  buildDashboardUi();
  return true;
}

static void runLvgl() {
  if (!lvglReady) {
    return;
  }

  unsigned long now = millis();
  lv_tick_inc(now - lastLvglTickAt);
  lastLvglTickAt = now;
  lv_timer_handler();
}

static void drawWaitingScreen() {
  if (!lvglReady) {
    return;
  }

  refreshDashboard(true);
}

static void scrubDisplayMemory() {
  if (!displayReady) {
    return;
  }

  gfx->fillScreen(RGB565_BLACK);
  delay(40);
  gfx->fillScreen(COLOR_BG);
  delay(40);
  gfx->fillScreen(RGB565_BLACK);
  delay(40);
  gfx->fillScreen(COLOR_BG);
}

static void initDisplay() {
  initDisplayExpander();
  displayReady = gfx->begin(LCD_BUS_SPEED);
  if (!displayReady) {
    debugPrintln("LCD init failed; BLE will keep running.");
    return;
  }

  bus->batchOperation(ST7796_VENDOR_INIT, sizeof(ST7796_VENDOR_INIT));
  gfx->setRotation(4);
  scrubDisplayMemory();
  if (!initLvgl()) {
    debugPrintln("LVGL init failed; BLE will keep running.");
    return;
  }
  drawWaitingScreen();
  debugPrint("LCD ready, size=");
  debugPrint(String(gfx->width()));
  debugPrint("x");
  debugPrintln(String(gfx->height()));
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    isConnected = true;
    connectionUiDirty = true;
  }

  void onDisconnect(BLEServer *server) override {
    isConnected = false;
    connectionUiDirty = true;
    BLEDevice::startAdvertising();
  }
};

class StatusCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }

    lastPayload = String(value.c_str());
    hasNewPayload = true;
  }
};

static bool isJsonWhitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static int findJsonValueStart(const String &json, const char *key) {
  String marker = String("\"") + key + "\"";
  int keyStart = json.indexOf(marker);
  while (keyStart >= 0) {
    int colon = keyStart + marker.length();
    while (colon < json.length() && isJsonWhitespace(json[colon])) {
      colon++;
    }

    if (colon < json.length() && json[colon] == ':') {
      int valueStart = colon + 1;
      while (valueStart < json.length() && isJsonWhitespace(json[valueStart])) {
        valueStart++;
      }
      return valueStart;
    }

    keyStart = json.indexOf(marker, keyStart + marker.length());
  }

  return -1;
}

static String extractJsonString(const String &json, const char *key) {
  int start = findJsonValueStart(json, key);
  if (start < 0 || start >= json.length() || json[start] != '"') {
    return "";
  }

  String value = "";
  bool escaped = false;
  for (int i = start + 1; i < json.length(); i++) {
    char c = json[i];
    if (escaped) {
      if (c == '"' || c == '\\' || c == '/') {
        value += c;
      } else if (c == 'n') {
        value += '\n';
      } else if (c == 'r') {
        value += '\r';
      } else if (c == 't') {
        value += '\t';
      } else {
        value += c;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return value;
    } else {
      value += c;
    }
  }

  return "";
}

static String extractJsonNumber(const String &json, const char *key) {
  int start = findJsonValueStart(json, key);
  if (start < 0) {
    return "";
  }

  int end = start;
  while (end < json.length()) {
    char c = json[end];
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-')) {
      break;
    }
    end++;
  }

  return json.substring(start, end);
}

static void renderStatus(const String &payload) {
  String providerName = extractJsonString(payload, "providerName");
  String status = extractJsonString(payload, "status");
  String balanceText = extractJsonString(payload, "balanceText");
  String currency = extractJsonString(payload, "currency");
  String resetAt = extractJsonString(payload, "resetAt");
  String refreshedAtText = extractJsonString(payload, "refreshedAtText");
  String ageText = extractJsonString(payload, "ageText");
  String ageSecondsText = extractJsonNumber(payload, "ageSeconds");

  if (providerName.length() == 0) {
    providerName = "Unknown";
  }
  if (status.length() == 0) {
    status = "using";
  }
  if (balanceText.length() == 0) {
    balanceText = extractJsonNumber(payload, "balance");
  }
  if (currency.length() == 0) {
    currency = "USD";
  }
  if (balanceText.length() == 0 && providerName == lastProviderName && lastBalanceText.length() > 0) {
    balanceText = lastBalanceText;
    currency = lastCurrency.length() > 0 ? lastCurrency : currency;
  }

  bool isFirstStatus = lastStatusRenderedAt == 0;

  lastProviderName = providerName;
  lastStatus = status;
  lastBalanceText = balanceText;
  lastCurrency = currency;
  lastResetAt = resetAt;
  lastRefreshedAtText = refreshedAtText;
  lastAgeText = ageText;
  lastAgeBaseSeconds = ageSecondsText.length() > 0 ? static_cast<unsigned long>(ageSecondsText.toInt()) : 0;
  lastStatusRenderedAt = millis();
  lastAgeRefreshAt = lastStatusRenderedAt;

  debugPrintln();
  debugPrintln("===== cc switch status =====");
  debugPrint("Provider: ");
  debugPrintln(providerName);
  debugPrint("Status: ");
  debugPrintln(status);
  debugPrint("Balance: ");
  debugPrint(balanceText);
  debugPrint(" ");
  debugPrintln(currency);
  debugPrint("Reset: ");
  debugPrintln(resetAt.length() ? resetAt : "--");
  debugPrint("Refreshed: ");
  debugPrintln(refreshedAtText.length() ? refreshedAtText : "--");
  debugPrint("Age: ");
  debugPrintln(ageText.length() ? ageText : syncAgeText());
  debugPrint("Raw: ");
  debugPrintln(payload);
  debugPrintln("============================");

  refreshDashboard(isFirstStatus);
  if (refreshRequestPending) {
    refreshRequestPending = false;
    showRefreshButtonText("UPDATED", 900);
  }
}

void setup() {
  DebugSerial.begin(115200);
  delay(500);
  debugPrintln("Starting CCSwitch BLE display...");
  initDisplay();

  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(185);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *statusCharacteristic = service->createCharacteristic(
      STATUS_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);

  statusCharacteristic->setCallbacks(new StatusCallbacks());
  statusCharacteristic->addDescriptor(new BLE2902());

  requestCharacteristic = service->createCharacteristic(
      REQUEST_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  requestCharacteristic->setValue("{\"action\":\"idle\"}");
  requestCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(0x06);
  advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));

  BLEAdvertisementData scanResponseData;
  scanResponseData.setName(DEVICE_NAME);

  advertising->setAdvertisementData(advertisementData);
  advertising->setScanResponseData(scanResponseData);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  debugPrintln("BLE advertising as CCSwitch");
}

void loop() {
  runLvgl();
  refreshButtonTick();

  if (connectionUiDirty) {
    connectionUiDirty = false;
    refreshConnectionField(false);
  }

  if (hasNewPayload) {
    hasNewPayload = false;
    renderStatus(lastPayload);
  }

  if (millis() - lastHeartbeatAt >= 30000) {
    lastHeartbeatAt = millis();
    debugPrint("BLE alive, connected=");
    debugPrintln(isConnected ? "yes" : "no");
  }

  if (lastStatusRenderedAt > 0 && millis() - lastAgeRefreshAt >= 30000) {
    lastAgeRefreshAt = millis();
    refreshAgeField(false);
  }

  runLvgl();
  delay(5);
}
