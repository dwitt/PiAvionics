/*
  Backup_Display.ino - Backup display, flight page prototype (bench, no CAN yet)
  Hardware: Adafruit Feather RP2040 + Adafruit I2C/SPI LCD backpack (MCP23008)
            + Newhaven NHD-0420DL-FL-GBW 20x4 character LCD
  Core:     Earle Philhower "Raspberry Pi Pico/RP2040" (board: Adafruit Feather RP2040)
  Library:  Adafruit LiquidCrystal

  The flight page, with simulated data:
      IAS 105   ALT  4520
      HHH VS +500 HDG 270
      HHH OAT 12C  G 1.0
      PG 1  IAGE QNH 29.92
  HHH is the mini horizon, 3 x 2 character cells (15 x 16 pixels) drawn with 6 of the 8 custom characters.
  Slot 6 is kept for the solid "adjusting" marker and slot 7 is spare.

  USB serial commands (115200) to check the horizon:
      P <deg>  hold the pitch (positive = nose up)      R <deg>  hold the roll (positive = right wing down)
      C <khz>  I2C clock in kHz (default 100)
      A        back to the moving simulation
      U <ms>   horizon update period in ms (default 100, 0 = as fast as the display allows)
      S <hz>   sweep the roll +/-45 degrees and the pitch +/-10 degrees at this many cycles per second (0 = off)
  Once a second the serial port shows how many horizon updates were made and the mean time of one.
*/

#include <Wire.h>
#include <Adafruit_LiquidCrystal.h>

#define VERSION "1.0.2"

const uint8_t LCD_COLS = 20;
const uint8_t LCD_ROWS = 4;

// mini horizon
const uint8_t HZ_COL = 0;            // top left cell
const uint8_t HZ_ROW = 1;
const uint8_t HZ_CELLS_WIDE = 3;
const uint8_t HZ_CELLS_HIGH = 2;
const int HZ_WIDTH_PX = HZ_CELLS_WIDE * 5;
const int HZ_HEIGHT_PX = HZ_CELLS_HIGH * 8;
const float HZ_PX_PER_DEG = 0.5f;    // pitch scale: the full height shows about +/- 16 degrees
const float HZ_MAX_ROLL_DEG = 60.0f;

Adafruit_LiquidCrystal lcd(0);       // I2C address 0x20 + 0 (A0-A2 not jumpered)

// Write a fixed-width string at col,row (pads/truncates, avoids clear() flicker)
void printAt(uint8_t col, uint8_t row, const char *s, uint8_t width) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-*.*s", width, width, s);
  lcd.setCursor(col, row);
  lcd.print(buf);
}

// Draw the horizon into the six custom characters. Ground is filled, sky is blank.
// pitchDeg: positive = nose up (the horizon moves down). rollDeg: positive = right wing down (the right end rises).
void horizonUpdate(float pitchDeg, float rollDeg) {
  static uint8_t shown[HZ_CELLS_WIDE * HZ_CELLS_HIGH][8];
  static bool first = true;

  rollDeg = constrain(rollDeg, -HZ_MAX_ROLL_DEG, HZ_MAX_ROLL_DEG);
  const float slope = tanf(rollDeg * (float)M_PI / 180.0f);
  const float cx = (HZ_WIDTH_PX - 1) / 2.0f;
  const float cy = (HZ_HEIGHT_PX - 1) / 2.0f + pitchDeg * HZ_PX_PER_DEG;

  for (uint8_t cell = 0; cell < HZ_CELLS_WIDE * HZ_CELLS_HIGH; cell++) {
    const int cellX = (cell % HZ_CELLS_WIDE) * 5;
    const int cellY = (cell / HZ_CELLS_WIDE) * 8;
    uint8_t glyph[8];
    for (uint8_t row = 0; row < 8; row++) {
      uint8_t bits = 0;
      for (uint8_t col = 0; col < 5; col++) {
        const float horizonY = cy - slope * ((cellX + col) - cx);
        if ((cellY + row) > horizonY) bits |= 1 << (4 - col);
      }
      glyph[row] = bits;
    }
    if (first || memcmp(glyph, shown[cell], 8) != 0) {
      lcd.createChar(cell, glyph);
      memcpy(shown[cell], glyph, 8);
    }
  }
  first = false;
}

// Put the six horizon characters on the screen (once, after clear)
void horizonDraw() {
  for (uint8_t r = 0; r < HZ_CELLS_HIGH; r++) {
    lcd.setCursor(HZ_COL, HZ_ROW + r);
    for (uint8_t c = 0; c < HZ_CELLS_WIDE; c++) lcd.write((uint8_t)(r * HZ_CELLS_WIDE + c));
  }
}

bool holdPitch = false, holdRoll = false;
unsigned long horizonPeriodMs = 100;
float sweepHz = 0;
float heldPitch = 0, heldRoll = 0;

void serialCommands() {
  while (Serial.available()) {
    char cmd = toupper(Serial.read());
    if (cmd == 'P') { heldPitch = Serial.parseFloat(); holdPitch = true; }
    else if (cmd == 'R') { heldRoll = Serial.parseFloat(); holdRoll = true; }
    else if (cmd == 'U') { horizonPeriodMs = max(0L, Serial.parseInt()); }
    else if (cmd == 'S') { sweepHz = max(0.0f, Serial.parseFloat()); holdPitch = holdRoll = false; }
    else if (cmd == 'C') { Wire.setClock(1000L * Serial.parseInt()); }
    else if (cmd == 'A') { holdPitch = holdRoll = false; sweepHz = 0; horizonPeriodMs = 100; }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}   // wait briefly for USB serial
  Serial.print("Backup_Display ");
  Serial.println(VERSION);

  Wire.begin();
  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    Serial.println("LCD backpack not found - check wiring/address");
    while (1) delay(100);
  }
  lcd.setBacklight(HIGH);
  lcd.clear();
  horizonUpdate(0, 0);
  horizonDraw();
}

void loop() {
  static unsigned long lastText = 0, lastHorizon = 0, lastReport = 0;
  static unsigned long updates = 0, updateMicros = 0;
  const unsigned long now = millis();
  serialCommands();

  const float t = now / 1000.0f;
  if (now - lastHorizon >= horizonPeriodMs) {
    lastHorizon = now;
    float pitch = holdPitch ? heldPitch : 8.0f * sin(t * 0.4f);
    float roll  = holdRoll  ? heldRoll  : 35.0f * sin(t * 0.25f);
    if (sweepHz > 0) {
      const float phase = 2.0f * (float)M_PI * sweepHz * t;
      roll = 45.0f * sin(phase);
      pitch = 10.0f * sin(phase * 0.5f);
    }
    const unsigned long start = micros();
    horizonUpdate(pitch, roll);
    updateMicros += micros() - start;
    updates++;
  }

  if (now - lastReport >= 1000) {
    Serial.printf("%lu horizon updates/s, %lu us each\n", updates, updates ? updateMicros / updates : 0);
    updates = updateMicros = 0;
    lastReport = now;
  }

  if (now - lastText < 100) return;            // the text rows refresh at 10 Hz
  lastText = now;

  // Simulated data
  const int ias = 105 + (int)(8 * sin(t * 0.5f));
  const int alt = 4520 + (int)(150 * sin(t * 0.1f));
  const int vs  = (int)(500 * cos(t * 0.1f));
  const int hdg = ((int)(t * 6) + 270) % 360;
  const int oat = 12;
  const float g = 1.0f + 0.1f * sin(t * 0.7f);

  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "IAS %3d   ALT %5d", ias, alt);      printAt(0, 0, buf, 20);
  snprintf(buf, sizeof(buf), " VS %+5d HDG %03d", vs, hdg);       printAt(3, 1, buf, 17);
  snprintf(buf, sizeof(buf), " OAT %2dC  G %.1f", oat, g);        printAt(3, 2, buf, 17);
  printAt(0, 3, "PG 1  IAGE QNH 29.92", 20);
}
