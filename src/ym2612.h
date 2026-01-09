#ifndef H_YM2612
#define H_YM2612

#include <stdint.h>

#define YM2612_BASEPORT     0xA04000

// YM2612 F-Numbers for one octave at Block = 4
// Notes: C  C#  D   D#  E   F   F#  G   G#  A   A#  B
static const unsigned short ym_fnum_table[12] = {
    0x22B, // C
    0x250, // C#
    0x279, // D
    0x2A4, // D#
    0x2D3, // E
    0x305, // F
    0x33B, // F#
    0x374, // G
    0x3B2, // G#
    0x3F4, // A
    0x43B, // A#
    0x487  // B
};

typedef struct {
    unsigned char block;
    unsigned short fnum;
} ym_pitch_t;

void __attribute__ ((noinline)) YM2612_reset(int takez80bus);
uint8_t YM2612_read(const uint16_t port);
uint8_t YM2612_readStatus();
void YM2612_write(const uint16_t port, const char data);
void YM2612_writeReg(const uint16_t part, const uint8_t reg, const uint8_t data);
void YM2612_enableDAC();
void YM2612_disableDAC();
void YM2612_latchDacDataReg();
void ym_write(int which, uint8_t addr, uint8_t value);
void play_sine_wave();
ym_pitch_t midi_to_ym2612(unsigned char midi_note);
void ym_set_pitch_ch0(unsigned char midi_note);
void noteon_chan0();
void noteoff_chan0();
void YM2612_writeSlotReg(uint16_t port, uint8_t ch, uint8_t sl,
			 uint8_t reg, uint8_t value);

#endif
