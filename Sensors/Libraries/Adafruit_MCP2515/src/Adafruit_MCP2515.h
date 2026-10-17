// Adapted for use with Adafruit_BusIO.
// Modified from original `MCP2515Class` class code:
// Copyright (c) Sandeep Mistry. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full
// license information.

#ifndef __ADAFRUIT_MCP2515_H__
#define __ADAFRUIT_MCP2515_H__

#include <Adafruit_SPIDevice.h>
#include <CANController.h>

#define MCP2515_DEFAULT_CLOCK_FREQUENCY 16e6

class Adafruit_MCP2515 : public CANControllerClass {

public:
  Adafruit_MCP2515(int8_t cspin, SPIClass *theSPI = &SPI);
  Adafruit_MCP2515(int8_t cspin, int8_t mosipin, int8_t misopin, int8_t sckpin);
  virtual ~Adafruit_MCP2515();

  virtual int begin(long baudRate);
  virtual void end();

  virtual int endPacket();

  virtual int parsePacket();

  virtual void onReceive(int intPin, void (*callback)(int));

  using CANControllerClass::filter;
  virtual int filter(int id, int mask);
  using CANControllerClass::filterExtended;
  virtual int filterExtended(long id, long mask);

  virtual int observe();
  virtual int loopback();
  virtual int sleep();
  virtual int wakeup();

  void setClockFrequency(long clockFrequency);

  // LOCAL PATCH (PiAvionics): bounded transmit, see PATCH_NOTES.md.
  // endPacket() gives up after the timeout instead of waiting for ever when no
  // node acknowledges the frame (for example a lone, unterminated module). After a
  // timeout, sends are skipped (return 0) for TX_RETRY_MS, then one frame is
  // tried again; the first frame that completes clears the "down" state.
  void setTxTimeoutMs(uint16_t ms) { _txTimeoutMs = ms; }
  bool txDown() const { return _txDown; }            ///< true while sends are failing
  uint32_t txTimeouts() const { return _txTimeouts; }   ///< frames abandoned
  uint32_t txSuppressed() const { return _txSuppressed; } ///< frames skipped while down
  uint8_t txErrorCount();   ///< MCP2515 TEC register
  uint8_t rxErrorCount();   ///< MCP2515 REC register
  uint8_t errorFlags();     ///< MCP2515 EFLG register
  /// LOCAL PATCH: true if our frames are not getting through: sends are being abandoned (txDown), or
  /// the chip's transmit error counter is high (EFLG TXWAR/TXEP/TXBO). A terminated module with its cable
  /// pulled never times out but its error counter climbs to 128. Reads EFLG at most every 200 ms.
  bool txFault();

  void dumpRegisters(Stream &out);

private:
  void reset();

  void handleInterrupt();

  uint8_t readRegister(uint8_t address);
  void modifyRegister(uint8_t address, uint8_t mask, uint8_t value);
  void writeRegister(uint8_t address, uint8_t value);

  static Adafruit_MCP2515 *instance;
  static void onInterrupt();

  Adafruit_SPIDevice *spi_dev = NULL; ///< Pointer to SPI bus interface
  long _clockFrequency;

  // LOCAL PATCH (PiAvionics)
  uint16_t _txTimeoutMs = 10;
  bool _txDown = false;
  uint32_t _txRetryAt = 0;
  uint32_t _txTimeouts = 0;
  uint32_t _txSuppressed = 0;
};

#endif
