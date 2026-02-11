#!/usr/bin/env python3
print("\nArduboy USB hexfile checker 1.00 by Mr.Blinky Aug.2020\n")

import sys
import os

def LoadHexFileData(filename):
    if not os.path.isfile(filename) :
        return bytearray()
    f = open(filename,"r")
    records = f.readlines()
    f.close()
    bytes = bytearray(b'\xFF' * 32768)
    flash_end = 0
    for rcd in records :
        if rcd == ":00000001FF" : break
        if rcd[0] == ":" :
            rcd_len  = int(rcd[1:3],16)
            rcd_typ  = int(rcd[7:9],16)
            rcd_addr = int(rcd[3:7],16)
            rcd_sum  = int(rcd[9+rcd_len*2:11+rcd_len*2],16)
            if (rcd_typ == 0) and (rcd_len > 0) :
                flash_addr = rcd_addr
                checksum = rcd_sum
                for i in range(1,9+rcd_len*2, 2) :
                    byte = int(rcd[i:i+2],16)
                    checksum = (checksum + byte) & 0xFF
                    if i >= 9:
                        bytes[flash_addr] = byte
                        flash_addr += 1
                        if flash_addr > flash_end:
                            flash_end = flash_addr
                if checksum != 0 :
                    print("Error: Hex file '{}' contains errors.".format(os.path.basename(filename)))
                    DelayedExit()
    flash_end = int((flash_end + 255) / 256) * 256
    return bytes[0:flash_end]

def checkNoUSB(program):
  if len(program) < 256: return True
  vector_10 = (program[0x2A] << 1) | (program[0x2B]  << 9) #USB General interrupt vector
  if program[vector_10:vector_10+4] == b'\x0c\x94\x00\x00': return True #bad  interrupts jumps to 0
  return False

################################################################################

for filenumber in range (1,len(sys.argv)): #support multiple files
  filename = sys.argv[filenumber]
  program = LoadHexFileData(filename)
  if checkNoUSB(program): print('!!! No USB support !!! in {}'.format(filename))
  else: print('USB support found in {}'.format(filename))
  
print('\nPress any key.')
input()
  