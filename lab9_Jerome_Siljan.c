// Hardware configuration:
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual
//   COM port Configured to 115,200 baud, 8N1
// Frequency counter and timer input:
//   SIGNAL_IN on PC6 (WT1CCP0)
// Red LED on PC7

// Device includes, defines, and assembler directives
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adc0.h"
#include "clock.h"
#include "tm4c123gh6pm.h"
#include "uart0.h"
#include "wait.h"

#define RED_LED                                                            \
    (*((volatile uint32_t *)(0x42000000 + (0x400253FC - 0x40000000) * 32 + \
                             1 * 4)))
// PortC masks
#define FREQ_IN_MASK 64
#define RED_LED_MASK 128

// PortE masks
#define AIN3_MASK 1

// bpm averaging consts
#define BPM_NUM 5
#define NUM_DIST 10

// shell vars
#define MAX_CHARS 80
#define MAX_FIELDS 5

// SPI
#define DATA_MASK 4
#define CLK_MASK 64

#define CLK                                                                \
    (*((volatile uint32_t *)(0x42000000 + (0x400073FC - 0x40000000) * 32 + \
                             6 * 4)))

#define DATA                                                               \
    (*((volatile uint32_t *)(0x42000000 + (0x400243FC - 0x40000000) * 32 + \
                             2 * 4)))

// Global variables
bool pulse_active = false;
bool timeMode = false;
uint32_t frequency = 0;
uint32_t time = 0;
uint32_t finger_missing_count = 0;

float bpm_array[BPM_NUM];
uint32_t bpm_index = 0;
uint8_t bpm_upper = 150;
uint8_t bpm_lower = 40;

char str[MAX_CHARS + 1];

typedef struct _USER_DATA {
    char buffer[MAX_CHARS + 1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

// function headers
char *getFieldString(USER_DATA *data, uint8_t fieldNumber);
int32_t getFieldInteger(USER_DATA *data, uint8_t fieldNumber);
void parseFields(USER_DATA *data);
int str_to_int(char *str);
bool str_comp(char *data_command, const char strCommand[]);
bool isCommand(USER_DATA *data, const char strCommand[], uint8_t minArguments);
uint16_t getsUart0(USER_DATA *data);

// Subroutines
uint16_t getsUart0(USER_DATA *data) {
    uint16_t count = 0;
    char c;
    while (count != MAX_CHARS) {
        c = getcUart0();
        if (count > 0 && (c == 8 | c == 127)) {
            count--;
        } else if (c == 13) {
            data->buffer[count] = '\0';
            return 0;
        } else if (c >= 32) {
            data->buffer[count] = c;
            count++;
        }
    }
    data->buffer[count] = '\0';
    return 0;
}

int str_to_int(char *str) {
    int32_t to_int = 0;
    uint8_t i = 0;
    while (str[i] != '\0') {
        to_int = to_int * 10;
        to_int += str[i] - 48;
        i++;
    }
    return to_int;
}

void parseFields(USER_DATA *data) {
    data->fieldCount = 0;
    uint8_t i = 0;
    bool first = true;

    while (i <= MAX_CHARS + 1) {
        if (data->buffer[i] > 47 && data->buffer[i] < 58) {  // 0-9
            if (first == true) {
                data->fieldType[data->fieldCount] = 'n';
                first = false;
            }
        } else if ((data->buffer[i] > 65 && data->buffer[i] < 90) |   // A-Z
                   (data->buffer[i] > 96 && data->buffer[i] < 123) |  // a-z
                   ((data->buffer[i] == '-') |                        // '-'
                    (data->buffer[i] == '.'))) {                      // '.'
            if (first == true) {
                data->fieldType[data->fieldCount] = 'a';
                first = false;
            }
        } else if ((data->buffer[i] == '\n') | (data->buffer[i] == '\0')) {
            data->buffer[i] = '\0';
            break;
        } else {
            first = true;
            data->buffer[i] = '\0';
            data->fieldPosition[data->fieldCount] = i;
            data->fieldCount++;
        }
        i++;
    }
}

int32_t getFieldInteger(USER_DATA *data, uint8_t fieldNumber) {
    char *str = getFieldString(data, fieldNumber);

    if (str == NULL) {
        return 0;
    } else {
        return str_to_int(str);
    }
}

char *getFieldString(USER_DATA *data, uint8_t fieldNumber) {
    uint8_t i;
    for (i = 0; i < MAX_CHARS + 1; i++) {
        str[i] = 0;
    }

    if (data->fieldType[fieldNumber] == '\0') {
        return NULL;
    }

    uint8_t field_index = 0;
    if (fieldNumber != 0) {
        field_index = data->fieldPosition[fieldNumber - 1];
        field_index++;
    }
    i = 0;
    while (data->buffer[field_index] != '\0') {
        str[i] = data->buffer[field_index];
        field_index++;
        i++;
    }

    return str;
}

bool str_comp(char *data_command, const char strCommand[]) {
    uint8_t i = 0;
    while (data_command[i] != '\0') {
        if (data_command[i] != strCommand[i]) {
            return false;
        }
        i++;
    }
    return true;
}

bool isCommand(USER_DATA *data, const char strCommand[], uint8_t minArguments) {
    if (str_comp(getFieldString(data, 0), strCommand)) {
        if (data->fieldCount >= minArguments) {
            return true;
        } else {
            return false;
        }
    }
    return false;
}

float calc_bpm(uint32_t time) {
    float micro = time / 40;
    float sec = micro / 1000000;
    float bpm = sec * 60;
    return bpm;
}

void disableCounterMode() {
    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;         // turn-off time base timer
    WTIMER1_CTL_R &= ~TIMER_CTL_TAEN;        // turn-off event counter
    NVIC_DIS0_R |= 1 << (INT_TIMER1A - 16);  // turn-off interrupt 37 (TIMER1A)
}

void enableTimerMode() {
    WTIMER1_CTL_R &= ~TIMER_CTL_TAEN;  // turn-off counter before reconfiguring
    WTIMER1_CFG_R = 4;                 // configure as 32-bit counter (A only)
    WTIMER1_TAMR_R = TIMER_TAMR_TACMR | TIMER_TAMR_TAMR_CAP | TIMER_TAMR_TACDIR;
    // configure for edge time mode, count up
    WTIMER1_CTL_R = TIMER_CTL_TAEVENT_POS;  // measure time from positive edge
                                            // to positive edge
    WTIMER1_IMR_R = TIMER_IMR_CAEIM;        // turn-on interrupts
    WTIMER1_TAV_R = 0;                      // zero counter for first period
    WTIMER1_CTL_R |= TIMER_CTL_TAEN;        // turn-on counter
    NVIC_EN3_R |=
        1 << (INT_WTIMER1A - 16 - 96);  // turn-on interrupt 112 (WTIMER1A)
}

// Period timer service publishing latest time measurements every positive edge
void wideTimer1Isr() {
    time = WTIMER1_TAV_R;               // read counter input
    WTIMER1_TAV_R = 0;                  // zero counter for next edge
    WTIMER1_ICR_R = TIMER_ICR_CAECINT;  // clear interrupt flag
}

// Initialize Hardware
void initHw() {
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Enable clocks
    SYSCTL_RCGCTIMER_R |=
        SYSCTL_RCGCTIMER_R1 | SYSCTL_RCGCTIMER_R3 | SYSCTL_RCGCTIMER_R4;
    SYSCTL_RCGCWTIMER_R |= SYSCTL_RCGCWTIMER_R1;
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R2 | SYSCTL_RCGCGPIO_R3 |
                         SYSCTL_RCGCGPIO_R4 | SYSCTL_RCGCGPIO_R5;
    _delay_cycles(3);

    // Timer 4 periodic setup
    TIMER4_CTL_R &= ~TIMER_CTL_TAEN;         // disable timer
    TIMER4_CFG_R = TIMER_CFG_32_BIT_TIMER;   // set CFG to 0
    TIMER4_TAMR_R = TIMER_TAMR_TAMR_PERIOD;  // set to periodic mode
    TIMER4_TAILR_R = 40000000;               // 1 Hz freq
    TIMER4_IMR_R = TIMER_IMR_TATOIM;         // enable interrupt
    NVIC_EN2_R |= 1 << (86 - 16 - 32 * 2);   // turn on interrupt 32 (TIMER1A)
    TIMER4_CTL_R |= TIMER_CTL_TAEN;          // enable timer

    // Configure LED pins
    GPIO_PORTC_DIR_R |= RED_LED_MASK;
    GPIO_PORTC_DEN_R |= RED_LED_MASK;

    // Configure SIGNAL_IN for frequency and time measurements
    GPIO_PORTC_PDR_R |= FREQ_IN_MASK;
    GPIO_PORTC_AFSEL_R |=
        FREQ_IN_MASK;  // select alternative functions for SIGNAL_IN pin
    GPIO_PORTC_PCTL_R &= ~GPIO_PCTL_PC6_M;  // map alt fns to SIGNAL_IN
    GPIO_PORTC_PCTL_R |= GPIO_PCTL_PC6_WT1CCP0;
    GPIO_PORTC_DEN_R |= FREQ_IN_MASK;  // enable bit 6 for digital input

    // Configure AIN3 as an analog input
    GPIO_PORTE_AFSEL_R |=
        AIN3_MASK;  // select alternative functions for AN3 (PE0)
    GPIO_PORTE_DEN_R &= ~AIN3_MASK;   // turn off digital operation on pin PE0
    GPIO_PORTE_AMSEL_R |= AIN3_MASK;  // turn on analog operation on pin PE0

    // set data pin input
    GPIO_PORTE_DIR_R &= ~DATA_MASK;
    GPIO_PORTE_DEN_R |= DATA_MASK;
    GPIO_PORTE_PDR_R |= DATA_MASK;

    // set clk pin output
    GPIO_PORTD_DIR_R |= CLK_MASK;
    GPIO_PORTD_DEN_R |= CLK_MASK;
}

void insert_bpm_array(float a) {
    if ((a < bpm_upper && a > bpm_lower) && a != bpm_array[bpm_index - 1]) {
        bpm_array[bpm_index] = a;
        if (bpm_index < BPM_NUM - 1) {
            bpm_index++;
        } else {
            bpm_index = 0;
        }
    }
}

float get_avg() {
    uint32_t i = 0, sum = 0, num_vals = 0;
    for (i = 0; i < BPM_NUM; i++) {
        if (bpm_array[i] != 0) {
            sum += bpm_array[i];
            num_vals++;
        }
    }
    return sum / num_vals;
}

void pulse_check() {
    GPIO_PORTC_DATA_R = RED_LED_MASK;
    uint32_t light_on = readAdc0Ss3();

    waitMicrosecond(50);

    GPIO_PORTC_DATA_R &= ~RED_LED_MASK;
    uint32_t light_off = readAdc0Ss3();

    int difference = light_off - light_on;
    if (light_on > 1500 && difference > 80) {
        pulse_active = true;
        GPIO_PORTC_DATA_R |= RED_LED_MASK;
    } else {
        if (pulse_active == true) {
            finger_missing_count++;
            if (finger_missing_count > 2) {
                pulse_active = false;
                GPIO_PORTC_DATA_R &= ~RED_LED_MASK;
            }
        }
    }
    TIMER4_ICR_R = TIMER_ICR_TATOCINT;
}

void show_bpm() {
    float bpm = calc_bpm(time);
    insert_bpm_array(bpm);
    char str[40];
    snprintf(str, sizeof(str), "Average BPM: %f\n", get_avg());
    putsUart0(str);
    /*
    GPIO_PORTC_DATA_R = RED_LED_MASK;
    snprintf(str, sizeof(str), "BPM:\t%f\n", bpm);
    putsUart0(str);
    uint16_t raw;
    raw = readAdc0Ss3();
    snprintf(str, sizeof(str), "Raw ADC:          %4" PRIu16 "\n", raw);
    putsUart0(str);
    snprintf(str, sizeof(str), "BPM:\t%f\n", bpm);
    putsUart0(str);
    */
    waitMicrosecond(500000);
}

void show_pulse() {
    float avg = get_avg();
    if (avg > bpm_lower && avg < bpm_upper) {
        show_bpm();
    } else {
        putsUart0("(not detected)\n");
    }
}

uint32_t get_breath() {
    char str[40];
    uint32_t value = 0;
    while (!DATA)
        ;
    value = 0;
    uint32_t i;
    for (i = 0; i < 24; i++) {
        CLK = 1;
        waitMicrosecond(3);
        uint32_t reading = DATA;
        snprintf(str, sizeof(str), "%d\n", reading);
        // putsUart0(str);
        value |= reading;
        value = value << 1;
        CLK = 0;
        _delay_cycles(10);
    }
    CLK = 1;
    waitMicrosecond(10);
    CLK = 0;
    _delay_cycles(10);

    snprintf(str, sizeof(str), "%d\n", value);
    // putsUart0(str);

    return value;
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void) {
    // Initialize hardware
    initHw();
    initUart0();
    initAdc0Ss3();

    // Use AIN3 input with N=4 hardware sampling
    setAdc0Ss3Mux(3);
    setAdc0Ss3Log2AverageCount(2);

    // set timer
    enableTimerMode();

    // set baud rate
    setUart0BaudRate(115200, 40e6);

    char buf_string[MAX_CHARS + 1];

    // initialize data struct
    USER_DATA data;

    char newstr[40];

    while (true) {
        snprintf(newstr, sizeof(newstr), "%d\n", get_breath());
        putsUart0(newstr);
        waitMicrosecond(500000);
    }

    /* show_bpm loop
    while (true) {
        if (pulse_active == true) {
            show_bpm();
            uint32_t i = 0;
            for (i = 0; i < BPM_NUM; i++) {
                putsUart0("array\n");
                snprintf(str, sizeof(str), "%f\n", bpm_array[i]);
                putsUart0(str);
            }
        } else {
            GPIO_PORTC_DATA_R &= ~RED_LED_MASK;
        }
    }
    */
}
