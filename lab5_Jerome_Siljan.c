// Serial Example
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL Evaluation Board
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// Red LED:
//   PF1 drives an NPN transistor that powers the red LED
// Green LED:
//   PF3 drives an NPN transistor that powers the green LED
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual
//   COM port Configured to 115,200 baud, 8N1

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

// includes
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "tm4c123gh6pm.h"
#include "uart0.h"

// bitbands, masks, structs
// Bitband aliases
#define RED_LED                                                            \
    (*((volatile uint32_t *)(0x42000000 + (0x400253FC - 0x40000000) * 32 + \
                             1 * 4)))
#define GREEN_LED                                                          \
    (*((volatile uint32_t *)(0x42000000 + (0x400253FC - 0x40000000) * 32 + \
                             3 * 4)))

// PortF masks
#define GREEN_LED_MASK 8
#define RED_LED_MASK 2

// Max Char
#define MAX_CHARS 80
#define MAX_FIELDS 5

// Const str
char str[MAX_CHARS + 1];

typedef struct _USER_DATA {
    char buffer[MAX_CHARS + 1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Initialize Hardware
void initHw() {
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Enable clocks
    SYSCTL_RCGCGPIO_R = SYSCTL_RCGCGPIO_R5;
    _delay_cycles(3);

    // Configure LED pins
    GPIO_PORTF_DIR_R |=
        GREEN_LED_MASK | RED_LED_MASK;  // bits 1 and 3 are outputs
    GPIO_PORTF_DR2R_R |=
        GREEN_LED_MASK |
        RED_LED_MASK;  // set drive strength to 2mA (not needed since default
                       // configuration -- for clarity)
    GPIO_PORTF_DEN_R |= GREEN_LED_MASK | RED_LED_MASK;  // enable LEDs
}

// function headers
char *getFieldString(USER_DATA *data, uint8_t fieldNumber);
int32_t getFieldInteger(USER_DATA *data, uint8_t fieldNumber);
void parseFields(USER_DATA *data);
int str_to_int(char *str);
bool str_comp(char *data_command, const char strCommand[]);
bool isCommand(USER_DATA *data, const char strCommand[], uint8_t minArguments);
uint16_t getsUart0(USER_DATA *data);

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

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void) {
    USER_DATA data;

    // Initialize hardware
    initHw();
    initUart0();

    // Setup UART0 baud rate
    setUart0BaudRate(115200, 40e6);

    data.buffer[0] = 'a';
    data.buffer[1] = 'd';
    data.buffer[2] = 'd';
    data.buffer[3] = ' ';
    data.buffer[4] = '1';
    data.buffer[5] = '2';
    data.buffer[6] = ',';
    data.buffer[7] = '2';
    data.buffer[8] = '\0';

    parseFields(&data);

    char *res_string = getFieldString(&data, 0);
    uint8_t field1 = getFieldInteger(&data, 1);
    uint8_t field2 = getFieldInteger(&data, 2);
    bool cmd = isCommand(&data, "add", 4);

    return 0;
}
