/*
  lcd_test.ino - Backup display bench test (no CAN)
  Hardware: Adafruit Feather RP2040 + Adafruit I2C/SPI LCD backpack (MCP23008)
            + HM2004A 20x4 HD44780 LCD
  Core:     Earle Philhower "Raspberry Pi Pico/RP2040" (board: Adafruit Feather RP2040)
  Library:  Adafruit LiquidCrystal (Library Manager)

  1. Scans I2C and reports to Serial (expect 0x20 with no address jumpers).
  2. Pixel test: all 80 cells solid block (check contrast / dead segments).
  3. Character test: shows ASCII range.
  4. Simulated EFIS page with changing fake data and a periodic "stale" field.
*/

#include <Wire.h>
#include <Adafruit_LiquidCrystal.h>

const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

Adafruit_LiquidCrystal lcd(0);   // I2C address 0x20 + 0 (A0-A2 not jumpered)

void i2cScan() {
  Serial.println("I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  none found - check wiring / power");
}

// Write a fixed-width string at col,row (pads/truncates, avoids clear() flicker)
void printAt(uint8_t col, uint8_t row, const char *s, uint8_t width) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-*.*s", width, width, s);
  lcd.setCursor(col, row);
  lcd.print(buf);
}

void pixelTest() {
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    lcd.setCursor(0, r);
    for (uint8_t c = 0; c < LCD_COLS; c++) lcd.write((uint8_t)0xFF);  // full block
  }
  delay(2000);
}

void charTest() {
  lcd.clear();
  uint8_t ch = 0x20;
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    lcd.setCursor(0, r);
    for (uint8_t c = 0; c < LCD_COLS; c++) lcd.write(ch++);
  }
  delay(3000);
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}   // wait briefly for USB serial

  Wire.begin();
  i2cScan();

  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    Serial.println("LCD backpack not found - check wiring/address");
    while (1) delay(100);
  }
  lcd.setBacklight(HIGH);

  lcd.clear();
  printAt(0, 0, "BACKUP DISPLAY TEST", 20);
  printAt(0, 1, "Feather RP2040", 20);
  printAt(0, 2, "HM2004A 20x4 I2C", 20);
  delay(2000);

  pixelTest();
  charTest();
  lcd.clear();
}

void loop() {
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();
  if (now - lastUpdate < 200) return;          // 5 Hz refresh
  lastUpdate = now;

  // Fake data, integers only (tenths where needed)
  const float t = now / 1000.0f;
  int ias   = 110 + (int)(8 * sin(t * 0.5f));
  int alt   = 4500 + (int)(150 * sin(t * 0.1f));
  int rpm   = 2400 + (int)(60 * sin(t * 0.8f));
  int oilP  = 60 + (int)(3 * sin(t * 0.3f));
  int oilT  = 185 + (int)(5 * sin(t * 0.05f));
  int volt10 = 138 + (int)(2 * sin(t * 0.7f));

  // Simulate a stale sensor: oil pressure drops out 3 s of every 15 s
  bool oilPStale = ((now / 1000) % 15) >= 12;

  char buf[21];

  snprintf(buf, sizeof(buf), "IAS %3dkt", ias);          printAt(0, 0, buf, 10);
  snprintf(buf, sizeof(buf), "ALT %5d", alt);            printAt(10, 0, buf, 10);

  snprintf(buf, sizeof(buf), "RPM %4d", rpm);            printAt(0, 1, buf, 10);
  if (oilPStale) snprintf(buf, sizeof(buf), "OILP ---");
  else           snprintf(buf, sizeof(buf), "OILP %3d", oilP);
  printAt(10, 1, buf, 10);

  snprintf(buf, sizeof(buf), "OILT %3dF", oilT);         printAt(0, 2, buf, 10);
  snprintf(buf, sizeof(buf), "V %2d.%d", volt10 / 10, volt10 % 10);
  printAt(10, 2, buf, 10);

  unsigned long s = now / 1000;
  snprintf(buf, sizeof(buf), "TEST  up %02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
  printAt(0, 3, buf, 20);
}
