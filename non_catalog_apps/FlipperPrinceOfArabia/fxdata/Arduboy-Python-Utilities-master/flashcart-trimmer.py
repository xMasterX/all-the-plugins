print("\nArduboy Flashcart image trimmer v0.1 by JJCooley Nov 2020\n")

import sys
import os

SLOT_SIZE_HEADER_INDEX = 12

HEADER_START_BYTES = bytearray("ARDUBOY".encode())

def GetTrimmedBinaryData(fullBinaryData):
    # Use header[12] concat header[13] to int, * 2*128 to determine slot size (header (256) + title (1024) + program + data)

    currentHeaderIndex = 0
    while currentHeaderIndex < len(fullBinaryData)-1:
        if HEADER_START_BYTES != fullBinaryData[currentHeaderIndex:currentHeaderIndex+len(HEADER_START_BYTES)]:
            break

        slotByteSize = ((fullBinaryData[currentHeaderIndex+SLOT_SIZE_HEADER_INDEX] << 8) + fullBinaryData[currentHeaderIndex+SLOT_SIZE_HEADER_INDEX+1])*2*128
        currentHeaderIndex += slotByteSize

    print("Trimming {}/{}".format(currentHeaderIndex, len(fullBinaryData)))

    return fullBinaryData[0:currentHeaderIndex]

if len(sys.argv) != 2:
    print("\nUsage: {} flashcart-image.bin\n".format(os.path.basename(sys.argv[0])))
    sys.exit()

flashcartBinFile = open(sys.argv[1], 'rb')
binaryData = flashcartBinFile.read()
flashcartBinFile.close()

trimmedBinData = GetTrimmedBinaryData(binaryData)

flashcartBinFile = open(sys.argv[1], 'wb')
flashcartBinFile.write(trimmedBinData)
flashcartBinFile.close()
