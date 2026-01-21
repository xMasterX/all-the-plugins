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
#include "Sensors.h"
#include <furi_hal_power.h>

//I/O ports that were not identified in the general list
const GpioPin SWC_10 = {.pin = LL_GPIO_PIN_14, .port = GPIOA};
const GpioPin SIO_12 = {.pin = LL_GPIO_PIN_13, .port = GPIOA};
const GpioPin TX_13 = {.pin = LL_GPIO_PIN_6, .port = GPIOB};
const GpioPin RX_14 = {.pin = LL_GPIO_PIN_7, .port = GPIOB};
const GpioPin ibutton_gpio = {.pin = LL_GPIO_PIN_14, .port = GPIOB};

//Number of available I/O ports
#define GPIO_ITEMS             (sizeof(GPIOList) / sizeof(GPIO))
//Number of interfaces
#define INTERFACES_TYPES_COUNT (int)(sizeof(interfaces) / sizeof(const Interface*))
//Number of sensor types
#define SENSOR_TYPES_COUNT     (int)(sizeof(sensorTypes) / sizeof(const SensorType*))

//List of available I/O ports
static const GPIO GPIOList[] = {
    {2, "2 (A7)", &gpio_ext_pa7},
    {3, "3 (A6)", &gpio_ext_pa6},
    {4, "4 (A4)", &gpio_ext_pa4},
    {5, "5 (B3)", &gpio_ext_pb3},
    {6, "6 (B2)", &gpio_ext_pb2},
    {7, "7 (C3)", &gpio_ext_pc3},
    {10, " 10(SWC) ", &SWC_10},
    {12, "12 (SIO)", &SIO_12},
    {13, "13 (TX)", &TX_13},
    {14, "14 (RX)", &RX_14},
    {15, "15 (C1)", &gpio_ext_pc1},
    {16, "16 (C0)", &gpio_ext_pc0},
    {17, "17 (1W)", &ibutton_gpio}};

//List of interfaces that are attached to GPIO (defined by index)
//NULL - port is free, pointer to interface - port is occupied by this interface
static const Interface* gpio_interfaces_list[GPIO_ITEMS] = {0};

const Interface SINGLE_WIRE = {
    .name = "Single wire",
    .allocator = unitemp_singlewire_alloc,
    .mem_releaser = unitemp_singlewire_free,
    .updater = unitemp_singlewire_update};
const Interface I2C = {
    .name = "I2C",
    .allocator = unitemp_I2C_sensor_alloc,
    .mem_releaser = unitemp_I2C_sensor_free,
    .updater = unitemp_I2C_sensor_update};
const Interface ONE_WIRE = {
    .name = "One wire",
    .allocator = unitemp_onewire_sensor_alloc,
    .mem_releaser = unitemp_onewire_sensor_free,
    .updater = unitemp_onewire_sensor_update};
const Interface SPI = {
    .name = "SPI",
    .allocator = unitemp_spi_sensor_alloc,
    .mem_releaser = unitemp_spi_sensor_free,
    .updater = unitemp_spi_sensor_update};

//List of connection interfaces
//static const Interface* interfaces[] = {&SINGLE_WIRE, &I2C, &ONE_WIRE, &SPI};
//List of sensors
static const SensorType* sensorTypes[] = {&DHT11,  &DHT12_SW,   &DHT20,      &DHT21,    &DHT22,
                                          &Dallas, &AM2320_SW,  &AM2320_I2C, &HTU21x,   &AHT10,
                                          &SHT30,  &GXHT30,     &LM75,       &HDC1080,  &BMP180,
                                          &BMP280, &BME280,     &BME680,     &MAX31855, &MAX6675,
                                          &SCD30,  &SCD40.super};

const SensorType* unitemp_sensors_getTypeFromInt(uint8_t index) {
    if(index > SENSOR_TYPES_COUNT) return NULL;
    return sensorTypes[index];
}

const SensorType* unitemp_sensors_getTypeFromStr(char* str) {
    UNUSED(str);
    if(str == NULL) return NULL;
    for(uint8_t i = 0; i < unitemp_sensors_getTypesCount(); i++) {
        if(!strcmp(str, sensorTypes[i]->typename)) {
            return sensorTypes[i];
        }
    }
    return NULL;
}

uint8_t unitemp_sensors_getTypesCount(void) {
    return SENSOR_TYPES_COUNT;
}
const SensorType** unitemp_sensors_getTypes(void) {
    return sensorTypes;
}

int unitemp_getIntFromType(const SensorType* type) {
    for(int i = 0; i < SENSOR_TYPES_COUNT; i++) {
        if(!strcmp(type->typename, sensorTypes[i]->typename)) {
            return i;
        }
    }
    return 255;
}
const GPIO* unitemp_gpio_getFromInt(uint8_t name) {
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(GPIOList[i].num == name) {
            return &GPIOList[i];
        }
    }
    return NULL;
}

const GPIO* unitemp_gpio_getFromIndex(uint8_t index) {
    return &GPIOList[index];
}

uint8_t unitemp_gpio_toInt(const GPIO* gpio) {
    if(gpio == NULL) return 255;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(GPIOList[i].pin->pin == gpio->pin->pin && GPIOList[i].pin->port == gpio->pin->port) {
            return GPIOList[i].num;
        }
    }
    return 255;
}

uint8_t unitemp_gpio_to_index(const GpioPin* gpio) {
    if(gpio == NULL) return 255;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(GPIOList[i].pin->pin == gpio->pin && GPIOList[i].pin->port == gpio->port) {
            return i;
        }
    }
    return 255;
}

uint8_t unitemp_gpio_getAviablePortsCount(const Interface* interface, const GPIO* extraport) {
    uint8_t aviable_ports_count = 0;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        //Check for one wire
        if(interface == &ONE_WIRE) {
            if(((gpio_interfaces_list[i] == NULL || gpio_interfaces_list[i] == &ONE_WIRE) &&
                (i != 12)) || //For some reason it doesn't work on port 17
               (unitemp_gpio_getFromIndex(i) == extraport)) {
                aviable_ports_count++;
            }
        }

        //Check for single wire
        if(interface == &SINGLE_WIRE || interface == &SPI) {
            if(gpio_interfaces_list[i] == NULL || (unitemp_gpio_getFromIndex(i) == extraport)) {
                aviable_ports_count++;
            }
        }

        if(interface == &I2C) {
            //I2C has two fixed ports
            return 0;
        }
    }
    return aviable_ports_count;
}

void unitemp_gpio_lock(const GPIO* gpio, const Interface* interface) {
    uint8_t i = unitemp_gpio_to_index(gpio->pin);
    if(i == 255) return;
    gpio_interfaces_list[i] = interface;
}

void unitemp_gpio_unlock(const GPIO* gpio) {
    uint8_t i = unitemp_gpio_to_index(gpio->pin);
    if(i == 255) return;
    gpio_interfaces_list[i] = NULL;
}

const GPIO*
    unitemp_gpio_getAviablePort(const Interface* interface, uint8_t index, const GPIO* extraport) {
    //Check for I2C
    if(interface == &I2C) {
        if((gpio_interfaces_list[10] == NULL || gpio_interfaces_list[10] == &I2C) &&
           (gpio_interfaces_list[11] == NULL || gpio_interfaces_list[11] == &I2C)) {
            //Return of truth
            return unitemp_gpio_getFromIndex(0);
        } else {
            //Return of lies
            return NULL;
        }
    }
    if(interface == &SPI) {
        if(!((gpio_interfaces_list[0] == NULL || gpio_interfaces_list[0] == &SPI) &&
             (gpio_interfaces_list[1] == NULL || gpio_interfaces_list[1] == &SPI) &&
             (gpio_interfaces_list[3] == NULL || gpio_interfaces_list[3] == &SPI))) {
            return NULL;
        }
    }

    uint8_t aviable_index = 0;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        //Check for one wire
        if(interface == &ONE_WIRE) {
            //For some reason it doesn't work on port 17
            if(((gpio_interfaces_list[i] == NULL || gpio_interfaces_list[i] == &ONE_WIRE) &&
                (i != 12)) || //For some reason it doesn't work on port 17
               (unitemp_gpio_getFromIndex(i) == extraport)) {
                if(aviable_index == index) {
                    return unitemp_gpio_getFromIndex(i);
                } else {
                    aviable_index++;
                }
            }
        }
        //Check for single wire
        if(interface == &SINGLE_WIRE || interface == &SPI) {
            if(gpio_interfaces_list[i] == NULL || unitemp_gpio_getFromIndex(i) == extraport) {
                if(aviable_index == index) {
                    return unitemp_gpio_getFromIndex(i);
                } else {
                    aviable_index++;
                }
            }
        }
    }

    return NULL;
}

void unitemp_sensor_delete(Sensor* sensor) {
    for(uint8_t i = 0; i < app->sensors_count; i++) {
        if(app->sensors[i] == sensor) {
            app->sensors[i]->status = UT_SENSORSTATUS_INACTIVE;
            unitemp_sensors_save();
            unitemp_sensors_reload();
            return;
        }
    }
}

Sensor* unitemp_sensor_getActive(uint8_t index) {
    uint8_t aviable_index = 0;
    for(uint8_t i = 0; i < app->sensors_count; i++) {
        if(app->sensors[i]->status != UT_SENSORSTATUS_INACTIVE) {
            if(aviable_index == index) {
                return app->sensors[i];
            } else {
                aviable_index++;
            }
        }
    }
    return NULL;
}

uint8_t unitemp_sensors_getCount(void) {
    if(app->sensors == NULL) return 0;
    return app->sensors_count;
}

uint8_t unitemp_sensors_getActiveCount(void) {
    if(app->sensors == NULL) return 0;
    uint8_t counter = 0;
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        if(app->sensors[i]->status != UT_SENSORSTATUS_INACTIVE) counter++;
    }
    return counter;
}

void unitemp_sensors_add(Sensor* sensor) {
    app->sensors =
        (Sensor**)realloc(app->sensors, (unitemp_sensors_getCount() + 1) * sizeof(Sensor*));
    app->sensors[unitemp_sensors_getCount()] = sensor;
    app->sensors_count++;
}

bool unitemp_sensors_load(void) {
    UNITEMP_DEBUG("Loading sensors...");

    //Allocation of memory per thread
    app->file_stream = file_stream_alloc(app->storage);

    //File path variable
    FuriString* filepath = furi_string_alloc();
    //Compiling the path to the file
    furi_string_printf(filepath, "%s/%s", APP_PATH_FOLDER, APP_FILENAME_SENSORS);

    //Opening a stream to a file with sensors
    if(!file_stream_open(
           app->file_stream, furi_string_get_cstr(filepath), FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        if(file_stream_get_error(app->file_stream) == FSE_NOT_EXIST) {
            FURI_LOG_W(APP_NAME, "Missing sensors file");
            //Closing a stream and freeing memory
            file_stream_close(app->file_stream);
            stream_free(app->file_stream);
            return false;
        } else {
            FURI_LOG_E(
                APP_NAME,
                "An error occurred while loading the sensors file: %d",
                file_stream_get_error(app->file_stream));
            //Closing a stream and freeing memory
            file_stream_close(app->file_stream);
            stream_free(app->file_stream);
            return false;
        }
    }

    //Calculating File Size
    uint16_t file_size = stream_size(app->file_stream);
    //If the file is empty, then:
    if(file_size == (uint8_t)0) {
        FURI_LOG_W(APP_NAME, "Sensors file is empty");
        //Closing a stream and freeing memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        return false;
    }
    //Allocation of memory for file download
    uint8_t* file_buf = malloc(file_size);
    //File buffer underrun
    memset(file_buf, 0, file_size);
    //Downloading the file
    if(stream_read(app->file_stream, file_buf, file_size) != file_size) {
        //Exit on read error
        FURI_LOG_E(APP_NAME, "Error reading sensors file");
        //Closing a stream and freeing memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        free(file_buf);
        return false;
    }

    //Pointer to the beginning of the line
    FuriString* file = furi_string_alloc_set_str((char*)file_buf);
    //How many bytes to the end of the line
    size_t line_end = 0;

    while(line_end != ((size_t)-1) && line_end != (size_t)(file_size - 1)) {
        //Sensor name
        char name[11] = {0};
        //Sensor type
        char type[11] = {0};
        //Temperature offset
        int temp_offset = 0;
        //Line offset to separate arguments
        int offset = 0;
        //Reading from a string
        sscanf(((char*)(file_buf + line_end)), "%s %s %d %n", name, type, &temp_offset, &offset);
        //Name length limit
        name[10] = '\0';

        //Replacement ?
        for(uint8_t i = 0; i < 10; i++) {
            if(name[i] == '?') name[i] = ' ';
        }

        char* args = ((char*)(file_buf + line_end + offset));
        const SensorType* stype = unitemp_sensors_getTypeFromStr(type);

        //Checking the sensor type
        if(stype != NULL && sizeof(name) > 0 && sizeof(name) <= 11) {
            Sensor* sensor =
                unitemp_sensor_alloc(name, unitemp_sensors_getTypeFromStr(type), args);
            if(sensor != NULL) {
                sensor->temp_offset = temp_offset;
                unitemp_sensors_add(sensor);
            } else {
                FURI_LOG_E(APP_NAME, "Failed sensor (%s:%s) mem allocation", name, type);
            }
        } else {
            FURI_LOG_E(APP_NAME, "Unsupported sensor name (%s) or sensor type (%s)", name, type);
        }
        //End of line calculation
        line_end = furi_string_search_char(file, '\n', line_end + 1);
    }

    free(file_buf);
    file_stream_close(app->file_stream);
    stream_free(app->file_stream);

    FURI_LOG_I(APP_NAME, "Sensors have been successfully loaded");
    return true;
}

bool unitemp_sensors_save(void) {
    UNITEMP_DEBUG("Saving sensors...");

    //Allocation of memory for a thread
    app->file_stream = file_stream_alloc(app->storage);

    //File path variable
    FuriString* filepath = furi_string_alloc();
    //Compiling the path to the file
    furi_string_printf(filepath, "%s/%s", APP_PATH_FOLDER, APP_FILENAME_SENSORS);
    //Creating a plugin folder
    storage_common_mkdir(app->storage, APP_PATH_FOLDER);
    //Opening a stream
    if(!file_stream_open(
           app->file_stream, furi_string_get_cstr(filepath), FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(
            APP_NAME,
            "An error occurred while saving the sensors file: %d",
            file_stream_get_error(app->file_stream));
        //Closing a stream and freeing memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        return false;
    }

    //Saving sensors
    for(uint8_t i = 0; i < unitemp_sensors_getActiveCount(); i++) {
        Sensor* sensor = unitemp_sensor_getActive(i);
        //Replacing a space with ?
        for(uint8_t i = 0; i < 10; i++) {
            if(sensor->name[i] == ' ') sensor->name[i] = '?';
        }

        stream_write_format(
            app->file_stream,
            "%s %s %d ",
            sensor->name,
            sensor->type->typename,
            sensor->temp_offset);

        if(sensor->type->interface == &SINGLE_WIRE) {
            stream_write_format(
                app->file_stream, "%d\n", unitemp_singlewire_sensorGetGPIO(sensor)->num);
        }
        if(sensor->type->interface == &SPI) {
            uint8_t gpio_num = ((SPISensor*)sensor->instance)->CS_pin->num;
            stream_write_format(app->file_stream, "%d\n", gpio_num);
        }

        if(sensor->type->interface == &I2C) {
            stream_write_format(
                app->file_stream, "%X\n", ((I2CSensor*)sensor->instance)->currentI2CAdr);
        }
        if(sensor->type->interface == &ONE_WIRE) {
            stream_write_format(
                app->file_stream,
                "%d %02X%02X%02X%02X%02X%02X%02X%02X\n",
                ((OneWireSensor*)sensor->instance)->bus->gpio->num,
                ((OneWireSensor*)sensor->instance)->deviceID[0],
                ((OneWireSensor*)sensor->instance)->deviceID[1],
                ((OneWireSensor*)sensor->instance)->deviceID[2],
                ((OneWireSensor*)sensor->instance)->deviceID[3],
                ((OneWireSensor*)sensor->instance)->deviceID[4],
                ((OneWireSensor*)sensor->instance)->deviceID[5],
                ((OneWireSensor*)sensor->instance)->deviceID[6],
                ((OneWireSensor*)sensor->instance)->deviceID[7]);
        }
    }

    //Closing a stream and freeing memory
    file_stream_close(app->file_stream);
    stream_free(app->file_stream);

    FURI_LOG_I(APP_NAME, "Sensors have been successfully saved");
    return true;
}
void unitemp_sensors_reload(void) {
    unitemp_sensors_deInit();
    unitemp_sensors_free();

    unitemp_sensors_load();
    unitemp_sensors_init();
}

bool unitemp_sensor_isContains(Sensor* sensor) {
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        if(app->sensors[i] == sensor) return true;
    }
    return false;
}

Sensor* unitemp_sensor_alloc(char* name, const SensorType* type, char* args) {
    if(name == NULL || type == NULL) return NULL;
    bool status = false;
    //Allocation of memory for the sensor
    Sensor* sensor = malloc(sizeof(Sensor));
    if(sensor == NULL) {
        FURI_LOG_E(APP_NAME, "Sensor %s allocation error", name);
        return NULL;
    }

    //Allocating memory for a name
    sensor->name = malloc(11);
    if(sensor->name == NULL) {
        FURI_LOG_E(APP_NAME, "Sensor %s name allocation error", name);
        return NULL;
    }
    //Recording the sensor name
    strcpy(sensor->name, name);
    //Sensor type
    sensor->type = type;
    //Status sensor by default - error
    sensor->status = UT_SENSORSTATUS_ERROR;
    //Time of last poll
    sensor->lastPollingTime =
        furi_get_tick() - 10000; //so that the first survey occurs as early as possible

    sensor->temp = -128.0f;
    sensor->hum = -128.0f;
    sensor->pressure = -128.0f;
    sensor->temp_offset = 0;
    //Memory allocation for a sensor instance depending on its interface
    status = sensor->type->interface->allocator(sensor, args);

    //Exit if the sensor is successfully deployed
    if(status) {
        UNITEMP_DEBUG("Sensor %s allocated", name);
        return sensor;
    }
    //Exit with clearing if memory for the sensor has not been allocated
    free(sensor->name);
    free(sensor);
    FURI_LOG_E(APP_NAME, "Sensor %s(%s) allocation error", name, type->typename);
    return NULL;
}

void unitemp_sensor_free(Sensor* sensor) {
    if(sensor == NULL) {
        FURI_LOG_E(APP_NAME, "Null pointer sensor releasing");
        return;
    }
    if(sensor->type == NULL) {
        FURI_LOG_E(APP_NAME, "Sensor type is null");
        return;
    }
    if(sensor->type->mem_releaser == NULL) {
        FURI_LOG_E(APP_NAME, "Sensor releaser is null");
        return;
    }
    bool status = false;
    //Freeing up memory for an instance
    status = sensor->type->interface->mem_releaser(sensor);

    if(status) {
        UNITEMP_DEBUG("Sensor %s memory successfully released", sensor->name);
    } else {
        FURI_LOG_E(APP_NAME, "Sensor %s memory is not released", sensor->name);
    }
    free(sensor->name);
}

void unitemp_sensors_free(void) {
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        unitemp_sensor_free(app->sensors[i]);
    }
    app->sensors_count = 0;
}

bool unitemp_sensors_init(void) {
    bool result = true;

    app->sensors_ready = false;

    //Searching through sensors from the list
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        //Turning on 5V if there is none on port 1 FZ
        //May disappear when USB is disconnected
        if(furi_hal_power_is_otg_enabled() != true) {
            furi_hal_power_enable_otg();
            UNITEMP_DEBUG("OTG enabled");
        }
        if(!(*app->sensors[i]->type->initializer)(app->sensors[i])) {
            FURI_LOG_E(
                APP_NAME,
                "An error occurred during sensor initialization %s",
                app->sensors[i]->name);
            result = false;
        }
        FURI_LOG_I(APP_NAME, "Sensor %s successfully initialized", app->sensors[i]->name);
    }

    app->sensors_ready = true;

    return result;
}

bool unitemp_sensors_deInit(void) {
    bool result = true;

    //Turning off 5 V if it was not turned on before
    if(app->settings.lastOTGState != true) {
        furi_hal_power_disable_otg();
        UNITEMP_DEBUG("OTG disabled");
    }

    //Searching through sensors from the list
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        if(!(*app->sensors[i]->type->deinitializer)(app->sensors[i])) {
            FURI_LOG_E(
                APP_NAME,
                "An error occurred during sensor deinitialization %s",
                app->sensors[i]->name);
            result = false;
        }
    }

    return result;
}

UnitempStatus unitemp_sensor_updateData(Sensor* sensor) {
    if(sensor == NULL) {
        return UT_SENSORSTATUS_ERROR;
    }

    //Checking the validity of the sensor polling
    if(furi_get_tick() - sensor->lastPollingTime < sensor->type->pollingInterval) {
        //Return an error if the last sensor poll was unsuccessful
        if(sensor->status == UT_SENSORSTATUS_TIMEOUT) {
            return UT_SENSORSTATUS_TIMEOUT;
        }
        return UT_SENSORSTATUS_EARLYPOOL;
    }

    sensor->lastPollingTime = furi_get_tick();

    if(!furi_hal_power_is_otg_enabled()) {
        furi_hal_power_enable_otg();
    }

    sensor->status = sensor->type->interface->updater(sensor);

    if(sensor->status != UT_SENSORSTATUS_OK && sensor->status != UT_SENSORSTATUS_POLLING) {
        UNITEMP_DEBUG("Sensor %s update status %d", sensor->name, sensor->status);
    }

    if(app->settings.humidity_unit == UT_HUMIDITY_DEWPOINT &&
       app->settings.temp_unit == UT_TEMP_CELSIUS && sensor->status == UT_SENSORSTATUS_OK) {
        unitemp_rhToDewpointC(sensor);
    }

    if(app->settings.humidity_unit == UT_HUMIDITY_DEWPOINT &&
       app->settings.temp_unit == UT_TEMP_FAHRENHEIT && sensor->status == UT_SENSORSTATUS_OK) {
        unitemp_rhToDewpointF(sensor);
    }

    if(sensor->status == UT_SENSORSTATUS_OK) {
        if(app->settings.heat_index &&
           ((sensor->type->datatype & (UT_TEMPERATURE | UT_HUMIDITY)) ==
            (UT_TEMPERATURE | UT_HUMIDITY))) {
            unitemp_calculate_heat_index(sensor);
        }
        if(app->settings.temp_unit == UT_TEMP_FAHRENHEIT) {
            unitemp_celsiusToFahrenheit(sensor);
        }
        sensor->temp += sensor->temp_offset / 10.f;
        if(app->settings.pressure_unit == UT_PRESSURE_MM_HG) {
            unitemp_pascalToMmHg(sensor);
        } else if(app->settings.pressure_unit == UT_PRESSURE_IN_HG) {
            unitemp_pascalToInHg(sensor);
        } else if(app->settings.pressure_unit == UT_PRESSURE_KPA) {
            unitemp_pascalToKPa(sensor);
        } else if(app->settings.pressure_unit == UT_PRESSURE_HPA) {
            unitemp_pascalToHPa(sensor);
        }
    }

    return sensor->status;
}

void unitemp_sensors_updateValues(void) {
    for(uint8_t i = 0; i < unitemp_sensors_getCount(); i++) {
        unitemp_sensor_updateData(unitemp_sensor_getActive(i));
    }
}
