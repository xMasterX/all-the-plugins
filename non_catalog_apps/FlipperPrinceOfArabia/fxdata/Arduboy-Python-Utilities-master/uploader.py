print("\nArduboy python uploader v1.2 by Mr.Blinky April 2018 - Jan 2019")

#requires pyserial to be installed. Use "python -m pip install pyserial" on commandline

#Python 2.7 and Python 3.7 compatible

#rename this script filename to 'uploader-1309.py' to patch uploads on the fly
#for use with SSD1309 displays

#rename this script filename to 'uploader-micro.py' to patch RX and TX LED 
#polarity on the fly for use with Arduino / Genuino Micro

#rename this script filename to 'uploader-micro-1309.py' to apply both patches

import sys
import time
import os
from serial.tools.list_ports  import comports
from serial import Serial
import zipfile

lcdBootProgram = b"\xD5\xF0\x8D\x14\xA1\xC8\x81\xCF\xD9\xF1\xAF\x20\x00"

compatibledevices = [
 #Arduboy Leonardo
 "VID:PID=2341:0036", "VID:PID=2341:8036",
 "VID:PID=2A03:0036", "VID:PID=2A03:8036",
 #Arduboy Micro
 "VID:PID=2341:0037", "VID:PID=2341:8037",
 "VID:PID=2A03:0037", "VID:PID=2A03:8037",
 #Genuino Micro
 "VID:PID=2341:0237", "VID:PID=2341:8237",
 #Sparkfun Pro Micro 5V
 "VID:PID=1B4F:9205", "VID:PID=1B4F:9206",
 #Adafruit ItsyBitsy 5V
 "VID:PID=239A:000E", "VID:PID=239A:800E",
]

bootloader_active = False
caterina_overwrite = False

flash_addr = 0
flash_data = bytearray(b'\xFF' * 32768)
flash_page       = 1
flash_page_count = 0
flash_page_used  = [False] * 256

def delayedExit():
  time.sleep(3)
  #raw_input()  
  sys.exit()

def getComPort(verbose):
  global bootloader_active
  devicelist = list(comports())
  for device in devicelist:
    for vidpid in compatibledevices:
      if  vidpid in device[2]:
        port=device[0]
        bootloader_active = (compatibledevices.index(vidpid) & 1) == 0
        if verbose : print("Found {} at port {}".format(device[1],port))
        return port
  if verbose : print("Arduboy not found.")

def bootloaderStart():
  global bootloader
  ## find and connect to Arduboy in bootloader mode ##
  port = getComPort(True)
  if port is None : delayedExit()
  if not bootloader_active:
    print("Selecting bootloader mode...")
    bootloader = Serial(port,1200)
    bootloader.close()
    time.sleep(0.5)
    #wait for disconnect and reconnect in bootloader mode
    while getComPort(False) == port :
      time.sleep(0.1)
      if bootloader_active: break        
    while getComPort(False) is None : time.sleep(0.1)
    port = getComPort(True)
  
  time.sleep(0.1)	
  bootloader = Serial(port,57600)
  
def bootloaderExit():
  global bootloader
  bootloader.write(b"E")
  bootloader.read(1)
  
################################################################################

if len(sys.argv) != 2 :
  print("\nUsage: {} hexfile.hex\n".format(os.path.basename(sys.argv[0])))
  delayedExit()

## Load and parse file ##
path = os.path.dirname(sys.argv[0]) + os.sep
filename = sys.argv[1]
if not os.path.isfile(filename) :
  print("File not found. [{}]".format(filename))
  delayedExit()
  
#if file is (.arduboy) zipfile, extract hex file
try:
  zip = zipfile.ZipFile(filename)
  for file in zip.namelist():
    if file.lower().endswith(".hex"):
      zipinfo = zip.getinfo(file)
      zipinfo.filename = os.path.basename(sys.argv[0]).replace(".py",".tmp")
      zip.extract(zipinfo,path)
      hexfile = path + zipinfo.filename
      print('\nLoading "{}" from Arduboy file "{}"'.format(file,os.path.basename(filename)))
      break
  tempfile = True
except:
  hexfile = filename
  print('\nLoading "{}"'.format(os.path.basename(hexfile)))
  tempfile = False
  
f = open(hexfile,"r")
records = f.readlines()
f.close()
if tempfile == True : os.remove(hexfile)

for rcd in records :
  if rcd == ":00000001FF" : break
  if rcd[0] == ":" :
    rcd_len  = int(rcd[1:3],16)
    rcd_typ  = int(rcd[7:9],16)
    rcd_addr = int(rcd[3:7],16)
    rcd_sum  = int(rcd[9+rcd_len*2:11+rcd_len*2],16)
    if (rcd_typ == 0) and (rcd_len > 0) :
      flash_addr = rcd_addr
      flash_page_used[int(rcd_addr / 128)] = True
      flash_page_used[int((rcd_addr + rcd_len - 1) / 128)] = True
      checksum = rcd_sum
      for i in range(1,9+rcd_len*2, 2) :
        byte = int(rcd[i:i+2],16)
        checksum = (checksum + byte) & 0xFF
        if i >= 9:
          flash_data[flash_addr] = byte
          flash_addr += 1
      if checksum != 0 :
        print("Hex file contains errors. upload aborted.")
        delayedExit()
        
## Apply patch for SSD1309 displays if script name contains 1309 ##
if os.path.basename(sys.argv[0]).find("1309") >= 0:
  lcdBootProgram_addr = flash_data.find(lcdBootProgram)
  if lcdBootProgram_addr >= 0:
    flash_data[lcdBootProgram_addr+2] = 0xE3;
    flash_data[lcdBootProgram_addr+3] = 0xE3;
    print("Found lcdBootProgram in hex file, upload will be patched for SSD1309 displays\n")
  else:
    print("lcdBootPgrogram not found. SSD1309 display patch NOT applied\n")

## Apply LED polarity patch for Arduino Micro if script name contains micro ##
if os.path.basename(sys.argv[0]).lower().find("micro") >= 0:
    for i in range(0,32768-4,2):
        if flash_data[i:i+2] == b'\x28\x98':   # RXLED1
            flash_data[i+1] = 0x9a
        elif flash_data[i:i+2] == b'\x28\x9a': # RXLED0
            flash_data[i+1] = 0x98
        elif flash_data[i:i+2] == b'\x5d\x98': # TXLED1
            flash_data[i+1] = 0x9a
        elif flash_data[i:i+2] == b'\x5d\x9a': # TXLED0
            flash_data[i+1] = 0x98
        elif flash_data[i:i+4] == b'\x81\xef\x85\xb9' : # Arduboy core init RXLED port
            flash_data[i] = 0x80
        elif flash_data[i:i+4] == b'\x84\xe2\x8b\xb9' : # Arduboy core init TXLED port
            flash_data[i+1] = 0xE0
            
## check  for data in catarina bootloader area ##  
for i in range (256) :
  if flash_page_used[i] :
    flash_page_count += 1
    if i >= 224 :
      caterina_overwrite = True

bootloaderStart()      
#test if bootloader can and will be overwritten by hex file
bootloader.write(b"V") #get bootloader software version
if bootloader.read(2) == b"10" : #original caterina 1.0 bootloader
  bootloader.write(b"r") #read lock bits
  if (ord(bootloader.read(1)) & 0x10 != 0) and caterina_overwrite :
    print("\nThis upload will most likely corrupt the bootloader. Upload aborted.")
    bootloaderExit()
    delayedExit()
      
## Flash ##
print("\nFlashing {} bytes. ({} flash pages)".format(flash_page_count * 128, flash_page_count))
for i in range (256) :
  if flash_page_used[i] :
    bootloader.write(bytearray([ord("A"), i >> 2, (i & 3) << 6]))
    bootloader.read(1)
    bootloader.write(b"B\x00\x80F")
    bootloader.write(flash_data[i * 128 : (i + 1) * 128])
    bootloader.read(1)
    flash_page += 1
    if flash_page % 4 == 0:
      sys.stdout.write("#")
      
## Verify ##
print("\n\nVerifying {} bytes. ({} flash pages)".format(flash_page_count * 128, flash_page_count))
for i in range (256) :
  if flash_page_used[i] :
    bootloader.write(bytearray([ord("A"), i >> 2, (i & 3) << 6]))
    bootloader.read(1)
    bootloader.write(b"g\x00\x80F")
    if bootloader.read(128) != flash_data[i * 128 : (i + 1) * 128] :
      print("\nVerify failed at address {:04X}. Upload unsuccessful.".format(i * 128))
      bootloaderExit()
      delayedExit()
    flash_page += 1
    if flash_page % 4 == 0:
      sys.stdout.write("#")
print("\n\nUpload success!!")
bootloaderExit()      
delayedExit()