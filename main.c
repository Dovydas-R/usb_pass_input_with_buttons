/**
 * Project: USB password input device
 * Version: 1.3
 * Date: 2013 07 14
 * Author: Dovydas Rusinskas
 * URL: www.cortex.lt (http://www.cortex.lt/e107_plugins/content/content.php?content.62)
 * Copyright: (c) Dovydas Rusinskas
 *
 * Some my comments are in english some in lithuanian language :D
 * information used from http://www.obdev.at/products/vusb/index.html, http://codeandlife.com
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#include "usbdrv.h"

#define sbi(a,b) a|=(1<<b) 
#define cbi(a,b) a&=~(1<<b)

#define BUTTON_PRESS_SEND	1500
#define BUTTON_PRESS_MAX	0xFFFF

#define SW1_IN	4
#define SW2_IN	3
#define SW3_IN	0

enum {SW1, SW2, SW3, SW_All};

//passwords that will be typed
PROGMEM uchar password1[] = "slaptazodis1\n";
PROGMEM uchar password2[] = "slaptazodis2\n";
PROGMEM uchar password3[] = "slaptazodis3\n";

// The buffer needs to accommodate the messages above and the password
#define MSG_BUFFER_SIZE 32 

EEMEM uchar stored_password[MSG_BUFFER_SIZE];


// ************************
// *** USB HID ROUTINES ***
// ************************

// From Frank Zhao's USB Business Card project
// http://www.frank-zhao.com/cache/usbbusinesscard_details.php
PROGMEM char usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,                    // USAGE (Keyboard)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x08,                    //   REPORT_COUNT (8)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)(Key Codes)
    0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)(224)
    0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)(231)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs) ; Modifier byte
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x81, 0x03,                    //   INPUT (Cnst,Var,Abs) ; Reserved byte
    0x95, 0x05,                    //   REPORT_COUNT (5)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x05, 0x08,                    //   USAGE_PAGE (LEDs)
    0x19, 0x01,                    //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05,                    //   USAGE_MAXIMUM (Kana)
    0x91, 0x02,                    //   OUTPUT (Data,Var,Abs) ; LED report
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x03,                    //   REPORT_SIZE (3)
    0x91, 0x03,                    //   OUTPUT (Cnst,Var,Abs) ; LED report padding
    0x95, 0x06,                    //   REPORT_COUNT (6)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)(Key Codes)
    0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))(0)
    0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)(101)
    0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
    0xc0                           // END_COLLECTION
};

typedef struct {
	uint8_t modifier;
	uint8_t reserved;
	uint8_t keycode[6];
} keyboard_report_t;

static keyboard_report_t keyboard_report; // sent to PC
volatile static uchar LED_state = 0xff; // received from PC
static uchar idleRate; // repeat rate for keyboards

#define STATE_SEND 1
#define STATE_DONE 0

static uchar messageState = STATE_DONE;
static uchar messageBuffer[MSG_BUFFER_SIZE];
static uchar messagePtr = 0;
static uchar messageCharNext = 1;

#define MOD_SHIFT_LEFT (1<<1)

// The buildReport is called by main loop and it starts transmitting
// characters when messageState == STATE_SEND. The message is stored
// in messageBuffer and messagePtr tells the next character to send.
// Remember to reset messagePtr to 0 after populating the buffer!
uchar buildReport() {
    uchar ch;
    
    if(messageState == STATE_DONE || messagePtr >= sizeof(messageBuffer) || messageBuffer[messagePtr] == 0) {
        keyboard_report.modifier = 0;
        keyboard_report.keycode[0] = 0;
        return STATE_DONE;
    }

    if(messageCharNext) { // send a keypress
        ch = messageBuffer[messagePtr++];
        
        // convert character to modifier + keycode
        if(ch >= '0' && ch <= '9') {
            keyboard_report.modifier = 0;
            keyboard_report.keycode[0] = (ch == '0') ? 39 : 30+(ch-'1');
        } else if(ch >= 'a' && ch <= 'z') {
            keyboard_report.modifier = (LED_state & 2) ? MOD_SHIFT_LEFT : 0;
            keyboard_report.keycode[0] = 4+(ch-'a');
        } else if(ch >= 'A' && ch <= 'Z') {
            keyboard_report.modifier = (LED_state & 2) ? 0 : MOD_SHIFT_LEFT;
            keyboard_report.keycode[0] = 4+(ch-'A');
        } else {
            keyboard_report.modifier = 0;
            keyboard_report.keycode[0] = 0;
            switch(ch) {
			case '.':
				keyboard_report.keycode[0] = 0x37;
				break;
			case '_':
				keyboard_report.modifier = MOD_SHIFT_LEFT;
			case '-':
				keyboard_report.keycode[0] = 0x2D;
				break;
            case ' ':
                keyboard_report.keycode[0] = 0x2C;
                break;
            case '\t':
                keyboard_report.keycode[0] = 0x2B;
                break;
            case '\n': //new line, ENTER
                keyboard_report.keycode[0] = 0x28;
                break;
            }
        }
    } else { // key release before the next keypress!
        keyboard_report.modifier = 0;
        keyboard_report.keycode[0] = 0;
    }
    
    messageCharNext = !messageCharNext; // invert
    
    return STATE_SEND;
}

usbMsgLen_t usbFunctionSetup(uchar data[8]) {
    usbRequest_t *rq = (void *)data;

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        switch(rq->bRequest) {
        case USBRQ_HID_GET_REPORT: // send "no keys pressed" if asked here
            // wValue: ReportType (highbyte), ReportID (lowbyte)
            usbMsgPtr = (void *)&keyboard_report; // we only have this one
            keyboard_report.modifier = 0;
            keyboard_report.keycode[0] = 0;
            return sizeof(keyboard_report);
		case USBRQ_HID_SET_REPORT: // if wLength == 1, should be LED state
            return (rq->wLength.word == 1) ? USB_NO_MSG : 0;
        case USBRQ_HID_GET_IDLE: // send idle rate to PC as required by spec
            usbMsgPtr = &idleRate;
            return 1;
        case USBRQ_HID_SET_IDLE: // save idle rate as required by spec
            idleRate = rq->wValue.bytes[1];
            return 0;
        }
    }
    
    return 0; // by default don't return any data
}

usbMsgLen_t usbFunctionWrite(uint8_t * data, uchar len) {
	if (data[0] == LED_state)
        return 1;
    else
        LED_state = data[0];
	
	return 1; // Data read, not expecting more
}

#define abs(x) ((x) > 0 ? (x) : (-x))

// Called by V-USB after device reset
void hadUsbReset() {
    int frameLength, targetLength = (unsigned)(1499 * (double)F_CPU / 10.5e6 + 0.5);
    int bestDeviation = 9999;
    uchar trialCal, bestCal = 0, step, region;

    // do a binary search in regions 0-127 and 128-255 to get optimum OSCCAL
    for(region = 0; region <= 1; region++) {
        frameLength = 0;
        trialCal = (region == 0) ? 0 : 128;
        
        for(step = 64; step > 0; step >>= 1) { 
            if(frameLength < targetLength) // true for initial iteration
                trialCal += step; // frequency too low
            else
                trialCal -= step; // frequency too high
                
            OSCCAL = trialCal;
            frameLength = usbMeasureFrameLength();
            
            if(abs(frameLength-targetLength) < bestDeviation) {
                bestCal = trialCal; // new optimum found
                bestDeviation = abs(frameLength -targetLength);
            }
        }
    }

    OSCCAL = bestCal;
}


// *********************
// *** PROGRAM LOGIC ***
// *********************


int main() {
	uchar i;
	unsigned int	buttons_press[SW_All];

    for(i=0; i<sizeof(keyboard_report); i++) // clear report initially
        ((uchar *)&keyboard_report)[i] = 0;
    
    wdt_enable(WDTO_1S); // enable 1s watchdog timer

    usbInit();
	
    usbDeviceDisconnect(); // enforce re-enumeration
    for(i = 0; i<250; i++) { // wait 500 ms
        wdt_reset(); // keep the watchdog happy
        _delay_ms(2);
    }
    usbDeviceConnect();
	
    TCCR0B |= (1 << CS01); // timer 0 at clk/8 will generate randomness
	
	for(i=0; i<SW_All; i++) // clear array
        buttons_press[i] = 0;

	//set port pins directions as inpunts
	cbi(DDRB, SW1_IN);
	cbi(DDRB, SW2_IN);
	cbi(DDRB, SW3_IN);
	
	//pullups for all buttons
	sbi(PORTB, SW1_IN);
	sbi(PORTB, SW2_IN);
	sbi(PORTB, SW3_IN);
    
    sei(); // Enable interrupts after re-enumeration
	
    while(1) {
        wdt_reset(); // keep the watchdog happy
        usbPoll();
		
		
		//mygtuku nuskaitymas ir paspaudimo laiko skaiciavimas (software debounce)
		if(!((PINB>>SW1_IN)&1)) { // button pressed
		if(buttons_press[SW1] < BUTTON_PRESS_MAX) buttons_press[SW1] ++;
		}
		else buttons_press[SW1] = 0;
		
		if(!((PINB>>SW2_IN)&1)) { // button pressed
		if(buttons_press[SW2] < BUTTON_PRESS_MAX) buttons_press[SW2] ++;
		}
		else buttons_press[SW2] = 0;
		
		if(!((PINB>>SW3_IN)&1)) { // button pressed
		if(buttons_press[SW3] < BUTTON_PRESS_MAX) buttons_press[SW3] ++;
		}
		else buttons_press[SW3] = 0;
		
		if(messageState != STATE_SEND){
	
			if(buttons_press[SW1] == BUTTON_PRESS_SEND) {

			memcpy_P(messageBuffer, password1, sizeof(password1)); //mukopijuoju pass, siuncia iki 0
			messagePtr = 0;
			messageState = STATE_SEND; ////pasakau, kad issiustu
			}
			
			if(buttons_press[SW2] == BUTTON_PRESS_SEND) {

			memcpy_P(messageBuffer, password2, sizeof(password1)); //mukopijuoju pass, siuncia iki 0
			messagePtr = 0;
			messageState = STATE_SEND; ////pasakau, kad issiustu
			}
			
			if(buttons_press[SW3] == BUTTON_PRESS_SEND) {

			memcpy_P(messageBuffer, password3, sizeof(password1)); //mukopijuoju pass, siuncia iki 0
			messagePtr = 0;
			messageState = STATE_SEND; ////pasakau, kad issiustu
			}
		}

        
        // characters are sent when messageState == STATE_SEND and after receiving
        // the initial LED state from PC (good way to wait until device is recognized)
        if(usbInterruptIsReady() && messageState == STATE_SEND && LED_state != 0xff){
            messageState = buildReport();
            usbSetInterrupt((void *)&keyboard_report, sizeof(keyboard_report));
        }
    }
	
    return 0;
}
