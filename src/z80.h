#include <stdint.h>

#define Z80_RAM      0xA00000
#define Z80_RAM_END  0xA01FFF
#define Z80_RAM_LEN  ((Z80_RAM_END - Z80_RAM) + 1)
#define Z80_RESET    0xA11200
#define Z80_RESET_PORT Z80_RESET
#define Z80_BUSREQ    ((volatile uint16_t*) 0xA11100) // HALT_PORT in sgdk
#define Z80_HALT_PORT Z80_BUSREQ
#define Z80_BANK_REG   ((volatile uint8_t*)  0xA06000)
#define Z80_BANK_REGISTER Z80_BANK_REG

int Z80_isBusTaken();
void Z80_requestBus(int wait);
int Z80_getAndRequestBus(int wait);
void Z80_releaseBus();
void Z80_startReset();
void Z80_endReset();
void Z80_setBank(const uint16_t bank);
uint8_t Z80_read(const uint16_t addr);
void Z80_write(const uint16_t addr, const uint8_t value);
void __attribute__((noinline)) Z80_clear();
void __attribute__((noinline)) Z80_upload(const uint16_t to, const uint8_t *from, const uint16_t size);
void __attribute__((noinline)) Z80_download(const uint16_t from, uint8_t *to, const uint16_t size);
void __attribute__((noinline)) Z80_loadDriverInternal(const uint8_t *drv, uint16_t size);
void __attribute__((noinline)) Z80_loadCustomDriver(const uint8_t *drv, uint16_t size);
void __attribute__((noinline)) Z80_init();
