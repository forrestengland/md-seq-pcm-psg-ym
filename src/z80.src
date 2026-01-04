#include "z80.h"

int Z80_isBusTaken()
{
    volatile uint16_t *pw;

    pw = (uint16_t *) Z80_HALT_PORT;
    if (*pw & 0x0100) return 0;
    else return 1;
}

void Z80_requestBus(int wait)
{
    volatile uint16_t *pw_bus;
    volatile uint16_t *pw_reset;

    // request bus (need to end reset)
    pw_bus = (uint16_t *) Z80_HALT_PORT;
    pw_reset = (uint16_t *) Z80_RESET_PORT;

    // take bus and end reset
    *pw_bus = 0x0100;
    *pw_reset = 0x0100;

    if (wait)
    {
        // wait for bus taken
        while (*pw_bus & 0x0100);
    }
}

int Z80_getAndRequestBus(int wait)
{
    volatile uint16_t *pw_bus;
    volatile uint16_t *pw_reset;

    pw_bus = (uint16_t *) Z80_HALT_PORT;

    // already requested ? just return 1
    if (!(*pw_bus & 0x0100)) return 1;

    pw_reset = (uint16_t *) Z80_RESET_PORT;

    // take bus and end reset
    *pw_bus = 0x0100;
    *pw_reset = 0x0100;

    if (wait)
    {
        // wait for bus taken
        while (*pw_bus & 0x0100);
    }

    return 0;
}

void Z80_releaseBus()
{
    volatile uint16_t *pw;

    pw = (uint16_t *) Z80_HALT_PORT;
    *pw = 0x0000;
}


void Z80_startReset()
{
    volatile uint16_t *pw;

    pw = (uint16_t *) Z80_RESET_PORT;
    *pw = 0x0000;
}

void Z80_endReset()
{
    volatile uint16_t *pw;

    pw = (uint16_t *) Z80_RESET_PORT;
    *pw = 0x0100;
}

void Z80_setBank(const uint16_t bank)
{
    volatile uint8_t *pb;
    uint16_t i, value;

    pb = (uint8_t *) Z80_BANK_REGISTER;

    i = 9;
    value = bank;
    while (i--)
    {
        *pb = value;
        value >>= 1;
    }
}

uint8_t Z80_read(const uint16_t addr)
{
    return ((volatile uint8_t*) Z80_RAM)[addr];
}

void Z80_write(const uint16_t addr, const uint8_t value)
{
    ((volatile uint8_t*) Z80_RAM)[addr] = value;
}

void __attribute__((noinline)) Z80_clear()
{
    int busTaken = Z80_getAndRequestBus(1);

    const uint8_t zero = 0;
    volatile uint8_t* dst = (uint8_t*) Z80_RAM;
    uint16_t len = Z80_RAM_LEN;

    while(len--) *dst++ = zero;

    // release bus
    if (!busTaken) Z80_releaseBus();
}

void __attribute__((noinline)) Z80_upload(const uint16_t to, const uint8_t *from, const uint16_t size)
{
    int busTaken = Z80_getAndRequestBus(1);

    // copy data to Z80 RAM (need to use byte copy here)
    uint8_t* src = (uint8_t*) from;
    volatile uint8_t* dst = (uint8_t*) (Z80_RAM + to);
    uint16_t len = size;

    while(len--) *dst++ = *src++;

    // release bus
    if (!busTaken) Z80_releaseBus();
}

void __attribute__((noinline)) Z80_download(const uint16_t from, uint8_t *to, const uint16_t size)
{
    int busTaken = Z80_getAndRequestBus(1);

    // copy data from Z80 RAM (need to use byte copy here)
    volatile uint8_t* src = (uint8_t*) (Z80_RAM + from);
    uint8_t* dst = (uint8_t*) to;
    uint16_t len = size;

    while(len--) *dst++ = *src++;

    // release bus
    if (!busTaken) Z80_releaseBus();
}


void __attribute__((noinline)) Z80_loadDriverInternal(const uint8_t *drv, uint16_t size)
{
    Z80_requestBus(1);

    // clear z80 memory
    Z80_clear();

    // upload Z80 driver
    Z80_upload(0, drv, size);

    // reset Z80
    Z80_startReset();
    Z80_releaseBus();
    // wait a bit so Z80 reset completed
    for (int i=0; i<10000; i++) {
      ;
    }
    Z80_endReset();
}

void __attribute__((noinline)) Z80_loadCustomDriver(const uint8_t *drv, uint16_t size)
{
    Z80_loadDriverInternal(drv, size);
}

void __attribute__((noinline)) Z80_init()
{
    // request Z80 bus
    Z80_requestBus(1);
    // set bank to 0
    Z80_setBank(0);
}
