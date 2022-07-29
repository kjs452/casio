/*
 * Casio Calculator Watch for Teensy 4.1
 *
 * Circuit:
 *  * 4 push buttons: pulled low. Attached to digitial pins 12, 11, 10, 9
 *  * Hex keypad: 4 wires go into output digital pins HKA, HKB, HKC, HKC,
 *              4 wires go into digitial input pins HK1, HK2, HK3, HK4
 *  * piezo buzzor. digitial output pin (pin 24) goes through a resitor and into buzzer positive terminal
 *              ground terminal.
 *  * 128x64 oled display, hooked up to I2C lines SDA (pin 18)/SCL (pin 19)
 *          pull up resistors are on each pin
 *      The I2C slave address of the display is TARGET (0x3C).
 *
 * Code Description:
 * This program is divided into these sections:
 *  Disp routines       - Talk to the OLED display, send frame buffer to display, set pixels
 *  Debug routines      - flash led light debug routines
 *  Device routines     - talk to all the connected devices and interrupt service routines
 *  Draw routines       - fonts and other graphic operations
 *  Casio routines      - emmulate the casio calculator watch
 *  main
 *
 * Dependencies:
 *  - The 'IntervalTimer' class is used to provide the 1/100 second time signal.
 *  - The 'Wire' class is used to talk to the display using I2C protocol
 *  - tone() to generate casio watch sounds
 *
 */

#include <Arduino.h>
#include <Wire.h>
#include <stdlib.h>

//////////////////////////////////////////////////////////////////////
//
// begin DISP
//
//////////////////////////////////////////////////////////////////////
const int TARGET = 0x3C;        // I2C slave address for display
#define FRAME_SIZE 1024         // # of bytes to contain a frame    128x64

#define BLACK_ON_WHITE yes

#ifdef BLACK_ON_WHITE
#   define CLR_MASK         0xff
#   define PIXEL_ON(p)      ((p) == 0)
#   define PIXEL_VALUE_ON   0
#   define PIXEL_VALUE_OFF  1
#else
#   define CLR_MASK         0x00
#   define PIXEL_ON(p)      ((p) != 0)
#   define PIXEL_VALUE_ON   1
#   define PIXEL_VALUE_OFF  0
#endif

void disp_setup()
{
    Wire.setSDA(18);
    Wire.setSCL(19);
    Wire.begin();
    Wire.setClock(400000);  // overwritten by begin()
}

int disp_init()
{
    /*
        Set MUX Ratio [$A8, $3F]
        Set display offset [$D3, $00]
        Set start line [$40]
        Set segment re-map $A0 / $A1
        Set COM output scan direction $C0 / $C8
        Set COM pin hardware configuration [$DA, $02]
        Set contrast [$81, $7F]
        Resume the display $A4
        Set Oscillator frequency [$D5, $80]
        Enable charge pump [$8D, $14]
        Turn the display on $AF
    */

    const char databuf[] = {
            0x00,
            0xae,               // display off
            0xa8, 0x3f,
            0xd3, 0x00,
            0x40,
            0xa0,
            0xc0,
//          0xda, 0x02,     // BAD: introduces skipped lines BAD. Use 0x12 to not have skipped lines
            0xda, 0x12,     // KJS testing line skip        Shit this was huge, fixed skip lines
            0x81, 0x7f,
            0xa4,
            0xd5, 0x80,
            0x8d, 0x14,
            0x20, 0x00,             // set adreessing mode: Horizontal
            0x2e,               // scroll off, kjs testing
            0xaf,
    };
    int datalen = sizeof(databuf);
    int len;

    // Transmit to Slave
    Wire.beginTransmission(TARGET);   // Slave address
    len = Wire.write(databuf, datalen); // Write string to I2C Tx buffer
    Wire.endTransmission();           // Transmit to Slave

    // Check if error occured
    if( len != datalen )
    {
        return -1;
    } else {
        return 0;
    }
}

int disp_set_contrast(int x)
{
    char buf[] = {
            0x00,
            0x81,
            0x00,
    };
    int len, rc;

    Wire.beginTransmission(TARGET);
    buf[2] = (char)x;
    len = Wire.write(buf, sizeof(buf));
    rc = Wire.endTransmission();

    if( len != 1 ) {
        return len;
    }

    return rc;
}

int disp_set_range()
{
    char buf[] = {
            0x00,
            0x21, 0x00, 0x7f,       // set column start/end range  (0-127)
            0x22, 0x00, 0x07,       // set page start/end range (0-7)
    };
    int len, rc;

    Wire.beginTransmission(TARGET);
    len = Wire.write(buf, sizeof(buf));
    rc = Wire.endTransmission();

    if( len != sizeof(buf)) {
        return len;
    }

    return rc;
}

int disp_update(const char *frame)
{
    const char buf[] = {
        0x40,
    };
    int i, len, err, rc;
    const char *p;

    err = 0;

    p = frame;
    for(i=0; i < FRAME_SIZE/128; i++)
    {
        Wire.beginTransmission(TARGET);

        len = Wire.write(buf, sizeof(buf));
        if( len != sizeof(buf) )
        {
            err = 2000 + len;
            break;
        }

        len = Wire.write(p, 128);
        if( len != 128 )
        {
            err = 2000 + len;
            break;
        }

        rc = Wire.endTransmission();
        if( rc )
        {
            err = 3000 + rc;
            break;
        }

        p += 128;
    }

    return err;
}

void disp_clear()
{
    static int init = 0;
    static char blank[FRAME_SIZE];

    if( init == 0 )
    {
        memset(blank, CLR_MASK, sizeof(blank));
        init = 1;
    }
    disp_update(blank);
}

void disp_pset(char *frame, int x, int y, int p)
{
    const int WIDTH =   128;
//  const int HEIGHT =   64;

    if( PIXEL_ON(p) )
    {
        frame[ x + (y>>3) * WIDTH ] |= (1 << (y % 8));
    }
    else
    {
        frame[ x + (y>>3) * WIDTH ] &= ~(1 << (y % 8));
    }
}

int disp_pget(char *frame, int x, int y)
{
    const int WIDTH =   128;
//  const int HEIGHT =   64;
    int byte, mask;

    byte = frame[ x + (y>>3) * WIDTH ];
    mask = (1 << (y % 8));

    return (byte & mask) ? PIXEL_VALUE_ON : PIXEL_VALUE_OFF;
}

void disp_invert(char *frame)
{
    const int WIDTH =   128;
    const int HEIGHT =   64;
    int x, y, p;

    for(x=0; x < WIDTH; x++)
    {
        for(y=0; y < HEIGHT; y++)
        {
            p = disp_pget(frame, x, y);
            p = (p == 1) ? 0 : 1;
            disp_pset(frame, x, y, p);
        }
    }
}

//////////////////////////////////////////////////////////////////////
// end DISP
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
//
// begin DEBUG
//
//////////////////////////////////////////////////////////////////////
void debug_flash(int rc)
{
    int i, N;

    if( rc == 0 )
        N = 3;
    else
        N = 30;

    for(i=0; i < N; i++)
    {
        digitalWriteFast(13, HIGH);
        delay(20);
        digitalWriteFast(13, LOW);
        delay(20);
    }
}

void debug_halt()
{
    digitalWriteFast(13, HIGH);
    for(;;);
}
//////////////////////////////////////////////////////////////////////
//
// end DEBUG
//
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
//
// begin DEVICE
//
//////////////////////////////////////////////////////////////////////

typedef enum {
    E_NONE,
    E_BUTTONA,
    E_BUTTONL,
    E_BUTTONB,
    E_BUTTONC,
    E_BUTTONA_RELEASE,
    E_BUTTONL_RELEASE,
    E_BUTTONB_RELEASE,
    E_BUTTONC_RELEASE,
    E_HEX_BUTTON_0,     // 0
    E_HEX_BUTTON_1,     // 1
    E_HEX_BUTTON_2,     // 2
    E_HEX_BUTTON_3,     // 3
    E_HEX_BUTTON_4,     // 4
    E_HEX_BUTTON_5,     // 5
    E_HEX_BUTTON_6,     // 6
    E_HEX_BUTTON_7,     // 7
    E_HEX_BUTTON_8,     // 8
    E_HEX_BUTTON_9,     // 9
    E_HEX_BUTTON_A,     // A
    E_HEX_BUTTON_B,     // B
    E_HEX_BUTTON_C,     // C
    E_HEX_BUTTON_D,     // D
    E_HEX_BUTTON_STAR,  // *
    E_HEX_BUTTON_POUND, // #
    E_HEX_BUTTON_0_RELEASE,     // 0
    E_HEX_BUTTON_1_RELEASE,     // 1
    E_HEX_BUTTON_2_RELEASE,     // 2
    E_HEX_BUTTON_3_RELEASE,     // 3
    E_HEX_BUTTON_4_RELEASE,     // 4
    E_HEX_BUTTON_5_RELEASE,     // 5
    E_HEX_BUTTON_6_RELEASE,     // 6
    E_HEX_BUTTON_7_RELEASE,     // 7
    E_HEX_BUTTON_8_RELEASE,     // 8
    E_HEX_BUTTON_9_RELEASE,     // 9
    E_HEX_BUTTON_A_RELEASE,     // A
    E_HEX_BUTTON_B_RELEASE,     // B
    E_HEX_BUTTON_C_RELEASE,     // C
    E_HEX_BUTTON_D_RELEASE,     // D
    E_HEX_BUTTON_STAR_RELEASE,  // *
    E_HEX_BUTTON_POUND_RELEASE, // #
    E_SECONDS_TIMER,
    E_SECONDS15,            // 1/6th second     every 100/15th seconds
    E_LIGHT_OFF,            // triggered when DEVICE.light goes to 0
} EVENT;

static volatile struct
{
    IntervalTimer it;
    long clock;             // 1/100th of seconds counter
    long epoch;             // seconds since jan 1, 1970
    int xxx;                // push button state
    int hex;                // hex button key press
    int hex_row;            // key keypad scan row
    int counter100;         // counter 1/100th second
    int counter15;          // counter 1/6.6th second
    int counter15_enable;   // enable E_SECOND15 event
    int light;
} DEVICE;

void isr_xxx1()
{
    DEVICE.xxx = 0x01;
}

void isr_xxx2()
{
    DEVICE.xxx = 0x02;
}

void isr_xxx3()
{
    DEVICE.xxx = 0x04;
}

void isr_xxx4()
{
    DEVICE.xxx = 0x08;
}

//////////////////////////////////////////////////////////////////////
// hex keypad
//
//  pins 25
//       26
//       27
//       28
//
//       29
//       30
//       31
//       32
//
//           1  2  3  4
//      A    1  2  3  A
//      B    4  5  6  B
//      C    7  8  9  C
//      D    *  0  #  D
//

#define HK1     25
#define HK2     26
#define HK3     27
#define HK4     28

#define HKA     29
#define HKB     30
#define HKC     31
#define HKD     32

int PMAP(int hex)
{
    // i think this is just: "return HK1 + (hex % 10)-1;"

    switch(hex)
    {
        case  1: return HK1; // D
        case  2: return HK2; // #
        case  3: return HK3; // 0
        case  4: return HK4; // *

        case  11: return HK1; // C
        case  12: return HK2; // 9
        case  13: return HK3; // 8
        case  14: return HK4; // 7

        case  21: return HK1; // B
        case  22: return HK2; // 6
        case  23: return HK3; // 5
        case  24: return HK4; // 4

        case  31: return HK1; // A
        case  32: return HK2; // 3
        case  33: return HK3; // 2
        case  34: return HK4; // 1
    }
    return 0;
}

//
// trigger this on a timer, every 1/100th seconds.
// - Increments all the clock and timer variables.
// - Implements the hex keypad scan logic.
//
void isr_hex_scan()
{
    int v, pin;

    DEVICE.clock += 1;

    DEVICE.counter100 += 1;
    if( DEVICE.counter100 > 99 ) {
        DEVICE.counter100 = 0;
        DEVICE.epoch += 1;
    }

    DEVICE.counter15 += 1;
    if( DEVICE.counter15 > 14 ) {
        DEVICE.counter15 = 0;
    }

    if( DEVICE.light > 0 ) {
        DEVICE.light -= 1;
    }

    if( DEVICE.hex != 0 ) {
        v = digitalRead( PMAP(DEVICE.hex) );
        if( v != 0 )
        {
            return;
        }
        else
        {
            DEVICE.hex = 0;
        }
    }

    if( DEVICE.xxx != 0 ) {
        switch(DEVICE.xxx) {
        case 0x01:  pin = 12; break;
        case 0x02:  pin = 11; break;
        case 0x04:  pin = 10; break;
        case 0x08:  pin = 9; break;
        }
        v = digitalRead(pin);
        if( v == 0 )
            DEVICE.xxx = 0;
    }

    DEVICE.hex_row += 1;
    if( DEVICE.hex_row > 3 )
        DEVICE.hex_row = 0;

    digitalWriteFast(HKA, DEVICE.hex_row == 0 ? HIGH : LOW);
    digitalWriteFast(HKB, DEVICE.hex_row == 1 ? HIGH : LOW);
    digitalWriteFast(HKC, DEVICE.hex_row == 2 ? HIGH : LOW);
    digitalWriteFast(HKD, DEVICE.hex_row == 3 ? HIGH : LOW);
}

void isr_hk1()
{
    DEVICE.hex = DEVICE.hex_row*10 + 1;
}

void isr_hk2()
{
    DEVICE.hex = DEVICE.hex_row*10 + 2;
}

void isr_hk3()
{
    DEVICE.hex = DEVICE.hex_row*10 + 3;
}

void isr_hk4()
{
    DEVICE.hex = DEVICE.hex_row*10 + 4;
}

void device_setup()
{
    int rc, rc2;

    Serial.begin(9600);

    pinMode(13, OUTPUT);        // built-in led

    pinMode(24, OUTPUT);        // buzzer

    pinMode(12, INPUT);         // push button 1 (upper left)
    pinMode(11, INPUT);         // push button 2 (upper right)
    pinMode(10, INPUT);         // push button 3 (lower left)
    pinMode(9, INPUT);          // push button 4 (lower right)

    attachInterrupt(12, isr_xxx1, RISING);
    attachInterrupt(11, isr_xxx2, RISING);
    attachInterrupt(10, isr_xxx3, RISING);
    attachInterrupt(9, isr_xxx4, RISING);

    //
    // hex keypad
    //
    pinMode(HKA, OUTPUT);
    pinMode(HKB, OUTPUT);
    pinMode(HKC, OUTPUT);
    pinMode(HKD, OUTPUT);

    digitalWriteFast(HKA, LOW);
    digitalWriteFast(HKB, LOW);
    digitalWriteFast(HKC, LOW);
    digitalWriteFast(HKD, LOW);

    pinMode(HK1, INPUT_PULLDOWN);
    pinMode(HK2, INPUT_PULLDOWN);
    pinMode(HK3, INPUT_PULLDOWN);
    pinMode(HK4, INPUT_PULLDOWN);

    attachInterrupt(HK1, isr_hk1, RISING);
    attachInterrupt(HK2, isr_hk2, RISING);
    attachInterrupt(HK3, isr_hk3, RISING);
    attachInterrupt(HK4, isr_hk4, RISING);

    disp_setup();

    rc = disp_init();
    if( rc )
    {
        Serial.println("disp_init failed");
        debug_flash(rc);
    }

    rc2 = disp_set_range();
    if( rc2 )
    {
        Serial.println("disp_set_range failed");
        debug_flash(rc2);
    }

    DEVICE.it.begin(isr_hex_scan, 10000); // 1/100th of a second

    interrupts();
}

//
// wait for event to occur. Monitor global variables for a change.
//
int device_get_event()
{
    static int saved_xxx = 0;
    static int saved_epoch = 0;
    static int saved_counter15 = 0;
    static int saved_hex = 0;
    static int saved_light = 0;
    int e;

    e = E_NONE;
    while( e == E_NONE )
    {
        if( saved_xxx != DEVICE.xxx )
        {
            if( DEVICE.xxx == 0 ) {
                if( saved_xxx & 0x01 ) {
                    e = E_BUTTONL_RELEASE;
                } else if( saved_xxx & 0x02 ) {
                    e = E_BUTTONC_RELEASE;
                } else if( saved_xxx & 0x04 ) {
                    e = E_BUTTONB_RELEASE;
                } else if( saved_xxx & 0x08 ) {
                    e = E_BUTTONA_RELEASE;
                }
            } else if( DEVICE.xxx & 0x01 ) {
                e = E_BUTTONL;
            } else if( DEVICE.xxx & 0x02 ) {
                e = E_BUTTONC;
            } else if( DEVICE.xxx & 0x04 ) {
                e = E_BUTTONB;
            } else if( DEVICE.xxx & 0x08 ) {
                e = E_BUTTONA;
            }
            saved_xxx = DEVICE.xxx;
        }
        else if( saved_hex != DEVICE.hex )
        {
            switch(DEVICE.hex)
            {
            case  0:
                    switch(saved_hex) {
                    case  1: e = E_HEX_BUTTON_D_RELEASE; break;         // D
                    case  2: e = E_HEX_BUTTON_POUND_RELEASE; break;     // #
                    case  3: e = E_HEX_BUTTON_0_RELEASE; break;         // 0
                    case  4: e = E_HEX_BUTTON_STAR_RELEASE; break;      // *

                    case  11: e = E_HEX_BUTTON_C_RELEASE; break;        // C
                    case  12: e = E_HEX_BUTTON_9_RELEASE; break;        // 9
                    case  13: e = E_HEX_BUTTON_8_RELEASE; break;        // 8
                    case  14: e = E_HEX_BUTTON_7_RELEASE; break;        // 7

                    case  21: e = E_HEX_BUTTON_B_RELEASE; break;        // B
                    case  22: e = E_HEX_BUTTON_6_RELEASE; break;        // 6
                    case  23: e = E_HEX_BUTTON_5_RELEASE; break;        // 5
                    case  24: e = E_HEX_BUTTON_4_RELEASE; break;        // 4

                    case  31: e = E_HEX_BUTTON_A_RELEASE; break;        // A
                    case  32: e = E_HEX_BUTTON_3_RELEASE; break;        // 3
                    case  33: e = E_HEX_BUTTON_2_RELEASE; break;        // 2
                    case  34: e = E_HEX_BUTTON_1_RELEASE; break;        // 1
                    }
                    break;
            case  1: e = E_HEX_BUTTON_D; break;         // D
            case  2: e = E_HEX_BUTTON_POUND; break;     // #
            case  3: e = E_HEX_BUTTON_0; break;         // 0
            case  4: e = E_HEX_BUTTON_STAR; break;      // *

            case  11: e = E_HEX_BUTTON_C; break;        // C
            case  12: e = E_HEX_BUTTON_9; break;        // 9
            case  13: e = E_HEX_BUTTON_8; break;        // 8
            case  14: e = E_HEX_BUTTON_7; break;        // 7

            case  21: e = E_HEX_BUTTON_B; break;        // B
            case  22: e = E_HEX_BUTTON_6; break;        // 6
            case  23: e = E_HEX_BUTTON_5; break;        // 5
            case  24: e = E_HEX_BUTTON_4; break;        // 4

            case  31: e = E_HEX_BUTTON_A; break;        // A
            case  32: e = E_HEX_BUTTON_3; break;        // 3
            case  33: e = E_HEX_BUTTON_2; break;        // 2
            case  34: e = E_HEX_BUTTON_1; break;        // 1
            }
            saved_hex = DEVICE.hex;
        }
        else if( saved_light != DEVICE.light )
        {
            if( DEVICE.light == 0 )
            {
                e = E_LIGHT_OFF;
            }
            saved_light = DEVICE.light;
        }
        else if( saved_epoch != DEVICE.epoch )
        {
            saved_epoch = DEVICE.epoch;
            e = E_SECONDS_TIMER;
        }
        else if( saved_counter15 != DEVICE.counter15 )
        {
            saved_counter15 = DEVICE.counter15;
            if( DEVICE.counter15_enable )
                e = E_SECONDS15;
        }
    }

    return e;
}
//////////////////////////////////////////////////////////////////////
// end DEVICE
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
//
// begin DRAW
//
//////////////////////////////////////////////////////////////////////

void draw_filled_block(char *frame, int x1, int y1, int x2, int y2)
{
    int x, y;

    for(x=x1; x < x2; x++) {
        for(y=y1; y < y2; y++) {
            disp_pset(frame, x, y, 1);
        }
    }
}

void draw_rect(char *frame, int x1, int y1, int width, int height)
{
    int x, y;
    int x2, y2;

    x2 = x1 + width;
    y2 = y1 + height;

    for(x=x1; x <= x2; x++) {
        disp_pset(frame, x, y1, 1);
        disp_pset(frame, x, y2, 1);
    }

    for(y=y1; y <= y2; y++) {
        disp_pset(frame, x1, y, 1);
        disp_pset(frame, x2, y, 1);
    }
}

//
//          (x0, y0)
//  0x01          ======
//              ||       ||
//  0x02        ||  ##   || 0x04
//              ||       ||
//  0x08          ======
//              ||       ||
//  0x10        ||  ##   || 0x20
//              ||       ||
//  0x40          ======    ##     0x80
//
// Top dot:         0x100
// bottom dot:      0x200
// decimal point:   0x80
//
void draw_segments(char *frame, int x, int y, int width, int height, int thick, int segments)
{
    int x1, x2, y1, y2;

    if( segments & 0x01 ) {
        x1 = x + thick;
        x2 = x + thick + width;
        y1 = y;
        y2 = y + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x02 ) {
        x1 = x;
        x2 = x + thick;
        y1 = y + thick;
        y2 = y + thick + height;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x04 ) {
        x1 = x + thick + width;
        x2 = x + thick + width + thick;
        y1 = y + thick;
        y2 = y + thick + height;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x08 ) {
        x1 = x + thick;
        x2 = x + thick + width;
        y1 = y + thick + height;
        y2 = y + thick + height + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x10 ) {
        x1 = x;
        x2 = x + thick;
        y1 = y + thick + height + thick;
        y2 = y + thick + height + thick + height;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x20 ) {
        x1 = x + thick + width;
        x2 = x + thick + width + thick;
        y1 = y + thick + height + thick;
        y2 = y + thick + height + thick + height;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x40 ) {
        x1 = x + thick;
        x2 = x + thick + width;
        y1 = y + thick + height + thick + height;
        y2 = y + thick + height + thick + height + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x80 ) {
        // decimal
        const int SPC = 2; // 1 looked okay

        x1 = x + thick + width + thick + SPC;
        x2 = x + thick + width + thick + SPC + thick;
        y1 = y + thick + height + thick + height;
        y2 = y + thick + height + thick + height + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x100 ) {
        // top dot
        x1 = x + thick + width/2 - thick/2;
        x2 = x + thick + width/2 - thick/2 + thick;
        y1 = y + thick + height/2 - thick/2;
        y2 = y + thick + height/2 - thick/2 + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }

    if( segments & 0x200 ) {
        // bottom dot
        x1 = x + thick + width/2 - thick/2;
        x2 = x + thick + width/2 - thick/2 + thick;
        y1 = y + thick + height + thick + height/2 - thick/2;
        y2 = y + thick + height + thick + height/2 - thick/2 + thick;

        draw_filled_block(frame, x1, y1, x2, y2);
    }
}

void draw_digit(char *frame, int x, int y, int width, int height, int thick, char digit, int dp)
{
    int segments;

    switch(digit)
    {
    case '0':
        segments = 0x01 | 0x02 | 0x04 | 0x10 | 0x20 | 0x40;
        break;
    case '1':
        segments = 0x04 | 0x20;
        break;
    case '2':
        segments = 0x01 | 0x04 | 0x08 | 0x10 | 0x40;
        break;
    case '3':
        segments = 0x01 | 0x04 | 0x08 | 0x020 | 0x40;
        break;
    case '4':
        segments = 0x02 | 0x08 | 0x04 | 0x020;
        break;
    case '5':
        segments = 0x01 | 0x02 | 0x08 | 0x20 | 0x40;
        break;
    case '6':
        segments = 0x01 | 0x02 | 0x08 | 0x10 | 0x20 | 0x40;
        break;
    case '7':
        segments = 0x01 | 0x02 | 0x04 | 0x20; 
        break;
    case '8':
        segments = 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40; 
        break;
    case '9':
    case 'g':
        segments = 0x01 | 0x02 | 0x04 | 0x08 | 0x20 | 0x40; 
        break;
    case 'A':
    case 'a':
        segments = 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20;
        break;
    case 'B':
    case 'b':
        segments = 0x02 | 0x10 | 0x40 | 0x20 | 0x08; 
        break;
    case 'C':
    case 'c':
        segments = 0x01 | 0x02 | 0x10 | 0x40;
        break;
    case 'D':
    case 'd':
        segments = 0x04 | 0x20 | 0x08 | 0x10 | 0x40;
        break;
    case 'E':
    case 'e':
        segments = 0x01 | 0x02 | 0x08 | 0x10 | 0x40;
        break;
    case 'F':
    case 'f':
        segments = 0x01 | 0x02 | 0x08 | 0x10;
        break;
    case 'H':
        segments = 0x02 | 0x04 | 0x08 | 0x10 | 0x20;
        break;
    case 'h':
        segments = 0x02 | 0x08 | 0x10 | 0x20;
        break;
    case 'i':
        segments = 0x20;
        break;
    case 'J':
        segments = 0x04 | 0x20 | 0x40 | 0x10;
        break;
    case 'L':
        segments = 0x02 | 0x10 | 0x40;
        break;
    case 'n':
        segments = 0x08 | 0x10 | 0x20;
        break;
    case 'o':
        segments = 0x08 | 0x10 | 0x20 | 0x40;
        break;
    case 'P':
        segments = 0x01 | 0x02 | 0x04 | 0x08 | 0x10;
        break;
    case 'U':
        segments = 0x02 | 0x04 | 0x10 | 0x20 | 0x40;
        break;
    case '-':
        segments = 0x08;
        break;
    case '_':
        segments = 0x04;
        break;
    case ':':
        segments = 0x200 | 0x100;
        break;
    case '.':
        segments = 0x80;
        break;
    case ' ':
        segments = 0x00;  /* BLANK */
        break;
    default:
        segments = 0x00;
        break;
    }

    if(dp) {
        segments |= 0x80;
    }

    draw_segments(frame, x, y, width, height, thick, segments);
}

void draw_segstr(char *frame, int x0, int y0, int width, int height, int thick, const char *str)
{
    int x;
    const char *p;
    const int SPACING = 3;

    x = x0;
    for(p=str; *p; p++)
    {
        if( *p == '.' )
            continue;
        draw_digit(frame, x, y0, width, height, thick, *p, p[1]=='.');
        x = x + width + thick*2 + SPACING;
    }
}

void draw_number(char *frame, int x0, int y0, int width, int height, int thick, int num)
{
    char buf[20];

    snprintf(buf, sizeof(buf), "%3d", num);

    draw_segstr(frame, x0, y0, width, height, thick, buf);
}

//////////////////////////////////////////////////////////////////////
//
// 7x5 font
//
//////////////////////////////////////////////////////////////////////

const int CHAR_WIDTH = 5;
const int CHAR_HEIGHT = 7;
const int CHAR_SPACING = 1;

// render a 5x7 font character into a frame buffer
//
// get the pixels for character 'ch' at coordinate (x,y)
// (0,0)
//      +-------+
//      |  * *  |
//      |   *   |
//      | ***** |
//      | *     |
//      | ****  |
//      | *     |
//      | ***** |
//      +-------+
//
//  5x7 text
//
//        0   1   2   3   4
//      +---+---+---+---+---+
//  0   | 4 | 3 | 2 | 1 | 0 |       4...0       top
//      +---+---+---+---+---+
//
//      +---+---+---+---+---+
//  1   |29 |28 |27 |26 |25 |       29...25     bits
//      +---+---+---+---+---+
//  2   |24 |23 |22 |21 |20 |       24...20
//      +---+---+---+---+---+
//  3   |19 |18 |17 |16 |15 |       19...15
//      +---+---+---+---+---+
//  4   |14 |13 |12 |11 |10 |       14...10
//      +---+---+---+---+---+
//  5   | 9 | 8 | 7 | 6 | 5 |       9...5
//      +---+---+---+---+---+
//  6   | 4 | 3 | 2 | 1 | 0 |       4...0
//      +---+---+---+---+---+
//

struct {
    char top[133];      // the top row (bits 0 to 4)
    long bits[133];     // the bottom 6 rows (bits 0 to 29)
} CasioFont = {
    //
    //  top:            +-+-+-+-+-+
    //        4 - 0     | | | | | | row 0
    //                  +-+-+-+-+-+
    //
    //  bits:           +-+-+-+-+-+
    //      29 - 25     | | | | | | row 1
    //                  +-+-+-+-+-+
    //      24 - 20     | | | | | | row 2
    //                  +-+-+-+-+-+
    //      19 - 15     | | | | | | row 3
    //                  +-+-+-+-+-+
    //      14 - 10     | | | | | | row 4
    //                  +-+-+-+-+-+
    //        9 - 5     | | | | | | row 5
    //                  +-+-+-+-+-+
    //        4 - 0     | | | | | | row 6
    //                  +-+-+-+-+-+
    //

    //
    // top row bits 0 - 4
    //
    0x00,          // 00000 <0 \00 - NUL>
    0x00,          // 00000 <1 \01 - SPACE>
    0x00,          // 00000 <2 \02 - A>
    0x02,          // 00010 <3 \03 - A1>
    0x08,          // 01000 <4 \04 - A2>
    0x0A,          // 01010 <5 \05 - A3>
    0x04,          // 00100 <6 \06 - A4>
    0x0A,          // 01010 <7 \07 - A5>
    0x09,          // 01001 <8 \10 - A6>
    0x0E,          // 01110 <9 \11 - A7>
    0x00,          // 00000 <10 \12 - B>
    0x00,          // 00000 <11 \13 - C>
    0x0F,          // 01111 <12 \14 - C1>
    0x02,          // 00010 <13 \15 - C2>
    0x00,          // 00000 <14 \16 - D>
    0x00,          // 00000 <15 \17 - E>
    0x02,          // 00010 <16 \20 - E1>
    0x08,          // 01000 <17 \21 - E2>
    0x04,          // 00100 <18 \22 - E3>
    0x0A,          // 01010 <19 \23 - E4>
    0x1F,          // 11111 <20 \24 - E5>
    0x00,          // 00000 <21 \25 - F>
    0x00,          // 00000 <22 \26 - G>
    0x0A,          // 01010 <23 \27 - G1>
    0x00,          // 00000 <24 \30 - H>
    0x00,          // 00000 <25 \31 - I>
    0x02,          // 00010 <26 \32 - I1>
    0x08,          // 01000 <27 \33 - I2>
    0x04,          // 00100 <28 \34 - I3>
    0x0A,          // 01010 <29 \35 - I4>
    0x04,          // 00100 <30 \36 - I5>
    0x00,          // 00000 <31 \37 - J>
    0x00,          // 00000 <32 \40 - K>
    0x00,          // 00000 <33 \41 - L>
    0x00,          // 00000 <34 \42 - L1>
    0x00,          // 00000 <35 \43 - M>
    0x00,          // 00000 <36 \44 - N>
    0x02,          // 00010 <37 \45 - N1>
    0x09,          // 01001 <38 \46 - N2>
    0x00,          // 00000 <39 \47 - O>
    0x02,          // 00010 <40 \50 - O1>
    0x08,          // 01000 <41 \51 - O2>
    0x04,          // 00100 <42 \52 - O3>
    0x0A,          // 01010 <43 \53 - O4>
    0x09,          // 01001 <44 \54 - O5>
    0x00,          // 00000 <45 \55 - OE>
    0x00,          // 00000 <46 \56 - P>
    0x00,          // 00000 <47 \57 - Q>
    0x00,          // 00000 <48 \60 - R>
    0x00,          // 00000 <49 \61 - S>
    0x02,          // 00010 <50 \62 - S1>
    0x0F,          // 01111 <51 \63 - S2>
    0x00,          // 00000 <52 \64 - T>
    0x1F,          // 11111 <53 \65 - T1>
    0x00,          // 00000 <54 \66 - U>
    0x02,          // 00010 <55 \67 - U1>
    0x08,          // 01000 <56 \70 - U2>
    0x04,          // 00100 <57 \71 - U3>
    0x0A,          // 01010 <58 \72 - U4>
    0x00,          // 00000 <59 \73 - V>
    0x00,          // 00000 <60 \74 - W>
    0x00,          // 00000 <61 \75 - X>
    0x00,          // 00000 <62 \76 - Y>
    0x00,          // 00000 <63 \77 - Z>
    0x02,          // 00010 <64 \100 - Z1>
    0x04,          // 00100 <65 \101 - Z2>
    0x00,          // 00000 <66 \102 - AE>
    0x00,          // 00000 <67 \103 - O7>
    0x04,          // 00100 <68 \104 - A8>
    0x0A,          // 01010 <69 \105 - A9>
    0x0A,          // 01010 <70 \106 - O8>
    0x00,          // 00000 <71 \107 - SYM1>
    0x00,          // 00000 <72 \110 - SYM2:B>
    0x00,          // 00000 <73 \111 - SYM3>
    0x00,          // 00000 <74 \112 - SYM4>
    0x00,          // 00000 <75 \113 - SYM5:E>
    0x0A,          // 01010 <76 \114 - SYM6:E>
    0x00,          // 00000 <77 \115 - SYM7>
    0x00,          // 00000 <78 \116 - SYM8>
    0x00,          // 00000 <79 \117 - SYM9:N>
    0x0A,          // 01010 <80 \120 - SYM10:N>
    0x00,          // 00000 <81 \121 - SYM11>
    0x00,          // 00000 <82 \122 - SYM12>
    0x00,          // 00000 <83 \123 - SYM13:M>
    0x00,          // 00000 <84 \124 - SYM14:H>
    0x00,          // 00000 <85 \125 - SYM15:O>
    0x00,          // 00000 <86 \126 - SYM16>
    0x00,          // 00000 <87 \127 - SYM17:P>
    0x00,          // 00000 <88 \130 - SYM18:C>
    0x00,          // 00000 <89 \131 - SYM19:T>
    0x00,          // 00000 <90 \132 - SYM20:y>
    0x00,          // 00000 <91 \133 - SYM21>
    0x00,          // 00000 <92 \134 - SYM22:X>
    0x00,          // 00000 <93 \135 - SYM23>
    0x00,          // 00000 <94 \136 - SYM24>
    0x00,          // 00000 <95 \137 - SYM25:W>
    0x00,          // 00000 <96 \140 - SYM26:W>
    0x00,          // 00000 <97 \141 - SYM27>
    0x00,          // 00000 <98 \142 - SYM28>
    0x00,          // 00000 <99 \143 - SYM29>
    0x00,          // 00000 <100 \144 - SYM30>
    0x00,          // 00000 <101 \145 - SYM31>
    0x00,          // 00000 <102 \146 - SYM32:R>
    0x00,          // 00000 <103 \147 - @>
    0x00,          // 00000 <104 \150 - !>
    0x00,          // 00000 <105 \151 - ?>
    0x00,          // 00000 <106 \152 - ,>
    0x00,          // 00000 <107 \153 - .>
    0x00,          // 00000 <108 \154 - :>
    0x00,          // 00000 <109 \155 - />
    0x00,          // 00000 <110 \156 - +>
    0x00,          // 00000 <111 \157 - ->
    0x00,          // 00000 <112 \160 - 0>
    0x00,          // 00000 <113 \161 - 1>
    0x00,          // 00000 <114 \162 - 2>
    0x00,          // 00000 <115 \163 - 3>
    0x00,          // 00000 <116 \164 - 4>
    0x00,          // 00000 <117 \165 - 5>
    0x00,          // 00000 <118 \166 - 6>
    0x00,          // 00000 <119 \167 - 7>
    0x00,          // 00000 <120 \170 - 8>
    0x00,          // 00000 <121 \171 - 9>
    0x00,          // 00000 <122 \172 - exchange>
    0x00,          // 00000 <123 \173 - hour glass>
    0x00,          // 00000 <124 \174 - left arrow>
    0x00,          // 00000 <125 \175 - right arrow>
    0x00,          // 00000 <126 \176 - bell>
    0x00,          // 00000 <127 \177 - dual time>
    0x00,          // 00000 <128 \200 - calculator-1>
    0x00,          // 00000 <129 \201 - calculator-2>
    0x00,          // 00000 <130 \202 - calculator-3>
    0x00,          // 00000 <131 \203 - divide>
    0x00,          // 00000 <132 \204 - stop watch>

    //
    // Bottom 6 rows
    //
    //                  +-+-+-+-+-+
    //      top         | | | | | | row 0
    //                  +-+-+-+-+-+
    //      29 - 25     | | | | | | row 1
    //                  +-+-+-+-+-+
    //      24 - 20     | | | | | | row 2
    //                  +-+-+-+-+-+
    //      19 - 15     | | | | | | row 3
    //                  +-+-+-+-+-+
    //      14 - 10     | | | | | | row 4
    //                  +-+-+-+-+-+
    //        9 - 5     | | | | | | row 5
    //                  +-+-+-+-+-+
    //        4 - 0     | | | | | | row 6
    //                  +-+-+-+-+-+
    //
    //             // 29 25 24 20 19 15 14 10 9   5 4   0
    //             // +---+ +---+ +---+ +---+ +---+ +---+
    0x00000000,    // 00000 00000 00000 00000 00000 00000 <0 \00 - NUL>
    0x00000000,    // 00000 00000 00000 00000 00000 00000 <1 \01 - SPACE>
    0x00E8FE31,    // 00000 01110 10001 11111 10001 10001 <2 \02 - A>
    0x08E8FE31,    // 00100 01110 10001 11111 10001 10001 <3 \03 - A1>
    0x08E8FE31,    // 00100 01110 10001 11111 10001 10001 <4 \04 - A2>
    0x08E8FE31,    // 00100 01110 10001 11111 10001 10001 <5 \05 - A3>
    0x14E8FE31,    // 01010 01110 10001 11111 10001 10001 <6 \06 - A4>
    0x00E8FE31,    // 00000 01110 10001 11111 10001 10001 <7 \07 - A5>
    0x2CE8FE31,    // 10110 01110 10001 11111 10001 10001 <8 \10 - A6>
    0x23F8C443,    // 10001 11111 10001 10001 00010 00011 <9 \11 - A7>
    0x01E8FA3E,    // 00000 11110 10001 11110 10001 11110 <10 \12 - B>
    0x00F8420F,    // 00000 01111 10000 10000 10000 01111 <11 \13 - C>
    0x21083C4F,    // 10000 10000 10000 01111 00010 01111 <12 \14 - C1>
    0x08F8420F,    // 00100 01111 10000 10000 10000 01111 <13 \15 - C2>
    0x01E8C63E,    // 00000 11110 10001 10001 10001 11110 <14 \16 - D>
    0x01F87A1F,    // 00000 11111 10000 11110 10000 11111 <15 \17 - E>
    0x09F87A1F,    // 00100 11111 10000 11110 10000 11111 <16 \20 - E1>
    0x09F87A1F,    // 00100 11111 10000 11110 10000 11111 <17 \21 - E2>
    0x15F87A1F,    // 01010 11111 10000 11110 10000 11111 <18 \22 - E3>
    0x01F87A1F,    // 00000 11111 10000 11110 10000 11111 <19 \23 - E4>
    0x21E87C43,    // 10000 11110 10000 11111 00010 00011 <20 \24 - E5>
    0x01F87A10,    // 00000 11111 10000 11110 10000 10000 <21 \25 - F>
    0x00F85E2F,    // 00000 01111 10000 10111 10001 01111 <22 \26 - G>
    0x08F85E2F,    // 00100 01111 10000 10111 10001 01111 <23 \27 - G1>
    0x0118FE31,    // 00000 10001 10001 11111 10001 10001 <24 \30 - H>
    0x00E2108E,    // 00000 01110 00100 00100 00100 01110 <25 \31 - I>
    0x08E2108E,    // 00100 01110 00100 00100 00100 01110 <26 \32 - I1>
    0x08E2108E,    // 00100 01110 00100 00100 00100 01110 <27 \33 - I2>
    0x14E2108E,    // 01010 01110 00100 00100 00100 01110 <28 \34 - I3>
    0x00E2108E,    // 00000 01110 00100 00100 00100 01110 <29 \35 - I4>
    0x00E2108E,    // 00000 01110 00100 00100 00100 01110 <30 \36 - I5>
    0x00710A4C,    // 00000 00111 00010 00010 10010 01100 <31 \37 - J>
    0x01197251,    // 00000 10001 10010 11100 10010 10001 <32 \40 - K>
    0x0108421F,    // 00000 10000 10000 10000 10000 11111 <33 \41 - L>
    0x0086610F,    // 00000 01000 01100 11000 01000 01111 <34 \42 - L1>
    0x011DD631,    // 00000 10001 11011 10101 10001 10001 <35 \43 - M>
    0x011CD671,    // 00000 10001 11001 10101 10011 10001 <36 \44 - N>
    0x091CD671,    // 00100 10001 11001 10101 10011 10001 <37 \45 - N1>
    0x0D1CD671,    // 00110 10001 11001 10101 10011 10001 <38 \46 - N2>
    0x00E8C62E,    // 00000 01110 10001 10001 10001 01110 <39 \47 - O>
    0x08E8C62E,    // 00100 01110 10001 10001 10001 01110 <40 \50 - O1>
    0x08E8C62E,    // 00100 01110 10001 10001 10001 01110 <41 \51 - O2>
    0x14E8C62E,    // 01010 01110 10001 10001 10001 01110 <42 \52 - O3>
    0x00E8C62E,    // 00000 01110 10001 10001 10001 01110 <43 \53 - O4>
    0x2CE8C62E,    // 10110 01110 10001 10001 10001 01110 <44 \54 - O5>
    0x00FA5E8F,    // 00000 01111 10100 10111 10100 01111 <45 \55 - OE>
    0x01E8FA10,    // 00000 11110 10001 11110 10000 10000 <46 \56 - P>
    0x00E8D64D,    // 00000 01110 10001 10101 10010 01101 <47 \57 - Q>
    0x01E8FA51,    // 00000 11110 10001 11110 10010 10001 <48 \60 - R>
    0x00F8383E,    // 00000 01111 10000 01110 00001 11110 <49 \61 - S>
    0x08F8383E,    // 00100 01111 10000 01110 00001 11110 <50 \62 - S1>
    0x20E0F84F,    // 10000 01110 00001 11110 00010 01111 <51 \63 - S2>
    0x01F21084,    // 00000 11111 00100 00100 00100 00100 <52 \64 - T>
    0x0842104F,    // 00100 00100 00100 00100 00010 01111 <53 \65 - T1>
    0x0118C62E,    // 00000 10001 10001 10001 10001 01110 <54 \66 - U>
    0x0918C62E,    // 00100 10001 10001 10001 10001 01110 <55 \67 - U1>
    0x0918C62E,    // 00100 10001 10001 10001 10001 01110 <56 \70 - U2>
    0x1518C62E,    // 01010 10001 10001 10001 10001 01110 <57 \71 - U3>
    0x0118C62E,    // 00000 10001 10001 10001 10001 01110 <58 \72 - U4>
    0x0118C544,    // 00000 10001 10001 10001 01010 00100 <59 \73 - V>
    0x011AD6AA,    // 00000 10001 10101 10101 10101 01010 <60 \74 - W>
    0x01151151,    // 00000 10001 01010 00100 01010 10001 <61 \75 - X>
    0x01151084,    // 00000 10001 01010 00100 00100 00100 <62 \76 - Y>
    0x01F1111F,    // 00000 11111 00010 00100 01000 11111 <63 \77 - Z>
    0x09F1111F,    // 00100 11111 00010 00100 01000 11111 <64 \100 - Z1>
    0x01F1111F,    // 00000 11111 00010 00100 01000 11111 <65 \101 - Z2>
    0x00FA7E97,    // 00000 01111 10100 11111 10100 10111 <66 \102 - AE>
    0x00F9D73E,    // 00000 01111 10011 10101 11001 11110 <67 \103 - O7>
    0x144747F1,    // 01010 00100 01110 10001 11111 10001 <68 \104 - A8>
    0x00E8FE31,    // 00000 01110 10001 11111 10001 10001 <69 \105 - A9>
    0x00E8C62E,    // 00000 01110 10001 10001 10001 01110 <70 \106 - O8>
    0x01F87A3E,    // 00000 11111 10000 11110 10001 11110 <71 \107 - SYM1>
    0x01E8FA3E,    // 00000 11110 10001 11110 10001 11110 <72 \110 - SYM2:B>
    0x01F4A108,    // 00000 11111 01001 01000 01000 01000 <73 \111 - SYM3>
    0x00E52BF1,    // 00000 01110 01010 01010 11111 10001 <74 \112 - SYM4>
    0x01F87A1F,    // 00000 11111 10000 11110 10000 11111 <75 \113 - SYM5:E>
    0x01F87A1F,    // 00000 11111 10000 11110 10000 11111 <76 \114 - SYM6:E>
    0x015ABAB5,    // 00000 10101 10101 01110 10101 10101 <77 \115 - SYM7>
    0x00E8983E,    // 00000 01110 10001 00110 00001 11110 <78 \116 - SYM8>
    0x0119D731,    // 00000 10001 10011 10101 11001 10001 <79 \117 - SYM9:N>
    0x0919D731,    // 00100 10001 10011 10101 11001 10001 <80 \120 - SYM10:N>
    0x013A6293,    // 00000 10011 10100 11000 10100 10011 <81 \121 - SYM11>
    0x007294B9,    // 00000 00111 00101 00101 00101 11001 <82 \122 - SYM12>
    0x011DD631,    // 00000 10001 11011 10101 10001 10001 <83 \123 - SYM13:M>
    0x0118FE31,    // 00000 10001 10001 11111 10001 10001 <84 \124 - SYM14:H>
    0x00E8C62E,    // 00000 01110 10001 10001 10001 01110 <85 \125 - SYM15:O>
    0x01F8C631,    // 00000 11111 10001 10001 10001 10001 <86 \126 - SYM16>
    0x01E8FA10,    // 00000 11110 10001 11110 10000 10000 <87 \127 - SYM17:P>
    0x00F8420F,    // 00000 01111 10000 10000 10000 01111 <88 \130 - SYM18:C>
    0x01FA9084,    // 00000 11111 10101 00100 00100 00100 <89 \131 - SYM19:T>
    0x01151098,    // 00000 10001 01010 00100 00100 11000 <90 \132 - SYM20:y>
    0x004755C4,    // 00000 00100 01110 10101 01110 00100 <91 \133 - SYM21>
    0x01151151,    // 00000 10001 01010 00100 01010 10001 <92 \134 - SYM22:X>
    0x01294BE1,    // 00000 10010 10010 10010 11111 00001 <93 \135 - SYM23>
    0x0118BC21,    // 00000 10001 10001 01111 00001 00001 <94 \136 - SYM24>
    0x015AD6BF,    // 00000 10101 10101 10101 10101 11111 <95 \137 - SYM25:W>
    0x015AD7E1,    // 00000 10101 10101 10101 11111 00001 <96 \140 - SYM26:W>
    0x0184392E,    // 00000 11000 01000 01110 01001 01110 <97 \141 - SYM27>
    0x0118E6B9,    // 00000 10001 10001 11001 10101 11001 <98 \142 - SYM28>
    0x01087A3E,    // 00000 10000 10000 11110 10001 11110 <99 \143 - SYM29>
    0x00E89E2E,    // 00000 01110 10001 00111 10001 01110 <100 \144 - SYM30>
    0x01297652,    // 00000 10010 10010 11101 10010 10010 <101 \145 - SYM31>
    0x00F8BD31,    // 00000 01111 10001 01111 01001 10001 <102 \146 - SYM32:R>
    0x01E0B6BE,    // 00000 11110 00001 01101 10101 11110 <103 \147 - @>
    0x00421004,    // 00000 00100 00100 00100 00000 00100 <104 \150 - !>
    0x00C91004,    // 00000 01100 10010 00100 00000 00100 <105 \151 - ?>
    0x00308800,    // 00000 00011 00001 00010 00000 00000 <106 \152 - '>
    0x0000018C,    // 00000 00000 00000 00000 01100 01100 <107 \153 - .>
    0x00020080,    // 00000 00000 00100 00000 00100 00000 <108 \154 - :>
    0x00111110,    // 00000 00001 00010 00100 01000 10000 <109 \155 - />
    0x00427C84,    // 00000 00100 00100 11111 00100 00100 <110 \156 - +>
    0x00007C00,    // 00000 00000 00000 11111 00000 00000 <111 \157 - ->
    0x0064A526,    // 00000 00110 01001 01001 01001 00110 <112 \160 - 0>
    0x00461084,    // 00000 00100 01100 00100 00100 00100 <113 \161 - 1>
    0x01E0BA1F,    // 00000 11110 00001 01110 10000 11111 <114 \162 - 2>
    0x01E0B83F,    // 00000 11110 00001 01110 00001 11111 <115 \163 - 3>
    0x00654BE2,    // 00000 00110 01010 10010 11111 00010 <116 \164 - 4>
    0x01F8783E,    // 00000 11111 10000 11110 00001 11110 <117 \165 - 5>
    0x00F87A2E,    // 00000 01111 10000 11110 10001 01110 <118 \166 - 6>
    0x01F88884,    // 00000 11111 10001 00010 00100 00100 <119 \167 - 7>
    0x00E8BA2E,    // 00000 01110 10001 01110 10001 01110 <120 \170 - 8>
    0x00E8BC3E,    // 00000 01110 10001 01111 00001 11110 <121 \171 - 9>
    0x0071D71C,    // 00000 00111 00011 10101 11000 11100 <122 \172 - exchange>
    0x01F8BB7F,    // 00000 11111 10001 01110 11011 11111 <123 \173 - hour glass>
    0x00119C61,    // 00000 00001 00011 00111 00011 00001 <124 \174 - left arrow>
    0x010C7310,    // 00000 10000 11000 11100 11000 10000 <125 \175 - right arrow>
    0x00473BE4,    // 00000 00100 01110 01110 11111 00100 <126 \176 - bell>
    0x00EADE2E,    // 00000 01110 10101 10111 10001 01110 <127 \177 - dual time>
    0x01FFC63F,    // 00000 11111 11111 10001 10001 11111 <128 \200 - calculator-1>
    0x00C8C62D,    // 00000 01100 10001 10001 10001 01101 <129 \201 - calculator-2>
    0x0125694B,    // 00000 10010 01010 11010 01010 01011 <130 \202 - calculator-3>
    0x00407C04,    // 00000 00100 00000 11111 00000 00100 <131 \203 - divide>
    0x00877541,    // 00000 01000 01110 11101 01010 00001 <132 \204 - stop watch>
};

void draw_char(char *frame, int x0, int y0, int mag, char ch)
{
    char top;
    long bits;
    int i, x, y, mx, my, pixel;

    top = CasioFont.top[ch];
    bits = CasioFont.bits[ch];

    // top row
    if( top != 0 )
    {
        x = 4;
        y = 0;
        for(i=0; i < 5; i++) {
            pixel = top & 1;
            top >>= 1;

            for(mx=0; mx < mag; mx++) {
                for(my=0; my < mag; my++) {
                    if( pixel )
                        disp_pset(frame, x0+x*mag+mx, y0+y*mag+my, pixel);
                }
            }
            x--;
        }
    }

    // bottom rows
    x = 4;
    y = 6;
    for(i=0; i < 30; i++) {
        pixel = bits & 1;
        bits >>= 1;

        for(mx=0; mx < mag; mx++) {
            for(my=0; my < mag; my++) {
                if( pixel )
                    disp_pset(frame, x0+x*mag+mx, y0+y*mag+my, pixel);
            }
        }

        x--;
        if( x < 0 ) {
            x = 4;
            y--;
            if( y < 0 ) y = 4;
        }
    }
}

void draw_blit(char *frame, int x0, int y0, int width, int height, long bits)
{
    int len;
    int i, x, y, pixel;

    len = width * height;
    x = width-1;
    y = height-1;
    for(i=0; i < len; i++) {
        pixel = bits & 1;
        bits >>= 1;

        if( pixel )
            disp_pset(frame, x0+x, y0+y, pixel);

        x--;
        if( x < 0 ) {
            x = width-1;
            y--;
            if( y < 0 )
                y = height-1;
        }
    }
}

//
// Day of the Week List
//
//      Sun Mon Tue Wed Thu Fri Sat
//
// ENG  SUN MON TUE WED THU FRI SAT
// POR  DOM SEG TER QUA QUI SEX S\000B
// ESP  DOM LUN MAR MI\000 JUE VIE S\000B
// FRA  DIM LUN MAR MER JEU VEN SAM
// NED  ZON MAA DIN WOE DON VRI ZAT
// DAN  S\000N MAN TIR ONS TOR FRE L\000R
// DEU  SON MON DIE MIT DON FRE SAM
// ITA  DOM LUN MAR MER GIO VEN SAB
// SVE  S\000N M\000N TIS ONS TOR FRE L\000R
// POL  NIE PON WTO \000RO CZE PI\000 SOB
// ROM  DUM LUN MAR MIE JOI VIN S\000M
// TUR  PAZ PZT SAL \000AR PER CUM CTS
// PYC  BC \000H B\000 CP \000\000 \000\000 C\000
//
// Languages:
//  ENG
//  POR
//  ESP
//  FRA
//  NED
//  DAN
//  DEU
//  ITA
//  SVE
//  POL
//  ROM
//  T\000R
//  P\000C
//

char AsciiMap[256];

void make_ascii()
{
    static char letters[] = {
             2, 10, 11, 14, 15, 21,         // A B C D E F
            22, 24, 25, 31, 32, 33,         // G H I J K L
            35, 36, 39, 46, 47, 48,         // M N O P Q R
            49, 52, 54, 59, 60, 61,         // S T U V W X
            62, 63,                         // Y Z
    };
    int i;

    // digits
    for(i=0; i <= 9; i++)
    {
        AsciiMap[ '0' + i ] = 112 + i;
    }

    // letters
    for(i=0; i < 26; i++)
    {
        AsciiMap[ 'A' + i ] = letters[i];
        AsciiMap[ 'a' + i ] = letters[i];
    }

    AsciiMap[' '] = '\001';

    // symbols
    AsciiMap['@'] = 103;
    AsciiMap['!'] = 104;
    AsciiMap['?'] = 105;
    AsciiMap[','] = 106;
    AsciiMap['.'] = 107;
    AsciiMap[':'] = 108;
    AsciiMap['/'] = 109;
    AsciiMap['+'] = 110;
    AsciiMap['-'] = 111;

    // special symbols
    AsciiMap[1] = 123;  // hour glass
    AsciiMap[2] = 124;  // left arrow
    AsciiMap[3] = 125;  // right arrow

    AsciiMap[4] = 126;  // bell
    AsciiMap[5] = 127;  // dual time
    AsciiMap[6] = 128;  // calculator1

    AsciiMap[7] = 129;  // calculator2
    AsciiMap[8] = 130;  // calculator3
    AsciiMap[9] = 131;  // divide

    AsciiMap[10] = 132; // stop watch

    AsciiMap[11] = 3;   // A1 \13
    AsciiMap[12] = 4;   // A2 \14
    AsciiMap[13] = 5;   // A3 \15
    AsciiMap[14] = 6;   // A4
    AsciiMap[15] = 7;   // A5
    AsciiMap[16] = 8;   // A6
    AsciiMap[17] = 9;   // A7
}

void draw_ascii_string(char *frame, int x0, int y0, int mag, const char *str)
{
    const char *p;
    int x, ch;

    x = x0;
    for(p=str; *p; p++)
    {
        ch = AsciiMap[*p];
        if( ch == 0 )
        {
            ch = ch + 128;
        }

        draw_char(frame, x, y0, mag, ch);
        x = x + CHAR_WIDTH * mag + CHAR_SPACING;
    }
}

void draw_string(char *frame, int x0, int y0, int mag, const char *str)
{
    const char *p;
    int x;

    x = x0;
    for(p=str; *p; p++)
    {
        draw_char(frame, x, y0, mag, *p);
        x = x + CHAR_WIDTH * mag + CHAR_SPACING;
    }
}

void draw_am1(char *frame)
{
//  draw_rect(frame, 1, 17, 5, 5);
    draw_ascii_string(frame, 1, 17, 1, "A");
}

void draw_pm1(char *frame)
{
//  draw_rect(frame, 7, 17, 5, 5);
    draw_ascii_string(frame, 7, 17, 1, "P");
}

void draw_am2(char *frame)
{
//  draw_rect(frame, 1, 50, 4, 4);
    draw_blit(frame, 1, 50, 4, 4, 0x000069F9);  // 0110 1001 1111 1001
}

void draw_pm2(char *frame)
{
//  draw_rect(frame, 5, 50, 4, 4);
    draw_blit(frame, 5, 50, 4, 4, 0x0000E9E8);  // 1110 1001 1110 1000
}

void draw_split(char *frame)
{
//  draw_rect(frame, 1, 1, 15, 5);
    draw_ascii_string(frame, 1, 0, 1, "SPL");
}

void draw_dst(char *frame)
{
//  draw_rect(frame, 1, 8, 15, 5);
    draw_ascii_string(frame, 1, 7, 1, "DST");
}

void draw_lt(char *frame)
{
//  draw_rect(frame, 62+10, 0, 12, 3);
//  draw_blit(frame, 62+10, 0, 6, 3, 0x000278B2); // 100111 100010 110010

//  draw_blit(frame, 62+10, 1, 6, 3, 0x000278B2); // 100111 100010 110010
    draw_blit(frame, 62+10, 0, 6, 4, 0x009E28B2); // 100111 100010 100010 110010
}

void draw_3sec(char *frame)
{
//  draw_rect(frame, 62+30, 0, 20, 3);
    draw_blit(frame, 62+30, 0, 10, 3, 0x36D52B6D); // 1101101101 0101001010 1101101101

    draw_blit(frame, 62+30, 0, 10, 1, 0x36D52B6D); // 1101101101 0101001010 1101101101
    draw_blit(frame, 62+30, 1, 10, 3, 0x36D52B6D); // 1101101101 0101001010 1101101101
}

void draw_snooze(char *frame)
{
//  draw_rect(frame, 64, 5, 12, 4);
//  draw_ascii_string(frame, 64-1, 4, 1, "SNZ");
    draw_ascii_string(frame, 64-1, 3, 1, "SNZ");
}

void draw_mute(char *frame)
{
//  draw_rect(frame, 64+12+10, 5, 20, 4);
//  draw_ascii_string(frame, 64+12+10-1, 4, 1, "MUTE");
    draw_ascii_string(frame, 64+12+10-1, 3, 1, "MUTE");
}

void draw_sig(char *frame)
{
//  draw_rect(frame, 64+12+20+6+10, 5, 12, 4);
//  draw_ascii_string(frame, 64+12+20+6+10-1, 4, 1, "SIG");
    draw_ascii_string(frame, 64+12+20+6+10-1, 3, 1, "SIG");
}

void draw_alarm(char *frame, int n)
{
//  draw_rect(frame, 62+(n-1)*13, 11, 12, 3);
    draw_blit(frame, 62+(n-1)*13, 11, 7, 3, 0x00096AD5);
}

void draw_text(char *frame, const char *str)
{
    draw_ascii_string(frame, 25, 0, 2, str);
}

void draw_main(char *frame, const char *str)
{
    draw_segstr(frame, 10, 20, 6, 10, 2, str);
}

void draw_secondary(char *frame, const char *str)
{
    draw_segstr(frame, 15, 52, 4, 4, 1, str);
}

void draw_hline(char *frame, int x0, int y0, int len)
{
    int i, x;

    x = x0;
    for(i=0; i < len; i++)
    {
        disp_pset(frame, x++, y0, 1);
    }
}

void draw_vline(char *frame, int x0, int y0, int len)
{
    int i, y;

    y = y0;
    for(i=0; i < len; i++)
    {
        disp_pset(frame, x0, y++, 1);
    }
}

//////////////////////////////////////////////////////////////////////
// end DRAW
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
//
// begin CASIO
//
//////////////////////////////////////////////////////////////////////

typedef enum {
    M_HOME,
    M_HOME_SET,
    M_DB,
    M_DB_SET,
    M_CAL,
    M_EX,
    M_ST,
    M_AL,
    M_AL_SET,
    M_DT,
    M_DT_SET,
} MODE;

typedef enum {
    L_ENG,      // 0 - english
    L_POR,      // 1 - portuguese
    L_ESP,      // 2 - spanish
    L_FRA,      // 3 - french
    L_NED,      // 4 - dutch
    L_DAN,      // 5 - danish
    L_DEU,      // 6 - german
    L_ITA,      // 7 - italian
    L_SVE,      // 8 - swedish
    L_POL,      // 9 - polish
    L_ROM,      // 10 - romanian
    L_TUR,      // 11 - turkish
    L_RUS,      // 12 - russian
} LANG;

//
// day of week table
//
// ENG  SUN MON TUE WED THU FRI SAT
// POR  DOM SEG TER QUA QUI SEX S\000B
// ESP  DOM LUN MAR MI\000 JUE VIE S\000B
// FRA  DIM LUN MAR MER JEU VEN SAM
// NED  ZON MAA DIN WOE DON VRI ZAT
// DAN  S\000N MAN TIR ONS TOR FRE L\000R
// DEU  SON MON DIE MIT DON FRE SAM
// ITA  DOM LUN MAR MER GIO VEN SAB
// SVE  S\000N M\000N TIS ONS TOR FRE L\000R
// POL  NIE PON WTO \000RO CZE PI\000 SOB
// ROM  DUM LUN MAR MIE JOI VIN S\000M
// TUR  PAZ PZT SAL \000AR PER CUM CTS
// PYC  BC \000H B\000 CP \000\000 \000\000 C\000
//

//
// language table
//
// ENG
// POR
// ESP
// FRA
// NED
// DAN
// DEU
// ITA
// SVE
// POL
// ROM
// T\000R
// \000\000\000
//

typedef struct {
    char hours;     // 0-23
    char minutes;   // 0-59
    char seconds;   // 0-59
} TIME;

typedef struct {
    char day;
    char month;
    short year;
    char dow;       // 0=sun 1=mon 2=tue 3=wed 4=thu 5=fri 6=sat
} DATE;

typedef struct {
    DATE date;
    TIME time;
} DATE_TIME;

typedef struct {
    char hours;     // 0-23
    char minutes;   // 0-59
    char seconds;   // 0-59
    char ks;        // 0-99
} TIMER;

typedef struct {
    char    mode;

    // home screen
    struct home {
        struct {
            int light : 1;
            int hrs24 : 1;
            int show_db : 1;
            int show_dt : 1;
        } flags;
        DATE_TIME   dt;
        DATE_TIME   now;
        char        lang;
        char        contrast;
    } home;

    // DB
    struct {
        char flags;
        char init;
        char text[10];
        char pos;
        char digits[15];
        char page;
    } db;

    // Calculator
    struct {
        char flags;
        char current[15];
        char op;
        double acc;
    } cal;

    // Alarms
    struct {
        char        flags;
        DATE_TIME   alarms[5];
        char        pos;
    } al;

    // Dual time mode
    struct {
        struct {
            int show_db : 1;
            int show_home : 1;
        } flags;
        char        tz[3];
        DATE_TIME   dt;
    } dt;

    // Stop watch
    struct {
        struct {
            int running : 1;
            int split : 1;
        } flags;
        long    timer_start;
        long    timer_stop;
        long    timer_split;
    } st;
} CASIO;

TIMER timer_set_from_100ths(long diff)
{
    TIMER result;

    const int SECONDS_PER = (100);
    const int MINUTES_PER = (100*60);
    const int HOURS_PER = (100*60*60);

    result.hours = diff / HOURS_PER;
    diff -= result.hours * HOURS_PER;

    result.minutes = diff / MINUTES_PER;
    diff -= result.minutes * MINUTES_PER;

    result.seconds = diff / SECONDS_PER;
    diff -= result.seconds * SECONDS_PER;

    result.ks = diff;
    return result;
}

DATE_TIME epoch_to_date_time(long epoch)
{
    static unsigned char month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    static unsigned char week_days[7] = {4,5,6,0,1,2,3};
    // Thu=4, Fri=5, Sat=6, Sun=0, Mon=1, Tue=2, Wed=3

    unsigned char ntp_hour, ntp_minute, ntp_second;
    unsigned char ntp_week_day, ntp_date, ntp_month, leap_days;
    unsigned short temp_days;
    unsigned int ntp_year, days_since_epoch, day_of_year;

    DATE_TIME result;

    leap_days = 0;
    
    // Add or substract time zone here. 
    epoch += 0; //GMT +5:30 = +19800 seconds 
    
    ntp_second = epoch%60;
    epoch /= 60;
    ntp_minute = epoch%60;
    epoch /= 60;
    ntp_hour    = epoch%24;
    epoch /= 24;
        
    days_since_epoch = epoch;                       // number of days since epoch
    ntp_week_day = week_days[days_since_epoch%7];   // Calculating WeekDay
      
    ntp_year = 1970+(days_since_epoch/365);         // ball parking year, may not be accurate!
 
    int i;
    for(i=1972; i<ntp_year; i+=4)                   // Calculating number of leap days since epoch/1970
        if( ((i%4==0) && (i%100!=0)) || (i%400==0) ) leap_days++;
            
    // Calculating accurate current year by (days_since_epoch - extra leap days)
    ntp_year = 1970 + ((days_since_epoch - leap_days)/365);
    day_of_year = ((days_since_epoch - leap_days)%365)+1;

    if( ((ntp_year%4==0) && (ntp_year%100!=0)) || (ntp_year%400==0) )
    {
        month_days[1] = 29;    // February = 29 days for leap years
    }
    else
    {
        month_days[1] = 28;     // February = 28 days for non-leap years 
    }

    temp_days = 0;
   
    for(ntp_month=0 ; ntp_month <= 11 ; ntp_month++)    // calculating current Month
    {
        if (day_of_year <= temp_days) break; 
        temp_days = temp_days + month_days[ntp_month];
    }
    
    temp_days = temp_days - month_days[ntp_month-1]; //calculating current Date
    ntp_date = day_of_year - temp_days;

    result.date.year = ntp_year;
    result.date.month = ntp_month;
    result.date.day = ntp_date;
    result.date.dow = ntp_week_day;

    result.time.hours = ntp_hour;
    result.time.minutes = ntp_minute;
    result.time.seconds = ntp_second;

    return result;
}

long date_time_to_epoch(DATE_TIME *goal)
{
#define IsLeapYear(y) ( ((y%4==0) && (y%100!=0)) || (y%400==0) )

    static unsigned char month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const int SEC_PER_DAY = 24*60*60;
    int i;
    long epoch;

    epoch = (goal->date.year - 1970) * 365 * SEC_PER_DAY;

    for(i=1970; i < goal->date.year; i++)
    {
        if( IsLeapYear(i) )
        {
            epoch += SEC_PER_DAY;
        }
    }

    for(i=1; i < goal->date.month; i++)
    {
        if( i == 2 && IsLeapYear(goal->date.year) )
            epoch += 29 * SEC_PER_DAY;
        else
            epoch += month_days[i-1] * SEC_PER_DAY;
    }

    epoch += (goal->date.day-1) * SEC_PER_DAY;
    epoch += goal->time.hours * 3600;
    epoch += goal->time.minutes * 60;
    epoch += goal->time.seconds;

    return epoch;
}

int casio_disp_hours(int hrs24, int hours)
{
    if( ! hrs24 ) {
        if( hours > 12 )
            hours -= 12;
        if( hours == 0 )
            hours = 12;
    }
    return hours;
}

void casio_update_db_screen(char *frame, CASIO *c);
void casio_update_dt_screen(char *frame, CASIO *c);

void casio_update_home_screen(char *frame, CASIO *c)
{
    DATE_TIME *d;
    const char *w;
    char buf[20];
    int hours;

    if( c->home.flags.show_db )
    {
        casio_update_db_screen(frame, c);
        return;
    }
    else if( c->home.flags.show_dt )
    {
        casio_update_dt_screen(frame, c);
        return;
    }

    d = &c->home.now;

    hours = casio_disp_hours(c->home.flags.hrs24, d->time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( d->time.hours < 12 )
            draw_am1(frame);
        else
            draw_pm1(frame);
    }

    snprintf(buf, sizeof(buf), "%2d:%02d %02d",
                    hours,
                    d->time.minutes,
                    d->time.seconds);
    draw_main(frame, buf);

    snprintf(buf, sizeof(buf), "%2d %02d %2d-%2d",
                    d->date.year / 100,
                    d->date.year % 100,
                    d->date.month,
                    d->date.day);

    draw_secondary(frame, buf);

    switch(d->date.dow) {
    case 0: w = "SUN"; break;
    case 1: w = "MON"; break;
    case 2: w = "TUE"; break;
    case 3: w = "WED"; break;
    case 4: w = "THU"; break;
    case 5: w = "FRI"; break;
    case 6: w = "SAT"; break;
    }
    draw_text(frame, w);
}

void casio_update_db_screen(char *frame, CASIO *c)
{
    char buf[10];
    char *p;
    char ch, cstart;
    int i;

    if( c->db.init > 0 )
    {
        draw_text(frame, "\001DB");
        draw_main(frame, " F: 15");
    }
    else
    {
        if( c->db.page != 0 ) {
            //
            p = buf;
            ch = (c->db.page-1) * 28 + 1;
            for(i=0; i < 8; i++) {
                *p++ = ch++;
            }
            *p = '\0';
            draw_string(frame, 25, 0, 2, buf);

            p = buf;
            for(i=0; i < 10; i++) {
                *p++ = ch++;
            }
            *p = '\0';
            draw_string(frame, 5, 23, 2, buf);

            p = buf;
            for(i=0; i < 10; i++) {
                *p++ = ch++;
            }
            *p = '\0';
            draw_string(frame, 5, 45, 2, buf);

        } else {
            draw_text(frame, c->db.text);
            draw_main(frame, "--------");
            draw_secondary(frame, " - -- -- --");
        }
    }
}

void casio_update_cal_screen(char *frame, CASIO *c)
{
    char buf[20];
    char xbuf[20];
    int delim;
    int hours;
    char *p;

    if( c->cal.current[0] != '\0' )
    {
//      snprintf(buf, sizeof(buf), "%8s", c->cal.current);
        snprintf(buf, sizeof(buf), "%8s", c->cal.current);
    }
    else
    {
//      snprintf(buf, sizeof(buf), "%.2f", c->cal.acc);
        snprintf(xbuf, sizeof(xbuf), "%.9f", c->cal.acc);
        // 8.12345678
        // 0123456789
        for(p=xbuf+strlen(xbuf)-1; p > xbuf; p--)
        {
            if( *p == '.' )
                break;
            if( *p == '0' )
                *p = '\0';
        }
        snprintf(buf, sizeof(buf), "%9s", xbuf);
    }
    draw_main(frame, buf);

    if( c->cal.op == 1 ) {
        draw_text(frame, "  +");
    } else if( c->cal.op == 2 ) {
        draw_text(frame, "  -");
    } else if( c->cal.op == 3 ) {
        draw_text(frame, "  *");
    } else if( c->cal.op == 4 ) {
        draw_text(frame, "  /");
    } else {
        draw_text(frame, "\006\007\010");
    }

    if( c->home.now.time.seconds % 2 == 0 )
        delim = ':';
    else
        delim = ' ';

    hours = casio_disp_hours(c->home.flags.hrs24, c->home.now.time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( c->home.now.time.hours < 12 )
            draw_am2(frame);
        else
            draw_pm2(frame);
    }

    snprintf(buf, sizeof(buf), "%2d%c%02d",
            hours,
            delim,
            c->home.now.time.minutes );

    draw_secondary(frame, buf);
}

void casio_update_al_screen(char *frame, CASIO *c)
{
    char buf[20];
    int delim;
    int hours;

    draw_text(frame, "\004AL");
    draw_main(frame, "12:00 - 1");

    if( c->home.now.time.seconds % 2 == 0 )
        delim = ':';
    else
        delim = ' ';

    hours = casio_disp_hours(c->home.flags.hrs24, c->home.now.time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( c->home.now.time.hours < 12 )
            draw_am2(frame);
        else
            draw_pm2(frame);
    }

    snprintf(buf, sizeof(buf), "%2d%c%02d --- -",
            hours,
            delim,
            c->home.now.time.minutes );

    draw_secondary(frame, buf);

    switch(c->al.pos)
    {
    case E_HEX_BUTTON_0:
            draw_pm1(frame);
            draw_pm2(frame);
            break;

    case E_HEX_BUTTON_1:
            draw_am1(frame);
            draw_am2(frame);
            break;
            
    case E_HEX_BUTTON_2:
            draw_split(frame);
            draw_dst(frame);
            break;

    case E_HEX_BUTTON_3:    draw_snooze(frame); break;
    case E_HEX_BUTTON_4:    draw_mute(frame); break;
    case E_HEX_BUTTON_5:    draw_alarm(frame,1); break;
    case E_HEX_BUTTON_6:    draw_alarm(frame,2); break;
    case E_HEX_BUTTON_7:    draw_alarm(frame,3); break;
    case E_HEX_BUTTON_8:    draw_alarm(frame,4); break;
    case E_HEX_BUTTON_9:    draw_alarm(frame,5); break;
    case E_HEX_BUTTON_A:
            draw_sig(frame);
            draw_lt(frame);
            draw_3sec(frame);
            break;

    case E_HEX_BUTTON_STAR:
            draw_pm1(frame);
            draw_pm2(frame);
            draw_am1(frame);
            draw_am2(frame);
            draw_alarm(frame,1);
            draw_alarm(frame,2);
            draw_alarm(frame,3);
            draw_alarm(frame,4);
            draw_alarm(frame,5);
            draw_split(frame);
            draw_dst(frame);
            draw_sig(frame);
            draw_lt(frame);
            draw_3sec(frame);
            draw_snooze(frame);
            draw_mute(frame);
            break;
    }
}

void casio_update_st_screen(char *frame, CASIO *c)
{
    char buf[20];
    long diff;
    TIMER t;
    char delim;
    int hours, ks;

    if( c->st.flags.running )
    {
        diff = DEVICE.clock - c->st.timer_start;
        t = timer_set_from_100ths(diff);
        ks = t.ks;
    }
    else
    {
        diff = c->st.timer_stop - c->st.timer_start;
        t = timer_set_from_100ths(diff);
    }

    if( c->st.flags.split )
    {
        diff = c->st.timer_split - c->st.timer_start;
        t = timer_set_from_100ths(diff);

        draw_split(frame);
    }

    if( c->st.flags.running )
        delim = (ks < 50) ? ':' : ' ';
    else
        delim = ':';

    snprintf(buf, sizeof(buf), "%2d%c%02d %02d", t.hours, delim, t.minutes, t.seconds);
    draw_main(frame, buf);      

    if( c->home.now.time.seconds % 2 == 0 )
        delim = ':';
    else
        delim = ' ';

    hours = casio_disp_hours(c->home.flags.hrs24, c->home.now.time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( c->home.now.time.hours < 12 )
            draw_am2(frame);
        else
            draw_pm2(frame);
    }

    snprintf(buf, sizeof(buf), "%2d%c%02d    %02d",
            hours,
            delim,
            c->home.now.time.minutes,
            t.ks);
    draw_secondary(frame, buf);

    draw_text(frame, "\012ST");
}

void casio_update_dt_screen(char *frame, CASIO *c)
{
    DATE_TIME d;
    char buf[20];
    int delim;
    int hours;

    if( c->dt.flags.show_home )
    {
        casio_update_home_screen(frame, c);
        return;
    }
    else if( c->dt.flags.show_db )
    {
        casio_update_db_screen(frame, c);
        return;
    }

    d = epoch_to_date_time( DEVICE.epoch + 3*60*60 + 30*60 );

    hours = casio_disp_hours(c->home.flags.hrs24, d.time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( d.time.hours < 12 )
            draw_am1(frame);
        else
            draw_pm1(frame);
    }

    snprintf(buf, sizeof(buf), "%2d:%02d %02d",
                    hours,
                    d.time.minutes,
                    d.time.seconds);
    draw_main(frame, buf);

    if( c->home.now.time.seconds % 2 == 0 )
        delim = ':';
    else
        delim = ' ';

    hours = casio_disp_hours(c->home.flags.hrs24, c->home.now.time.hours);

    if( ! c->home.flags.hrs24 )
    {
        if( c->home.now.time.hours < 12 )
            draw_am2(frame);
        else
            draw_pm2(frame);
    }

    snprintf(buf, sizeof(buf), "%2d%c%02d",
            hours,
            delim,
            c->home.now.time.minutes );
    draw_secondary(frame, buf);

    draw_text(frame, "\005DT");
}

void casio_update_screen(CASIO *c)
{
    char frame[FRAME_SIZE];
    int rc;

    memset(frame, CLR_MASK, FRAME_SIZE);

    draw_hline(frame, 0, 15, 128);
    draw_hline(frame, 60, 4, 128-60);
    draw_hline(frame, 60, 10, 128-60);
    draw_vline(frame, 60, 0, 15);

    switch(c->mode)
    {
    case M_HOME:
        casio_update_home_screen(frame, c);
        break;
    case M_DB:
        casio_update_db_screen(frame, c);
        break;
    case M_CAL:
        casio_update_cal_screen(frame, c);
        break;
    case M_AL:
        casio_update_al_screen(frame, c);
        break;
    case M_ST:
        casio_update_st_screen(frame, c);
        break;
    case M_DT:
        casio_update_dt_screen(frame, c);
        break;
    }

    if( ! c->home.flags.light )
    {
        // disp_invert(frame);
    }

    rc = disp_update(frame);
    if( rc )
    {
        Serial.printf("disp_update %d\r\n", rc);
    }
}

void casio_process_home_event(int e, CASIO *c)
{
    int xx;

    if( e == E_BUTTONB )
    {
        c->mode = M_DB;
        c->db.init = 50;
    }
    else if( e == E_BUTTONC )
    {
        tone(24, 410, 80);
        c->home.flags.hrs24 = (c->home.flags.hrs24) ? 0 : 1;
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        if( e == E_HEX_BUTTON_A )
        {
            c->home.flags.show_dt = 1;
        }
        else if( e == E_HEX_BUTTON_D )
        {
            c->home.flags.show_db = 1;
        }
    }
    else if( e == E_HEX_BUTTON_A_RELEASE )
    {
        c->home.flags.show_dt = 0;
    }
    else if( e == E_HEX_BUTTON_D_RELEASE )
    {
        c->home.flags.show_db = 0;
    }
}

void casio_process_db_event(int e, CASIO *c)
{
    int xx;

    if( e == E_BUTTONB )
    {
        c->mode = M_CAL;
    }
    else if( e == E_BUTTONC )
    {
        tone(24, 410, 80);
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        switch(e)
        {
        case E_HEX_BUTTON_0:        strcpy(c->db.text, "ABC"); break;
        case E_HEX_BUTTON_1:        strcpy(c->db.text, "DEF"); break;
        case E_HEX_BUTTON_2:        strcpy(c->db.text, "GHI"); break;
        case E_HEX_BUTTON_3:        strcpy(c->db.text, "JKL"); break;
        case E_HEX_BUTTON_4:        strcpy(c->db.text, "MNO"); break;
        case E_HEX_BUTTON_5:        strcpy(c->db.text, "PQR"); break;
        case E_HEX_BUTTON_6:        strcpy(c->db.text, "STU"); break;
        case E_HEX_BUTTON_7:        strcpy(c->db.text, "VWX"); break;
        case E_HEX_BUTTON_8:        strcpy(c->db.text, "YZ1"); break;
        case E_HEX_BUTTON_9:        strcpy(c->db.text, "234"); break;

        case E_HEX_BUTTON_A:
                strcpy(c->db.text, "567");
                if( c->db.page == 0 ) {
                    c->db.page = 1;
                } else {
                    c->db.page -= 1;
                    if( c->db.page < 1 ) c->db.page = 5;
                }
                break;

        case E_HEX_BUTTON_B:        strcpy(c->db.text, "890"); break;
        case E_HEX_BUTTON_C:        strcpy(c->db.text, "%<>"); break;

        case E_HEX_BUTTON_D:
                strcpy(c->db.text, "\13\14\15");
                if( c->db.page == 0 ) {
                    c->db.page = 5;
                } else {
                    c->db.page += 1;
                    if( c->db.page > 5 ) c->db.page = 1;
                }
                break;

        case E_HEX_BUTTON_POUND:    strcpy(c->db.text, "\6\7\10"); break;
        case E_HEX_BUTTON_STAR:     strcpy(c->db.text, ""); break;
        }

    }
    else if( e == E_SECONDS15 )
    {
        if( c->db.init > 0 )
            c->db.init -= 1;
    }
}

void casio_process_cal_event(int e, CASIO *c)
{
    int xx, len;
    char buf[10];
    double val;

    if( e == E_BUTTONB )
    {
        c->mode = M_AL;
    }
    else if( e == E_BUTTONC )
    {
        c->cal.op = 0;
        c->cal.acc = 0.0;
        c->cal.current[0] = '\0';
        tone(24, 410, 80);
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_9 )
        {
            len = strlen(c->cal.current);
            if( len < 8 )
            {
                buf[0] = '0' + (e - E_HEX_BUTTON_0);
                buf[1] = '\0';
                strcat(c->cal.current, buf);
//              c->cal.op = 0;
            }
        }
        else if( e == E_HEX_BUTTON_STAR )
        {
            len = strlen(c->cal.current);
            if( len < 8 )
            {
                buf[0] = '.';
                buf[1] = '\0';
                strcat(c->cal.current, buf);
//              c->cal.op = 0;
            }
        }
        else if( e >= E_HEX_BUTTON_A && e <= E_HEX_BUTTON_D )
        {
            // 0=NO-OP, 1=A=+, 2=B=-, 3=C=*, 4=D=/

            if( c->cal.op != 0 )
            {
                // enter K-mode (constant mode)
            }

            c->cal.op = (e - E_HEX_BUTTON_A) + 1;
            val = atof(c->cal.current);
            c->cal.acc = val;
            c->cal.current[0] = '\0';
        }
        else if( e == E_HEX_BUTTON_POUND )
        {
            if( c->cal.op != 0 )
            {
                // parse number, apply op to number and store in acc
                val = atof(c->cal.current);
                switch(c->cal.op)
                {
                case 1: c->cal.acc += val; break;
                case 2: c->cal.acc -= val; break;
                case 3: c->cal.acc *= val; break;
                case 4: c->cal.acc /= val; break;
                }
                c->cal.current[0] = '\0';
                c->cal.op = 0;
            }
            else if( c->cal.current[0] != '\0' )
            {
                val = atof(c->cal.current);
                c->cal.acc = val;
                c->cal.current[0] = '\0';
                c->cal.op = 0;
            }
        }

    }
}

void casio_process_al_event(int e, CASIO *c)
{
    int xx;

    if( e == E_BUTTONB )
    {
        c->mode = M_ST;
    }
    else if( e == E_BUTTONC )
    {
        tone(24, 410, 80);
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        c->al.pos = e;
    }
}

void casio_process_st_event(int e, CASIO *c)
{
    int diff1, diff2;
    int xx;

    if( e == E_BUTTONA )
    {
        if( c->st.flags.split ) {
            c->st.flags.split = 0;
        } else {
            tone(24, 410, 80);
            if( c->st.flags.running ) {
                c->st.timer_split = DEVICE.clock;
                c->st.flags.split = 1;
            } else {
                c->st.timer_start = c->st.timer_stop = 0;
            }
        }
    }
    else if( e == E_BUTTONB )
    {
        c->mode = M_DT;
    }
    else if( e == E_BUTTONC )
    {
        tone(24, 410, 80);

        if( c->st.flags.running ) {
            c->st.timer_stop = DEVICE.clock;
            c->st.flags.running = 0;
        } else {
            diff1 = c->st.timer_stop - c->st.timer_start;
            if( c->st.flags.split )
            {
                diff2 = c->st.timer_split - c->st.timer_start;
            }

            c->st.timer_start = DEVICE.clock - diff1;
            c->st.flags.running = 1;

            if( c->st.flags.split )
            {
                c->st.timer_split = c->st.timer_start + diff2;
                c->st.flags.running = 1;
            }
        }
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        if( e == E_HEX_BUTTON_A )
        {
            c->home.contrast -= 10;
            disp_set_contrast(c->home.contrast);
        }
        else if( e == E_HEX_BUTTON_D )
        {
            c->home.contrast += 10;
            disp_set_contrast(c->home.contrast);
        }
    }
}

void casio_process_dt_event(int e, CASIO *c)
{
    int xx;

    if( e == E_BUTTONB )
    {
        c->mode = M_HOME;
    }
    else if( e >= E_HEX_BUTTON_0 && e <= E_HEX_BUTTON_POUND )
    {
        xx = (e - E_HEX_BUTTON_0)+1;

        tone(24, xx*100, 100);

        if( e == E_HEX_BUTTON_A )
        {
            c->dt.flags.show_home = 1;
        }
        else if( e == E_HEX_BUTTON_D )
        {
            c->dt.flags.show_db = 1;
        }
    }
    else if( e == E_HEX_BUTTON_A_RELEASE )
    {
        c->dt.flags.show_home = 0;
    }
    else if( e == E_HEX_BUTTON_D_RELEASE )
    {
        c->dt.flags.show_db = 0;
    }
}

void casio_process_event(int e, CASIO *c)
{
    if( e == E_SECONDS_TIMER )
    {
        c->home.now = epoch_to_date_time(DEVICE.epoch);
    }

    if( e == E_BUTTONL )
    {
        if( ! c->home.flags.light )
        {
            c->home.flags.light = 1;
            DEVICE.light = 160;
            disp_set_contrast(0xff);
        }
    }

    if( e == E_LIGHT_OFF )
    {
        c->home.flags.light = 0;
        disp_set_contrast(0x7f);
    }

    switch(c->mode)
    {
    case M_HOME:
        casio_process_home_event(e, c);
        break;
    case M_DB:
        casio_process_db_event(e, c);
        break;
    case M_CAL:
        casio_process_cal_event(e, c);
        break;
    case M_AL:
        casio_process_al_event(e, c);
        break;
    case M_ST:
        casio_process_st_event(e, c);
        break;
    case M_DT:
        casio_process_dt_event(e, c);
        break;
    }

    // cancel high speed tick events if stop watch not running
    if( (c->mode == M_ST && c->st.flags.running)
        || (c->mode == M_DB && c->db.init > 0) )
    {
        DEVICE.counter15_enable = 1;
    }
    else
    {
        DEVICE.counter15_enable = 0;
    }
}

void casio_init(CASIO *c)
{
    memset(c, 0, sizeof(*c));
    c->mode = M_HOME;

    c->home.dt.date.day = 24;
    c->home.dt.date.month = 4;
    c->home.dt.date.year = 2022;

    c->home.dt.time.hours = 13;
    c->home.dt.time.minutes = 58;
    c->home.dt.time.seconds = 00;

    c->home.contrast = 0x7f;

    DEVICE.epoch = date_time_to_epoch(&c->home.dt);
}

void casio_run()
{
    CASIO c;
    int e;

    make_ascii();

    device_setup();

    casio_init(&c);

    disp_clear();

    for(;;)
    {
        e = device_get_event();

        casio_process_event(e, &c);
        casio_update_screen(&c);
    }
}

//////////////////////////////////////////////////////////////////////
// end CASIO
//////////////////////////////////////////////////////////////////////

extern "C" int main(void)
{
    casio_run();
}
