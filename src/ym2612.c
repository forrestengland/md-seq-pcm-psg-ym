#include "ym2612.h"
#include "z80.h"

// write a global register
void YM2612_writeReg(const uint16_t part, const uint8_t reg, const uint8_t data)
{
    volatile char *pb;
    uint16_t port;

    pb = (char*) YM2612_BASEPORT;
    port = (part << 1) & 2;

    // wait while YM2612 busy
    while (*pb < 0);
    // set reg
    pb[port + 0] = reg;

    // need a minimum of 12 cycles between address and data write
    __asm__ __volatile__("nop");    
    //    asm volatile ("nop");

    // set data
    pb[port + 1] = data;

    // busy flag is not updated immediatly, force wait (needed on MD2)
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");        
}

// write a register for a specific channel and operator (sl = operator number 0-3)
static void writeSlotReg(uint16_t port, uint8_t ch, uint8_t sl,
			 uint8_t reg, uint8_t value) {
    YM2612_write((port * 2) + 0, reg | (sl * 4) | ch);
    YM2612_write((port * 2) + 1, value);
}

// write a specific channel register
static void writeChannelReg(uint16_t port, uint8_t ch, uint8_t reg,
			    uint8_t value)
{
    YM2612_write((port * 2) + 0, reg | ch);
    YM2612_write((port * 2) + 1, value);
}

void __attribute__ ((noinline)) YM2612_reset(int takez80bus)
{
    uint16_t p, ch, sl;
    uint16_t busTaken = 0;

    if (takez80bus) {
      busTaken = Z80_getAndRequestBus(1);
    }

    // disable LFO
    YM2612_write(0, 0x22);
    YM2612_write(1, 0x00);

    // disable timer & set channel 6 to normal mode
    //    YM2612_write(0, 0x27);
    //    YM2612_write(1, 0x00);

    // disable DAC
    //    YM2612_write(0, 0x2B);
    //    YM2612_write(1, 0x00);

    for(p = 0; p < 2; p++)
    {
        for(ch = 0; ch < 3; ch++)
        {
            for(sl = 0; sl < 4; sl++)
            {

	      if (p == 1 && ch == 2) continue; // don't mess with our dac channel
		
                // DT1 - MUL
                writeSlotReg(p, ch, sl, 0x30, 0x00);
                // TL set to max (silent)
                writeSlotReg(p, ch, sl, 0x40, 0x7F);
                // RS - AR
                writeSlotReg(p, ch, sl, 0x50, 0x00);
                // AM - D1R
                writeSlotReg(p, ch, sl, 0x60, 0x00);
                // D2R
                writeSlotReg(p, ch, sl, 0x70, 0x00);
                // D1L - RR set to max
                writeSlotReg(p, ch, sl, 0x80, 0xFF);
                // SSG-EG
                writeSlotReg(p, ch, sl, 0x90, 0x00);
            }
        }
    }

    for(p = 0; p < 2; p++)
    {
        for(ch = 0; ch < 3; ch++)
        {

	  if (p == 1 && ch == 2) continue; // don't mess with our dac channel
	  
            // Freq LSB
            writeChannelReg(p, ch, 0xA0, 0x00);
            // Block - Freq MSB
            writeChannelReg(p, ch, 0xA4, 0x00);
            // Freq LSB - CH3 spe
            writeChannelReg(p, ch, 0xA8, 0x00);
            // Block - Freq MSB - CH3 spe
            writeChannelReg(p, ch, 0xAC, 0x00);
            // Feedback - Algo
            writeChannelReg(p, ch, 0xB0, 0x00);
            // enable LR output
            writeChannelReg(p, ch, 0xB4, 0xC0);
        }
    }

    // extra stuff from play_sinewave needed to make it play - not sure why yet
    ym_write(0, 0x22, 8 & 1); // Enable LFO
    ym_write(0, 0x27, 0x00); // Normal mode (Timer/Ch3)
    ym_write(0, 0xB0, 0x06); // Algorithm 0 , Feedback 6
    ym_write(0, 0xB4, 0xFD); // Panning: Left + Right enable (bit 8 and 7), amplitude mod sensitivity (bit 6 and 5),
                             // frequency mod sensitivity (bit 3,2,1)
    ym_write(0, 0x3C, 0x01); // DT1/Multi: Multiplier 1 ch0 op4
    ym_write(0, 0x38, 0x52); // DT1/Multi: (bits 7-5 detune) (bits 4-1 multiplier) ch0 op3
    ym_write(0, 0x44, 0x7F); // Op 2 TL: 127 (Mute)
    ym_write(0, 0x48, 0x0a); // Op 3 TL: 127 (Mute)
    ym_write(0, 0x4C, 0x00); // Op 4 TL
    ym_write(0, 0x5C, 0x1F); // Attack Rate: 31 (Instant) ch 0 operator 4    

    // ALL KEY OFF
    //    YM2612_write(0, 0x28);
    //    for (ch = 0; ch < 3; ch++)
    //    {
    //        YM2612_write(1, 0x00 | ch);
    //        YM2612_write(1, 0x04 | ch);
    //    }

    if (!busTaken)
        Z80_releaseBus();
}


uint8_t YM2612_read(const uint16_t port)
{
    volatile uint8_t *pb;

    pb = (uint8_t*) YM2612_BASEPORT;

    return pb[port & 3];
}

uint8_t YM2612_readStatus()
{
    volatile uint8_t *pb;

    // status is on port #0 for YM3438 (all ports for YM2612)
    pb = (uint8_t*) YM2612_BASEPORT;

    return *pb;
}


void YM2612_write(const uint16_t port, const char data)
{
    volatile int8_t *pb;

    pb = (volatile int8_t*) YM2612_BASEPORT;

    // wait while YM2612 busy
    while (*pb < 0);
    // write data
    pb[port & 3] = data;

    // busy flag is not updated immediatly, force wait (needed on MD2)
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");   
}

void YM2612_enableDAC()
{
    // enable DAC
    YM2612_writeReg(0, 0x2B, 0x80);
}

void YM2612_disableDAC()
{
    // disable DAC
    YM2612_writeReg(0, 0x2B, 0x00);
}

void ym_write(int which, uint8_t addr, uint8_t value) {
  YM2612_write(which, addr);
  YM2612_write(which+1, value);
}

void play_sine_wave() {

  // --- Global Setup ---
//    ym_write(0, 0x22, 0x00); // Disable LFO

    ym_write(0, 0x22, 8 & 1); // Enable LFO

    ym_write(0, 0x27, 0x00); // Normal mode (Timer/Ch3)
    //    ym_write(0, 0x2B, 0x00); // DAC Disable (Enable FM6)

    // --- Channel 1 Setup (Bank 0) ---
    //    ym_write(0, 0xB0, 0x07); // Algorithm 7 (All carriers), Feedback 0
    ym_write(0, 0xB0, 0x06); // Algorithm 0 , Feedback 6
    
    ym_write(0, 0xB4, 0xFD); // Panning: Left + Right enable (bit 8 and 7), amplitude mod sensitivity (bit 6 and 5),
                             // frequency mod sensitivity (bit 3,2,1)
    
    // Setup Operator 1 (The only active carrier)
    //    ym_write(0, 0x30, 0x01); // DT1/Multi: Multiplier 1
    // Setup Operator 4, the carrier for most algorithms
    ym_write(0, 0x3C, 0x01); // DT1/Multi: Multiplier 1 ch0 op4
    // setup operator 3, the modulator
    ym_write(0, 0x38, 0x52); // DT1/Multi: (bits 7-5 detune) (bits 4-1 multiplier) ch0 op3
    
    //    ym_write(0, 0x40, 0x00); // Total Level: 0 (Max Volume)
    // Mute Operators 2, 3, and 4 to isolate Op 1's sine
    ym_write(0, 0x44, 0x7F); // Op 2 TL: 127 (Mute)
    ym_write(0, 0x48, 0x0a); // Op 3 TL: 127 (Mute)
    ym_write(0, 0x4C, 0x00); // Op 4 TL
    
    ym_write(0, 0x5C, 0x1F); // Attack Rate: 31 (Instant) ch 0 operator 4
    
    //    ym_write(0, 0x58, 0x1F); // Attack Rate: 31 (Instant) ch 0 operator 3
    
    //    ym_write(0, 0x6C, 0x05); // enable amplitude modulation (bit 8), ch0 op4
                             // Decay Rate 1: 0 (Infinite) (bit 5-1)
    //    ym_write(0, 0x68, 0x05); // ch0 op3
    
    
    //    ym_write(0, 0x8C, 0x09); // Sustain Level: 0 (Max) (bit 8-5) ch0 op4
                             // Release Rate           (bit 4-1)
    //    ym_write(0, 0x88, 0x09); // Sustain Level: 0 (Max) (bit 8-5) ch0 op3    

    //    ym_write(0, 0x7C, 0x02); // sustain rate ch0 op4

    // --- Set Pitch & Trigger ---
    //    const unsigned int fnum = 0x22b; // middle c
    //    const char block = 4;
    //    ym_write(0, 0xA4, ((block << 3) | fnum >> 8)); // Frequency High (Block 4)
    //    ym_write(0, 0xA0, fnum & 0xff); // Frequency Low
    
    // Key On: Trigger Channel 1 (Operators 1-4)
    
    // ym_write(0, 0x28, 0xF0);
}

ym_pitch_t midi_to_ym2612(unsigned char midi_note)
{
    ym_pitch_t p;

    // Clamp MIDI range if needed
    if (midi_note < 12)
        midi_note = 12;
    if (midi_note > 107)
        midi_note = 107;

    // Note within octave (0–11)
    unsigned char note = midi_note % 12;

    // MIDI octave number
    int octave = (midi_note / 12) - 1;

    // Block mapping:
    // C4 (octave 4) -> Block 4
    int block = octave;

    if (block < 0) block = 0;
    if (block > 7) block = 7;

    p.block = (unsigned char)block;
    p.fnum  = ym_fnum_table[note];

    return p;
}

void ym_set_pitch_ch0(unsigned char midi_note)
{
    ym_pitch_t p = midi_to_ym2612(midi_note);

    // Block + FNUM high - write first
    ym_write(0, 0xA4, (p.block << 3) | (p.fnum >> 8));

    // FNUM low
    ym_write(0, 0xA0, p.fnum & 0xFF);
    
}


void noteon_chan0() {
  ym_write(0, 0x28, 0xF0); // key on
}

void noteoff_chan0() {
  ym_write(0, 0x28, 0x00); // key off
}
