print("Starduino hex file patcher v1.0 by Mr.Blinky Jan. 2022\n")

import sys
import time
import os

CONTRAST_MAX    = 0xFF #Starduino uses max contrast
CONTRAST_NORMAL = 0xC0 #Default Arduboy contrast
CONTRAST_DIMMED = 0x7F 
CONTRAST_LOW    = 0x2F 
CONTRAST_MIN    = 0x00 

CONTRAST = CONTRAST_NORMAL

classic_patch = [
  #addr, discription, original data, patch data
  (0x1024, "Display and sound init patch",
   [0x80, 0xEF, 0x8A, 0xB9, 0x90, 0xE2, 0x9B, 0xB9, 0x1D, 0xB8, 0x90, 0xE4, 0x9E, 0xB9, 0x10, 0xBA, 0x81, 0xBB, 0x8C, 0xE5, 0x8C, 0xBD, 0x81, 0xE0, 0x8D, 0xBD, 0x1E, 0xBC, 0x10, 0x92, 0x72, 0x00, 0x10, 0x92, 0x71, 0x00, 0xEF, 0xE6, 0xF0, 0xE0] ,
   [0x8A, 0xEF, 0x8A, 0xB9, 0x90, 0xE2, 0x9B, 0xB9, 0x1D, 0xB8, 0x90, 0xE4, 0x9E, 0xB9, 0x10, 0xBA, 0x80, 0xEF, 0x81, 0xBB, 0x8C, 0xE5, 0x8C, 0xBD, 0x81, 0xE0, 0x8D, 0xBD, 0x1E, 0xBC, 0xE1, 0xE7, 0xF0, 0xE0, 0x11, 0x82, 0x10, 0x82, 0xEF, 0xE6]
  ),
  (0x11A0, "Activate display reset patch",
   [0x5F, 0x98],
   [0x59, 0x98]
  ),
  (0x11B2, "Deactivate display reset patch",
   [0x5F, 0x9A],
   [0x59, 0x9A]
  ),
  (0x2882,"Sound channel 1 patch",
   [0x30, 0x91, 0x5A, 0x06, 0x63, 0x23],
   [0x0C, 0x94, 0x48, 0x00, 0x60, 0x74],
  ),
  (0x0090, "Sound channel 2 patch (extension in int vector area)",
   [0x0C, 0x94, 0x6D, 0x06, 0x0C, 0x94, 0x6D, 0x06, 0x0C, 0x94, 0x6D, 0x06] ,
   [0x67, 0xFD, 0x5F, 0x9A, 0x67, 0xFF, 0x5F, 0x98, 0x0C, 0x94, 0x43, 0x14]
  ),
  (0x00B7, "Display contrast patch",
  [0x81,0xFF],
  [0x81,CONTRAST]
  )
]

turbo_patch = [
  #addr, discription, original data, patch data
  (0x1024, "Display and sound init patch",
   [0x80, 0xEF, 0x8A, 0xB9, 0x90, 0xE2, 0x9B, 0xB9, 0x1D, 0xB8, 0x90, 0xE4, 0x9E, 0xB9, 0x10, 0xBA, 0x81, 0xBB, 0x8C, 0xE5, 0x8C, 0xBD, 0x81, 0xE0, 0x8D, 0xBD, 0x1E, 0xBC, 0x10, 0x92, 0x72, 0x00, 0x10, 0x92, 0x71, 0x00, 0xEF, 0xE6, 0xF0, 0xE0] ,
   [0x8A, 0xEF, 0x8A, 0xB9, 0x90, 0xE2, 0x9B, 0xB9, 0x1D, 0xB8, 0x90, 0xE4, 0x9E, 0xB9, 0x10, 0xBA, 0x80, 0xEF, 0x81, 0xBB, 0x8C, 0xE5, 0x8C, 0xBD, 0x81, 0xE0, 0x8D, 0xBD, 0x1E, 0xBC, 0xE1, 0xE7, 0xF0, 0xE0, 0x11, 0x82, 0x10, 0x82, 0xEF, 0xE6]
  ),
  (0x11A0, "Activate display reset patch",
   [0x5F, 0x98],
   [0x59, 0x98]
  ),
  (0x11B2, "Deactivate display reset patch",
   [0x5F, 0x9A],
   [0x59, 0x9A]
  ),
  (0x290c,"Sound channel 1 patch",
   [0x30, 0x91, 0x5C, 0x06, 0x63, 0x23],
   [0x0C, 0x94, 0x48, 0x00, 0x60, 0x74],
  ),
  (0x0090, "Sound channel 2 patch (extension in int vector area)",
   [0x0C, 0x94, 0x6D, 0x06, 0x0C, 0x94, 0x6D, 0x06, 0x0C, 0x94, 0x6D, 0x06] ,
   [0x67, 0xFD, 0x5F, 0x9A, 0x67, 0xFF, 0x5F, 0x98, 0x0C, 0x94, 0x88, 0x14]
  ),
  (0x00B7, "Display contrast patch",
  [0x81,0xFF],
  [0x81,CONTRAST]
  )
]

def delayedExit():
  time.sleep(2)
  sys.exit()

def checkPatch(patch):
  global data
  for i in range(len(patch)):
    addr = patch[i][0]
    for p in range(len(patch[i][2])):
      if data[addr+p] != patch[i][2][p]: return False
  return True

def applyPatch(patch):
  global data
  for i in range(len(patch)):
    addr = patch[i][0]
    for p in range(len(patch[i][3])):
      data[addr+p] = patch[i][3][p]
    print("-applying {}".format(patch[i][1]))
  return

################################################################################

if len(sys.argv) != 2 :
  print("Usage: {} game.hex | starduino-turbo.hex\n".format(os.path.basename(sys.argv[0])))
  print("This script creates a patched version of Starduino hex file for use on Homemade")
  print("Arduboys that use a Pro Micro with alternate wiring and a SSD1306 or SSD1309")
  print("displays. The original Starduino hex files can be downloaded from:\n")
  print("https://rv6502.ca/post/2019/01/02/starduino-for-arduboy-3d-gaming-inside-28kb/")
  delayedExit()

hexfile = os.path.abspath(sys.argv[1])
if not os.path.isfile(hexfile) :
  print("File not found. [{}]".format(hexfile))
  delayedExit()

data_addr = 0
data_end  = 0
data = bytearray(b'\xFF' * 32768)

#load hexfile into byte array 
print('Reading hex file "{}"\n'.format(os.path.basename(hexfile)))
f = open(hexfile,"r")
records = f.readlines()
f.close()
data = bytearray(b'\xFF' * 32768)
for rcd in records :
  if rcd == ":00000001FF" : break
  if rcd[0] == ":" :
    rcd_len  = int(rcd[1:3],16)
    rcd_typ  = int(rcd[7:9],16)
    rcd_addr = int(rcd[3:7],16)
    rcd_sum  = int(rcd[9+rcd_len*2:11+rcd_len*2],16)
    if (rcd_typ == 0) and (rcd_len > 0) :
      data_addr = rcd_addr
      checksum = rcd_sum
      for i in range(1,9+rcd_len*2, 2) :
        byte = int(rcd[i:i+2],16)
        checksum = (checksum + byte) & 0xFF
        if i >= 9:
          data[data_addr] = byte
          data_addr += 1
          if data_addr > data_end: data_end = data_addr
      if checksum != 0 :
        print("Hex file contains errors.")
        delayedExit()
        
#patch data
if checkPatch(classic_patch): applyPatch(classic_patch)
elif checkPatch(turbo_patch): applyPatch(turbo_patch)
else:
  print("This Starduino hex file version is not supported.")
  delayedExit()
  
#save patched data to hexfile
hexfile = os.path.splitext(hexfile)[0]+"-alt.hex"
print('\nWriting patched hex file "{}"'.format(os.path.basename(hexfile)))
f = open(hexfile,"w")
data_addr = 0
while data_addr < data_end:
  rcd_len = data_end - data_addr
  if rcd_len > 16: rcd_len = 16
  rcd = ":{:02X}{:04X}00".format(rcd_len,data_addr)
  checksum = (rcd_len + (data_addr >> 8) + (data_addr & 0xFF) ) & 0xFF
  for i in range(rcd_len):
    rcd += "{:02X}".format(data[data_addr])
    checksum = (checksum + data[data_addr]) & 0xFF
    data_addr += 1
  rcd += "{:02X}\n".format((-checksum) & 0xFF)
  f.write(rcd)
f.write(":00000001FF\n")
f.close()
