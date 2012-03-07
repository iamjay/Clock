#include <msp430.h>

#include <stdint.h>

#include "ht1632.h"

#define delay(x)        __delay_cycles(x)

/* Display */
static const uint8_t font[][4] = {
        { 0x3E, 0x41, 0x41, 0x3E },
        { 0x00, 0x20, 0x7F, 0x00 },
        { 0x27, 0x49, 0x49, 0x31 },
        { 0x41, 0x49, 0x49, 0x36 },
        { 0x78, 0x08, 0x08, 0x7F },
        { 0x79, 0x49, 0x49, 0x46 },
        { 0x3E, 0x49, 0x49, 0x26 },
        { 0x40, 0x40, 0x40, 0x7F },
        { 0x36, 0x49, 0x49, 0x36 },
        { 0x31, 0x49, 0x49, 0x3E },
        { 0x00, 0x00, 0x00, 0x00 }, /*   */
        { 0x00, 0x36, 0x36, 0x00 }, /* : */
        { 0x20, 0x0E, 0x11, 0x11 }, /* c */
        { 0x09, 0x02, 0x04, 0x09 }, /* % */
};

static void clear_display(void)
{
    uint16_t addr;

    for (addr = 0; addr < 96; addr += 2)
        ht1632_write8(addr, 0);
}

static void draw_char(uint8_t x, uint8_t y, uint8_t ch)
{
    uint8_t i;

    for (i = 0; i < 4; ++i, ++x)
        ht1632_write8(HT1632_XY(x, y + 1), font[ch][i]);
}

static void draw_2digit(uint8_t x, uint8_t val, uint8_t blink)
{
    if (blink) {
        draw_char(x, 2, 10);
        draw_char(x + 5, 2, 10);
    } else {
        draw_char(x, 2, (val & 0xF0) >> 4);
        draw_char(x + 5, 2, (val & 0x0F));
    }
}

/* SHT11 */
#define SHT11_SDA       BIT2    /* P1.2 */
#define SHT11_SCL       BIT0    /* P1.0 */

#define SHT11_PxDIR     P1DIR
#define SHT11_PxIN      P1IN
#define SHT11_PxOUT     P1OUT

#define SDA_0           (SHT11_PxDIR |= SHT11_SDA)
#define SDA_1           (SHT11_PxDIR &= ~SHT11_SDA)
#define SDA_IS_1        (SHT11_PxIN & SHT11_SDA)
#define SCL_0           (SHT11_PxOUT &= ~SHT11_SCL)
#define SCL_1           (SHT11_PxOUT |= SHT11_SCL)

#define MEASURE_TEMP    0x03
#define MEASURE_HUMI    0x05

static void sht11_start(void)
{
    SDA_1; SCL_0;
    delay(1);
    SCL_1;
    delay(1);
    SDA_0;
    delay(1);
    SCL_0;
    delay(1);
    SCL_1;
    delay(1);
    SDA_1;
    delay(1);
    SCL_0;
}

#if 0
static void sht11_reset(void)
{
    uint8_t i;

    SDA_1;
    SCL_0;
    for (i = 0; i < 9 ; i++) {
        SCL_1;
        delay(1);
        SCL_0;
    }
    sht11_start();
}
#endif

static uint8_t sht11_write(uint8_t c)
{
    uint8_t i;

    for (i = 0; i < 8; i++, c <<= 1) {
        if (c & 0x80)
            SDA_1;
        else
            SDA_0;
        SCL_1;
        delay(1);
        SCL_0;
    }

    SDA_1;
    SCL_1;
    delay(1);
    i = !SDA_IS_1;

    SCL_0;

    return i;
}

static unsigned sht11_read(int send_ack)
{
    uint8_t i;
    uint8_t c = 0x00;

    SDA_1;
    for (i = 0; i < 8; i++) {
        c <<= 1;
        SCL_1;
        delay(1);
        if (SDA_IS_1)
            c |= 0x1;
        SCL_0;
    }

    if (send_ack)
        SDA_0;

    SCL_1;
    delay(1);
    SCL_0;

    SDA_1;

    return c;
}

static unsigned int sht11_read_val()
{
    uint8_t t0, t1;

    t0 = sht11_read(1);
    t1 = sht11_read(0);

    return (t0 << 8) | t1;
}

static uint8_t sht11_cmd(unsigned cmd)
{
    sht11_start();
    return sht11_write(cmd);
}

static void sht11_init(void)
{
    SHT11_PxOUT &= ~(SHT11_SDA | SHT11_SCL);
    SHT11_PxDIR |= SHT11_SCL;
}

/* Timer */
static volatile uint8_t ticks = 128; /* 128 ticks = 1 seconds */
static volatile uint8_t pending_secs = 0;

#pragma vector=TIMERA0_VECTOR
__interrupt void timer_a_isr(void)
{
    if (--ticks == 0) {
        ++pending_secs;
        ticks = 128;
    }
    _bic_SR_register_on_exit(LPM3_bits);
}

static void setup_timer(void)
{
    BCSCTL1 = DIVA0 | DIVA1 | CALBC1_1MHZ; /* ACLK / 8 */
    DCOCTL = CALDCO_1MHZ; /* Calibrated 1 MHz DCO */
    BCSCTL3 = XCAP_3; /* Use external 32768 Hz for ACLK */

    delay(100);

    TACCR0 = 32 - 1; /* 1 second = 4096 / 32 = 128 timer ticks */
    TACCTL0 = CCIE;
    TACTL = TASSEL0 | MC0; /* ACLK, Up mode */

    _bis_SR_register(GIE);
}

/* Buttons */
#define BUTTON1         BIT7
#define BUTTON2         BIT4

static uint8_t buttons = 0;
static uint8_t buttons_flag = 0;
static uint8_t button1_hold;
static uint8_t button2_hold;

static void check_buttons(void)
{
    static uint8_t debounce = 0;
    static uint8_t prev_data = 0;
    uint8_t data = P1IN & (BUTTON1 | BUTTON2);

    if (data == prev_data) {
        if (debounce > 0) {
            if (--debounce == 0) {
                buttons_flag = buttons ^ data;
                buttons = data;

                if (buttons_flag & BUTTON1)
                    button1_hold = 0xff;
                if (buttons_flag & BUTTON2)
                    button2_hold = 0xff;
            }
        } else {
            if ((buttons & BUTTON1) && button1_hold)
                --button1_hold;
            if ((buttons & BUTTON2) && button2_hold)
                --button2_hold;
        }
    } else {
        debounce = 4; /* Debounce for 4 * 7.8125 ms */
        prev_data = data;
    }
}

static void setup_ports(void)
{
    P1DIR &= ~(BUTTON1 | BUTTON2);
    P1OUT &= ~(BUTTON1 | BUTTON2);
    P1REN |= (BUTTON1 | BUTTON2);

    P1DIR |= BIT3;
}

/* main */
#define STATE_NORMAL            0
#define STATE_SET_HOUR          1
#define STATE_SET_MINUTE        2

#define BLINK_NONE              BIT0
#define BLINK_HOUR              BIT1
#define BLINK_MINUTE            BIT2

static union {
    uint8_t value;
    struct {
        unsigned need_refresh:1;
        unsigned sensor_type:1; /* 0: Temperature, 1: Humidity */
        unsigned read_pending:1;
    } bit;
} flags = { 0 };

static uint8_t state = STATE_NORMAL;
static uint8_t old_ticks;
static uint8_t blink_digit = 0;
static uint8_t sec = 0;
static uint8_t min = 0;
static uint8_t hour = 0;
static uint8_t brightness = 7;
static unsigned int sensor_value = 0;

static uint8_t add_one_and_check(uint8_t *val, uint8_t check_val)
{
    *val = _bcd_add_short(*val, 1);
    if (*val >= check_val) {
        *val = 0;
        return 1;
    }
    return 0;
}

static void set_blink_bit(uint8_t mask)
{
    uint8_t old_blink = blink_digit;

    if ((old_ticks >> 4) & BIT0)
        blink_digit |= mask;
    else
        blink_digit &= ~mask;
    if ((old_blink ^ blink_digit) & mask)
        flags.bit.need_refresh = 1;
}

void main(void)
{
    WDTCTL = WDTPW + WDTHOLD;

    setup_ports();
    setup_timer();

    ht1632_init();
    clear_display();

    sht11_init();

    while (1) {
        buttons_flag = 0;

        while (pending_secs) {
            --pending_secs;
            if (add_one_and_check(&sec, 0x60)) {
                if (add_one_and_check(&min, 0x60))
                    add_one_and_check(&hour, 0x24);
            }

            if (flags.bit.read_pending) {
                if (flags.bit.sensor_type) {
                    sensor_value = sht11_read_val();
                    sensor_value -= 3960;

                    flags.bit.sensor_type = 0;
                } else {
                    /* rh_lin = 367 * raw / 100 - ((4 * raw) / 100) ^ 2 / 10 - 204 */
                    /* rh_true = (t - 2500) * (1 + (8 * raw) / 1000) + rh_lin * 100 */

                    int tmp1;
                    unsigned int tmp2;

                    tmp1 = sensor_value - 2500;

                    sensor_value = sht11_read_val();

                    tmp2 = 8 * sensor_value;
                    tmp2 /= 1000;
                    tmp2 += 1;
                    tmp1 /= 100;
                    tmp1 *= tmp2;

                    tmp2 = sensor_value / 100;
                    tmp2 *= 367;

                    sensor_value *= 4;
                    sensor_value /= 100;
                    sensor_value = sensor_value * sensor_value;
                    sensor_value /= 10;

                    sensor_value = tmp2 - sensor_value - 204;
                    sensor_value += tmp1;

                    flags.bit.sensor_type = 1;
                }
                flags.bit.read_pending = 0;
            } else {
                if (flags.bit.sensor_type)
                    sht11_cmd(MEASURE_TEMP);
                else
                    sht11_cmd(MEASURE_HUMI);
                flags.bit.read_pending = 1;
            }

            flags.bit.need_refresh = 1;
        }

        if (old_ticks != ticks) {
            old_ticks = ticks;

            check_buttons();
        }

        switch (state) {
        case STATE_NORMAL:
            if (buttons_flag & BUTTON2) {
                if (buttons & BUTTON2) {
                    if (++brightness > 15)
                        brightness = 0;
                    ht1632_command(HT1632_SETPWM + brightness);
                }
            } else if (button1_hold < 255 - 192) {
                state = STATE_SET_HOUR;
                ht1632_command(HT1632_BLINKOFF);
            }
            break;
        case STATE_SET_HOUR:
            if (buttons_flag & BUTTON1) {
                if (buttons & BUTTON1) {
                    state = STATE_SET_MINUTE;
                    blink_digit &= ~BLINK_HOUR;
                    flags.bit.need_refresh = 1;
                    break;
                }
            } else if (buttons_flag & BUTTON2) {
                if (buttons & BUTTON2)
                    add_one_and_check(&hour, 0x24);
            } else if (button2_hold < 255 - 128) {
                add_one_and_check(&hour, 0x24);
                button2_hold = 255 - (128 - 64);
            }
            set_blink_bit(BLINK_HOUR);
            break;
        case STATE_SET_MINUTE:
            if (buttons_flag & BUTTON1) {
                if (buttons & BUTTON1) {
                    state = STATE_NORMAL;
                    blink_digit &= ~BLINK_MINUTE;
                    flags.bit.need_refresh = 1;
                    break;
                }
            } else if (buttons_flag & BUTTON2) {
                if (buttons & BUTTON2)
                    add_one_and_check(&min, 0x60);
            } else if (button2_hold < 255 - 128) {
                add_one_and_check(&min, 0x60);
                button2_hold = 255 - (128 - 64);
            }
            set_blink_bit(BLINK_MINUTE);
            break;
        }

        if (flags.bit.need_refresh) {
            flags.bit.need_refresh = 0;

            draw_2digit(1, hour, blink_digit & BLINK_HOUR);
            draw_2digit(14, min, blink_digit & BLINK_MINUTE);

            if (sec & 0x01)
                draw_char(10, 2, 11);
            else
                draw_char(10, 2, 10);

            draw_char(2, 0, (sensor_value / 1000) % 10);
            draw_char(7, 0, (sensor_value / 100) % 10);
            ht1632_write8(HT1632_XY(12, 0 + 1), 0x01);
            draw_char(14, 0, ((sensor_value / 10) % 10));
            if (flags.bit.sensor_type)
                draw_char(19, 0, 13); /* % */
            else
                draw_char(19, 0, 12); /* c */
        }
        _bis_SR_register(LPM3_bits + GIE);
    }
}
