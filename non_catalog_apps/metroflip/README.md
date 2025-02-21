# Metroflip
Metroflip is a multi-protocol metro card reader app for the Flipper Zero, inspired by the Metrodroid project. It enables the parsing and analysis of metro cards from transit systems around the world, providing a proof-of-concept for exploring transit card data in a portable format. 

# Author
[@luu176](https://github.com/luu176)

# Discord Community Server 

Please join the server https://discord.gg/NR5hhbAXqS if you have any questions for me.
---

![Menu-Top-Screenshot](screenshots/Menu-Top.png)

# Setup Instructions

## Using a pre-built release: Stable (Recommended) or Beta (Newer updates, less stable)
1. Download the appropriate `metroflip.fap` file from the [Releases section](https://github.com/luu176/Metroflip/releases).
2. Drag and drop the `metroflip.fap` file into the `apps` folder on your Flipper Zero's SD card.

## Manual Build Instructions
To build Metroflip manually, follow these steps:

1. **Install Git**  
   Download and install Git on your Windows computer.  
   Run the first command to download the app:  

**Either**:
Stable Release (recommended): 
```git clone https://github.com/luu176/Metroflip.git```

**OR**:
Beta (newer updates but not fully tested): 
```git clone --single-branch --branch dev https://github.com/luu176/Metroflip.git```

2. **Navigate to the Project Folder**  
Run the second command to enter the app folder:  

```cd Metroflip```

3. **Install Python**  
Download and install Python from the [official website](https://www.python.org).  

4. **Install UFBT**  
Run the third command to install UFBT:  

```pip install ufbt```

5. **Update and Build the Project**  
Run the following commands in order to build the app:  

```ufbt update```
```ufbt fap_metroflip```

6. **Connect Your Flipper Zero**  
Ensure your Flipper Zero is connected via USB and close the QFlipper application (if it’s open).  

7. **Launch the Build**  
Run the final command to launch the app on your flipper:  

```ufbt launch```

---

# Metroflip - Card Support TODO List

This is a list of metro cards and transit systems that need support or have partial support.

## ✅ Supported Cards

| **Card / Agency**  | **Country / City**                          | **Card Type**     |
|--------------------|---------------------------------------------|-------------------|
| **Bip!**           | 🇨🇱 Santiago de Chile, Chile                | Mifare Classic    |
| **Charliecard**    | 🇺🇸 Boston, MA, USA                        | Mifare Classic    |
| **Clipper**        | 🇺🇸 San Francisco, CA, USA                  | Mifare DESFire    |
| **ITSO**           | 🇬🇧 United Kingdom                          | Mifare DESFire    |
| **Metromoney**     | 🇬🇪 Tbilisi, Georgia                        | Mifare Classic    |
| **myki**           | 🇦🇺 Melbourne (and surrounds), VIC, Australia | Mifare DESFire    |
| **Navigo**         | 🇫🇷 Paris, France                           | Calypso           |
| **Opal**           | 🇦🇺 Sydney (and surrounds), NSW, Australia  | Mifare DESFire    |
| **Opus**           | 🇨🇦 Montreal, QC, Canada                    | Calypso           |
| **Rav-Kav**        | 🇮🇱 Israel                                  | Calypso           |
| **SmartRider**     | :australia: Western Australia, Australia   | Mifare Classic    |
| **Troika**         | 🇷🇺 Moscow, Russia                          | Mifare Classic    |


---

# Credits
- **App Author**: [@luu176](https://github.com/luu176)
- **Charliecard Parser**: [@zacharyweiss](https://github.com/zacharyweiss)
- **Rav-Kav Parser**: [@luu176](https://github.com/luu176)
- **Navigo Parser**: [@luu176](https://github.com/luu176), [@DocSystem](https://github.com/DocSystem)
- **Opus Parser**: [@DocSystem](https://github.com/DocSystem)
- **Metromoney Parser**: [@Leptopt1los](https://github.com/Leptopt1los)
- **Bip! Parser**: [@rbasoalto](https://github.com/rbasoalto), [@gornekich](https://github.com/gornekich)
- **Clipper Parser**: [@ke6jjj](https://github.com/ke6jjj)
- **Troika Parser**: [@gornekich](https://github.com/gornekich)
- **Myki Parser**: [@gornekich](https://github.com/gornekich)
- **Opal Parser**: [@gornekich](https://github.com/gornekich)
- **ITSO Parser**: [@gsp8181](https://github.com/gsp8181), [@hedger](https://github.com/hedger), [@gornekich](https://github.com/gornekich)
- **Info Slaves**: [@equipter](https://github.com/equipter), [TheDingo8MyBaby](https://github.com/TheDingo8MyBaby)

---

### Special Thanks
Huge thanks to [@equipter](https://github.com/equipter) for helping out the community!
