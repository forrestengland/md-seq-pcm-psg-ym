#ifndef H_CONTROLLER
#define H_CONTROLLER

#include <stdint.h>

// Memory addresses for the Controller Port 1 Data and Control registers
#define CTRL_PORT1_DATA   *((volatile uint8_t*)0xA10003)
#define CTRL_PORT1_CONTROL *((volatile uint8_t*)0xA10009)

// Pin definitions (based on DB9 connector)
#define PIN_UP_BIT     0x01
#define PIN_DOWN_BIT   0x02
#define PIN_LEFT_BIT   0x04
#define PIN_RIGHT_BIT  0x08
#define PIN_A_B_BIT    0x10 // Pin 6: A when Select is Low, B when Select is High
#define PIN_SELECT_BIT 0x40 // Pin 7: Select signal (TH - Toggle High)
#define PIN_START_C_BIT 0x80 // Pin 9: Start when Select is Low, C when Select is High

// Button state structure
typedef struct {
    uint8_t up    : 1;
    uint8_t down  : 1;
    uint8_t left  : 1;
    uint8_t right : 1;
    uint8_t a     : 1;
    uint8_t b     : 1;
    uint8_t c     : 1;
    uint8_t start : 1;
} ControllerState;

void read_controller1(ControllerState* state);

#endif
