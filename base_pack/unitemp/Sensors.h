/*
    Unitemp - Universal temperature reader
    Copyright (C) 2022-2023  Victor Nikitchuk (https://github.com/quen0n)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef UNITEMP_SENSORS
#define UNITEMP_SENSORS
#include <furi.h>
#include <input/input.h>

//Bit masks to define return types
#define UT_TEMPERATURE 0b00000001
#define UT_HUMIDITY    0b00000010
#define UT_PRESSURE    0b00000100
#define UT_CO2         0b00001000
#define UT_CALIBRATION 0b10000000

//Sensor polling statuses
typedef enum {
    UT_DATA_TYPE_TEMP = UT_TEMPERATURE,
    UT_DATA_TYPE_TEMP_HUM = UT_TEMPERATURE | UT_HUMIDITY,
    UT_DATA_TYPE_TEMP_PRESS = UT_TEMPERATURE | UT_PRESSURE,
    UT_DATA_TYPE_TEMP_HUM_PRESS = UT_TEMPERATURE | UT_HUMIDITY | UT_PRESSURE,
    UT_DATA_TYPE_TEMP_HUM_CO2 = UT_TEMPERATURE | UT_HUMIDITY | UT_CO2,
} SensorDataType;

//Return Types
typedef enum {
    UT_SENSORSTATUS_OK, //Everything is fine, the survey is successful
    UT_SENSORSTATUS_TIMEOUT, //The sensor did not respond
    UT_SENSORSTATUS_EARLYPOOL, //Poll before the required delay
    UT_SENSORSTATUS_BADCRC, //Invalid checksum
    UT_SENSORSTATUS_ERROR, //Other errors
    UT_SENSORSTATUS_POLLING, //A transformation occurs in the sensor
    UT_SENSORSTATUS_INACTIVE, //The sensor is being edited or deleted

} UnitempStatus;

//Flipper Zero I/O port
typedef struct GPIO {
    const uint8_t num;
    const char* name;
    const GpioPin* pin;
} GPIO;

typedef struct Sensor Sensor;

/**
 * @brief Function pointer to allocate memory and prepare a sensor instance
 */
typedef bool(SensorAllocator)(Sensor* sensor, char* args);
/**
 * @brief Pointer to the sensor memory release function
 */
typedef bool(SensorFree)(Sensor* sensor);
/**
 * @brief Sensor initialization function pointer
 */
typedef bool(SensorInitializer)(Sensor* sensor);
/**
 * @brief Pointer to the sensor deinitialization function
 */
typedef bool(SensorDeinitializer)(Sensor* sensor);
/**
 * @brief Pointer to the sensor value update function
 */
typedef UnitempStatus(SensorUpdater)(Sensor* sensor);

typedef UnitempStatus(Calibrate)(Sensor*, float);

//Sensor connection types
typedef struct Interface {
    //Interface name
    const char* name;
    //Interface memory allocation function
    SensorAllocator* allocator;
    //Interface memory release function
    SensorFree* mem_releaser;
    //Sensor value update function via interface
    SensorUpdater* updater;
} Interface;

//Sensor types
typedef struct {
    //Sensor model
    const char* typename;
    //Full name with analogues
    const char* altname;
    //Return type
    SensorDataType datatype;
    //Connection interface
    const Interface* interface;
    //Sensor polling interval
    uint16_t pollingInterval;
    //Sensor memory allocation function
    SensorAllocator* allocator;
    //Sensor memory release function
    SensorFree* mem_releaser;
    //Sensor initialization function
    SensorInitializer* initializer;
    //Sensor deinitialization function
    SensorDeinitializer* deinitializer;
    //Sensor value update function
    SensorUpdater* updater;
} SensorType;

typedef struct {
    SensorType super;
    Calibrate* calibrate;
} SensorTypeWithCalibration;

//Sensor
typedef struct Sensor {
    //Sensor name
    char* name;
    //Temperature
    float temp;
    //Heat index
    float heat_index;
    //Relative humidity
    float hum;
    //Atmospheric pressure
    float pressure;
    //CO2 concentration
    float co2;
    //Sensor type
    const SensorType* type;
    //Last sensor poll status
    UnitempStatus status;
    //Time of the last sensor poll
    uint32_t lastPollingTime;
    //Temperature offset (x10)
    int8_t temp_offset;
    //Sensor instance
    void* instance;
} Sensor;

extern const Interface SINGLE_WIRE; //Proprietary single-wire protocol for DHTXX and AM23XX sensors
extern const Interface ONE_WIRE; //Dallas Single Wire Protocol
extern const Interface I2C; //I2C_2 (PC0, PC1)
extern const Interface SPI; //SPI_1 (MOSI - 2, MISO - 3, CS - 4, SCK - 5)

/* ============================= Sensor(s) ============================================ */
/**
 * @brief Memory allocation for sensor
 * 
 * @param name Sensor name
 * @param type Sensor type
 * @param args Pointer to a string with sensor parameters
 * @return Pointer to the sensor in case of successful memory allocation, NULL on error
 */
Sensor* unitemp_sensor_alloc(char* name, const SensorType* type, char* args);

/**
 * @brief Freeing up the memory of a specific sensor
 * @param sensor Pointer to sensor
 */
void unitemp_sensor_free(Sensor* sensor);

/**
 * @brief Update the data of the specified sensor
 * @param sensor Pointer to sensor
 * @return Sensor poll status
 */
UnitempStatus unitemp_sensor_updateData(Sensor* sensor);

/**
 * @brief Checking whether a sensor is in memory
 * 
 * @param sensor Pointer to sensor
 * @return True if this sensor is already loaded, false if this is a new sensor
 */
bool unitemp_sensor_isContains(Sensor* sensor);

/**
 * @brief Get sensor from list by index
 * 
 * @param index Sensor index (0 - unitemp_sensors_getCount())
 * @return Pointer to sensor on success, NULL on failure
 */
Sensor* unitemp_sensor_getActive(uint8_t index);

/**
 * @brief Loading sensors from SD card
 * @return True if the upload was successful
 */
bool unitemp_sensors_load();

/**
 * @brief Function for rebooting sensors from an SD card
*/
void unitemp_sensors_reload(void);

/**
 * @brief Saving sensors to SD card
 * @return True if save was successful
 */
bool unitemp_sensors_save(void);

/**
 * @brief Removing a sensor
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_sensor_delete(Sensor* sensor);

/**
 * @brief Initializing loaded sensors
 * @return True if everything went well
 */
bool unitemp_sensors_init(void);

/**
 * @brief Deinitialize loaded sensors
 * @return True if everything went well
 */
bool unitemp_sensors_deInit(void);

/**
 * @brief Freeing up the memory of all sensors
 */
void unitemp_sensors_free(void);

/**
 * @brief Update all sensor data
 */
void unitemp_sensors_updateValues(void);

/**
 * @brief Get number of loaded sensors
 * @return Number of sensors
 */
uint8_t unitemp_sensors_getCount(void);

/**
 * @brief Add sensor to general list
 * @param sensor Pointer to sensor
 */
void unitemp_sensors_add(Sensor* sensor);

/**
* @brief Get a list of available sensor types
* @return Pointer to a list of sensors
*/
const SensorType** unitemp_sensors_getTypes(void);

/**
* @brief Get number of available sensor types
* @return Number of available sensor types
*/
uint8_t unitemp_sensors_getTypesCount(void);

/**
 * @brief Get sensor type by its index
 * @param index Sensor type index (0 to SENSOR_TYPES_COUNT)
 * @return const SensorType* 
 */
const SensorType* unitemp_sensors_getTypeFromInt(uint8_t index);

/**
 * @brief Convert lowercase sensor name to index
 * 
 * @param str Sensor name as a string
 * @return Pointer to the sensor type on success, otherwise NULL
 */
const SensorType* unitemp_sensors_getTypeFromStr(char* str);

/**
 * @brief Get the number of active sensors
 * 
 * @return Number of active sensors
 */
uint8_t unitemp_sensors_getActiveCount(void);

/* ============================= GPIO ============================= */
/**
 * @brief Converting the port number on the FZ case to GPIO
 * @param name Port number on the FZ case
 * @return Pointer to GPIO on success, NULL on error
 */
const GPIO* unitemp_gpio_getFromInt(uint8_t name);
/**
 * @brief Converting GPIO to number on the FZ case
 * @param gpio Pointer to port
 * @return Port number on the FZ case
 */
uint8_t unitemp_gpio_toInt(const GPIO* gpio);

/**
 * @brief Locking GPIO by specified interface
 * @param gpio Pointer to port
 * @param interface Pointer to the interface on which the port will be occupied
 */
void unitemp_gpio_lock(const GPIO* gpio, const Interface* interface);

/**
 * @brief Unblocking the port
 * @param gpio Pointer to port
 */
void unitemp_gpio_unlock(const GPIO* gpio);
/**
 * @brief Get the number of available ports for the specified interface
 * @param interface Pointer to interface
 * @return Number of available ports
 */
uint8_t unitemp_gpio_getAviablePortsCount(const Interface* interface, const GPIO* extraport);
/**
 * @brief Get a pointer to the port available for the interface by index
 * @param interface Pointer to interface
 * @param index Port number (from 0 to unitemp_gpio_getAviablePortsCount())
 * @param extraport Pointer to an additional port that will be forced to be considered available. 
 * @return Pointer to an available port
 */
const GPIO*
    unitemp_gpio_getAviablePort(const Interface* interface, uint8_t index, const GPIO* extraport);

/* Sensors */
//DHTxx and their derivatives
#include "./interfaces/SingleWireSensor.h"
//DS18x2x
#include "./interfaces/OneWireSensor.h"
#include "./sensors/LM75.h"
//BMP280, BME280, BME680
#include "./sensors/BMx280.h"
#include "./sensors/BME680.h"
#include "./sensors/AM2320.h"
#include "./sensors/DHT20.h"
#include "./sensors/SHT30.h"
#include "./sensors/BMP180.h"
#include "./sensors/HTU21x.h"
#include "./sensors/HDC1080.h"
#include "./sensors/MAX31855.h"
#include "./sensors/MAX31725.h"
#include "./sensors/MAX6675.h"
#include "./sensors/SCD30.h"
#include "./sensors/SCD40.h"
#endif
