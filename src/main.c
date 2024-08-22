#include <mcs51/at89x051.h>
#include <stdint.h>
/* 
 * P1 = which segments to light, 0 = on, 1 = off
 *  P1_0 = g
 *  P1_1 = f
 *  P1_2 = a
 *  P1_3 = b
 *  P1_4 = c
 *  P1_5 = e
 *  P1_6 = d
 *  P1_7 = double colon (2 x 2 single leds)
 * 
 * P3 = which display to activate, 0 = on, 1 = off
 *  P3_0 = index 1, second digit from the left
 *  P3_1 = index 4, second digit from the right
 *  P3_2 = index 3, third digit from the right
 *  P3_3 = index 5, right most digit
 *  P3_4 = index 0, left most digit
 *  P3_5 = index 2, third digit from the left
 * 
 * P3 = inputs/outputs
 *  P3_2 = Button (switch) 1
 *  P3_4 = Button (switch) 2
 *  P3_5 = Button (switch) 3
 *  P3_6 = empty / unused
 *  P3_7 = buzzer
 * 
 * CLOCK OPERATION INSTRUCTIONS
 *  B1 = S1 = Right Button
 *  B2 = S2 = Middle Button
 *  B3 = S3 = Left Button
 *
 *  Set current time:
 *   1.  start clock in NORMAL mode (showing current time)
 *   2.  LONG press B1 to edit time's hour
 *   3a. SHORT press B2 to increase time's hour
 *   3b. or LONG press B2 to increase time's hour quickly
 *   4.  SHORT press B1 to edit time's minute
 *   5a. SHORT press B2 to increase time's minute
 *   5b. or LONG press B2 to increase time's minute quickly
 *   6.  SHORT press B1 to return to NORMAL mode
 * 
 *  Set alarm time:
 *   1.  start clock in NORMAL mode (showing current time)
 *   2.  SHORT press B1 to switch to ALARM mode
 *   2.  LONG press B2 to edit alarm's hour
 *   3a. SHORT press B2 to increase alarm's hour
 *   3b. or LONG press B2 to increase alarm's hour quickly
 *   4.  SHORT press B1 to edit alarm's minute
 *   5a. SHORT press B2 to increase alarm's minute
 *   5b. or LONG press B2 to increase alarm's minute quickly
 *   6.  SHORT press B1 to return to NORMAL mode
 * 
 *  Display Modes
 *   - current time (hh:mm:ss)
 *   - alarm time (hh:mm)
 *   - alarm enable y/n
 */

// so said EVELYN the modified DOG
#pragma less_pedantic

// Clock related defines
#define CLOCK_TIMER_HIGH      0x3C
#define CLOCK_TIMER_LOW       0xD5 // original ASM source was 0xB5, but experimentation shows this should be a more accurate value
#define CLOCK_TIMER_COUNT     20
#define CLOCK_COLON_COUNT     10   // 1/2 of CLOCK_TIMER_COUNT
#define CLOCK_BLINK_COUNT     5
#define CLOCK_INCREMENT_COUNT 4

// Button related defines
#define BUTTON_PRESS          2
#define BUTTON_PRESS_LONG     40

// Display related defines
#define LED_BLANK            10
#define LED_A                11
#define LED_L                12
#define LED_y                13
#define LED_n                14

// digit to led digit lookup table
// 0 = on, 1 = off
// dp,d,e,c,b,a,f,g
const uint8_t ledtable[] = {
    0b10000001,    // 0
    0b11100111,    // 1
    0b10010010,    // 2
    0b10100010,    // 3
    0b11100100,    // 4
    0b10101000,    // 5
    0b10001000,    // 6
    0b11100011,    // 7
    0b10000000,    // 8
    0b10100000,    // 9
    0b11111111,    // <blank>
    0b11000000,    // 'A'
    0b10011101,    // 'L'
    0b10100100,    // 'y'
    0b11001110,    // 'n'
};

// display buffer
uint8_t dbuf[6];

// mapping of 0-indexed digits to ICSK058A's 7-segment digits
const uint8_t digitmap[] = {
    4, // 0, left most digit
    0, // 1, second from the left
    5, // 2, third from the left
    2, // 3, third from the right
    1, // 4, second from the right
    3  // 5, right most digit
};

// clock variables
volatile uint8_t clock_hour = 12;
volatile uint8_t clock_minute = 0;
volatile uint8_t clock_second = 55;
volatile uint8_t next_second = CLOCK_TIMER_COUNT;
volatile uint8_t next_blink = CLOCK_BLINK_COUNT;
volatile uint8_t next_increment = CLOCK_INCREMENT_COUNT;

volatile __bit CLOCK_RUNNING = 1;

// alarm variables
volatile uint8_t alarm_hour = 6;
volatile uint8_t alarm_minute = 58;
volatile __bit ALARM_ENABLE = 0;

// flag to determine whether or not to display the colon (every half second)
// can also be repurposed for blinking numbers during time set
volatile __bit show_colon = 0;

// flag when to display a digit (for blinking purposes during set time mode)
volatile __bit show_blink = 0;

// flag when to make the next digit increment
// used while holding down the digit increment button during set time mode
volatile __bit clock_increment = 0;

// button variables
volatile uint8_t debounce[0] = {0, 0, 0};
volatile __bit B1_PRESSED = 0;
volatile __bit B1_RELEASED = 0;
volatile __bit B1_PRESSED_LONG = 0;
volatile __bit B1_RELEASED_LONG = 0;
volatile __bit B2_PRESSED = 0;
volatile __bit B2_RELEASED = 0;
volatile __bit B2_PRESSED_LONG = 0;
volatile __bit B2_RELEASED_LONG = 0;
// volatile __bit B3_PRESSED = 0;
// volatile __bit B3_RELEASED = 0;
// volatile __bit B3_PRESSED_LONG = 0;
// volatile __bit B3_RELEASED_LONG = 0;

// clock state
typedef enum {
    NORMAL,
    EDIT_HOUR,
    EDIT_MIN,
    SHOW_ALARM,
    EDIT_ALARM_HOUR,
    EDIT_ALARM_MIN,
    ENABLE_ALARM,
    ALARMING
} clock_state_t;
clock_state_t clock_state = NORMAL;

// check button status and set appropriate flags
// Bn_PRESSED* flags will be set ONCE PER PRESS. This means software can UNSET these flags if desired.
void button_status(void) {

    // button 1
    if (P3_2 == 0) {
        if (debounce[0] < BUTTON_PRESS_LONG) {
            debounce[0]++;
            if (debounce[0] == BUTTON_PRESS) {
                B1_RELEASED = 0;
                B1_PRESSED = 1;    
            } 
            if (debounce[0] == BUTTON_PRESS_LONG) {
                B1_RELEASED_LONG = 0;
                B1_PRESSED_LONG = 1;
            }    
        }
    } else {
        debounce[0] = 0;
        if (B1_PRESSED) {
            B1_RELEASED = 1;
        }
        B1_PRESSED = 0;
        if (B1_PRESSED_LONG) {
            B1_RELEASED_LONG = 1;
        }
        B1_PRESSED_LONG = 0;
    }

    // button 2
    if (P3_4 == 0) {
        if (debounce[1] < BUTTON_PRESS_LONG) {
            debounce[1]++;
            if (debounce[1] == BUTTON_PRESS) {
                B2_RELEASED = 0;
                B2_PRESSED = 1;
            } 
            if (debounce[1] == BUTTON_PRESS_LONG) {
                B2_RELEASED_LONG = 0;
                B2_PRESSED_LONG = 1;
            }
        }
    } else {
        debounce[1] = 0;
        if (B2_PRESSED) {
            B2_RELEASED = 1;
        }
        B2_PRESSED = 0;
        if (B2_PRESSED_LONG) {
            B2_RELEASED_LONG = 1;
        }
        B2_PRESSED_LONG = 0;
    }

    // // button 3
    // if (P3_5 == 0) {
    //     if (debounce[2] < BUTTON_PRESS_LONG) {
    //         debounce[2]++;
    //         if (debounce[2] == BUTTON_PRESS) {
    //             B3_RELEASED = 0;
    //             B3_PRESSED = 1;
    //         } 
    //         if (debounce[2] == BUTTON_PRESS_LONG) {
    //             B3_RELEASED_LONG = 0;
    //             B3_PRESSED_LONG = 1;
    //         }
    //     }
    // } else {
    //     debounce[2] = 0;
    //     if (B3_PRESSED) {
    //         B3_RELEASED = 1;
    //     }
    //     B3_PRESSED = 0;
    //     if (B3_PRESSED_LONG) {
    //         B3_RELEASED_LONG = 1;
    //     }
    //     B3_PRESSED_LONG = 0;
    // }
}

void increment_hour_ref(uint8_t* h) {
    if (++*h == 24) {
        *h = 0;
    }
}
// void decrement_hour_ref(uint8_t* h) {
//     if (--*h == -1) {
//         *h = 23;
//     }
// }
void increment_minute_ref(uint8_t* m) {
    if (++*m == 60) {
        *m = 0;
    }
}
void decrement_minute_ref(uint8_t* m) {
    if (--*m == -1) {
        *m = 59;
    }
}
void increment_second() {
    if (++clock_second == 60) {
        clock_second = 0;
        if (CLOCK_RUNNING) {
            increment_minute_ref(&clock_minute);
        }
    }
}

// Timer0 ISR, used to maintain the time, executes every 50ms
void timer0_isr(void) __interrupt (1) __using (1) {

    // reset timer
    TL0 = CLOCK_TIMER_LOW;
    TH0 = CLOCK_TIMER_HIGH;

    // is the clock running?
    if (CLOCK_RUNNING) {

        // then keep track of when to increment the seconds
        if (--next_second == 0) {
            next_second = CLOCK_TIMER_COUNT;
            show_colon = 1;
            increment_second();
        } else if (next_second == CLOCK_COLON_COUNT) {
            show_colon = 0;
        }
    } 

    // toggle digit blinking
    if (--next_blink == 0) {
        next_blink = CLOCK_BLINK_COUNT;
        show_blink = !show_blink;
    }

    // control digit increment
    if (--next_increment == 0) {
        next_increment = CLOCK_INCREMENT_COUNT;
        clock_increment = 1;        // flag will be unset by software
    }

    button_status();
}

// execute 500 DJNZ instructions which should equate to 1 millisecond
void delay1ms(void) {
    __asm
        MOV R0,#250
        MOV R1,#250
    00001$:
        DJNZ R0, 00001$
    00002$:
        DJNZ R1, 00002$
    __endasm;
}

void delay(uint16_t ms) {
    do {
        delay1ms();
    } while (--ms > 0);
}

// display refresh
void display_update(void) {
    uint8_t digit = 6;

    // disable all digits
    P3 |= 0x3F;

    for (digit = 0; digit<6; digit++) {
    
        // enable appropriate segments
        P1 = dbuf[digit];

        // enable appropriate digit (set it to 0; leave others 1)
        P3 &= (~(1 << digitmap[(digit % 6)]));

        // delay
        delay1ms();

        // disable all digits
        P3 |= 0x3F;
    }

    // Perform actual colon blinking
    if (show_colon) {
        P1_7 = 0;
    } else {
        P1_7 = 1;
    }
}

// set the display buffer first and second digits to the current hour
void set_hour_dbuf(uint8_t display_hour) {
    dbuf[0] = ledtable[(display_hour/10)];
    dbuf[1] = ledtable[(display_hour%10)];
}

void init(void) {

    // Display initialization
    P1 = ledtable[LED_BLANK]; // disable all segments
	P3 |= 0x3F;	              // disable all digits
    
    // Timer0 initialization
    TMOD = 0x01;            // Set Timer0 to mode 1, 16-bit 
    TH0 = CLOCK_TIMER_HIGH;
    TL0 = CLOCK_TIMER_LOW;        // Set counter to 15541, overlfow at 65536, a difference of 49995; about 50 milliseconds per trigger
    PT0 = 1;            // Giver Timer0 high priority
    ET0 = 1;                // Enable Timer0 interrupt
    TR0 = 1;                // Enable Timer0
    EA = 1;                // Enable global interrupts

    // Other
    P3_7 = 1;            // disable buzzer
}

void main(void) {

    // initialization routine
    init();

    // main program loop
    while(1) {

        // alarm mode
        if (ALARM_ENABLE && CLOCK_RUNNING && alarm_hour == clock_hour && alarm_minute == clock_minute) {
            if (clock_second == 0 && clock_state != ALARMING) {
                clock_state = ALARMING;
            } 
            if (clock_state == ALARMING) {
                if (show_colon == 1) {
                    P3_7 = !P3_7; // this toggling creates an interesting effect
                } else {
                    P3_7 = 1; // if doing the toggling effect, need to make sure buzzer is off during blink
                }
            }
        } else if (clock_state == ALARMING) { 
            clock_state = NORMAL; // turn off the alarm after 1 minute
            P3_7 = 1;             // turn off the alarm
        }

        // handle button events based on current clock state
        switch (clock_state) {

            case ALARMING:
                if (B1_RELEASED || B2_RELEASED /* || B3_RELEASED */) {
                    clock_state = NORMAL;
                    P3_7 = 1;                // turn off alarm
                    B1_RELEASED = 0;
                    B2_RELEASED = 0;
                    // B3_RELEASED = 0;
                }
                break;

            case EDIT_ALARM_HOUR:
                if (B1_PRESSED) {
                    clock_state = EDIT_ALARM_MIN;
                    B1_PRESSED = 0;
                } else if (B2_PRESSED) {
                    increment_hour_ref(&alarm_hour);
                    B2_PRESSED = 0;                    
                } else if (B2_PRESSED_LONG && clock_increment == 1) {
                    increment_hour_ref(&alarm_hour);
                    clock_increment = 0;
                } /* else if (B3_PRESSED) {
                    decrement_hour_ref(&alarm_hour);
                    B3_PRESSED = 0;                    
                } else if (B3_PRESSED_LONG && clock_increment == 1) {
                    decrement_hour_ref(&alarm_hour);
                    clock_increment = 0;
                } */
                break;

            case EDIT_ALARM_MIN:
                if (B1_PRESSED) {
                    clock_state = ENABLE_ALARM;
                    B1_PRESSED = 0;
                } else if (B2_PRESSED) {
                    increment_minute_ref(&alarm_minute);
                    B2_PRESSED = 0;
                } else if (B2_PRESSED_LONG && clock_increment == 1) {
                    increment_minute_ref(&alarm_minute);
                    clock_increment = 0;
                } /* else if (B3_PRESSED) {
                    decrement_minute_ref(&alarm_minute);
                    B3_PRESSED = 0;
                } else if (B3_PRESSED_LONG && clock_increment == 1) {
                    decrement_minute_ref(&alarm_minute);
                    clock_increment = 0;
                } */
                break;

            case ENABLE_ALARM:
                if (B1_PRESSED) {
                    clock_state = NORMAL;
                    B1_PRESSED = 0;
                } else if (B2_PRESSED) {
                    ALARM_ENABLE = !ALARM_ENABLE;
                    B2_PRESSED = 0;
                }
                break;

            case SHOW_ALARM:
                if (B1_PRESSED_LONG) {
                    clock_state = EDIT_ALARM_HOUR;
                    B1_PRESSED = 0;
                    B1_PRESSED_LONG = 0;
                } else if (B1_RELEASED) {
                    clock_state = ENABLE_ALARM;
                    B1_RELEASED = 0;
                }
                break;
    
            case EDIT_HOUR:
                CLOCK_RUNNING = 0;
                if (B1_PRESSED) {
                    clock_state = EDIT_MIN;
                    B1_PRESSED = 0;
                } else if (B2_PRESSED) {
					// hold down the button to rapidly increase hour
                    increment_hour_ref(&clock_hour);
					// reset seconds to 0 when time is changed
                    clock_second = 0;
                    B2_PRESSED = 0;                    
                } else if (B2_PRESSED_LONG && clock_increment == 1) {
                    increment_hour_ref(&clock_hour);
                    clock_increment = 0;
                } /* else if (B3_PRESSED) {
                    decrement_hour_ref(&clock_hour); // hold down the button to rapidly decrease hour
                    clock_second = 0; // reset seconds to 0 when time is changed
                    B3_PRESSED = 0;                    
                } else if (B3_PRESSED_LONG && clock_increment == 1) {
                    decrement_hour_ref(&clock_hour); // hold down the button to rapidly decrease hour
                    clock_increment = 0;
                } */
                break;

            case EDIT_MIN:
                if (B1_PRESSED) {
                    clock_state = NORMAL;
                    B1_PRESSED = 0;
                    CLOCK_RUNNING = 1;
                } else if (B2_PRESSED) {
                    increment_minute_ref(&clock_minute);
                    clock_second = 0;
                    B2_PRESSED = 0;
                } else if (B2_PRESSED_LONG && clock_increment == 1) {
                    increment_minute_ref(&clock_minute);
                    clock_increment = 0;
                } /* else if (B3_PRESSED) {
                    decrement_minute_ref(&clock_minute);
                    clock_second = 0; // reset seconds to 0 when time is changed
                    B3_PRESSED = 0;
                } else if (B3_PRESSED_LONG && clock_increment == 1) {
                    // hold down the button to rapidly decrease minute
                    decrement_minute_ref(&clock_minute);
                    clock_increment = 0;
                } */
                break;
            
            case NORMAL:
            default:
                if (B2_RELEASED) {
                    clock_state = CLOCK_RUNNING;
                    B2_RELEASED = 0;
                } else if (B1_PRESSED_LONG) {
                    clock_state = EDIT_HOUR;
                    B1_PRESSED = 0;
                    B1_PRESSED_LONG = 0;
                } else if (B1_RELEASED) {
                    clock_state = SHOW_ALARM;
                    B1_RELEASED = 0;
                }
                break;
        }

        // generate display based on current clock state
        // button events and display are separated as state may change during button events
        switch (clock_state) {

            case ALARMING:
                if (show_colon == 1) {
                    set_hour_dbuf(clock_hour);
                    dbuf[2] = ledtable[(clock_minute/10)];
                    dbuf[3] = ledtable[(clock_minute%10)];
                    dbuf[4] = ledtable[0];
                    dbuf[5] = ledtable[0];
                } else {
                    dbuf[0] = ledtable[LED_BLANK];
                    dbuf[1] = ledtable[LED_BLANK];
                    dbuf[2] = ledtable[LED_BLANK];
                    dbuf[3] = ledtable[LED_BLANK];
                    dbuf[4] = ledtable[LED_BLANK];
                    dbuf[5] = ledtable[LED_BLANK];
                }
                break;

            case ENABLE_ALARM:
                dbuf[0] = ledtable[LED_A];
                dbuf[1] = ledtable[LED_L];
                dbuf[2] = ledtable[LED_BLANK];
                if (ALARM_ENABLE) {
                    dbuf[3] = ledtable[LED_y];
                } else {
                    dbuf[3] = ledtable[LED_n];
                }
                dbuf[4] = ledtable[LED_BLANK];
                dbuf[5] = ledtable[LED_BLANK];
                break;

			case EDIT_ALARM_HOUR:

                if (show_blink == 1) {
                    set_hour_dbuf(alarm_hour);
                } else {
                    dbuf[0] = ledtable[LED_BLANK];
                    dbuf[1] = ledtable[LED_BLANK];
                }
                dbuf[2] = ledtable[(alarm_minute/10)];
                dbuf[3] = ledtable[(alarm_minute%10)];
                dbuf[4] = ledtable[LED_BLANK];
                dbuf[5] = ledtable[LED_BLANK];
                break;

            case EDIT_ALARM_MIN:

                set_hour_dbuf(alarm_hour);
                if (show_blink == 1) {
                    dbuf[2] = ledtable[(alarm_minute/10)];
                    dbuf[3] = ledtable[(alarm_minute%10)];
                } else {
                    dbuf[2] = ledtable[LED_BLANK];
                    dbuf[3] = ledtable[LED_BLANK];
                }
                dbuf[4] = ledtable[LED_BLANK];
                dbuf[5] = ledtable[LED_BLANK];
                break;

            case SHOW_ALARM :

                if (show_colon == 1) {
                    set_hour_dbuf(alarm_hour);
                    dbuf[2] = ledtable[(alarm_minute/10)];
                    dbuf[3] = ledtable[(alarm_minute%10)];
                    dbuf[4] = ledtable[0];
                    dbuf[5] = ledtable[0];
                } else {
                    dbuf[0] = ledtable[LED_BLANK];
                    dbuf[1] = ledtable[LED_BLANK];
                    dbuf[2] = ledtable[LED_BLANK];
                    dbuf[3] = ledtable[LED_BLANK];
                    dbuf[4] = ledtable[LED_BLANK];
                    dbuf[5] = ledtable[LED_BLANK];
                }
                break;

            case EDIT_MIN:

                set_hour_dbuf(clock_hour);
                if (show_blink == 1) {
                    dbuf[2] = ledtable[(clock_minute/10)];
                    dbuf[3] = ledtable[(clock_minute%10)];
                } else {
                    dbuf[2] = ledtable[LED_BLANK];
                    dbuf[3] = ledtable[LED_BLANK];
                }
                dbuf[4] = ledtable[LED_BLANK];
                dbuf[5] = ledtable[LED_BLANK];

                // colon does not blink when setting time
                show_colon = 0;
                break;

            case EDIT_HOUR:

                if (show_blink == 1) {
                    set_hour_dbuf(clock_hour);
                } else {
                    dbuf[0] = ledtable[LED_BLANK];
                    dbuf[1] = ledtable[LED_BLANK];
                }
                dbuf[2] = ledtable[(clock_minute/10)];
                dbuf[3] = ledtable[(clock_minute%10)];
                dbuf[4] = ledtable[(clock_second/10)];
                dbuf[5] = ledtable[(clock_second%10)];

                // colon does not blink when setting time
                show_colon = 0;
                break;

            case NORMAL:
            default:

                // update display buffer to show current time
                set_hour_dbuf(clock_hour);
                dbuf[2] = ledtable[(clock_minute/10)];
                dbuf[3] = ledtable[(clock_minute%10)];
                dbuf[4] = ledtable[(clock_second/10)];
                dbuf[5] = ledtable[(clock_second%10)];
                break;
        }

        // update the display
        display_update();
    }
}
