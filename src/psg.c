#include <stdint.h>
#include "psg.h"

const uint16_t midiNoteToPSGCounter[12] = {
  851,
  803,
  758,
  715,
  675,
  637,
  601,
  568,
  536,
  506,
  477,
  450
};

void psg_reset() {

  volatile uint8_t *pb;
  uint16_t i;

  pb = (uint8_t*) PSG_PORT;

  for (i = 0; i < 4; i++)
    {
        // set tone to 0
        *pb = 0x80 | (i << 5) | 0x00;
        *pb = 0x00;

        // set envelope to silent
        *pb = 0x90 | (i << 5) | 0x0F;
    }  
}

void psg_write(uint8_t data) {

  volatile uint8_t *pb;

  pb = (uint8_t*) PSG_PORT;
  *pb = data;  
}

void psg_setEnvelope(uint8_t channel, uint8_t value) {

  volatile uint8_t *pb;

  pb = (uint8_t*) PSG_PORT;
  *pb = 0x90 | ((channel & 3) << 5) | (value & 0xF);  
}

void psg_setTone(uint8_t channel, uint16_t frequency_value) {

  //  volatile uint8_t *pb;

  //  pb = (uint8_t*) PSG_PORT;
  //    *pb = 0x80 | ((channel & 3) << 5) | (value >> 7);
  //    *pb = value & 0x7F;
      // Ensure the channel is valid (0-2) and frequency is 10-bit

  //if (channel > 2 || frequency_value > 0x3FF) {
  //        return;
  //    }
    
    // 1. Latch tone register and write low 4 bits: %1cctdddd (c=channel, t=type 0 for tone, d=data)
  uint8_t latch_byte = 0x80;
  latch_byte |= channel << 5;
  latch_byte |= frequency_value & 0x0f;
  psg_write(latch_byte);
  // 2. Write the high 6 bits (only low 6 bits of the byte are used, high two bits are 0)
  uint8_t data_byte = (frequency_value >> 4) & 0x3F; // Mask to 6 relevant bits
  psg_write(data_byte);
}

void psg_setToneLow(uint8_t channel, uint8_t value) {

  volatile uint8_t *pb;

  pb = (uint8_t*) PSG_PORT;
  *pb = 0x80 | ((channel & 3) << 5) | (value & 0xF);  
}

void psg_setFrequency(uint8_t channel, uint8_t value) {

  uint16_t data;

  if (value)
    {
      // frequency to tone conversion
      if (IS_PAL_SYSTEM) data = 3546893 / (value * 32);
      else data = 3579545 / (value * 32);
    }
  else data = 0;

  psg_setTone(channel, data);  
}

void psg_setNoise(uint8_t type, uint8_t frequency) {

  volatile uint8_t *pb;

  pb = (uint8_t *) PSG_PORT;
  *pb = 0xE0 | ((type & 1) << 2) | (frequency & 0x3);  
}
