# SubGHz Signal Generator by RocketGod 📡🔬

This **SubGHz Signal Generator** for the Flipper Zero, made by **RocketGod**, is a comprehensive RF testing tool for generating various signal types across multiple radio frequencies and modulation schemes. Below is an in-depth look at each mode, from its technical details to practical applications in RF testing and development.

## 🎥 Internal CC1101 Demonstration

https://github.com/user-attachments/assets/4a34bc73-d419-480e-bb87-90216eb8a1e0

## 🎥 External CC1101 Demonstration

https://github.com/user-attachments/assets/1053ec27-a15f-4313-9257-2360135c5e96

## 🎥 Modulation Modes

https://github.com/user-attachments/assets/77970e50-d46f-4d59-bbb0-6e2624a98127

## 🧪 Signal Analysis Examples

### 🧪 Reference Signal: Car Fob in Controlled Lab Test (-28dBm)

![car_fob](https://github.com/user-attachments/assets/35cdb9c7-fcbe-4fdf-a10e-9e020a504f6f)

### 🧪 Community .sub files in Controlled Lab Test (-8dBm narrow)

![signal_sub_files](https://github.com/user-attachments/assets/a2ad93ae-4e08-4af8-97cc-5ec85f9759a4)

### 🧪 Signal Generator App and Internal CC1101/Antenna in Controlled Lab Test (-8dBm wide)

![rocketgod_signal_gen_app](https://github.com/user-attachments/assets/6ed6bb9b-2379-491c-9a69-845695de2542)

### 🧪 Signal Generator App and External CC1101/Antenna Flux Capacitor by Rabbit Labs (10dBm) [TinySA Ultra hard wired w/25W attenuator]

![External_Flux-Capacitor_Rabbit-Labs](https://github.com/user-attachments/assets/63e7a4d8-1584-425b-8113-9495a6041836)

https://github.com/user-attachments/assets/53a3123e-b749-4af6-b542-e1784b131084

## 📡 External CC1101 Notes

- **To use an external CC1101, attach it to the GPIO before starting the app.**
- Tested with [Rabbit Labs - Flux Capacitor amplified external CC1101](https://rabbit-labs.com/product/rabbit-labs-flux-capacitor-amplified-cc1101/)

## 📡 Frequency Control

The app supports multiple frequency bands within the sub-GHz spectrum:

- **Band 1**: 300 MHz – 348 MHz
- **Band 2**: 387 MHz – 464 MHz
- **Band 3**: 779 MHz – 928 MHz

You can adjust frequencies with **precision**:

- **Left/Right** arrows move between digits to adjust.
- **Up/Down** arrows increase or decrease the selected digit.

The app will automatically correct the frequency if it's outside the valid range for the selected band.

---

## ⚙️ Signal Generation Modes

Each mode generates distinct modulation schemes and data patterns for testing various RF systems and protocols.

### 🦾 **OOK 650 kHz** (On-Off Keying):

- **Pattern**: A continuous stream of `0xFF` (all bits set to `1`).
- **Technical Details**: OOK modulation where carrier presence/absence represents binary data.
- **Applications**: Testing OOK receivers, analyzing sensitivity to continuous carrier signals, evaluating AGC behavior in simple RF systems.

### ⚡ **2FSK 2.38 kHz** (Frequency Shift Keying):

- **Pattern**: Alternates between `0xAA` (`10101010`) and `0x55` (`01010101`).
- **Technical Details**: Narrow deviation FSK with 2.38 kHz frequency separation.
- **Applications**: Testing narrowband FSK receivers, evaluating demodulator performance, analyzing channel selectivity.

### 🔥 **2FSK 47.6 kHz**:

- **Pattern**: Alternates between `0xAA` and `0x55`.
- **Technical Details**: Wide deviation FSK with 47.6 kHz frequency separation.
- **Applications**: Testing wideband FSK systems, evaluating adjacent channel rejection, analyzing capture effect in FM receivers.

### 💥 **MSK 99.97 Kb/s** (Minimum Shift Keying):

- **Pattern**: Random data stream.
- **Technical Details**: Continuous phase modulation with minimal frequency deviation.
- **Applications**: Testing high-speed digital links, evaluating BER performance, analyzing spectral efficiency of communication systems.

### 📶 **GFSK 9.99 Kb/s** (Gaussian Frequency Shift Keying):

- **Pattern**: Random data stream.
- **Technical Details**: FSK with Gaussian pulse shaping for reduced spectral occupancy.
- **Applications**: Testing Bluetooth/BLE systems, evaluating low-power RF links, analyzing modulation quality.

### 🚀 **Pattern 0xFF**:

- **Pattern**: Continuous `0xFF` bytes.
- **Technical Details**: Unmodulated carrier or continuous high state.
- **Applications**: Testing receiver saturation points, evaluating RF front-end linearity, measuring blocking performance.

### 🎶 **Sine Wave**:

- **Pattern**: Pure sinusoidal waveform.
- **Technical Details**: Continuous wave (CW) signal generation.
- **Applications**: Calibrating RF equipment, testing filter responses, evaluating receiver sensitivity.

### 🟥 **Square Wave**:

- **Pattern**: Alternating high/low states.
- **Technical Details**: Digital pulse train with sharp transitions.
- **Applications**: Testing digital demodulators, analyzing rise/fall time effects, evaluating pulse detection systems.

### 📈 **Sawtooth Wave**:

- **Pattern**: Linear ramp followed by sharp drop.
- **Technical Details**: Asymmetric waveform with gradual frequency changes.
- **Applications**: Testing PLL lock range, evaluating frequency tracking systems, analyzing sweep responses.

### 🎲 **White Noise**:

- **Pattern**: Random amplitude values across spectrum.
- **Technical Details**: Uniform spectral density noise generation.
- **Applications**: SNR testing, evaluating noise figure, analyzing system noise immunity.

### 🔺 **Triangle Wave**:

- **Pattern**: Symmetric linear transitions.
- **Technical Details**: Continuous phase modulation with linear frequency changes.
- **Applications**: Testing FM demodulators, evaluating linearity, analyzing harmonic content.

### 📡 **Chirp Signal**:

- **Pattern**: Frequency sweep within burst.
- **Technical Details**: Linear frequency modulation over time.
- **Applications**: Testing radar systems, evaluating frequency agility, analyzing dispersive channels.

### 🎲 **Gaussian Noise**:

- **Pattern**: Gaussian-distributed random values.
- **Technical Details**: Noise with bell-curve amplitude distribution.
- **Applications**: Realistic noise floor simulation, BER testing, evaluating error correction performance.

### 💥 **Burst Mode**:

- **Pattern**: Periodic high-intensity bursts.
- **Technical Details**: Pulsed transmission with controlled duty cycle.
- **Applications**: Testing packet reception, evaluating AGC response time, analyzing burst synchronization.

---

## 🎛️ Controls

- **Up/Down Buttons**: Modify the currently selected digit in the frequency.
- **Left/Right Buttons**: Move between digits to adjust frequency values.
- **OK Button**: Switch signal generation modes in real-time.
- **Back Button**: Stop signal generation and exit the app.

---

## 🔬 Use Cases

1. **RF System Testing**: Generate test signals to evaluate receiver performance
2. **Protocol Development**: Test new modulation schemes and data patterns
3. **Education**: Learn about different RF modulation techniques
4. **Equipment Calibration**: Use CW signals for calibrating RF test equipment
5. **Interference Analysis**: Study how different signal types affect RF systems
6. **Research**: Explore sub-GHz spectrum characteristics

---

## ⚠️ Legal Notice

This tool is designed for legitimate RF testing, research, and educational purposes. Users must:

- Ensure compliance with local RF regulations
- Use only in controlled environments or with proper authorization
- Respect spectrum allocation and avoid interference with licensed services
- Understand that improper use may violate telecommunications laws

---

**Note**: This is a signal generator for testing and development purposes. It should only be used by qualified individuals in appropriate settings with proper authorization.

<img width="1500" height="1000" alt="rocketgod_logo_transparent" src="https://github.com/user-attachments/assets/86c2c409-4fbe-4b77-a671-c7b487144e4c" />

