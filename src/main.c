#include "md.h"
#include "z80.h"
#include "controller.h"
#include "z80driver.h" // z80 driver
#include "rx21kit.h" // sound samples
#include "psg.h"
#include "ym2612.h"

#include <stdint.h>

// tile for background of level meter
const uint32_t blankTile[8] = {
  0x11111111,
  0x11111111,
  0x11111111,
  0x11111111,
  0x11111111,
  0x11111111,
  0x11111111,
  0x11111111
};

// level meter active tile
const uint32_t fillTile[8] = {
  0x33333333,
  0x44444444,
  0x55555555,
  0x66666666,
  0x66666666,
  0x55555555,
  0x44444444,
  0x33333333
};

// background tile
const uint32_t gradTile[8] = {
  0x789aa987,
  0x789aa987,
  0x789aa987,
  0x789aa987,
  0x789aa987,
  0x789aa987,
  0x789aa987,
  0x789aa987
};

/* z80 playback stuff */
// z80 pcm driver command addresses
#define stopCommand_addr 0x0100 // a 1 here will stop any pcm playback
#define playCommand_addr 0x00FF // a > 0 here will start playback
#define sampleStart_addr 0x00103  // 2 bytes - sample start offset
#define sampleLength_addr 0x0101 // 2 bytes - sample length
#define accent_addr 0x00FE // a 0 here will be half as loud, 1 is loudest
#define speed_addr 0x0FD // a lower number will give a faster playback rate
#define outputValue_addr 0x0105
#define sampleMax 9 // maximum sample index for our sequencer - sample / chop count,
                    // used by the 68000 to set the bank, start address and length

/* sequencer stuff */
// gate / sample number sequence
int gateseq[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int accseq[16] = {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}; // accent sequence
int speedseq[16] = {20,21,22,30,29,28,10,12,14,15,13,11,9,8,7,6}; // playback speed sequence
int seqpos = 0; // current playback sequence position
//int framemod = 11; // how many frames to wait before the next sequencer step
int tempo = 11;
int tempo_old = -1;

/* gui stuff */
int column = 0; // editing column
int oldcolumn = 0; // last editing column to check for A button press change
#define COLUMN_COUNT 3 // number of columns
int screen = 0; // whether we're viewing the pcm or psg screen
int oldscreen = -1;
#define SCREEN_COUNT 5 // number of different screens to switch through by pressing the B button
#define SCREEN_PCM_SEQ 0
#define SCREEN_PSG_SEQ 1
#define SCREEN_YM_SEQ 2
#define SCREEN_YM_INST 3
#define SCREEN_PROJECT 4
int playing = 0; // whether to advance the sequencer
int playingCanChange = 1;
/* ym inst gui */
int ym_select_field = 0;
int ym_select_field_old = -1;
#define YM_FIELD_LFO_ENABLE 0
#define YM_FIELD_LFO_SPEED 1
#define YM_FIELD_DETUNE 2
#define YM_FIELD_MULT 3
#define YM_FIELD_LEVEL 4
#define YM_FIELD_ATTACK 5
#define YM_FIELD_RELEASE 6
#define YM_FIELD_COUNT 7
uint8_t ym_lfo_enable = 0;
uint8_t ym_lfo_enable_old = 255;
uint8_t ym_lfo_speed = 0;
uint8_t ym_lfo_speed_old = 255;
uint8_t ym_detune = 0;
uint8_t ym_detune_old = 0;
uint8_t ym_mult = 1;
uint8_t ym_mult_old = 1;
uint8_t ym_level = 0;
uint8_t ym_level_old = 255;
uint8_t ym_attack = 0x1F;
uint8_t ym_attack_old = 255;
uint8_t ym_release = 0xF;
uint8_t ym_release_old = 255;
uint8_t ym_sustain = 0;
uint8_t ym_sustain_old = 255;

/* psg sequencer */
int psgNoteSeq[16] = {20,0,22,0,29,28,0,0,14,15,0,11,0,0,7,6}; // playback speed sequence

/* ym sequencer */
int ymNoteSeq[16] = {20,0,22,0,29,28,0,0,14,15,0,11,0,0,7,6}; // playback speed sequence


void set_ym_lfo(uint8_t enable, uint8_t speed) {
  Z80_requestBus(1);
  YM2612_write(0, 0x22);
  YM2612_write(1, (enable << 3) | (speed & 0x07));
  YM2612_latchDacDataReg();
  Z80_releaseBus();  
}

void set_ym_detune_mult(uint8_t detune, uint8_t mult) {
  Z80_requestBus(1);
  uint8_t val = ((detune & 0x07) << 4) | (mult & 0x0F);
  ym_write(0, 0x3C, val); // DT1/Multi: Multiplier 1 ch0 op4
  YM2612_latchDacDataReg();
  Z80_releaseBus();  
}

void set_ym_level(uint8_t level) {
  Z80_requestBus(1);
  uint8_t val = level & 0x7F;
  ym_write(0, 0x4C, val); // DT1/Multi: Multiplier 1 ch0 op4
  YM2612_latchDacDataReg();
  Z80_releaseBus();  
}

void set_ym_attack(uint8_t attack) {
  Z80_requestBus(1);
  uint8_t val = attack & 0x1F;
  ym_write(0, 0x5C, val); // DT1/Multi: Multiplier 1 ch0 op4
  YM2612_latchDacDataReg();
  Z80_releaseBus();  
}

void set_ym_release_sustain(uint8_t release, uint8_t sustain) { // sustain - 0 is max, 15 is none
  Z80_requestBus(1);
  uint8_t val = ((sustain & 0xF) << 4) | (release & 0xF);
  ym_write(0, 0x8C, val); // DT1/Multi: Multiplier 1 ch0 op4
  YM2612_latchDacDataReg();
  Z80_releaseBus();  
}

/* savegame stuff */
// Define key SRAM memory addresses as volatile pointers
// Volatile is crucial as the hardware might change values outside the C program's control
// used to save the sequence
#define SRAM_START_ADDR ((volatile uint8_t*)0x200001)
#define SRAM_END_ADDR ((volatile uint8_t*)0x20FFFF)
#define SRAM_LOCK_ADDR  ((volatile uint8_t*)0xA130F1)

// Define a simple structure for game save data
typedef struct {
  uint16_t magic;

  uint8_t tempo;
  uint8_t ym_attack;
  uint8_t ym_lfo_enable;
  uint8_t ym_lfo_speed;
  uint8_t ym_detune;
  uint8_t ym_mult;
  uint8_t ym_level;
  uint8_t ym_release;
  uint8_t ym_sustain;  
  
  uint8_t sequence[16];
  uint8_t accent[16];
  uint8_t speed[16];
  uint8_t psgnote[16];
  int8_t ymNoteCh0[16];
  
  uint8_t  checksum;      // Simple checksum for data integrity - (ignored here)
  uint8_t  padding;       // Padding to ensure alignment if needed, although 8-bit access is standard
  
} GameSaveData;

GameSaveData mySave; // our data to save/load

void save_game_to_sram(const GameSaveData* data);
uint8_t load_game_from_sram(GameSaveData* data);
uint8_t calculate_checksum(const GameSaveData* data);
void unlock_sram(void);
void lock_sram(void);

void unlock_sram(void) {
    // Write 1 to the SRAM lock address to enable writing
    *SRAM_LOCK_ADDR = 1;
}

void lock_sram(void) {
    // Write 0 to the SRAM lock address to disable writing
    *SRAM_LOCK_ADDR = 0;
}

uint8_t calculate_checksum(const GameSaveData* data) {
    uint8_t sum = 0;
    const uint8_t* byte_ptr = (const uint8_t*)data;
    // Iterate over all bytes except the checksum itself (last byte before padding)
    for (long unsigned int i = 0; i < sizeof(GameSaveData) - sizeof(uint8_t); i++) {
        sum += byte_ptr[i];
    }
    return sum;
}

void save_game_to_sram(const GameSaveData* data) {
    // In bare-metal C, you need to disable interrupts before accessing memory-mapped I/O
    // This is system-specific, usually involving assembly code or specific CPU registers.
    // Assuming you have a function `disable_interrupts()` and `enable_interrupts()`

    // disable_interrupts(); 
    unlock_sram();

    // The Sega Genesis uses an odd 8-bit addressing scheme for SRAM
    // This means we must write to every *other* physical address.
    // In C, we can iterate over our structure byte by byte and manually
    // write to the correct addresses, offsetting by 2 bytes in memory for each logical byte of data.

    const uint8_t* src = (const uint8_t*)data;
    volatile uint8_t* dest = SRAM_START_ADDR;
    
    for (long unsigned int i = 0; i < sizeof(GameSaveData); i++) {
        // Write the byte from the source structure to the SRAM destination
        *dest = src[i];
        // Move to the next valid SRAM address (skip the next byte)
        dest += 2; 
    }

    lock_sram();
    // enable_interrupts(); 
}

uint8_t load_game_from_sram(GameSaveData* data) {
    // disable_interrupts(); // Disable interrupts

    const volatile uint8_t* src = SRAM_START_ADDR;
    uint8_t* dest = (uint8_t*)data;

    for (long unsigned int i = 0; i < sizeof(GameSaveData); i++) {
        // Read the byte from the SRAM source
        dest[i] = *src;
        // Move to the next valid SRAM address (skip the next byte)
        src += 2;
    }

    // enable_interrupts(); // Re-enable interrupts

    // Verify data integrity using the magic number and checksum
    if (data->magic != 0xABCE) { // Check if the save data has been initialized
	vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
	vdp_puts(VDP_PLAN_A, "incorrect magic", 3, 18);
        return 0; 
    }
    if (data->checksum != calculate_checksum(data)) { // Check if data is corrupted
	vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
	vdp_puts(VDP_PLAN_A, "checksum mismatch", 3, 18);
//        return 0;
	return 1;
    }

    return 1; // Success
}

void savegame_init(void) {

    // Try to load data first
    if (load_game_from_sram(&mySave)) {
        // Data loaded successfully, continue game
        // ... use mySave.player_score, etc.
      tempo = mySave.tempo;
      ym_attack = mySave.ym_attack;
      ym_lfo_enable = mySave.ym_lfo_enable;
      ym_lfo_speed = mySave.ym_lfo_speed;
      ym_detune = mySave.ym_detune;
      ym_mult = mySave.ym_mult;
      ym_level = mySave.ym_level;
      ym_release = mySave.ym_release;
      ym_sustain = mySave.ym_sustain;	      
      
      for (int i=0; i<16; i++) {
	gateseq[i] = mySave.sequence[i];
	accseq[i] = mySave.accent[i];
	speedseq[i] = mySave.speed[i];
	psgNoteSeq[i] = mySave.psgnote[i];
	ymNoteSeq[i] = mySave.ymNoteCh0[i];
      }

      // send the saved settings to the ym chip
      set_ym_lfo(ym_lfo_enable, ym_lfo_speed);
      set_ym_detune_mult(ym_detune, ym_mult);
      set_ym_level(ym_level);
      set_ym_attack(ym_attack);
      set_ym_release_sustain(ym_release, ym_sustain);
      
      vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
      vdp_puts(VDP_PLAN_A, "saved sequence loaded", 3, 18);
    } else {
        // No valid save data found, start a new game and initialize structure
        mySave.magic = 0xABCE; // Set magic number

	mySave.tempo = tempo;
	mySave.ym_attack = ym_attack;
	mySave.ym_lfo_enable = ym_lfo_enable;
	mySave.ym_lfo_speed = ym_lfo_speed;
	mySave.ym_detune = ym_detune;
	mySave.ym_mult = ym_mult;
	mySave.ym_level = ym_level;
	mySave.ym_release = ym_release;
	mySave.ym_sustain = ym_sustain;		
	
	for (int i=0; i<16; i++) {
	  mySave.sequence[i] = gateseq[i];
	  mySave.sequence[i] = accseq[i];
	  mySave.speed[i] = speedseq[i];
	  mySave.psgnote[i] = psgNoteSeq[i];
	  mySave.ymNoteCh0[i] = ymNoteSeq[i];	  
	}
	
        // Calculate and set initial checksum
        mySave.checksum = calculate_checksum(&mySave); 
        // Save the initial data to SRAM immediately
        save_game_to_sram(&mySave);
    }
}

void savegame() {

  mySave.tempo = tempo;
  mySave.ym_attack = ym_attack;
  mySave.ym_lfo_enable = ym_lfo_enable;
  mySave.ym_lfo_speed = ym_lfo_speed;
  mySave.ym_detune = ym_detune;
  mySave.ym_mult = ym_mult;
  mySave.ym_level = ym_level;
  mySave.ym_release = ym_release;
  mySave.ym_sustain = ym_sustain;  

  for (int i=0; i<16; i++) {
    mySave.sequence[i] = gateseq[i];
    mySave.accent[i] = accseq[i];
    mySave.speed[i] = speedseq[i];
    mySave.psgnote[i] = psgNoteSeq[i];
    mySave.ymNoteCh0[i] = ymNoteSeq[i];    
  }
  
  mySave.checksum = calculate_checksum(&mySave); // Update checksum before saving
  save_game_to_sram(&mySave);
  vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
  vdp_puts(VDP_PLAN_A, "sequence saved", 3, 18);
}

void play_sample() {
  Z80_requestBus(1);
  Z80_write(playCommand_addr, 1);
  Z80_releaseBus();

}

void stop_sample() {
  Z80_requestBus(1);
  Z80_write(stopCommand_addr, 1);
  Z80_write(playCommand_addr, 0);  
  Z80_releaseBus();  
}

void set_sample_length(uint16_t length) {
  Z80_requestBus(1);
  Z80_write(sampleLength_addr, length & 0x00FF);
  Z80_write(sampleLength_addr+1, length >> 8);
  Z80_releaseBus();  
}

void set_sample_start(uint16_t length) {
  Z80_requestBus(1);
  Z80_write(sampleStart_addr, length & 0x00FF);
  Z80_write(sampleStart_addr+1, length >> 8);
  Z80_releaseBus();  
}

void set_accent(int accent) {
  Z80_requestBus(1);
  if (accent) {
    Z80_write(accent_addr, 1);
  } else {
    Z80_write(accent_addr, 0);
  }

  Z80_releaseBus();  
}

void set_dacSpeed(uint8_t speed) {
  Z80_requestBus(1);
  Z80_write(speed_addr, speed);
  Z80_releaseBus();  
}

void set_kit_bank() {
  Z80_requestBus(1);
//  uint32_t pcmaddr = (uint32_t)amen_unsigned_raw;
  uint32_t pcmaddr = (uint32_t)rx21kit_raw;
  uint16_t bank = pcmaddr >> 15;
  Z80_setBank(bank);
  Z80_releaseBus();
}

char s[255] = "";
ControllerState player1_state;
int downpressed = 0;
int uppressed = 0;
int rightpressed = 0;
int leftpressed = 0;
int apressed = 0;
int bpressed = 0;
int cpressed = 0;

int frame = 0;
int laststep = 0;
int selectstep = 0;
int lastselectstep = 0;

void clearScreen() {
  for (int i=0; i<SCREEN_TILEH; i++) {
    vdp_text_clear(VDP_PLAN_A, 0, i, SCREEN_TILEH);
  }
}

void displayPCMScreen() {

  // stuff to do when the screen just changed to PCM
  if (screen != oldscreen) {

    clearScreen();

    vdp_puts(VDP_PLAN_A, "PCM SEQ", SCREEN_TILEW - 8, 0);    
    
    // print the step number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", step);
      vdp_puts(VDP_PLAN_A, s, 3, step);
    }

    // print the sample number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", gateseq[step]);
      vdp_puts(VDP_PLAN_A, s, 6, step);
    }  

    // print the accent column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", accseq[step]);
      vdp_puts(VDP_PLAN_A, s, 9, step);
    }  

    // print the speed column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02X", speedseq[step]);
      vdp_puts(VDP_PLAN_A, s, 12, step);
    }  

    // print the cursors
    vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);
    vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
    vdp_puts(VDP_PLAN_A, "<", 8, selectstep);    
  }

  // update the cursors
  if (seqpos != laststep) {
      vdp_text_clear(VDP_PLAN_A, 0, laststep, 3);
      vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);      
      laststep = seqpos;
    }

    // update the values displayed
    if (selectstep != lastselectstep) {
      if (column == 0) {
	vdp_text_clear(VDP_PLAN_A, 5, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
      } else if (column == 1) {
	vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 11, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 8, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 11, selectstep);            
      } else if (column == 2) {
	vdp_text_clear(VDP_PLAN_A, 11, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 14, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 11, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 14, selectstep);            
      }
      lastselectstep = selectstep;
    }
}

void displayPSGScreen() {

  if (screen != oldscreen) {
    clearScreen();
    vdp_puts(VDP_PLAN_A, "PSG SEQ", SCREEN_TILEW - 8, 0);

    // print the step number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", step);
      vdp_puts(VDP_PLAN_A, s, 3, step);
    }

    // print the note number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", psgNoteSeq[step]);
      vdp_puts(VDP_PLAN_A, s, 6, step);
    }      

    // print the cursors
    vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);
    vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
    vdp_puts(VDP_PLAN_A, "<", 8, selectstep);    

  }

  // update the cursors
  if (seqpos != laststep) {
      vdp_text_clear(VDP_PLAN_A, 0, laststep, 3);
      vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);      
      laststep = seqpos;
  }

    // update the values displayed
    if (selectstep != lastselectstep) {
      vdp_text_clear(VDP_PLAN_A, 5, lastselectstep, 1);
      vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);      
      vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
      vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
      lastselectstep = selectstep;
    }  
}

void displayYMScreen() {

  if (screen != oldscreen) {

    clearScreen();
    vdp_puts(VDP_PLAN_A, "YM SEQ ", SCREEN_TILEW - 8, 0);

    // print the step number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", step);
      vdp_puts(VDP_PLAN_A, s, 3, step);
    }

    // print the note number column
    for (int step = 0; step < 16; step++) {
      sprintf(s, "%02d", ymNoteSeq[step]);
      vdp_puts(VDP_PLAN_A, s, 6, step);
    }      

    // print the cursors
    vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);
    vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
    vdp_puts(VDP_PLAN_A, "<", 8, selectstep);    
  }

  // update the cursors
  if (seqpos != laststep) {
      vdp_text_clear(VDP_PLAN_A, 0, laststep, 3);
      vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);      
      laststep = seqpos;
  }

  if (selectstep != lastselectstep) {
    vdp_text_clear(VDP_PLAN_A, 5, lastselectstep, 1);
    vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);      
    vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
    vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
    lastselectstep = selectstep;
  }  
}

void displayYMInstScreen() {

  char s[255];
  
  if (screen != oldscreen) {

    clearScreen();
    vdp_puts(VDP_PLAN_A, "YM INST", SCREEN_TILEW - 8, 0);

    vdp_puts(VDP_PLAN_A, "lfo enable:", 0, 0);
    sprintf(s, "%03d", ym_lfo_enable);
    vdp_puts(VDP_PLAN_A, s, 12, 0);

    vdp_puts(VDP_PLAN_A, "lfo speed :", 0, 1);
    sprintf(s, "%03d", ym_lfo_speed);
    vdp_puts(VDP_PLAN_A, s, 12, 1);

    vdp_puts(VDP_PLAN_A, "detune    :", 0, 2);
    sprintf(s, "%03d", ym_detune);
    vdp_puts(VDP_PLAN_A, s, 12, 2);

    vdp_puts(VDP_PLAN_A, "mult      :", 0, 3);
    sprintf(s, "%03d", ym_mult);
    vdp_puts(VDP_PLAN_A, s, 12, 3);    

    vdp_puts(VDP_PLAN_A, "level     :", 0, 4);
    sprintf(s, "%03d", ym_level);
    vdp_puts(VDP_PLAN_A, s, 12, 4);    

    vdp_puts(VDP_PLAN_A, "attack    :", 0, 5);
    sprintf(s, "%03d", ym_attack);
    vdp_puts(VDP_PLAN_A, s, 12, 5);    

    vdp_puts(VDP_PLAN_A, "release   :", 0, 6);
    sprintf(s, "%03d", ym_release);
    vdp_puts(VDP_PLAN_A, s, 12, 6);    
    
    vdp_puts(VDP_PLAN_A, ">", 11, ym_select_field);
    vdp_puts(VDP_PLAN_A, "<", 15, ym_select_field);
    
  } else {
    if (ym_select_field != ym_select_field_old) {
      if (ym_select_field_old > -1) { // set to -1 on startup, otherwise erase old cursor pos
	vdp_puts(VDP_PLAN_A, " ", 11, ym_select_field_old);
	vdp_puts(VDP_PLAN_A, " ", 15, ym_select_field_old);
      }
      vdp_puts(VDP_PLAN_A, ">", 11, ym_select_field);
      vdp_puts(VDP_PLAN_A, "<", 15, ym_select_field);
      ym_select_field_old = ym_select_field;
    }
    if (ym_lfo_enable != ym_lfo_enable_old) {
      sprintf(s, "%03d", ym_lfo_enable);
      vdp_puts(VDP_PLAN_A, s, 12, 0);
      ym_lfo_enable_old = ym_lfo_enable;
    }
    if (ym_lfo_speed != ym_lfo_speed_old) {
      sprintf(s, "%03d", ym_lfo_speed);
      vdp_puts(VDP_PLAN_A, s, 12, 1);
      ym_lfo_speed_old = ym_lfo_speed;
    }
    if (ym_detune != ym_detune_old) {
      vdp_puts(VDP_PLAN_A, "detune    :", 0, 2);
      sprintf(s, "%03d", ym_detune);
      vdp_puts(VDP_PLAN_A, s, 12, 2);
      ym_detune_old = ym_detune;
    }
    if (ym_mult != ym_mult_old) {
      vdp_puts(VDP_PLAN_A, "mult      :", 0, 3);
      sprintf(s, "%03d", ym_mult);
      vdp_puts(VDP_PLAN_A, s, 12, 3);
      ym_mult_old = ym_mult;
    }
    if (ym_level != ym_level_old) {
      vdp_puts(VDP_PLAN_A, "level     :", 0, 4);
      sprintf(s, "%03d", ym_level);
      vdp_puts(VDP_PLAN_A, s, 12, 4);
      ym_level_old = ym_level;
    }
    if (ym_attack != ym_attack_old) {
      vdp_puts(VDP_PLAN_A, "attack    :", 0, 5);
      sprintf(s, "%03d", ym_attack);
      vdp_puts(VDP_PLAN_A, s, 12, 5);
      ym_attack_old = ym_attack;
    }
    if (ym_release != ym_release_old) {
      vdp_puts(VDP_PLAN_A, "release   :", 0, 6);
      sprintf(s, "%03d", ym_release);
      vdp_puts(VDP_PLAN_A, s, 12, 6);
      ym_release_old = ym_release;
    }    
  }
}

void displayProjectScreen() {

  char s[255];
  
  if (screen != oldscreen) {

    clearScreen();
    vdp_puts(VDP_PLAN_A, "PROJECT", SCREEN_TILEW - 8, 0);

    vdp_puts(VDP_PLAN_A, "tempo:", 0, 0);
    sprintf(s, "%03d", tempo);
    vdp_puts(VDP_PLAN_A, s, 12, 0);

  } else {
    if (tempo != tempo_old) {
      sprintf(s, "%03d", tempo);
      vdp_puts(VDP_PLAN_A, s, 12, 0);
      tempo_old = tempo;
    }
  }
}

int main() {

  vdp_init();
  enable_ints;
    
  vdp_color(0, 0x888); // background

  vdp_color(7, 0x600);
  vdp_color(8, 0x800);
  vdp_color(9, 0xa00);
  vdp_color(10, 0xc00);

  vdp_color(3, 0x040); // vu meter
  vdp_color(4, 0x060);
  vdp_color(5, 0x080);
  vdp_color(6, 0x0A0);

  Z80_init();  
  Z80_loadDriverInternal(z80driver_bin, z80driver_bin_len);
  set_kit_bank(); // let z80 access our pcm data

  YM2612_reset(1);

  savegame_init(); // after resetting ym2612  

  vdp_tiles_load(blankTile, 100, 1);
  vdp_tiles_load(fillTile, 101, 1);
  vdp_tiles_load(gradTile, 102, 1);

  for (int x=0; x<SCREEN_TILEW; x++) {
    for (int y=0; y<SCREEN_TILEH; y++) {
      vdp_map_xy(VDP_PLAN_B, 102, x, y);
    }
  }

  while(1) {
    
    read_controller1(&player1_state);

    // check if down was pressed
    if (player1_state.down) {
      if (!downpressed) {
	if (screen == SCREEN_YM_INST) {
	  ym_select_field++;
	  if (ym_select_field >= YM_FIELD_COUNT) ym_select_field = YM_FIELD_COUNT - 1;
	} else {
	  selectstep = (selectstep + 1) % 16;
	}
	downpressed = 1;
      }
    } else {
      downpressed = 0;
    }

    // check if up was pressed
    if (player1_state.up) {
      if (!uppressed) {
	if (screen == SCREEN_YM_INST) {
	  ym_select_field--;
	  if (ym_select_field < 0) ym_select_field = 0;
	} else {
	  selectstep = selectstep - 1;
	  if (selectstep < 0) selectstep = 15;
	}
	uppressed = 1;	
      }
    } else {
      uppressed = 0;
    }

    // check if left was pressed
    if (player1_state.left) {
      if (!leftpressed) {
	if (screen == SCREEN_PCM_SEQ) {
	  if (column == 0) {
	    gateseq[selectstep]--;
	    if (gateseq[selectstep] < 0) gateseq[selectstep] = 0;
	
	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	    sprintf(s, "%02d", gateseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 6, selectstep);      
	  } else if (column == 1) {
	    accseq[selectstep]--;
	    if (accseq[selectstep] < 0) accseq[selectstep] = 0;
	
	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 9, selectstep, 2);
	    sprintf(s, "%02d", accseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 9, selectstep);      

	  } else if (column == 2) {

	    speedseq[selectstep]--;
	    if (speedseq[selectstep] < 1) speedseq[selectstep] = 1;
	
	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 12, selectstep, 2);
	    sprintf(s, "%02d", speedseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 12, selectstep);      
	  }
	} else if (screen == SCREEN_PSG_SEQ) {
	  
	  psgNoteSeq[selectstep]--;
	  
	  if (psgNoteSeq[selectstep] < 0) psgNoteSeq[selectstep] = 0;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", psgNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);
	} else if (screen == SCREEN_YM_SEQ) {

	  ymNoteSeq[selectstep]--;
	  
	  if (ymNoteSeq[selectstep] < -1) ymNoteSeq[selectstep] = -1;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", ymNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);	  
	} else if (screen == SCREEN_YM_INST) {
	  if (ym_select_field == YM_FIELD_LFO_ENABLE) {
	    ym_lfo_enable = !ym_lfo_enable;
	    set_ym_lfo(ym_lfo_enable, ym_lfo_speed);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_LFO_SPEED) {
	    if (ym_lfo_speed > 0)
	      ym_lfo_speed--;
	    set_ym_lfo(ym_lfo_enable, ym_lfo_speed);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_DETUNE) {
	    if (ym_detune > 0) ym_detune--;
	    set_ym_detune_mult(ym_detune, ym_mult);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_MULT) {
	    if (ym_mult > 0) ym_mult--;
	    set_ym_detune_mult(ym_detune, ym_mult);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_LEVEL) {
	    if (ym_level > 0) ym_level--;
	    set_ym_level(ym_level);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_ATTACK) {
	    if (ym_attack > 0) ym_attack--;
	    set_ym_attack(ym_attack);
	    savegame();
	  } else if (ym_select_field == YM_FIELD_RELEASE) {
	    if (ym_attack > 0) ym_release--;
	    set_ym_release_sustain(ym_release, ym_sustain);
	    savegame();
	  }
	} else if (screen == SCREEN_PROJECT) {
	  if (tempo > 1) {
	    tempo--;
	    savegame();
	  }
	}
	leftpressed = 1;
      }
    } else {
      leftpressed = 0;
    }

    // check if right was pressed
    if (player1_state.right) {
      if (!rightpressed) {
	if (screen == SCREEN_PCM_SEQ) {
	  if (column == 0) {

	    gateseq[selectstep]++;
	    if (gateseq[selectstep] > sampleMax) gateseq[selectstep] = sampleMax;

	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	    sprintf(s, "%02d", gateseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 6, selectstep);
	    rightpressed = 1;
	  } else if (column == 1) {
	    accseq[selectstep]++;
	    if (accseq[selectstep] > 1) accseq[selectstep] = 1;

	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 9, selectstep, 2);
	    sprintf(s, "%02d", accseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 9, selectstep);

	  }  else if (column == 2) {

	    speedseq[selectstep]++;
	    if (speedseq[selectstep] > 255) speedseq[selectstep] = 255;
	
	    savegame();

	    vdp_text_clear(VDP_PLAN_A, 12, selectstep, 2);
	    sprintf(s, "%02d", speedseq[selectstep]);
	    vdp_puts(VDP_PLAN_A, s, 12, selectstep);      
	  
	  }  
	} else if (screen == SCREEN_PSG_SEQ) { // psg

	  psgNoteSeq[selectstep]++;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", psgNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);      	  
	} else if (screen == SCREEN_YM_SEQ) {
	  
	  ymNoteSeq[selectstep]++;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", ymNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);
	  
	} else if (screen == SCREEN_YM_INST) { // ym instrument screen
	  
	  if (ym_select_field == YM_FIELD_LFO_ENABLE) {
	    ym_lfo_enable = !ym_lfo_enable;
	    set_ym_lfo(ym_lfo_enable, ym_lfo_speed);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_LFO_SPEED) {
	    ym_lfo_speed++;
	    if (ym_lfo_speed > 7) ym_lfo_speed = 7;
	    set_ym_lfo(ym_lfo_enable, ym_lfo_speed);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_DETUNE) {
	    ym_detune++;
	    if (ym_detune > 7) ym_detune = 7;
	    set_ym_detune_mult(ym_detune, ym_mult);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_MULT) {
	    ym_mult++;
	    if (ym_mult > 0x0F) ym_mult = 0x0F;
	    set_ym_detune_mult(ym_detune, ym_mult);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_LEVEL) {
	    if (ym_level < 0x7F) ym_level++;
	    set_ym_level(ym_level);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_ATTACK) {
	    if (ym_attack < 0x1F) ym_attack++;
	    set_ym_attack(ym_attack);
	    savegame();	    
	  } else if (ym_select_field == YM_FIELD_RELEASE) {
	    if (ym_release < 0xF) ym_release++;
	    set_ym_release_sustain(ym_release, ym_sustain);
	    savegame();	    
	  }
	} else if (screen == SCREEN_PROJECT) {
	  if (tempo < 255) {
	    tempo++;
	    savegame();	    
	  }
	}

	rightpressed = 1;
      }
    } else {
      rightpressed = 0;
    }

    // check if A was pressed
    if (player1_state.a) {
      if (!apressed) {
	if (screen == SCREEN_PCM_SEQ) { // pcm
	  column = (column + 1) % COLUMN_COUNT;
	  if (column == 1) {
	    vdp_text_clear(VDP_PLAN_A, 5, selectstep, 1);
	    vdp_text_clear(VDP_PLAN_A, 8, selectstep, 1);      
	    vdp_puts(VDP_PLAN_A, ">", 8, selectstep);
	    vdp_puts(VDP_PLAN_A, "<", 11, selectstep);            
	  } else if (column == 0) {
	    vdp_text_clear(VDP_PLAN_A, 11, selectstep, 1);
	    vdp_text_clear(VDP_PLAN_A, 14, selectstep, 1);      
	    vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
	    vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
	  } else if (column == 2) {
	    vdp_text_clear(VDP_PLAN_A, 8, selectstep, 1);
	    vdp_text_clear(VDP_PLAN_A, 11, selectstep, 1);      
	    vdp_puts(VDP_PLAN_A, ">", 11, selectstep);
	    vdp_puts(VDP_PLAN_A, "<", 14, selectstep);
	  }
	  oldcolumn = column;
	}
	apressed = 1;
      }
    } else {
      apressed = 0;
    }

    // check if B was pressed
    if (player1_state.b) {
      if (!bpressed) {
	screen = (screen + 1) % SCREEN_COUNT;
      }
      bpressed = 1;      
    } else {
      bpressed = 0;
    }    

    // check if C was pressed - doesn't work some reason
    if (apressed && downpressed) {
      // need to change playing once and set something so it can't be changed
      // until one of the buttons is released
      if (playingCanChange) {
	playing = !playing;
	if (playing) {
	  vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
	  vdp_puts(VDP_PLAN_A, "playing", 3, 18);
	} else {
	  vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
	  vdp_puts(VDP_PLAN_A, "stopped", 3, 18);
	  stop_sample(); // stop any playing
	  psg_setEnvelope(0, 15);	  
	}
	playingCanChange = 0;
      }

    } else {
      playingCanChange = 1;
    }
    
    if (screen == SCREEN_PCM_SEQ) {
      displayPCMScreen();
    } else if (screen == SCREEN_PSG_SEQ) {
      displayPSGScreen();
    } else if (screen == SCREEN_YM_SEQ) {
      displayYMScreen();
    } else if (screen == SCREEN_YM_INST) {
      displayYMInstScreen();
    } else if (screen == SCREEN_PROJECT) {
      displayProjectScreen();
    }
    oldscreen = screen;

    // check if we need to update the sequencer
    //    if (frame % framemod == 0) {
    if (frame % tempo == 0) {    

      if (playing) {

	/* psg sequencer */
	if (psgNoteSeq[seqpos]) {
	  int note = psgNoteSeq[seqpos] - 1;
	  int octave = 0;
	  if (note > 11 && note < 24) {
	    note -= 12;
	    octave++;
	  } else if (note >= 24 && note < 36) {
	    note -= 24;
	    octave += 2;
	  } else if (note >= 36 && note < 48) {
	    note -= 36;
	    octave += 3;
	  } else if (note >= 48 && note < 60) {
	    note -= 48;
	    octave += 4;
	  } else if (note >= 60) {
	    note -= 60;
	    octave += 5;
	  }
	  int counterVal = midiNoteToPSGCounter[note];
	  if (octave == 1) {
	    counterVal /= 2;
	  } else if (octave == 2) {
	    counterVal /= 4;
	  } else if (octave == 3) {
	    counterVal /= 8;
	  } else if (octave == 4) {
	    counterVal /= 16;
	  } else if (octave == 5) {
	    counterVal /= 32;
	  }
	  psg_setTone(0, counterVal);
	  psg_setEnvelope(0, 5);	  
	} else {
	  psg_setEnvelope(0, 15);	  	  
	}

	/* ym sequencer */	
	if (ymNoteSeq[seqpos] > 0) {
	  Z80_requestBus(1);
	  noteoff_chan0();
	  ym_set_pitch_ch0(ymNoteSeq[seqpos]);
	  noteon_chan0();
	  YM2612_latchDacDataReg();
	  Z80_releaseBus();
	} else if (ymNoteSeq[seqpos] == -1) {
	  Z80_requestBus(1);
	  noteoff_chan0();
	  YM2612_latchDacDataReg();
	  Z80_releaseBus();
	}

	/* pcm sequencer */
	if (gateseq[seqpos]) { // do we need to play a sample?
	  
	  stop_sample(); // stop any playing first
	  
	  if (accseq[seqpos] == 1) {
	    set_accent(1); // set the accent
	  } else {
	    set_accent(0);
	  }
	  if (gateseq[seqpos] == 1) { // play sample 1 - clap
	    set_sample_start(0);
	    set_sample_length(659);
	  } else if (gateseq[seqpos] == 2) { // play sample 2 - cymbal
	    set_sample_start(659);
	    set_sample_length(7761);
	  } else if (gateseq[seqpos] == 3) { // play sample 3 - hat closed
	    set_sample_start(659+7761);
	    set_sample_length(863);
	  } else if (gateseq[seqpos] == 4) { // play sample 4 - hat open
	    set_sample_start(659+7761+863);
	    set_sample_length(4299);
	  } else if (gateseq[seqpos] == 5) { // play sample 5 - kick
	    set_sample_start(659+7761+863+4299);
	    set_sample_length(662);
	  } else if (gateseq[seqpos] == 6) { // play sample 6 - snare
	    set_sample_start(659+7761+863+4299+662);
	    set_sample_length(1058);
	  } else if (gateseq[seqpos] == 7) { // play sample 7 - tom high
	    set_sample_start(659+7761+863+4299+662+1058);
	    set_sample_length(1585);
	  } else if (gateseq[seqpos] == 8) { // play sample 8 - tom low
	    set_sample_start(659+7761+863+4299+662+1058+1585);
	    set_sample_length(1585);
	  } else if (gateseq[seqpos] == 9) { // play sample 9 - tom mid
	    set_sample_start(659+7761+863+4299+662+1058+1585+1585);
	    set_sample_length(1607);
	  }
	  set_dacSpeed(speedseq[seqpos]); // set the playback speed
	  play_sample();
	} else {
	  // we have to stop the sample if it's not set every step or we hear noise.
	  // didn't happen until I added the ym code
//	  stop_sample();
	}
	seqpos = (seqpos + 1) % 16;
	
      }
    }    

    Z80_requestBus(1);
    uint8_t color = Z80_read(outputValue_addr);
    Z80_releaseBus();  

    //    vdp_color(0, color);
    color = color >> 4;
    for (int i=0; i<38; i++) {
      if (i <= color) {
	vdp_map_xy(VDP_PLAN_A, 101, i+1, 26);
      } else {
	vdp_map_xy(VDP_PLAN_A, 100, i+1, 26);
      }
    }

    vdp_vsync();
    frame++;
  }
	
  return 0;
}
