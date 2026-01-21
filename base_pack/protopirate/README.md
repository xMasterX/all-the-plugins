# **ProtoPirate**

### _for Flipper Zero_

ProtoPirate is an experimental rolling-code analysis toolkit developed by members of **The Pirates' Plunder**.
The app currently supports decoding for multiple automotive key-fob families (Kia, Ford, Subaru, Suzuki, VW, and more), with the goal of being a drop-in Flipper app (.fap) that is free, open source, and can be used on any Flipper Zero firmware.

## **Supported Protocols**

Decoders:

- KIA V0
- KIA V1
- KIA V2
- KIA V3 / V4
- KIA V5
- KIA V6
- Fiat V0
- Ford V0
- Scher-Khan
- StarLine
- Subaru
- Suzuki
- Volkswagen (VW)

Encoders:

- Fiat V0
- Ford V0
- KIA V0
- KIA V1
- KIA V2
- KIA V3 / V4
- StarLine
- Subaru
- Suzuki
- More Coming Soon

## **Features**

### 📡 Protocol Receiver

Real-time signal capture and decoding with animated radar display. Supports frequency hopping.

### 📂 Sub Decode

Load and analyze existing `.sub` files from your SD card. Browse `/ext/subghz/` to decode previously captured signals.

### ⏱️ Timing Tuner

Tool for protocol developers to compare real fob signal timing against protocol definitions.

- **Protocol Definition**: Expected short/long pulse durations and tolerance
- **Received Signal**: Measured timing from real fob (avg, min, max, sample count)
- **Analysis**: Difference from expected, jitter measurements
- **Conclusion**: Whether timing matches or needs adjustment with specific recommendations

## **Credits**

The following contributors are recognized for helping us keep open sourced projects and the freeware community alive.

### **App Development**

- RocketGod
- MMX
- Skorp - Thanks, I sneaked a lot from Weather App!
- Vadim's Radio Driver

### **Protocol Magic**

- L0rdDiakon
- YougZ
- RocketGod
- DoobTheGoober
- Skorp
- Slackware
- Trikk
- Wootini
- Li0ard

### **Reverse Engineering Support**

- DoobTheGoober
- MMX
- NeedNotApply
- RocketGod
- Slackware
- Trikk
- Li0ard

## **Community & Support**

Join **The Pirates' Plunder** on Discord for development updates, testing, protocol research, community support, and a bunch of badasses doing fun shit:

➡️ **[https://discord.gg/thepirates](https://discord.gg/thepirates)**

<img width="1500" height="1000" alt="rocketgod_logo_transparent" src="https://github.com/user-attachments/assets/ad15b106-152c-4a60-a9e2-4d40dfa8f3c6" />
