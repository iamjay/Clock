#include "ht1632.h"

#include <msp430.h>

#define HT1632_OP_READ  0x6
#define HT1632_OP_WR    0x5
#define HT1632_OP_CMD   0x4

static uint8_t const init_cmd[] = {
        HT1632_RC,
        HT1632_C16PMOS,
        HT1632_BLINKON,
        HT1632_SYSEN,
        HT1632_LEDON,
        HT1632_SETPWM + 7,
};

#define ht1632_cs()     P1OUT &= ~0x02
#define ht1632_cs_off() P1OUT |= 0x02

static void ht1632_command_array(uint8_t const *cmd, uint8_t len)
{
    ht1632_cs();

    USISR = (HT1632_OP_CMD << 13) + (*cmd << 5);
    USICNT = USI16B | 12;

    while (--len)
    {
        while ((USICTL1 & USIIFG) == 0)
            ;

        USISRH = *++cmd;
        USISRL = 0;
        USICNT = USI16B | 9;
    }

    while (0 == (USICTL1 & USIIFG))
        ;

    ht1632_cs_off();
}

void ht1632_init(void)
{
    P1DIR |= 0x02;
    ht1632_cs_off();

    USICTL0 = USIPE6 | USIPE5 | USIMST | USIOE | USISWRST;
    USICTL1 = 0;
    USICKCTL = USISSEL1 | USICKPL;
    USICNT = 0;
    USICTL0 &= ~USISWRST;

    ht1632_command_array(init_cmd, sizeof(init_cmd));
}

void ht1632_command(uint8_t cmd)
{
    ht1632_command_array(&cmd, 1);
}

#if 0
void ht1632_write(uint8_t addr, uint8_t data)
{
    ht1632_cs();

    USISR = (HT1632_OP_WR << 13) | ((uint16_t)(addr & 0x7F) << 6) | ((data & 0x0F) << 2);
    USICNT = USI16B | 14;

    while ((USICTL1 & USIIFG) == 0)
        ;

    ht1632_cs_off();
}
#endif

void ht1632_write8(uint8_t addr, uint8_t data)
{
    ht1632_cs();

    USISR = (HT1632_OP_WR << 13) | ((uint16_t)(addr & 0x7F) << 6);
    USICNT = USI16B | 10;

    while ((USICTL1 & USIIFG) == 0)
        ;

    USISRL = data;
    USICNT = 8;

    while ((USICTL1 & USIIFG) == 0)
        ;

    ht1632_cs_off();
}
