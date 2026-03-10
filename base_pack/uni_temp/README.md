![Flipper usage](https://user-images.githubusercontent.com/10090793/211182642-e41919c5-3091-4125-815a-2d6a77a859f6.png)
# Unitemp - Universal temperature sensor reader
[![GitHub release](https://img.shields.io/github/release/quen0n/unitemp-flipperzero?include_prereleases=&sort=semver&color=blue)](https://github.com/quen0n/unitemp-flipperzero/releases/)
[![License](https://img.shields.io/github/license/quen0n/unitemp-flipperzero)](https://github.com/quen0n/unitemp-flipperzero/blob/dev/LICENSE.md)
[![Build dev](https://github.com/quen0n/unitemp-flipperzero/actions/workflows/build_dev.yml/badge.svg?branch=dev)](https://github.com/quen0n/unitemp-flipperzero/actions/workflows/build_dev.yml)

[Flipper Zero](https://flipper.net/products/flipper-zero) application for reading temperature, humidity and pressure sensors like a DHT11/22, DS18B20, BMP280, HTU21 and more. 

## 🚧 Unitemp 2 is under construction 🚧
Development of the Unitemp app began in 2022. Much has changed since then: the Flipper Zero firmware API has stabilized, and new features and improvements have been added. The new version of Unitemp will update the code to reflect these changes, and accumulated bugs will be fixed. It is also planned to add a detailed description of connecting sensors to the Flipper Zero. 

❗You can already try out the updated app by installing it yourself or as part of the [Unleashed Firmware](https://web.unleashedflip.com/) dev builds. You can review the changes in [CHANGELOG.md](CHANGELOG.md). Please test the app and report bugs in [Issues](https://github.com/quen0n/unitemp-flipperzero/issues/new) or on [Discord](https://discord.com/channels/740930220399525928/1056727938747351060).

## How to install latest dev version
- Go to [Actions](https://github.com/quen0n/unitemp-flipperzero/actions), select last workflow run
<img width="1814" height="503" alt="image" src="https://github.com/user-attachments/assets/cc3c7459-c2a7-41f9-bd01-40a400757cca" />

- Click to download icon
<img width="1882" height="732" alt="image" src="https://github.com/user-attachments/assets/d08e3d74-447d-426c-b7a9-46a6ee8619b0" />

- Download the archive and unzip it
- Upload unitemp.fap to the flipper in SD Card/apps/GPIO via qFlipper, [Flipper lab](https://lab.flipper.net/archive), or mobile app
<img width="862" height="532" alt="image" src="https://github.com/user-attachments/assets/7d104513-21fc-4772-b405-982385f8ecd5" />

- Enjoy!



## List of supported sensors
| Model                | Interface     | Temperature range        | Humidity range      | Extra range                |Image and link |
|:---------------------|:-------------:|:-------------------------|:--------------------|:---------------------------|:--------------:|
|AHT10                 |I2C            |-40...85°C (±0.3, 0.01)   |0...100% (±2, 0.024) |                            ||
|AHT20                 |I2C            |-40...85°C (±0.3, 0.01)   |0...100% (±2, 0.024) |                            ||
|AM2320                |Single Wire/I2C|-40...80°C (±0.5, 0.1)    |0...100% (±3, 0.1)   |                            ||                 
|BMP180                |I2C            |-40...85°C (±0.5, 0.01)   |                     |300...1100hPa (±1.0, 0.01)  ||
|BMP280                |I2C            |-40...85°C (±1.0, 0.01)   |                     |300...1100hPa (±1.0, 0.0016)||
|BME280                |I2C            |-40...85°C (±1.0, 0.01)   |0...100% (±3, 0.008) |300...1100hPa (±1.0, 0.0016)||
|BME680                |I2C            |-40...85°C (±0.5, 0.01)   |0...100% (±3, 0.008) |300...1100hPa (±0.6, 0.18)  ||
|DHT11 (AOSONG)        | Single Wire   | 0...50°C (±2, 1.0)       | 20...90% (±5, 1.0)  |                            |[<img src=".github\images\sensors\DHT11\general.png" width="150"/>](https://www.aliexpress.com/w/wholesale-DHT11-module.html) |
|DHT11 (ASAIR)         | Single Wire   | -20...60°C (±2, 0.1)     | 5...95% (±5, 1.0)   |                            |[<img src=".github\images\sensors\DHT11\general.png" width="150"/>](https://www.aliexpress.com/w/wholesale-DHT11-module.html) |
|DHT12                 | Single Wire   | -20...60°C (±0.5, 0.1)   | 20...90% (±5, 0.1)  |                            |[<img src=".github\images\sensors\DHT12\general.png" width="150"/>](https://www.aliexpress.com/w/wholesale-DHT12-module.html) |
|DHT20/AM2108          |I2C            |-40...80°C (±0.5, 0.1)    |0...100% (±3, 0.1)   |                            ||
|DHT21/AM2301          |Single Wire    |-40...80°C (±1.0, 0.1)    |0...100% (±3, 0.1)   |                            ||
|DHT22/AM2302          |Single Wire    |-40...80°C (±0.5, 0.1)    |0...100% (±2, 0.1)   |                            ||
|DS18B20               |One Wire       |-55...125°C (±0.5, 0.0625)|                     |                            ||
|DS18S20 (DS1820)      |One Wire       |-55...125°C (±0.5, 0.5)   |                     |                            ||
|DS1822                |One Wire       |-55...125°C (±2.0, 0.0625)|                     |                            ||
|HDC1080               |I2C            |-40...125°C (±0.2, 0.1)   |0...100% (±2, 0.1)   |                            ||
|HDC2080               |I2C            |-40...125°C (±0.2, 0.1)   |0...100% (±2, 0.1)   |                            ||
|HTU21D(F)             |I2C            |-40...125°C (±0.3, 0.1)   |0...100% (±2, 0.04)  |                            ||
|LM75                  |I2C            |-55...125°C (±2.0, 0.1)   |                     |                            ||
|MAX6675               |SPI            |0...1024°C (±9.0, 0.25)   |                     |                            ||
|MAX31725              |I2C            |-40...105°C (±0.5, 0.004) |                     |                            ||
|MAX31855              |SPI            |-200...1800°C (±2.0, 0.25)|                     |                            ||
|SCD30                 |I2C            |0...50°C (±0.4, 0.01)     |0...100% (±3, 0.004) |0...40000 ppm CO2 (±30, 1.0)||
|SHT20                 |I2C            |-40...125°C (±0.3, 0.01)  |0...100% (±3, 0.04)  |                            ||
|SHT21                 |I2C            |-40...125°C (±0.3, 0.01)  |0...100% (±2, 0.04)  |                            ||
|SHT25                 |I2C            |-40...125°C (±0.2, 0.01)  |0...100% (±1.8, 0.04)|                            ||
|Si7021                |I2C            |-40...125°C (±0.3, 0.01)  |0...100% (±2, 0.025) |                            ||
|SHT30/GXHT30          |I2C            |-40...125°C (±0.2, 0.01)  |0...100% (±2, 0.01)  |                            ||
|SHT31/GXHT31          |I2C            |-40...125°C (±0.2, 0.01)  |0...100% (±2, 0.01)  |                            ||
|SHT35/GXHT35          |I2C            |-40...125°C (±0.2, 0.01)  |0...100% (±1.5, 0.01)|                            ||

A comprehensive overview of the sensors can be found here (RU): https://kotyara12.ru/iot/th_sensors/
## Gratitudes
- Special thanks [xMasterX](https://github.com/xMasterX), [vladin79](https://github.com/vladin79), [divinebird](https://github.com/divinebird), [jamisonderek](https://github.com/jamisonderek), [kaklik](https://github.com/kaklik)
- [Svaarich](https://github.com/Svaarich) for the UI design 
- [Unleashed firmware](https://github.com/DarkFlippers/unleashed-firmware) community for sensors testing and feedbacks
- ...and everyone who helped with development and testing
