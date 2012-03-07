#ifndef HT1632_H
#define HT1632_H

#include <stdint.h>

#define HT1632_XY(x,y)  (((7 - ((x) % 8)) << 2) + (3 - (y)) + (((x) / 8) << 5))

#define HT1632_SYSDIS   0x0
#define HT1632_SYSEN    0x1
#define HT1632_LEDOFF   0x2
#define HT1632_LEDON    0x3
#define HT1632_BLINKOFF 0x8
#define HT1632_BLINKON  0x9
#define HT1632_SLAVE    0x10
#define HT1632_MASTER   0x14
#define HT1632_RC       0x18
#define HT1632_EXTCLK   0x1C
#define HT1632_C16PMOS  0x2C
#define HT1632_SETPWM   0xA0

void ht1632_init(void);
void ht1632_command(uint8_t cmd);
void ht1632_write(uint8_t addr, uint8_t data);
void ht1632_write8(uint8_t addr, uint8_t data);

#endif
