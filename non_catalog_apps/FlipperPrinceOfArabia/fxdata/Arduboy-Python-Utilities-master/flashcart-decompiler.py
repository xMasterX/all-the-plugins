print("\nArduboy Flashcart image decompiler v0.1 by JJCooley Nov 2020\n")

# Applying the "menu patch" when building may make original .hex different from decompiled .hex (multiple recompiles/decompiles will yield the same file after this)

# requires PILlow. Use 'python -m pip install pillow' to install

import sys
import os
import math
import shutil
try:
  from PIL import Image
except:
  print("The PILlow module is required but not installed!")
  print("Use 'python -m pip install pillow' from the commandline to install.")
  sys.exit()

# Array indices
CATEGORY_INDEX = 0
PROGRAM_INDEX = 1
TITLE_IMAGE_INDEX = 2
PROGRAM_HEXFILE_INDEX = 3
DATAFILE_INDEX = 4

# CSV IDs
ID_LIST  = 0
ID_TITLE = 1
ID_TITLESCREEN = 2
ID_HEXFILE = 3
ID_DATAFILE = 4
ID_SAVEFILE = 5

HEADER_LENGTH = 256
TITLE_IMAGE_LENGTH = 1024

SLOT_SIZE_HEADER_INDEX = 12
CATEGORY_HEADER_INDEX = 7
PROGRAM_SIZE_HEADER_INDEX = 14

HEADER_START_BYTES = bytearray("ARDUBOY".encode())

def GetBit(byte, index):
    return byte&(2**index) != 0

def IntToHex(intNum, placesToFill):
    hexString = hex(intNum).replace("0x", "").upper()
    hexString = "0"*(placesToFill-len(hexString)) + hexString
    return hexString

def DecompileTitleScreenData(byteData):
    byteLength = len(byteData)
    pixels = bytearray(8192)
    for b in range(0, byteLength):
        for i in range(0, 8):
            yPos = b//128*8+i
            xPos = b%128
            pixels[yPos * 128 + xPos] = 255 if GetBit(byteData[b], i) else 0

    img = Image.frombytes("L", (128, 64), bytes(pixels))

    return img

def DecompileHexFileData(byteData):
    byteLength = len(byteData)

    hexData = []
    for byteNum in range(0, byteLength, 16):
        hexLine = ":10" + IntToHex(byteNum, 4) + "00"
        for i in range(0, 16):
            if byteNum+i > byteLength-1:
                break
            hexLine += IntToHex(byteData[byteNum+i], 2)
        lineSum = 0
        for i in range(1, 41, 2):
            hexByte = hexLine[i] + hexLine[i+1]
            lineSum += int(hexByte, 16)

        checkSum = 256-lineSum%256
        if checkSum == 256:
            checkSum = 0
        hexLine += IntToHex(checkSum, 2)
        hexData.append(hexLine)

    if byteLength%16 > 0:
        lineSum = (byteLength%16) + (byteLength-byteLength%16)
        hexLine = ":" + IntToHex(byteLength%16, 2) + IntToHex(byteLength-byteLength%16, 4) + "00"
        for i in range(0, byteLength%16):
            lineSum += int(byteData[byteLength-byteLength%16+i])
            hexLine += IntToHex(int(byteData[byteLength-byteLength%16+i]))
        hexLine += IntToHex(256-lineSum%256, 2)
        hexData.append(hexLine)

    fullHexString = ""
    for hexLine in range(0, len(hexData)):
        fullHexString += hexData[hexLine]
        if hexLine != len(hexData)-1:
            fullHexString += "\n"

    return fullHexString


def GetProgramSections(fullBinaryData):
    # Use header[14]*128 to determine program size
    # Use header[12] concat header[13] to int, * 2*128 to determine slot size (header (256) + title (1024) + program + data)
    # Use header[7] to determine category number

    programDataArray = []

    currentHeaderIndex = 0
    previousCategory = 0
    programIndex = 0
    while currentHeaderIndex < len(fullBinaryData)-1:
        if HEADER_START_BYTES != fullBinaryData[currentHeaderIndex:currentHeaderIndex+len(HEADER_START_BYTES)]:
            break

        slotByteSize = ((fullBinaryData[currentHeaderIndex+SLOT_SIZE_HEADER_INDEX] << 8) + fullBinaryData[currentHeaderIndex+SLOT_SIZE_HEADER_INDEX+1])*2*128

        programCategory = fullBinaryData[currentHeaderIndex+CATEGORY_HEADER_INDEX]
        titleImageData = fullBinaryData[currentHeaderIndex+HEADER_LENGTH:currentHeaderIndex+HEADER_LENGTH+TITLE_IMAGE_LENGTH]
        programData = fullBinaryData[currentHeaderIndex+HEADER_LENGTH+TITLE_IMAGE_LENGTH:currentHeaderIndex+HEADER_LENGTH+TITLE_IMAGE_LENGTH+fullBinaryData[currentHeaderIndex+PROGRAM_SIZE_HEADER_INDEX]*128]
        datafileData = fullBinaryData[currentHeaderIndex+HEADER_LENGTH+TITLE_IMAGE_LENGTH+fullBinaryData[currentHeaderIndex+PROGRAM_SIZE_HEADER_INDEX]*128:currentHeaderIndex+slotByteSize]

        if previousCategory != programCategory:
            programIndex = 0

        programDataArray.append([programCategory, programIndex, titleImageData, programData, datafileData])

        currentHeaderIndex += slotByteSize

        previousCategory = programCategory
        programIndex += 1

    print("Found {} program sections".format(len(programDataArray)))

    return programDataArray

def DecompileProgramParts(programSections):
    decompiledProgramData = []

    print("-"*6 + "  " + "-"*7 + "  " + "-"*8 + "  " + "-"*5 + "  " + "-"*12)
    print("% Done  Abs Pos  Category  Index  Program Size")

    for programAbsoluteIndex in range(0, len(programSections)):
        programData = programSections[programAbsoluteIndex]

        decompiledProgramData.append([programData[CATEGORY_INDEX], programData[PROGRAM_INDEX], DecompileTitleScreenData(programData[TITLE_IMAGE_INDEX]), DecompileHexFileData(programData[PROGRAM_HEXFILE_INDEX]), programData[DATAFILE_INDEX]])

        print("{:7} {:7} {:9} {:6} {:13}".format(str(round(100*programAbsoluteIndex/(len(programSections)-1), 2))+"%", programAbsoluteIndex, programData[CATEGORY_INDEX], programData[PROGRAM_INDEX], len(programData[PROGRAM_HEXFILE_INDEX])))

    print("-"*6 + "  " + "-"*7 + "  " + "-"*8 + "  " + "-"*5 + "  " + "-"*12)
    print("% Done  Abs Pos  Category  Index  Program Size")

    return decompiledProgramData

def WriteDecompiledProgramData(decompiledProgramData):
    decompiledPath = os.path.splitext(sys.argv[1])[0]

    if (os.path.isdir(decompiledPath)):
        shutil.rmtree(decompiledPath)
    os.mkdir(decompiledPath)

    indexCSVString = "List;Discription;Title screen;Hex file;Data file;Save file"

    for programDataIndex in range(0, len(decompiledProgramData)):
        categoryIndex = str(decompiledProgramData[programDataIndex][CATEGORY_INDEX])
        programIndex = str(decompiledProgramData[programDataIndex][PROGRAM_INDEX])

        categoryDirPath = os.path.join(decompiledPath, categoryIndex)
        programDirPath = os.path.join(os.path.join(decompiledPath, categoryIndex), programIndex)
        if not os.path.isdir(categoryDirPath):
            os.mkdir(categoryDirPath)
        if not os.path.isdir(programDirPath):
            os.mkdir(programDirPath)

        titleImagePath = os.path.join(programDirPath, programIndex + ".png")
        decompiledProgramData[programDataIndex][TITLE_IMAGE_INDEX].save(titleImagePath)

        shouldIncludeHexFile = len(decompiledProgramData[programDataIndex][PROGRAM_HEXFILE_INDEX]) > 0
        if shouldIncludeHexFile:
            hexFilePath = os.path.join(programDirPath, programIndex + ".hex")
            hexFile = open(hexFilePath, 'w')
            hexFile.write(decompiledProgramData[programDataIndex][PROGRAM_HEXFILE_INDEX])
            hexFile.close()

        shouldIncludeDataFile = len(decompiledProgramData[programDataIndex][DATAFILE_INDEX]) > 0
        if shouldIncludeDataFile:
            datafilePath = os.path.join(programDirPath, programIndex + ".bin")
            datafile = open(datafilePath, 'wb')
            datafile.write(decompiledProgramData[programDataIndex][DATAFILE_INDEX])
            datafile.close()

        indexCSVString += "\n"
        indexCSVString += categoryIndex + ";"
        indexCSVString += categoryIndex + "-" + programIndex + ";"
        indexCSVString += os.path.join(os.path.join(categoryIndex, programIndex), programIndex + ".png") + ";"
        indexCSVString += (os.path.join(os.path.join(categoryIndex, programIndex), programIndex + ".hex") if shouldIncludeHexFile else "") + ";"
        indexCSVString += ";"

    indexCSVFile = open(os.path.join(decompiledPath, "flashcart-index.csv"), 'w')
    indexCSVFile.write(indexCSVString)
    indexCSVFile.close()


if len(sys.argv) != 2:
    print("\nUsage: {} flashcart-image.bin\n".format(os.path.basename(sys.argv[0])))
    sys.exit()

flashcartBinFile = open(sys.argv[1], 'rb')
flashcartBinData = flashcartBinFile.read()
flashcartBinFile.close()

programSections = GetProgramSections(flashcartBinData)
decompiledProgramData = DecompileProgramParts(programSections)
WriteDecompiledProgramData(decompiledProgramData)
