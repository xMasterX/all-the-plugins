#!/usr/bin/env python3
VERSION = " v1.10"
VERSION_DATE =" Apr.2020 - Jun.2021"
print("Arduboy Uploader GUI/FX activator/flasher" + VERSION + VERSION_DATE + " by Mr.Blinky running\n")

from tkinter import filedialog
from tkinter import *
from tkinter.ttk import *
from threading import Thread
from tkinter import messagebox
import sys
import os
import time

## defaults ##
fxActivator = "activator" in os.path.basename(sys.argv[0])
fxFlasher = "flasher" in os.path.basename(sys.argv[0])
ssd1309patch = "ssd1309" in os.path.basename(sys.argv[0])
if len(sys.argv) > 1:
  if sys.argv[1].lower() == 'uploader' : fxActivator = False
  elif sys.argv[1].lower() == 'activator' : fxActivator = True
  elif sys.argv[1].lower() == 'flasher' :
    fxActivator = False
    fxFlasher = True
  for arg in sys.argv:
    if arg.lower() == 'ssd1309':
      ssd1309patch = True
path = os.path.dirname(os.path.abspath(sys.argv[0]))+os.sep
if fxActivator:
  title = "FX Activator"
  defaultAppFilename = path + "arduboy-activator.hex"
  defaultFlashFilename = path + "flash-image.bin"
elif fxFlasher:
  title = "FX Flasher"
  defaultAppFilename = ""
  defaultFlashFilename = path + "flashcart-image91820.bin"
else:
  title = "Arduboy uploader GUI"
  defaultAppFilename = path + "hex-file.hex"
  defaultFlashFilename = path + "flash-image.bin"
defaultDevDataFilename = path + "fxdata.bin"
selectAppInitialDir = path
selectFlashInitialDir = path
selectDevDataInitialDir = path
selectEEPROMinitialDir = path

try:
  from serial.tools.list_ports  import comports
  from serial import Serial
except:
  print("The PySerial module is required but not installed!")
  print("Use 'python -m pip install pyserial' from the commandline to install.")
  time.sleep(3)
  sys.exit()

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

manufacturers = {
  0x01 : "Spansion",
  0x14 : "Cypress",
  0x1C : "EON",
  0x1F : "Adesto(Atmel)",
  0x20 : "Micron",
  0x37 : "AMIC",
  0x9D : "ISSI",
  0xC2 : "General Plus",
  0xC8 : "Giga Device",
  0xBF : "Microchip",
  0xEF : "Winbond"
}

PAGESIZE = 256
BLOCKSIZE = 65536
PAGES_PER_BLOCK = BLOCKSIZE // PAGESIZE
MAX_PAGES = 65536
bootloader_active = False

def addLog(s):
    log.insert(END, s +'\n')
    log.see("end")
    #root.update()

def addLogRed(s):
    log.insert(END, s +'\n','red')
    log.see("end")
    #root.update()

def addLogGreen(s):
    log.insert(END, s +'\n','green')
    log.see("end")
    #root.update()

def addLogBlack(s):
    log.insert(END, s +'\n','black')
    log.see("end")
    #root.update()

def delayedExit():
  time.sleep(3)
  sys.exit()

def getComPort(verbose):
  global bootloader_active
  devicelist = list(comports())
  for device in devicelist:
    for vidpid in compatibledevices:
      if  vidpid in device[2]:
        port=device[0]
        bootloader_active = (compatibledevices.index(vidpid) & 1) == 0
        if verbose : addLog("Found {} at port {}".format(device[1],port))
        return port
  if verbose : addLogRed("Arduboy not found! Please Check Arduboy is switched on or try a different USB cable.")

def bootloaderStart():
  global bootloader
  ## find and connect to Arduboy in bootloader mode ##
  port = getComPort(True)
  if port is None :
    return False
  if not bootloader_active:
    addLog("Selecting bootloader mode...")
    try:
      bootloader = Serial(port,1200)
      bootloader.close()
      time.sleep(0.5)	
      #wait for disconnect and reconnect in bootloader mode
      while getComPort(False) == port :
        time.sleep(0.1)
        if bootloader_active: break        
      while getComPort(False) is None : time.sleep(0.1)
      port = getComPort(True)
    except:
      addLogRed("Error accessing port {}".format(port))
      return False
  log.insert(END, "Opening port..")
  log.see("end")
  root.update_idletasks()
  for retries in range(20):
    try:
      time.sleep(0.1)  
      bootloader = Serial(port, 57600)
      break
    except:
      if retries == 19:
        addLogRed(" Failed!")
        return False
      log.insert(END, ".")
      log.see("end")
      root.update_idletasks()
      time.sleep(0.4)
  addLog("succes")
  return True
  
def getVersion():
  bootloader.write(b"V")
  return int(bootloader.read(2))

def getJedecID():
  bootloader.write(b"j")
  jedec_id = bootloader.read(3)
  time.sleep(0.5)  
  bootloader.write(b"j")
  jedec_id2 = bootloader.read(3)
  if jedec_id2 != jedec_id or jedec_id == b'\x00\x00\x00' or jedec_id == b'\xFF\xFF\xFF':
    addLogRed("No flash cart detected.")
    return
  return bytearray(jedec_id)
  
def bootloaderExit():
  global bootloader
  bootloader.write(b"E")
  bootloader.read(1)
  bootloader.close()

def disableButtons():
  flashButton['state'] = DISABLED
  if not fxFlasher:
    hexButton['state'] = DISABLED
  if not fxFlasher and not fxActivator:
    devDataButton['state'] = DISABLED

def enableButtons():
  flashButton['state'] = NORMAL
  if not fxFlasher:
    hexButton['state'] = NORMAL
  if not fxFlasher and not fxActivator:
    devDataButton['state'] = NORMAL
    
## Uploader ####################################################################

def uploadHexfile():
  disableButtons()
  hexfile = appFilename.get()
  if not os.path.isfile(hexfile) :
    addLogRed('File not found! "{}"\n'.format(hexfile))
    enableButtons()
    return

  addLog('\nLoading "{}"\n'.format(hexfile))
  f = open(hexfile,"r")
  records = f.readlines()
  f.close()
  flash_addr = 0
  flash_data = bytearray(b'\xFF' * 32768)
  flash_page       = 1
  flash_page_count = 0
  flash_page_used  = [False] * 256
  caterina_overwrite = False
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
          addLogRed("Hex file contains errors. upload aborted.")
          enableButtons()
          return
  
  # check and apply patch for SSD1309
  if applySsd1309patch.get():
    lcdBootProgram_addr = flash_data.find(lcdBootProgram)
    if lcdBootProgram_addr >= 0:
      flash_data[lcdBootProgram_addr+2] = 0xE3;
      flash_data[lcdBootProgram_addr+3] = 0xE3;
      addLog("Found lcdBootProgram in hex file, upload will be patched for SSD1309 displays\n")
  
  ## check  for data in catarina bootloader area ##  
  for i in range (256) :
    if flash_page_used[i] :
      flash_page_count += 1
      if i >= 224 :
        caterina_overwrite = True
  progressbar['value'] = 0
  progressbar['maximum'] = 512
  if not bootloaderStart(): 
    enableButtons()
    return
  
  #test if bootloader can and will be overwritten by hex file
  bootloader.write(b"V") #get bootloader software version
  if bootloader.read(2) == b"10" : #original caterina 1.0 bootloader
    bootloader.write(b"r") #read lock bits
    if (ord(bootloader.read(1)) & 0x10 != 0) and caterina_overwrite :
      addLogRed("This upload will most likely corrupt the catarina bootloader. Upload aborted.")
      bootloaderExit()
      enableButtons()
      return
      
  ## Flash ##
  addLog("Flashing {} bytes. ({} flash pages)".format(flash_page_count * 128, flash_page_count))
  for i in range (256) :
    if flash_page_used[i] :
      bootloader.write(bytearray([ord("A"), i >> 2, (i & 3) << 6]))
      bootloader.read(1)
      bootloader.write(b"B\x00\x80F")
      bootloader.write(flash_data[i * 128 : (i + 1) * 128])
      bootloader.read(1)
      flash_page += 1
    progressbar.step()
    root.update_idletasks()
  
  ## Verify ##
  addLog("Verifying {} bytes. ({} flash pages)".format(flash_page_count * 128, flash_page_count))
  for i in range (256) :
    if flash_page_used[i] :
      bootloader.write(bytearray([ord("A"), i >> 2, (i & 3) << 6]))
      bootloader.read(1)
      bootloader.write(b"g\x00\x80F")
      if bootloader.read(128) != flash_data[i * 128 : (i + 1) * 128] :
        addLogRed("Verify failed at address {:04X}. Upload unsuccessful.\n".format(i * 128))
        bootloaderExit()
        enableButtons()
        return
      flash_page += 1
    progressbar['value'] = progressbar['value'] + 1
    root.update_idletasks()
  
  addLogGreen("\nUpload success!!")
  bootloaderExit()      
  enableButtons()
  
## flasher #####################################################################

def flashImage():
  disableButtons()
  progressbar['value'] = 0
  filename = flashFilename.get()
  ## load and pad imagedata to multiple of PAGESIZE bytes ##
  if not os.path.isfile(filename):
    addLogRed('File not found! "{}" \n'.format(filename))
    enableButtons()
    return()
    
  addLog('\nLoading flash image from file "{}"\n'.format(filename))
  f = open(filename,"rb")
  flashdata = bytearray(f.read())
  f.close
  
  if applySsd1309patch.get():
    addLog("Patching flash image for SSD1309 displays...\n")
    lcdBootProgram_addr = 0
    while lcdBootProgram_addr >= 0:
      lcdBootProgram_addr = flashdata.find(lcdBootProgram, lcdBootProgram_addr)
      if lcdBootProgram_addr >= 0:
        flashdata[lcdBootProgram_addr+2] = 0xE3;
        flashdata[lcdBootProgram_addr+3] = 0xE3;
  
  if (len(flashdata) % PAGESIZE != 0):
    flashdata += b'\xFF' * (PAGESIZE - (len(flashdata) % PAGESIZE))
  pagenumber = 0
  if not bootloaderStart(): 
    enableButtons()
    return

  #check version
  if getVersion() < 13:
    addLogRed("Bootloader does not support writing to flash. Write aborted!\nPlease update bootloader first.")
    enableButtons()
    return
  
  ## detect flash cart ##
  jedec_id = getJedecID()
  if jedec_id[0] in manufacturers.keys():
    manufacturer = manufacturers[jedec_id[0]]
  else:
    manufacturer = "unknown"
  capacity = 1 << jedec_id[2]
  addLog("\nFlash JEDEC ID    : {:02X}{:02X}{:02X}".format(jedec_id[0],jedec_id[1],jedec_id[2]))
  addLog("Flash Manufacturer: {}".format(manufacturer))
  if manufacturer != "unknown": addLog("Flash capacity    : {} KB\n".format(capacity // 1024))
    
  # when ending partially in a block, preserve the ending of old block data
  if len(flashdata) % BLOCKSIZE:
    blocklen = BLOCKSIZE - len(flashdata) % BLOCKSIZE
    blockaddr = pagenumber + len(flashdata) // PAGESIZE     
    #read partial block data end
    bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
    bootloader.read(1)
    bootloader.write(bytearray([ord("g"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
    flashdata += bootloader.read(blocklen)

  ## write to flash cart ##
  verifyAfterWrite = flashVerify.get()
  blocks = len(flashdata) // BLOCKSIZE
  log.insert(END,"writing {} blocks/{}KB to flash".format(blocks, len(flashdata) // 1024))
  if verifyAfterWrite:
    addLog(" with verify")
  else:
    addLog("")
  progressbar['maximum'] = 2 * blocks
  for block in range (blocks):
    if (block & 1 == 0) or verifyAfterWrite:
      bootloader.write(b"x\xC2") #RGB LED RED, buttons disabled
    else:  
      bootloader.write(b"x\xC0") #RGB LED OFF, buttons disabled
    bootloader.read(1)
    blockaddr = pagenumber + block * BLOCKSIZE // PAGESIZE
    blocklen = BLOCKSIZE
    #write block 
    bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
    bootloader.read(1)
    bootloader.write(bytearray([ord("B"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
    bootloader.write(flashdata[block * BLOCKSIZE : block * BLOCKSIZE + blocklen])
    bootloader.read(1)
    progressbar.step()
    root.update_idletasks()
    if verifyAfterWrite:
      bootloader.write(b"x\xC1") #RGB BLUE RED, buttons disabled
      bootloader.read(1)
      bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
      bootloader.read(1)
      bootloader.write(bytearray([ord("g"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
      if bootloader.read(blocklen) != flashdata[block * BLOCKSIZE : block * BLOCKSIZE + blocklen]:
        addLogRed(" verify failed!\n\nWrite aborted.")
        break
    progressbar['value'] = progressbar['value'] + 1
    root.update_idletasks()
  
  #write complete  
  bootloader.write(b"x\xC4")#RGB LED GREEN, buttons disabled
  bootloader.read(1)
  time.sleep(0.5)           #keep LED on for half a second
  bootloader.write(b"x\x40")#RGB LED OFF, buttons enabled
  bootloader.read(1)
  bootloader.close() # Stay in bootloader Menu
  #bootloaderExit()  # Exit bootloader menu and start sketch
  addLogGreen("\nUploaded flash image successfully!!\n")
  if not fxFlasher:
    addLog("Press LEFT or RIGHT on Arduboy to browse through the game catagogories.")
    addLog("Press UP or DOWN to select a game followed by A or B to load and play a game.")
    addLog("Press A or B on the Loader title screen to play last loaded game.")
  enableButtons()

def flashDevData():
  disableButtons()
  progressbar['value'] = 0
  filename = devDataFilename.get()
  ## load and pad imagedata to multiple of PAGESIZE bytes ##
  if not os.path.isfile(filename):
    addLogRed('File not found! "{}" \n'.format(filename))
    enableButtons()
    return()
    
  addLog('\nLoading development data from file "{}"\n'.format(filename))
  f = open(filename,"rb")
  flashdata = bytearray(f.read())
  f.close
  if (len(flashdata) % PAGESIZE != 0):
    flashdata += b'\xFF' * (PAGESIZE - (len(flashdata) % PAGESIZE))
  programpage = MAX_PAGES - (len(flashdata) // PAGESIZE)
  pagenumber = programpage
  
  if not bootloaderStart(): 
    enableButtons()
    return

  #check version
  if getVersion() < 13:
    addLogRed("Bootloader does not support writing to flash. Write aborted!\nPlease update bootloader first.")
    enableButtons()
    return
  
  ## detect flash cart ##
  jedec_id = getJedecID()
  if jedec_id[0] in manufacturers.keys():
    manufacturer = manufacturers[jedec_id[0]]
  else:
    manufacturer = "unknown"
  capacity = 1 << jedec_id[2]
  addLog("\nFlash JEDEC ID    : {:02X}{:02X}{:02X}".format(jedec_id[0],jedec_id[1],jedec_id[2]))
  addLog("Flash Manufacturer: {}".format(manufacturer))
  if manufacturer != "unknown": addLog("Flash capacity    : {} KB\n".format(capacity // 1024))
    
  # when starting partially in a block, preserve the beginning of old block data
  if pagenumber % PAGES_PER_BLOCK:
    blocklen  = pagenumber % PAGES_PER_BLOCK * PAGESIZE
    blockaddr = pagenumber // PAGES_PER_BLOCK * PAGES_PER_BLOCK
    #read partial block data start
    bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
    bootloader.read(1)
    bootloader.write(bytearray([ord("g"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
    flashdata = bootloader.read(blocklen) + flashdata
    pagenumber = blockaddr
    
  ## write to flash cart ##
  verifyAfterWrite = flashVerify.get()
  blocks = len(flashdata) // BLOCKSIZE
  log.insert(END,"writing {} blocks/{}KB to flash".format(blocks, len(flashdata) // 1024))
  if verifyAfterWrite:
    addLog(" with verify")
  else:
    addLog("")
  progressbar['maximum'] = 2 * blocks
  for block in range (blocks):
    if (block & 1 == 0) or verifyAfterWrite:
      bootloader.write(b"x\xC2") #RGB LED RED, buttons disabled
    else:  
      bootloader.write(b"x\xC0") #RGB LED OFF, buttons disabled
    bootloader.read(1)
    blockaddr = pagenumber + block * BLOCKSIZE // PAGESIZE
    blocklen = BLOCKSIZE
    #write block 
    bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
    bootloader.read(1)
    bootloader.write(bytearray([ord("B"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
    bootloader.write(flashdata[block * BLOCKSIZE : block * BLOCKSIZE + blocklen])
    bootloader.read(1)
    progressbar.step()
    root.update_idletasks()
    if verifyAfterWrite:
      bootloader.write(b"x\xC1") #RGB BLUE RED, buttons disabled
      bootloader.read(1)
      bootloader.write(bytearray([ord("A"), blockaddr >> 8, blockaddr & 0xFF]))
      bootloader.read(1)
      bootloader.write(bytearray([ord("g"), (blocklen >> 8) & 0xFF, blocklen & 0xFF,ord("C")]))
      if bootloader.read(blocklen) != flashdata[block * BLOCKSIZE : block * BLOCKSIZE + blocklen]:
        addLogRed(" verify failed!\n\nWrite aborted.")
        break
    progressbar['value'] = progressbar['value'] + 1
    root.update_idletasks()
  
  #write complete  
  bootloader.write(b"x\x40")#RGB LED OFF, buttons enabled
  bootloader.read(1)
  bootloaderExit()  # Exit bootloader menu and start sketch
  addLogGreen("\nUploaded development data successfully!!\n")
  log.insert(END,"Please use the following line in your ")
  log.insert(END,"fxdata.h",'black')
  addLog(" header file:\n")
  addLogBlack("#define PROGRAM_DATA_PAGE 0x{:04X}".format(programpage))
  enableButtons()
  
## backup EEPROM ###############################################################

def backupEEPROM():
  filename = selectEEPROMbackupFile()
  if filename == None: return
  disableButtons()
  progressbar['value'] = 0
  progressbar['maximum'] = 1024
  
  if not bootloaderStart(): 
    addLogRed("\nEEPROM backup failed.")
    enableButtons()
    return  

  addLog("\nReading 1K EEPROM data...")
  eepromdata = bytearray()
  for addr in range(0,1024,128):
    bootloader.write(bytearray([ord('A'),addr >> 8,addr & 0xFF]))
    bootloader.read(1)
    bootloader.write(b'g\x00\x80E')
    eepromdata += bytearray(bootloader.read(128))
    progressbar['value'] = progressbar['value'] + 128
    root.update_idletasks()
  bootloaderExit()
  addLog("Saving EEPROM data to '{}'".format(filename))
  try:
    f = open(filename,"wb")
    f.write(eepromdata)
    f.close
    addLogGreen("\nEEPROM backup succesfully saved.")
  except:
    addLog("Saving EEPROM backup failed.")
  enableButtons()
  
## restore EEPROM ##############################################################

def restoreEEPROM():
  filename = selectEEPROMrestoreFile()
  if filename == None: return
  addLog("\nLoading EEPROM backup file '{}'".format(filename))
  try:
    f = open(filename,"rb")
    eepromdata = bytearray(f.read())
    f.close
  except:
    addLogRed("\nFailed to load EEPROM restore file.")
    return
  if len(eepromdata) != 1024:
    addLogRed("\nEEPROM restore file has incorrect size (Must be 1024 bytes).")
    return
  disableButtons()
  progressbar['value'] = 0
  progressbar['maximum'] = 1024
  
  if not bootloaderStart(): 
    addLogRed("\nEEPROM backup failed.")
    enableButtons()
    return  

  addLog("\nRestoring EEPROM data...")
  for addr in range(0,1024,64):
    bootloader.write(bytearray([ord('A'),addr >> 8,addr & 0xFF]))
    bootloader.read(1)
    bootloader.write(b'B\x00\x40E')
    bootloader.write(eepromdata[addr:addr+64])
    bootloader.read(1)
    progressbar['value'] = progressbar['value'] + 64
    root.update_idletasks()
  bootloaderExit()
  addLogGreen("\nEEPROM restored successfully.")
  enableButtons()
  
## view EEPROM ###############################################################

def viewEEPROM():
 
  disableButtons()
  progressbar['value'] = 0
  progressbar['maximum'] = 1024
  
  if not bootloaderStart(): 
    addLogRed("\nEEPROM read failed.")
    enableButtons()
    return  

  addLog("\nReading 1K EEPROM data...")
  eepromdata = bytearray()
  for addr in range(0,1024,128):
    bootloader.write(bytearray([ord('A'),addr >> 8,addr & 0xFF]))
    bootloader.read(1)
    bootloader.write(b'g\x00\x80E')
    eepromdata += bytearray(bootloader.read(128))
    progressbar['value'] = progressbar['value'] + 128
  bootloaderExit()
  
  addLog('\nEEPROM contents:\n')
  h='----- '
  for i in range (16):
    h += '{:02X} '.format(i)
  h += '-' * 16
  addLogBlack(h)  
  for addr in range(0,1024,16):
    s = '{:04X}: '.format(addr)
    for i in range (16):
      s += '{:02X} '.format(eepromdata[addr+i])
    for i in range (16):
      if eepromdata[addr+i] < 32: s += '.'
      else: s += eepromdata[addr+i:addr+i+1].decode('cp1252')
    
    addLogBlack(s)
  addLogBlack(h)  
  enableButtons()
    
## erase EEPROM ################################################################

def eraseEEPROM():
  if messagebox.showwarning("Erase EEPROM", "Are you sure you want to erase the EEPROM?", type = "yesno") != "yes":
    return
    
  disableButtons()
  progressbar['value'] = 0
  progressbar['maximum'] = 1024
  if not bootloaderStart(): 
    addLogRed("\nEEPROM erase failed.")
    enableButtons()
    return  
  addLog("\nErasing EEPROM memory...\n")
  for addr in range(0,1024,64):
    bootloader.write(bytearray([ord("A"),addr >> 8,addr & 0xFF]))
    bootloader.read(1)
    bootloader.write(b"B\x00\x40E")
    bootloader.write(b"\xFF" * 64)
    bootloader.read(1)
    progressbar['value'] = progressbar['value'] + 64
  bootloaderExit()
  addLogGreen("EEPROM erased successfully.")
  enableButtons()
 
## GUI interface ###############################################################

## menu commands ##

def selectHexFile():
    global selectAppInitialDir
    selectHexFilename = filedialog.askopenfilename(initialdir = selectAppInitialDir, title = "Select Hex file",filetypes = (("hex files","*.hex"),("all files","*.*")))    
    if selectHexFilename != '':
      selectHexFilename = os.path.abspath(selectHexFilename)
      selectAppInitialDir = os.path.dirname(selectHexFilename)+os.sep
      appFilename.set(selectHexFilename)

def selectFlashFile():
    global selectFlashInitialDir
    selectFlashFilename = filedialog.askopenfilename(initialdir = selectFlashInitialDir, title = "Select Flash image",filetypes = (("bin files","*.bin"),("all files","*.*")))    
    if selectFlashFilename != '':
      selectFlashFilename = os.path.abspath(selectFlashFilename)
      selectFlashInitialDir = os.path.dirname(selectFlashFilename)+os.sep
      flashFilename.set(selectFlashFilename)

def selectDevDataFile():
    global selectDevDataInitialDir
    selectDevDataFilename = filedialog.askopenfilename(initialdir = selectDevDataInitialDir, title = "Select development data",filetypes = (("bin files","*.bin"),("all files","*.*")))    
    if selectDevDataFilename != '':
      selectDevDataFilename = os.path.abspath(selectDevDataFilename)
      selectDevDataInitialDir = os.path.dirname(selectDevDataFilename)+os.sep
      devDataFilename.set(selectDevDataFilename)

def selectEEPROMbackupFile():
    global selectEEPROMinitialDir
    filename = filedialog.asksaveasfilename(initialdir = selectEEPROMinitialDir, initialfile = time.strftime("eeprom-backup-%Y%m%d-%H%M%S.eep", time.localtime()), title = "Select EEPROM backup file",filetypes = (("eep files","*.eep"),("bin files","*.bin"),("all files","*.*")))    
    if filename != '':
      filename = os.path.abspath(filename)
      selectEEPROMinitialDir = os.path.dirname(filename)+os.sep
      return filename
    return None

def selectEEPROMrestoreFile():
    global selectEEPROMinitialDir
    filename = filedialog.askopenfilename(initialdir = selectEEPROMinitialDir,  title = "Select EEPROM restore file",filetypes = (("eep files","*.eep"),("bin files","*.bin"),("all files","*.*")))    
    if filename != '':
      filename = os.path.abspath(filename)
      selectEEPROMinitialDir = os.path.dirname(filename)+os.sep
      return filename
    return None
    
def clearLog():
    log.delete(1.0, END)

def uploadHexfileThread():
  Thread(target = uploadHexfile).start()
  
def flashImageThread():
  Thread(target = flashImage).start()

def devDataImageThread():
  Thread(target = flashDevData).start()

def backupEEPROMThread():
  Thread(target = backupEEPROM).start()
  
def restoreEEPROMThread():
  Thread(target = restoreEEPROM).start()

def viewEEPROMThread():
  Thread(target = viewEEPROM).start()

def eraseEEPROMThread():
  Thread(target = eraseEEPROM).start()
  
## events ##

def onResize(event):
    pass

def selectHexFileHotKey(event):
    selectHexFile()

def selectFlashFileHotKey(event):
    selectFlashFile()

def selectDevDataFileHotKey(event):
    selectDevDataFile()

def backupEEPROMHotKey(event):
    backupEEPROMThread()

def restoreEEPROMHotKey(event):
    restoreEEPROMThread()

def viewEEPROMHotKey(event):
    viewEEPROMThread()
    
def ExitAppHotKey(event):
    root.quit()
    
## create form and widgets ##

root = Tk()
root.geometry("700x400")
root.title(title + VERSION)
try:
  root.iconbitmap("icon.ico")
except:
  pass
#add progress bar at bottom (first ensure Vscrollbar rests on progress bar)
progressbar=Progressbar(root, orient='horizontal', mode='determinate')
progressbar.pack(side="bottom", expand=False, fill='both')

#add app button and selector frame
if not fxFlasher:
  appFrame = Frame(root)
  appFrame.pack(side = TOP, fill = BOTH)
  appFilename = StringVar(appFrame, value = defaultAppFilename)
  hexButton = Button(appFrame, text="Upload Hex file", width = 23, command = uploadHexfileThread)
  hexButton.pack(side = LEFT)
  hexDirButton = Button(appFrame, text="...", width = 2, command = selectHexFile).pack(side = RIGHT)
  hexEntry = Entry(appFrame, textvariable = appFilename).pack(side = LEFT, expand = True, fill = X)

#add flash button and selector frame
flashFrame = Frame(root)
flashFrame.pack(side = TOP, fill = BOTH)
flashFilename = StringVar(flashFrame, value = defaultFlashFilename)
flashButton = Button(flashFrame, text="Upload Flash image", width = 23, command = flashImageThread)
flashButton.pack(side = LEFT)
flashDirButton = Button(flashFrame, text="...", width = 2, command = selectFlashFile).pack(side = RIGHT)
flashEntry = Entry(flashFrame, textvariable = flashFilename).pack(side = LEFT, expand = True, fill = X)

#add development data button and selector frame
if not fxFlasher and not fxActivator:
  devDataFrame = Frame(root)
  devDataFrame.pack(side = TOP, fill = BOTH)
  devDataFilename = StringVar(devDataFrame, value = defaultDevDataFilename)
  devDataButton = Button(devDataFrame, text="Upload development data", width = 23, command = devDataImageThread)
  devDataButton.pack(side = LEFT)
  devDataDirButton = Button(devDataFrame, text="...", width = 2, command = selectDevDataFile).pack(side = RIGHT)
  devDataEntry = Entry(devDataFrame, textvariable = devDataFilename).pack(side = LEFT, expand = True, fill = X)

#create log text area with scrollbar
scrollbar = Scrollbar(root)
scrollbar.pack(side = RIGHT, fill = Y)
log = Text(root, wrap = NONE, yscrollcommand = scrollbar.set, font=("Courier New", 10))
log.tag_configure("red", foreground="red", font=("Courier New", 10,"bold"))
log.tag_configure("green", foreground="green",font=("Courier New", 10,"bold"))
log.tag_configure("black", foreground="black",font=("Courier New", 10,"bold"))
scrollbar.config(command = log.yview)

#Menu checkmarks
appVerify = BooleanVar()
flashVerify  = BooleanVar()
applySsd1309patch  = BooleanVar()

#create menus
mainmenu = Menu(root)
filemenu = Menu(mainmenu, tearoff=0)
if not fxFlasher:
  filemenu.add_command(label = "Select Hex file", underline = 7, accelerator = "Ctrl + H", command = selectHexFile)
filemenu.add_command(label = "Select Flash image", underline = 7, accelerator = "Ctrl + F", command = selectFlashFile)
if not fxFlasher and not fxActivator:
  filemenu.add_command(label = "Select development data", underline = 0, accelerator = "Ctrl + D", command = selectDevDataFile)
if not (fxActivator or fxFlasher):
  filemenu.add_separator()
  filemenu.add_command(label = "Backup EEPROM", underline = 0, accelerator = "Ctrl + B", command = backupEEPROMThread)
  filemenu.add_command(label = "Restore EEPROM", underline = 0, accelerator = "Ctrl + R", command = restoreEEPROMThread)
  filemenu.add_command(label = "View EEPROM", underline = 0, accelerator = "Ctrl + L", command = viewEEPROMThread)
  filemenu.add_command(label = "Erase EEPROM", underline = 0, command = eraseEEPROMThread)
  filemenu.add_separator()
filemenu.add_command(label = "Exit", underline = 1, accelerator = "Ctrl + X", command = root.quit)
optionmenu = Menu(mainmenu, tearoff = 0)
if not fxFlasher:
  optionmenu.add_checkbutton(label="Verify Hex file after upload",onvalue=True,offvalue=False,variable=appVerify)
  appVerify.set(True)
optionmenu.add_checkbutton(label="Verify flash data",onvalue=True,offvalue=False,variable=flashVerify)
#flashVerify.set(True)
optionmenu.add_checkbutton(label="Apply SSD1309 display patch",onvalue=True,offvalue=False,variable=applySsd1309patch)
applySsd1309patch.set(ssd1309patch)
optionmenu.add_command(label="Clear log",command=clearLog)
mainmenu.add_cascade(label="File", menu = filemenu)
mainmenu.add_cascade(label="Options", menu = optionmenu)
root.config(menu=mainmenu)

# default log
if fxActivator:
  addLog("\nArduboy FX activator" + VERSION + VERSION_DATE + " by Mr.Blinky.\n\nInstructions:\n-------------\n1) Connect Arduboy and turn power on.\n2) Click Upload Hex file button and wait for upload to complete.\n3) Run Flash mod chip option on Arduboy.\n4) Run Flash bootloader option on Arduboy.\n5) Click 'Upload Flash image' button and wait for upload to complete.\n6) Enjoy your Arduboy FX.\n")
elif fxFlasher:  
  addLog("\nArduboy FX flasher" + VERSION + VERSION_DATE + " by Mr.Blinky.\n\nInstructions:\n-------------\n1) Connect Arduboy and turn power on.\n2) Click 'Upload Flash image' button.\n3) Wait for upload to complete.\n4) Press LEFT then RIGHT to view ARDUBOY FX LOADER screen.\n")
else:
  addLog("\nArduboy uploader GUI" + VERSION + VERSION_DATE + " by Mr.Blinky.\n\n1) Use File menu or […] button to browse for a Hex file or Flash image.\n2) Press the appropriate upload button to upload the file.\n")
log.pack(side="top", expand=True, fill='both')
log.bind("<Configure>", onResize)

#create hot keys
root.bind_all("<Control-x>", ExitAppHotKey)
if not fxFlasher:
  root.bind_all("<Control-h>", selectHexFileHotKey)
root.bind_all("<Control-f>", selectFlashFileHotKey)
if not fxFlasher and not fxActivator:
  root.bind_all("<Control-d>", selectDevDataFileHotKey)
  root.bind_all("<Control-b>", backupEEPROMHotKey)
  root.bind_all("<Control-r>", restoreEEPROMHotKey)
  root.bind_all("<Control-l>", viewEEPROMHotKey)
#run application
root.mainloop()