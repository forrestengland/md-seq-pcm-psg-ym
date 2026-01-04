#ifndef H_SRAM
#define H_SRAM

#include <stdint.h>

// Define key SRAM memory addresses as volatile pointers
// Volatile is crucial as the hardware might change values outside the C program's control
#define SRAM_START_ADDR ((volatile uint8_t*)0x200001)
#define SRAM_LOCK_ADDR  ((volatile uint8_t*)0xA130F1)

// Define a simple structure for game save data
typedef struct {
    uint16_t magic;         // Magic number to verify if data is initialized (e.g., 0xABCD)
    uint16_t player_score;
    uint8_t  player_lives;
    uint8_t  player_level;
    uint8_t  checksum;      // Simple checksum for data integrity
    uint8_t  padding;       // Padding to ensure alignment if needed, although 8-bit access is standard
} GameSaveData;

// --- Function Prototypes ---
void save_game_to_sram(const GameSaveData* data);
uint8_t load_game_from_sram(GameSaveData* data);
uint8_t calculate_checksum(const GameSaveData* data);
void unlock_sram(void);
void lock_sram(void);


// --- Function Implementations ---

/**
 * Unlock SRAM for writing.
 */
void unlock_sram(void) {
    // Write 1 to the SRAM lock address to enable writing
    *SRAM_LOCK_ADDR = 1;
}

/**
 * Lock SRAM to prevent accidental writes.
 */
void lock_sram(void) {
    // Write 0 to the SRAM lock address to disable writing
    *SRAM_LOCK_ADDR = 0;
}

/**
 * Calculate a simple checksum for the save data.
 */
uint8_t calculate_checksum(const GameSaveData* data) {
    uint8_t sum = 0;
    const uint8_t* byte_ptr = (const uint8_t*)data;
    // Iterate over all bytes except the checksum itself (last byte before padding)
    for (int i = 0; i < sizeof(GameSaveData) - sizeof(uint8_t); i++) {
        sum += byte_ptr[i];
    }
    return sum;
}

/**
 * Save game data to the Sega Genesis SRAM.
 */
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
    
    for (int i = 0; i < sizeof(GameSaveData); i++) {
        // Write the byte from the source structure to the SRAM destination
        *dest = src[i];
        // Move to the next valid SRAM address (skip the next byte)
        dest += 2; 
    }

    lock_sram();
    // enable_interrupts(); 
}

/**
 * Load game data from the Sega Genesis SRAM.
 * Returns 1 if successful (valid data), 0 otherwise.
 */
uint8_t load_game_from_sram(GameSaveData* data) {
    // disable_interrupts(); // Disable interrupts

    const volatile uint8_t* src = SRAM_START_ADDR;
    uint8_t* dest = (uint8_t*)data;

    for (int i = 0; i < sizeof(GameSaveData); i++) {
        // Read the byte from the SRAM source
        dest[i] = *src;
        // Move to the next valid SRAM address (skip the next byte)
        src += 2;
    }

    // enable_interrupts(); // Re-enable interrupts

    // Verify data integrity using the magic number and checksum
    if (data->magic != 0xABCD) { // Check if the save data has been initialized
        return 0; 
    }
    if (data->checksum != calculate_checksum(data)) { // Check if data is corrupted
        return 0;
    }

    return 1; // Success
}

// Example usage within a theoretical main loop:

void main(void) {
    GameSaveData mySave;

    // Try to load data first
    if (load_game_from_sram(&mySave)) {
        // Data loaded successfully, continue game
        // ... use mySave.player_score, etc.
    } else {
        // No valid save data found, start a new game and initialize structure
        mySave.magic = 0xABCD; // Set magic number
        mySave.player_score = 0;
        mySave.player_lives = 3;
        mySave.player_level = 1;
        // Calculate and set initial checksum
        mySave.checksum = calculate_checksum(&mySave); 
        // Save the initial data to SRAM immediately
        save_game_to_sram(&mySave);
    }

    // Main game loop
    while (1) {
        // ... game logic ...

        // Example: when the player levels up, update data and save
        if (/* level up event */ 0) {
            mySave.player_level++;
            mySave.checksum = calculate_checksum(&mySave); // Update checksum before saving
            save_game_to_sram(&mySave);
        }
    }
}

#endif
