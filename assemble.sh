../vasm/vasmz80_std -Fbin z80driver.asm -o z80driver.bin
xxd -i z80driver.bin > z80driver.h
cp z80driver.h src/

# xxd -i elec808cowbell.raw > src/elec808cowbell.h
# xxd -i wave.raw > src/wave.h
