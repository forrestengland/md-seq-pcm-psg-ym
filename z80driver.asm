; Z80 pcm Sound Driver - Forrest England 2025

.equ ramStart, 0x0000
.equ ramEnd, 0x2000

.equ ymPort0_addr, 0x4000
.equ ymPort0_data, 0x4001
.equ ymDacEnable_reg, 0x2B ; ym register address for dac enable on channel 6
.equ ymDacEnable_val, 0x80
.equ ymDacData_reg, 0x2A

.equ stopCommand_addr, 0x0100
.equ playCommand_addr, 0x00FF
.equ sampleLength_addr, 0x0101 ; 2 bytes
.equ sampleStart_addr, 0x0103 ; 2 bytes
.equ sampleBank_addr, 0x8000 ; start of sample data in banked area
.equ accent_addr, 0x00FE
.equ speed_addr, 0x00FD
.equ outputValue_addr, 0x0105
	
.org ramStart ; where in ram we'll be loaded
	
    di              ; Disable interrupts
    ld sp, ramEnd   ; Set Z80 stack pointer to top of 8KB RAM

    ; Enable DAC on YM2612 (Register $2B = $80)
    ld a, ymDacEnable_reg ; Address for DAC enable
    ld (ymPort0_addr), a  ; Port 0 Address
    ld a, ymDacEnable_val ; Value to enable
    ld (ymPort0_data), a  ; Port 0 Data

stop_playing: 

  xor a ; zero out
  ld (stopCommand_addr), a  ; clear stop command byte

 main_loop:
     ld a, (playCommand_addr)  ; get play command byte value
     or a            ; Is it non-zero?
     jr z, main_loop ; Wait if 0

    ;;  2. Select DAC Data Register 
    ld a, ymDacData_reg
    ld (ymPort0_addr), a

play_pcm_start:
	
    ; Play PCM Sample (Example: fixed location at Z80 $8000)
    ld hl, sampleBank_addr  ;; Sample start (mapped via Z80 bank register) 
    ld bc, (sampleStart_addr) ;; add requested start offset
    add hl, bc

    ; Length of sample 
    ld bc, (sampleLength_addr)

play_pcm:

    ; load accent value (1 or 0)
    ld a, (accent_addr)
    or a ; is it non zero?
    jr nz, skip_attenuate ;jump if not zero

    ; accent is zero, attenuate
    ld a, (hl)      ; Load sample byte    
    srl a          ; divide it by 2
;    lsr a          ; divide it by 2
    ld (ymPort0_data), a ; output to dac
    jr inc_samplecounter

skip_attenuate: 
    ld a, (hl)      ; Load sample byte
    nop ; make take same amount of time as accented
    nop
    nop     
    ld (ymPort0_data), a ; output to dac

inc_samplecounter:  

    ; save dac output value in case we want to visualize it
    ld (outputValue_addr), a
	
    inc hl          ; Next byte
    ; Timing delay (controls sample rate ~8-12kHz)
    ld a, (speed_addr)
    ld d, a
	
wait:

  ld a, (stopCommand_addr)  ; get stop command byte value
  or a            ; Is it non-zero?
  jr nz, stop_playing ; stop if 1

  dec d
  jr nz, wait

    dec bc          ; sample Counter
    ld a, b
    or c
	
    jr nz, play_pcm ; Loop until finished

    ;; jr play_pcm_start

    xor a
    ld (playCommand_addr), a  ; Clear command byte
    jr main_loop

