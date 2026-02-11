print("\nArduboy image viewer v1.0 by Mr.Blinky Jul.2019\n")

#requires pyserial and PILlow to be installed.
#Use "python -m pip install pyserial" and "python -m pip install pillow" on commandline

import sys
import time
import os
try: 
  from serial.tools.list_ports  import comports
  from serial import Serial
except:
  print("The pySerial module is required but not installed!")
  print("Use 'python -m pip install pyserial' from the commandline to install.")
  sys.exit()
try: 
  from PIL import Image
except:
  print("The PILlow module is not installed.")
  print("type 'python -m pip install pillow' on commandline to install")
  sys.exit()
  
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

def delayedExit():
  time.sleep(2)
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

  sys.stdout.write("Opening port ...")
  sys.stdout.flush()
  for retries in range(20):
    try:
      time.sleep(0.1)  
      bootloader = Serial(port,57600)
      break
    except:
      if retries == 19:
        print(" Failed!")
        delayedExit()
      sys.stdout.write(".")
      sys.stdout.flush()
      time.sleep(0.4)
  print("")
  
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
    print("No flash cart detected.")
    delayedExit()
  return bytearray(jedec_id)

def ledControl(b):                           #Bit 7 set: OLED display off (v1.1) Disable bootloader menu buttons (v1.3+)
  bootloader.write(bytearray([ord('x'), b])) #Bit 6 set: RGB Breathing function off (v1.1 to v1.3 only)
  bootloader.read(1)                         #Bit 5 set: RxTx status function off
                                             #Bit 4 set: Rx LED on
                                             #Bit 3 set: Tx LED on
                                             #Bit 2 set: RGB LED green on
                                             #Bit 1 set: RGB LED red on
                                             #Bit 0 set: RGB LED blue on

def readButtons():
  bootloader.write(b'v')
  buttons = ((ord(bootloader.read(1)) - ord('1') << 2)) | ((ord(bootloader.read(1)) - ord('A') << 4))
  return buttons

def display(image):
  bootloader.write(b'A\x00\x00')
  bootloader.read(1)
  bootloader.write(b'B\x04\x00D' + image[0:1024]) #display supports 1K blocks
  bootloader.read(1)
    
def bootloaderExit():
  bootloader.write(b"E")
  bootloader.read(1)

################################################################################

def usage():
  print("\nUSAGE:\n\n{} imagefile".format(os.path.basename(sys.argv[0])))
  print()
  print("Displays a 128x64 pixel image on Arduboy's display in bootloader mode until a ")
  print("button is pressed (Cathy3K bootloader required).")
  delayedExit()

################################################################################
  
if len(sys.argv) < 2 : usage()
#load and convert image
img = Image.open(sys.argv[1]).convert("1")
width, height  = img.size
if (width != 128) or (height != 64) :
  print("Error: Title screen '{}' is not 128 x 64 pixels.".format(filename))
  DelayedExit()
pixels = list(img.getdata())
bytes = bytearray(int((height // 8) * width))
i = 0
b = 0
for y in range (0,height,8):
  for x in range (0,width):
    for p in range (0,8):
      b = b >> 1  
      if pixels[(y + p) * width + x] > 0:
        b |= 0x80
    bytes[i] = b
    i += 1
    
#select bootloader mode
bootloaderStart()

if getVersion() < 13:
  print("Arduboy requires Cathy3K Bootloader v1.3 or later")
  bootloaderExit()
  DelayedExit()
ledControl(0x80) #Disable menu buttons
#display image
print("\nDisplaying image on Arduboy. Press any button on Arduboy to end.")
while not readButtons():
  display(bytes)
while readButtons():  
  pass
ledControl(0) #Enable menu buttons
bootloaderExit()