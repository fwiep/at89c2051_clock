#include <mcs51/at89x051.h>
#include <stdint.h>

/* KIT BUILD INSTRUCTION NOTES
 *   - Do not install R3 and R4 as the MCU has internal pullups and they disrupt operation when 
 *     on battery backup. 
 *   - R6 is a trickle-charge for a rechargeable backup battery. If using non-rechargeable backup
 *     battery this could create issues.
 * 
 * Not optimize for anything; once feature complete, should look at optimizing for code size
 *
 * TODO: 
 *   - finish alarm
 *       - Timer1 for a 1 minute alarm (or use something in the CLOCK routine????
 *	 - display dimmer
 *       - will have to put toggling of buzzer into a timer ISR to avoid differences in main loop
 *         timing because display_update = 5ms when display on, and like .001ms when display is not on
 *   - look at optimizations (mainly just removing repetitive code, unnecessary variables) 
 *     such as moving to a single machine state variable and enum
 *
 *
 * B1 short press should change to display alarm
 * B1 long press on alarm display should edit alarm
 * how to read a short B1 press w/o screwing with b1_long press?
 *
 * trigger a "pressed" on release
 * trigger a "long" while still held down
 *
 * but i like an immediate short press pickup when setting time
 * could always reduce long press time
 *
 *
 *	Button Functions Described!
 *
 *	B1 = S1 = Left Button
 *	B2 = S2 = Right Button
 *
 *	One button will be for cycling through display modes, and to increment number during set time mode
 *  Other button, triggered by a LONG press, will enter edit time mode; also used to cycle through values being edited
 *
 *  S1 = edit, digit cycle
 *  S2 = display mode cycle
 *
 *  Display Modes
 *		- current time (hh:mm), blinking colon
 *		- current minute/seconds (mm:ss), blinkin colon
 *		- 12/24hr format (no AM/PM indicator, perhaps not suited for this clock)
 *		- alarm, STATIC colon
 *        there's no AM/PM indicator, so really can only do 24hr for setting alarm
 *		- alarm enable
 *		- date ??? (thats... best left to an RTC, leap years and shit)
 *
 */


// so said EVELYN the modified DOG
#pragma less_pedantic

// Clock related defines
#define CLOCK_TIMER_HIGH		0x3C
#define CLOCK_TIMER_LOW			0xD5	// original ASM source was 0xB5, but experimentation shows this should be a more accurate value
#define CLOCK_TIMER_COUNT		20
#define CLOCK_COLON_COUNT		10		// 1/2 of CLOCK_TIMER_COUNT
#define CLOCK_BLINK_COUNT		5
#define CLOCK_INCREMENT_COUNT	4
#define MAX_DISPLAY_DIM			20

// Button related defines
#define BUTTON_PRESS			2
#define BUTTON_PRESS_LONG		40

// Display related defines
#define LED_BLANK			10
#define LED_h				11
#define LED_A				12
#define LED_L				13
#define LED_y				14
#define LED_n				15

// digit to led digit lookup table
// 1 = on, 0 = off
// a, f, b, e, d, c, g, dp
const uint8_t ledtable[] = {
	0b11111100,	// 0
	0b00100100,	// 1
	0b10111010,	// 2
	0b10101110,	// 3
	0b01100110,	// 4
	0b11001110,	// 5
	0b11011110,	// 6
	0b10100100,	// 7
	0b11111110,	// 8
	0b11101110,	// 9
	0b00000000, // <blank>
	0b01010110,	// 'h'
	0b11110110, // 'A'
	0b01011000,	// 'L'
	0b01101110,	// 'y'
	0b00010110, // 'n'
};

// display buffer
uint8_t dbuf[4];

// clock variables
volatile uint8_t clock_hour = 12;
volatile uint8_t clock_minute = 0;
volatile uint8_t clock_second = 55;
volatile uint8_t next_second = CLOCK_TIMER_COUNT;
volatile uint8_t next_blink = CLOCK_BLINK_COUNT;
volatile uint8_t next_increment = CLOCK_INCREMENT_COUNT;

volatile __bit CLOCK_RUNNING = 1;
volatile __bit TWELVE_TIME = 0;			// 0 = 24 hour, 1 = 12 hour

// alarm variables
volatile uint8_t alarm_hour = 12;
volatile uint8_t alarm_minute = 1;
volatile __bit ALARM_ENABLE = 1;

// flag to determine whether or not to display the colon (every half second)
// can also be repurposed for blinking numbers during time set
volatile __bit show_colon = 0;

// flag when to display a digit (for blinking purposes during set time mode)
volatile __bit show_blink = 0;

// flag when to make the next digit increment
// used while holding down the digit increment button during set time mode
volatile __bit clock_increment = 0;

// number of main program loops per single display refresh, valid values are 1 - MAX_DISPLAY_DIM
//uint8_t display_dimmer = 10;

// button variables
volatile uint8_t debounce[0] = {0, 0};
volatile __bit B1_PRESSED = 0;
volatile __bit B1_RELEASED = 0;
volatile __bit B1_PRESSED_LONG = 0;
volatile __bit B2_PRESSED = 0;
volatile __bit B2_RELEASED = 0;
volatile __bit B2_PRESSED_LONG = 0;

// button modes
typedef enum {
	B_NORMAL,
	B_MIN_SEC,
	B_EDIT_HOUR,
	B_EDIT_MIN,
	B_SET_24H,
	B_SHOW_ALARM,
	B_EDIT_ALARM_HOUR,
	B_EDIT_ALARM_MIN,
	B_ENABLE_ALARM,
	B_ALARMING
} button_mode_t;
button_mode_t bmode = B_NORMAL;

// display modes
typedef enum {
	D_NORMAL,
	D_MIN_SEC,
	D_EDIT_HOUR,
	D_EDIT_MIN,
	D_SET_24H,
	D_SHOW_ALARM,
	D_EDIT_ALARM_HOUR,
	D_EDIT_ALARM_MIN,
	D_ENABLE_ALARM,
	D_ALARMING
} display_mode_t;
display_mode_t dmode = D_NORMAL;

// check button status and set appropriate flags
// Bn_PRESSED* flags will be set ONCE PER PRESS. This means software can UNSET these flags if desired.
void button_status(void) {

	// button 1
	if (P3_4 == 0) {
		if (debounce[0] < BUTTON_PRESS_LONG) {
			debounce[0]++;
			if (debounce[0] == BUTTON_PRESS) {
				B1_RELEASED = 0;
				B1_PRESSED = 1;	
			} 
			if (debounce[0] == BUTTON_PRESS_LONG) {
				B1_PRESSED_LONG = 1;
			}	
		}
	} else {
		debounce[0] = 0;
		if (B1_PRESSED) {
			B1_RELEASED = 1;
		}
		B1_PRESSED = 0;
		B1_PRESSED_LONG = 0;
	}

	// button 2
	if (P3_5 == 0) {
		if (debounce[1] < BUTTON_PRESS_LONG) {
			debounce[1]++;
			if (debounce[1] == BUTTON_PRESS) {
				B2_RELEASED = 0;
				B2_PRESSED = 1;
			} 
			if (debounce[1] == BUTTON_PRESS_LONG) {
				B2_PRESSED_LONG = 1;
			}
		}
	} else {
		debounce[1] = 0;
		if (B2_PRESSED) {
			B2_RELEASED = 1;
		}
		B2_PRESSED = 0;
		B2_PRESSED_LONG = 0;
	}
}

// increment hour by 1
void increment_hour() {
	if (++clock_hour == 24) {
		clock_hour = 0;
	}
}

// increment minute by 1
void increment_minute() {
	if (++clock_minute == 60) {
		clock_minute = 0;
		if (CLOCK_RUNNING) {
			increment_hour();
		}
	}
}

// increment second by 1
void increment_second() {
	if (++clock_second == 60) {
		clock_second = 0;
		if (CLOCK_RUNNING) {
			increment_minute();
		}
	}
}

void increment_alarm_hour() {
	if (++alarm_hour == 24) {
		alarm_hour = 0;
	}
}

// increment minute by 1
void increment_alarm_minute() {
	if (++alarm_minute == 60) {
		alarm_minute = 0;
		if (CLOCK_RUNNING) {
			increment_alarm_hour();
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
		clock_increment = 1;						// flag will be unset by software
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
	uint8_t digit = 4;
//	static uint8_t display_dimmer_counter = 0;

//	if (display_dimmer_counter == 0) {
//		display_dimmer_counter = display_dimmer;

		// disable all digits
		P3 |= 0x0F;

		for (digit = 0; digit<4; digit++) {
		
			// enable appropriate segments
			P1 = dbuf[digit];

			// enable appropriate digit (set it to 0; leave others 1)
			P3 &= (~(1 << digit));

			// delay
			delay1ms();

			// disable all digits
			P3 |= 0x0F;
		}
//	} else {
//		display_dimmer_counter--;
//		delay1ms();
//	}
}

// set the display buffer first and second digits to the current hour
void set_hour_dbuf(uint8_t display_hour) {
	uint8_t hour;

	if (TWELVE_TIME) {
		hour = display_hour % 12;
		if (hour==0) {
			hour = 12;
		}
	} else {
		hour = display_hour;
	}

	dbuf[0] = ledtable[(hour/10)];
	dbuf[1] = ledtable[(hour%10)];
}

void init(void) {

	// Display initialization
	P1 = 0x00;				// disable all segments
	P3 |= 0x04;				// disable all digits

	// Timer0 initialization
    TMOD = 0x01;			// Set Timer0 to mode 1, 16-bit 
    TH0 = CLOCK_TIMER_HIGH;
    TL0 = CLOCK_TIMER_LOW;	// Set counter to 15541, overlfow at 65536, a difference of 49995; about 50 milliseconds per trigger
	PT0 = 1;				// Giver Timer0 high priority
    ET0 = 1;		        // Enable Timer0 interrupt
    TR0 = 1;    		    // Enable Timer0
    EA = 1;					// Enable global interrupts

	// Other
	P3_7 = 1;				// disable buzzer
}

void main(void) {

	// initialization routine
	init();

	// main program loop
    while(1) {

		// alarm mode
		if (ALARM_ENABLE && CLOCK_RUNNING && alarm_hour == clock_hour && alarm_minute == clock_minute) {
			if (clock_second == 0 && bmode != B_ALARMING) {
				bmode = B_ALARMING;
				dmode = D_ALARMING;
			} 
			if (bmode == B_ALARMING) {
				if (show_colon == 1) {
					P3_7 = !P3_7;				// P3_7 = show_colon; (this toggling creates an interesting effect)
				} else {
					P3_7 = 1;					// if doing the toggling effect, need to make sure buzzer is off during blink
				}
			}
		} else if (bmode == B_ALARMING) {	// turn off the alarm after 1 minute
			bmode = B_NORMAL;
			dmode = D_NORMAL;
			P3_7 = 1;						// turn off the alarm
		}

		// handle button events
		switch (bmode) {

			case B_ALARMING:
				if (B1_RELEASED || B2_RELEASED) {
					bmode = B_NORMAL;
					dmode = D_NORMAL;
					P3_7 = 1;				// turn off alarm
					B1_RELEASED = 0;
					B2_RELEASED = 0;
				}
				break;

			case B_EDIT_ALARM_MIN:
				if (B1_PRESSED) {
					bmode = B_ENABLE_ALARM;
					dmode = D_ENABLE_ALARM;
					B1_PRESSED = 0;
				} else if (B2_PRESSED) {
					increment_alarm_minute();
					B2_PRESSED = 0;
				} else if (B2_PRESSED_LONG && clock_increment == 1) {
					increment_alarm_minute();
					clock_increment = 0;
				}
				break;

			case B_EDIT_ALARM_HOUR:
				if (B1_PRESSED) {
					bmode = B_EDIT_ALARM_MIN;
					dmode = D_EDIT_ALARM_MIN;
					B1_PRESSED = 0;
				} else if (B2_PRESSED) {
					increment_alarm_hour();
					B2_PRESSED = 0;					
				} else if (B2_PRESSED_LONG && clock_increment == 1) {
					increment_alarm_hour();
					clock_increment = 0;
				}
				break;

			case B_ENABLE_ALARM:
				if (B1_PRESSED) {
					bmode = B_NORMAL;
					dmode = D_NORMAL;
					B1_PRESSED = 0;
				} else if (B2_PRESSED) {
					ALARM_ENABLE = !ALARM_ENABLE;
					B2_PRESSED = 0;
				}
				break;

			case B_SHOW_ALARM:
				if (B1_PRESSED_LONG) {
					dmode = D_EDIT_ALARM_HOUR;
					bmode = B_EDIT_ALARM_HOUR;
					B1_PRESSED = 0;
					B1_PRESSED_LONG = 0;
				} else if (B1_RELEASED) {
					dmode = D_ENABLE_ALARM;
					bmode = B_ENABLE_ALARM;
					B1_RELEASED = 0;
				}
				break;
	
			case B_SET_24H:
				if (B2_RELEASED) {
					bmode = B_NORMAL;
					dmode = D_NORMAL;
					B2_RELEASED = 0;
				} else if (B1_RELEASED) {
					TWELVE_TIME = !TWELVE_TIME;
					B1_RELEASED = 0;
				}
				break;

			case B_EDIT_MIN:
				if (B1_PRESSED) {			// exit edit mode
					bmode = B_NORMAL;
					dmode = D_NORMAL;
					B1_PRESSED = 0;
					CLOCK_RUNNING = 1;
				} else if (B2_PRESSED) {	// increment minute
					increment_minute();
					clock_second = 0;		// reset seconds to 0 when time is changed
					B2_PRESSED = 0;
				} else if (B2_PRESSED_LONG && clock_increment == 1) {
					increment_minute();		// hold down the button to rapidly increase minute
					clock_increment = 0;
				}
				break;

			case B_EDIT_HOUR:
				CLOCK_RUNNING = 0;
				if (B1_PRESSED) {			// switch to edit minute mode
					bmode = B_EDIT_MIN;
					dmode = D_EDIT_MIN;
					B1_PRESSED = 0;
				} else if (B2_PRESSED) {	// increment hour
					increment_hour();
					clock_second = 0;		// reset seconds to 0 when time is changed
					B2_PRESSED = 0;					
				} else if (B2_PRESSED_LONG && clock_increment == 1) {
					increment_hour();		// hold down the button to rapidly increase hour
					clock_increment = 0;
				}
				break;

			case B_MIN_SEC:
				if (B2_RELEASED) {
					dmode = B_SET_24H;
					bmode = B_SET_24H;
					B2_RELEASED = 0;
				}
				break;
			
			case B_NORMAL:
			default:
				if (B2_RELEASED) {
					dmode = D_MIN_SEC;
					bmode = B_MIN_SEC;
					B2_RELEASED = 0;
				} else if (B1_PRESSED_LONG) {
					dmode = D_EDIT_HOUR;
					bmode = B_EDIT_HOUR;
					B1_PRESSED = 0;
					B1_PRESSED_LONG = 0;
				} else if (B1_RELEASED) {
					dmode = D_SHOW_ALARM;
					bmode = B_SHOW_ALARM;
					B1_RELEASED = 0;
				}
				break;
		}

		// display mode
		switch (dmode) {
			case D_ALARMING:
				if (show_colon == 1) {
					set_hour_dbuf(clock_hour);
					dbuf[2] = ledtable[(clock_minute/10)];
					dbuf[3] = ledtable[(clock_minute%10)];
					dbuf[1] |= 1;
				} else {
					dbuf[0] = ledtable[LED_BLANK];
					dbuf[1] = ledtable[LED_BLANK];
					dbuf[2] = ledtable[LED_BLANK];
					dbuf[3] = ledtable[LED_BLANK];
				}
				break;

			case D_ENABLE_ALARM:
				dbuf[0] = ledtable[LED_A];
				dbuf[1] = ledtable[LED_L];
				dbuf[2] = ledtable[LED_BLANK];
				if (ALARM_ENABLE) {
					dbuf[3] = ledtable[LED_y];
				} else {
					dbuf[3] = ledtable[LED_n];
				}
				break;

			case D_EDIT_ALARM_MIN:

				set_hour_dbuf(alarm_hour);
				if (show_blink == 1) {
					dbuf[2] = ledtable[(alarm_minute/10)];
					dbuf[3] = ledtable[(alarm_minute%10)];
				} else {
					dbuf[2] = ledtable[LED_BLANK];
					dbuf[3] = ledtable[LED_BLANK];
				}
				if (!TWELVE_TIME || alarm_hour > 11) {
					dbuf[1] |= 1;
				}
				break;

			case D_EDIT_ALARM_HOUR:

				if (show_blink == 1) {
					set_hour_dbuf(alarm_hour);
				} else {
					dbuf[0] = ledtable[LED_BLANK];
					dbuf[1] = ledtable[LED_BLANK];
				}
				dbuf[2] = ledtable[(alarm_minute/10)];
				dbuf[3] = ledtable[(alarm_minute%10)];
				if (!TWELVE_TIME || alarm_hour > 11) {
					dbuf[1] |= 1;
				}
				break;

			case D_SHOW_ALARM :

				if (show_colon == 1) {
					set_hour_dbuf(alarm_hour);
					dbuf[2] = ledtable[(alarm_minute/10)];
					dbuf[3] = ledtable[(alarm_minute%10)];
					dbuf[1] |= 1;
				} else {
					dbuf[0] = ledtable[LED_BLANK];
					dbuf[1] = ledtable[LED_BLANK];
					dbuf[2] = ledtable[LED_BLANK];
					dbuf[3] = ledtable[LED_BLANK];
				}
				/*
				set_hour_dbuf(alarm_hour);
				dbuf[2] = ledtable[(alarm_minute/10)];
				dbuf[3] = ledtable[(alarm_minute%10)];
				if (TWELVE_TIME && alarm_hour > 11) {
					dbuf[1] |= 1;
				}
				*/
				break;

			case D_SET_24H:

				if (TWELVE_TIME) {
					dbuf[0] = ledtable[1];
					dbuf[1] = ledtable[2];
				} else {
					dbuf[0] = ledtable[2];
					dbuf[1] = ledtable[4];
				}
				dbuf[2] = ledtable[LED_h];
				dbuf[3] = ledtable[LED_BLANK];
				break;

			case D_EDIT_MIN:

				set_hour_dbuf(clock_hour);
				if (show_blink == 1) {
					dbuf[2] = ledtable[(clock_minute/10)];
					dbuf[3] = ledtable[(clock_minute%10)];
				} else {
					dbuf[2] = ledtable[LED_BLANK];
					dbuf[3] = ledtable[LED_BLANK];
				}

				// colon does not blink when setting time
				// in 12 hour format, colon is only on when hour represents PM
				if (!TWELVE_TIME || clock_hour > 11) {
					dbuf[1] |= 1;
				}

				break;

			case D_EDIT_HOUR:

				if (show_blink == 1) {
					set_hour_dbuf(clock_hour);
				} else {
					dbuf[0] = ledtable[LED_BLANK];
					dbuf[1] = ledtable[LED_BLANK];
				}
				dbuf[2] = ledtable[(clock_minute/10)];
				dbuf[3] = ledtable[(clock_minute%10)];

				// colon does not blink when setting time
				// in 12 hour format, colon is only on when hour represents PM
				if (!TWELVE_TIME || clock_hour > 11) {
					dbuf[1] |= 1;
				}

				break;

			case D_MIN_SEC:

				// update display buffer to show current time
				dbuf[0] = ledtable[(clock_minute/10)];
				dbuf[1] = ledtable[(clock_minute%10)];
				dbuf[2] = ledtable[(clock_second/10)];
				dbuf[3] = ledtable[(clock_second%10)];

				// blinking colon
				if (show_colon == 1) {
					dbuf[1] |= 1;
				}
				break;

			case D_NORMAL:
			default:

				// update display buffer to show current time
				set_hour_dbuf(clock_hour);
				dbuf[2] = ledtable[(clock_minute/10)];
				dbuf[3] = ledtable[(clock_minute%10)];

				// blinking colon
				if (show_colon == 1) {
					dbuf[1] |= 1;
				}
				break;
		}

		display_update();			// update the display
    }
}