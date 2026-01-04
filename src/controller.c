#include "controller.h"

void read_controller1(ControllerState* state) {
    uint8_t data1, data2;

    // 1. Configure Pin 7 (Select) as output, set low (default is input)
    // Writing to the control register sets pin direction (0=input, 1=output)
    CTRL_PORT1_CONTROL |= PIN_SELECT_BIT; 

    // 2. Set Select line to LOW (output 0)
    // Writing to the data register controls output value for pins set as output
    CTRL_PORT1_DATA &= ~PIN_SELECT_BIT;

    // 3. Read data for the first set of buttons (Up, Down, A, Start)
    data1 = CTRL_PORT1_DATA;

    // 4. Set Select line to HIGH (output 1)
    CTRL_PORT1_DATA |= PIN_SELECT_BIT;

    // 5. Read data for the second set of buttons (Up, Down, Left, Right, B, C)
    data2 = CTRL_PORT1_DATA;

    // 6. Convert raw data to a usable state struct (note: active low logic)
    state->up    = !(data1 & PIN_UP_BIT);
    state->down  = !(data1 & PIN_DOWN_BIT);
    state->a     = !(data1 & PIN_A_B_BIT);
    state->start = !(data1 & PIN_START_C_BIT);

    state->left  = !(data2 & PIN_LEFT_BIT);
    state->right = !(data2 & PIN_RIGHT_BIT);
    state->b     = !(data2 & PIN_A_B_BIT);
    state->c     = !(data2 & PIN_START_C_BIT);
    
    // 7. Reconfigure Pin 7 back to input (optional, good practice)
    CTRL_PORT1_CONTROL &= ~PIN_SELECT_BIT;
}
