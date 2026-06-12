#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

static const int32_t LCD_BUS_SPEED = 1000000;

static const uint8_t XL9555_ADDR = 0x20;
static const uint8_t XL9555_REG_OUTPUT0 = 0x02;
static const uint8_t XL9555_REG_CONFIG0 = 0x06;
static const uint8_t XL9555_LCD_RST = 0x02;
static const uint8_t XL9555_BL_CTR = 0x08;
static uint8_t xl9555Output0 = 0xFF;
static uint8_t xl9555Config0 = 0xFF;

static const uint8_t ST7796_VENDOR_INIT[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x01,
    END_WRITE,
    DELAY, 150,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
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
    WRITE_COMMAND_8, 0x21,
    WRITE_COMMAND_8, 0x29,
    WRITE_COMMAND_8, 0x2C,
    END_WRITE,
    DELAY, 20,
};

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
    38 /* DC/RS */, 39 /* CS */, 45 /* WR */, 14 /* RD */,
    13 /* D0 */, 12 /* D1 */, 11 /* D2 */, 10 /* D3 */,
    9 /* D4 */, 46 /* D5 */, 3 /* D6 */, 8 /* D7 */,
    18 /* D8 */, 17 /* D9 */, 16 /* D10 */, 15 /* D11 */,
    7 /* D12 */, 6 /* D13 */, 5 /* D14 */, 4 /* D15 */);
static Arduino_GFX *gfx = new Arduino_ST7796_SplitAddress(
    bus, GFX_NOT_DEFINED /* RST */, 1 /* landscape */, true /* IPS */);

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
    Serial.println("XL9555 not found");
    return;
  }

  xl9555ReadReg(XL9555_REG_OUTPUT0, xl9555Output0);
  if (!xl9555SetPort0Outputs(XL9555_LCD_RST | XL9555_BL_CTR)) {
    Serial.println("XL9555 config failed");
    return;
  }

  xl9555WritePort0Bits(XL9555_LCD_RST | XL9555_BL_CTR, XL9555_LCD_RST | XL9555_BL_CTR);
  delay(5);
  xl9555WritePort0Bits(XL9555_LCD_RST, 0);
  delay(20);
  xl9555WritePort0Bits(XL9555_LCD_RST | XL9555_BL_CTR, XL9555_LCD_RST | XL9555_BL_CTR);
  delay(150);
  Serial.println("LCD reset via XL9555");
}

static void showColor(uint16_t color, const char *label, uint16_t textColor) {
  gfx->fillScreen(color);
  gfx->setCursor(24, 42);
  gfx->setTextColor(textColor);
  gfx->setTextSize(4);
  gfx->print(label);
  gfx->setCursor(24, 96);
  gfx->setTextSize(2);
  gfx->print("ST7796 split address test");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting ST7796 LCD self-test...");

  initDisplayExpander();
  if (!gfx->begin(LCD_BUS_SPEED)) {
    Serial.println("gfx begin failed");
    return;
  }

  bus->batchOperation(ST7796_VENDOR_INIT, sizeof(ST7796_VENDOR_INIT));
  gfx->setRotation(1);
  Serial.print("LCD size: ");
  Serial.print(gfx->width());
  Serial.print("x");
  Serial.println(gfx->height());
}

void loop() {
  showColor(RGB565_RED, "RED", RGB565_WHITE);
  delay(1200);
  showColor(RGB565_GREEN, "GREEN", RGB565_BLACK);
  delay(1200);
  showColor(RGB565_BLUE, "BLUE", RGB565_WHITE);
  delay(1200);
  showColor(RGB565_WHITE, "WHITE", RGB565_BLACK);
  delay(1200);
  showColor(RGB565_BLACK, "BLACK", RGB565_WHITE);
  delay(1200);
}
