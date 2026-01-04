#ifndef H_PSG
#define H_PSG

#define PSG_PORT 0xC00011
#define PSG_ENVELOPE_MIN 15
#define PSG_ENVELOPE_MAX 0
#define PSG_NOISE_TYPE_PERIODIC 0
#define PSG_NOISE_TYPE_WHITE 1
#define PSG_NOISE_FREQ_CLOCK2   0
#define PSG_NOISE_FREQ_CLOCK4   1
#define PSG_NOISE_FREQ_CLOCK8   2
#define PSG_NOISE_FREQ_TONE3    3
#define IS_PAL_SYSTEM 0

extern const uint16_t midiNoteToPSGCounter[12];

void psg_reset();
void psg_write(uint8_t data);
void psg_setEnvelope(uint8_t channel, uint8_t value);
void psg_setTone(uint8_t channel, uint16_t frequency_value);
void psg_setToneLow(uint8_t channel, uint8_t value);
void psg_setFrequency(uint8_t channel, uint8_t value);
void psg_setNoise(uint8_t type, uint8_t frequency);

#endif
