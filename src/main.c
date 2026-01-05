#include "md.h"
#include "z80.h"
#include "controller.h"
#include "z80driver.h"
#include "amen_unsigned.h"
#include "psg.h"
#include "ym2612.h"

#include <stdint.h>

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
#define sampleMax 2 // maximum sample index for our sequencer - sample / chop count,
                    // used by the 68000 to set the bank, start address and length

/* sequencer stuff */
// gate / sample number sequence
int gateseq[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int accseq[16] = {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}; // accent sequence
int speedseq[16] = {20,21,22,30,29,28,10,12,14,15,13,11,9,8,7,6}; // playback speed sequence
int seqpos = 0; // current playback sequence position
int framemod = 10; // how many frames to wait before the next sequencer step
int column = 0; // editing column
int oldcolumn = 0; // last editing column to check for A button press change
#define COLUMN_COUNT 3 // number of columns
int screen = 0; // whether we're viewing the pcm or psg screen
int oldscreen = -1;
#define SCREEN_COUNT 3 // number of different screens to switch through by pressing the B button
int playing = 0; // whether to advance the sequencer
int playingCanChange = 1;

/* psg sequencer */
int psgNoteSeq[16] = {20,0,22,0,29,28,0,0,14,15,0,11,0,0,7,6}; // playback speed sequence

/* ym sequencer */
int ymNoteSeq[16] = {20,0,22,0,29,28,0,0,14,15,0,11,0,0,7,6}; // playback speed sequence

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
  uint8_t sequence[16];
  uint8_t accent[16];
  uint8_t speed[16];
  uint8_t psgnote[16];
  uint8_t ymNoteCh0[16];
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
      for (int i=0; i<16; i++) {
	gateseq[i] = mySave.sequence[i];
	accseq[i] = mySave.accent[i];
	speedseq[i] = mySave.speed[i];
	psgNoteSeq[i] = mySave.psgnote[i];
	ymNoteSeq[i] = mySave.ymNoteCh0[i];
      }
      vdp_text_clear(VDP_PLAN_A, 3, 18, 40);
      vdp_puts(VDP_PLAN_A, "saved sequence loaded", 3, 18);
    } else {
        // No valid save data found, start a new game and initialize structure
        mySave.magic = 0xABCE; // Set magic number
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

void set_amen_bank() {
  Z80_requestBus(1);
  uint32_t pcmaddr = (uint32_t)amen_unsigned_raw;
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

    vdp_puts(VDP_PLAN_A, "PCM", SCREEN_TILEW - 4, 0);    
    
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
    vdp_puts(VDP_PLAN_A, "PSG", SCREEN_TILEW - 4, 0);

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
    vdp_puts(VDP_PLAN_A, "YMF", SCREEN_TILEW - 4, 0);

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

  savegame_init();

  set_amen_bank(); // let z80 access our pcm data

  YM2612_reset(1);

  //      Z80_requestBus(1);
  //      play_sine_wave();
  //      Z80_releaseBus();
  
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
	//	if (screen == 0) {
	  selectstep = (selectstep + 1) % 16;
	  //	}
	downpressed = 1;
      }
    } else {
      downpressed = 0;
    }

    // check if up was pressed
    if (player1_state.up) {
      if (!uppressed) {
	//	if (screen == 0) {
	  selectstep = selectstep - 1;
	  if (selectstep < 0) selectstep = 15;
	  //	}
	uppressed = 1;	
      }
    } else {
      uppressed = 0;
    }

    // check if left was pressed
    if (player1_state.left) {
      if (!leftpressed) {
	if (screen == 0) {
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
	} else if (screen == 1) {
	  
	  psgNoteSeq[selectstep]--;
	  
	  if (psgNoteSeq[selectstep] < 0) psgNoteSeq[selectstep] = 0;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", psgNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);
	} else if (screen == 2) {

	  ymNoteSeq[selectstep]--;
	  
	  if (ymNoteSeq[selectstep] < 0) ymNoteSeq[selectstep] = 0;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", ymNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);	  
	  }
	leftpressed = 1;
      }
    } else {
      leftpressed = 0;
    }

    // check if right was pressed
    if (player1_state.right) {
      if (!rightpressed) {
	if (screen == 0) {
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
	} else if (screen == 1) { // psg

	  psgNoteSeq[selectstep]++;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", psgNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);      	  
	} else if (screen == 2) {
	  
	  ymNoteSeq[selectstep]++;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", ymNoteSeq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);      	  	  
	  }
	rightpressed = 1;
      }
    } else {
      rightpressed = 0;
    }

    // check if A was pressed
    if (player1_state.a) {
      if (!apressed) {
	if (screen == 0) { // pcm
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
    
    if (screen == 0) {
      displayPCMScreen();
    } else if (screen == 1) {
      displayPSGScreen();
    } else if (screen == 2) {
      displayYMScreen();
      }
    oldscreen = screen;

    // check if we need to update the sequencer
    if (frame % framemod == 0) {

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
	if (ymNoteSeq[seqpos]) {
	  Z80_requestBus(1);
	  noteoff_chan0();
	  ym_set_pitch_ch0(ymNoteSeq[seqpos]);
	  noteon_chan0();
	  Z80_releaseBus();
	} else {
	  Z80_requestBus(1);
	  noteoff_chan0();
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
	  if (gateseq[seqpos] == 1) { // play sample 1 (whole break)
	    set_sample_start(0);
	    set_sample_length(amen_unsigned_raw_len);
	  } else if (gateseq[seqpos] == 2) { // play sample 2 (second half of break)
	    set_sample_start(amen_unsigned_raw_len / 2 - 1);
	    set_sample_length(amen_unsigned_raw_len / 2);
	  }
	  set_dacSpeed(speedseq[seqpos]); // set the playback speed
	  play_sample();
	} else {
	  // we have to stop the sample if it's not set every step or we hear noise.
	  // didn't happen until I added the ym code
	  stop_sample();
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
