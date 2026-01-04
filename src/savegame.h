#ifndef H_SAVEGAME
#define H_SAVEGAME

// Define key SRAM memory addresses as volatile pointers
// Volatile is crucial as the hardware might change values outside the C program's control
#define SRAM_START_ADDR ((volatile uint8_t*)0x200001)
#define SRAM_END_ADDR ((volatile uint8_t*)0x20FFFF)
#define SRAM_LOCK_ADDR  ((volatile uint8_t*)0xA130F1)

#endif
