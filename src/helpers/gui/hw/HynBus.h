#pragma once

// Bus helpers shared by the Hynitron probe (B1, #2108) and the cst66xx driver
// (B2, #2109). Hynitron register addresses go on the wire big-endian. Our own
// code: the register sequences are the chips' (see ../HynInfo.h).

#include <stddef.h>
#include <stdint.h>

#include <Wire.h>

#include "../HynInfo.h"

// Holds `rst` low for 10 ms, then waits 50 ms for the controller to boot.
// Nothing happens if `rst` is negative.
void hynReset(int rst);

// Writes a `reg_len`-byte register address (and nothing else). `stop` false
// leaves the bus held for a read after a repeated start.
bool hynSendReg(TwoWire& w, uint32_t reg, uint8_t reg_len, bool stop = true);

// Writes the register address, then reads `len` bytes after a repeated start.
bool hynReadReg(TwoWire& w, uint32_t reg, uint8_t reg_len, uint8_t* buf, size_t len);

// Reads `len` more bytes in a transaction of their own, as the chip expects
// for the rest of a report.
bool hynReadMore(TwoWire& w, uint8_t* buf, size_t len);

// cst66xx normal mode: the low-power I2C wake twice, then the mode writes.
void hyn66xxNormalMode(TwoWire& w);

// Puts a cst66xx in normal mode and reads its info block, up to 4 tries.
// `buf` gets the last block read (kCst66xxInfoLen bytes). True when a block
// parsed; `*read_ok` says whether the last read returned every byte.
bool hyn66xxReadInfo(TwoWire& w, uint8_t* buf, offband::gui::HynInfo* info, bool* read_ok);
