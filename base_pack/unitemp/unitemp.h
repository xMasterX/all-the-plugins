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
#ifndef UNITEMP
#define UNITEMP

/* Connecting standard libraries */

/* Flipper Zero API connection */
//File stream
#include <toolbox/stream/file_stream.h>
//Screen
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
//Notifications
#include <notification/notification.h>
#include <notification/notification_messages.h>

/* Internal libraries */
//Sensor connection interfaces
#include "Sensors.h"

/* Declaring Macro Substitutions */
//Application name
#define APP_NAME              "Unitemp"
//Application version
#define UNITEMP_APP_VER       "1.6"
//Plugin file storage path
#define APP_PATH_FOLDER       "/ext/unitemp"
//Settings file name
#define APP_FILENAME_SETTINGS "settings.cfg"
//Sensor file name
#define APP_FILENAME_SENSORS  "sensors.cfg"

//Text buffer size
#define BUFF_SIZE 32

#define UNITEMP_D

#ifdef FURI_DEBUG
#define UNITEMP_DEBUG(msg, ...) FURI_LOG_D(APP_NAME, msg, ##__VA_ARGS__)
#else
#define UNITEMP_DEBUG(msg, ...)
#endif

/* Declaration of transfers */
//Temperature units
typedef enum {
    UT_TEMP_CELSIUS,
    UT_TEMP_FAHRENHEIT,
    UT_TEMP_COUNT
} tempMeasureUnit;
//Pressure units
typedef enum {
    UT_PRESSURE_MM_HG,
    UT_PRESSURE_IN_HG,
    UT_PRESSURE_KPA,
    UT_PRESSURE_HPA,

    UT_PRESSURE_COUNT
} pressureMeasureUnit;
// Humidity units
typedef enum {
    UT_HUMIDITY_RELATIVE, // Relative humidity
    UT_HUMIDITY_DEWPOINT, // Dewpoint
    UT_HUMIDITY_COUNT // Number of humidity modes
} humidityUnit;
/* Declaration of structures */
//Plugin settings
typedef struct {
    //Endless backlight operation
    bool infinityBacklight;
    //Temperature unit
    tempMeasureUnit temp_unit;
    // Humidity units
    humidityUnit humidity_unit;
    //Pressure unit
    pressureMeasureUnit pressure_unit;
    // Do calculate and show heat index
    bool heat_index;
    //Latest OTG status
    bool lastOTGState;
} UnitempSettings;

//Basic plugin structure
typedef struct {
    //System
    bool sensors_ready; //Sensor readiness flag for polling
    bool sensors_update; //Sensor polling permissibility flag
    //Basic settings
    UnitempSettings settings;
    //Array of pointers to sensors
    Sensor** sensors;
    //Number of loaded sensors
    uint8_t sensors_count;
    //SD card
    Storage* storage; //Storage
    Stream* file_stream; //File stream

    //Screen
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;
    Widget* widget;
    Popup* popup;
    //Buffer for various text
    char* buff;
} Unitemp;

/* Declaring Function Prototypes */

/**
 * @brief Converting sensor temperature value from Celsius to Fahrenheit
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_celsiusToFahrenheit(Sensor* sensor);

/**
 * @brief Calculates the heat index in Celsius from the temperature and humidity and stores it in the sensor heat_index field
 *
 * @param sensor The sensor struct, with temperature in Celcius and humidity in percent
 */
void unitemp_calculate_heat_index(Sensor* sensor);

/**
 * @brief Calculate dewpoint in C from relative humidity
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_rhToDewpointC(Sensor* sensor);

/**
 * @brief Calculate dewpoint in F from relative humidity
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_rhToDewpointF(Sensor* sensor);

/**
 * @brief Converting pressure from pascals to mmHg.
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_pascalToMmHg(Sensor* sensor);

/**
 * @brief Converting pressure from pascals to kilopascals
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_pascalToKPa(Sensor* sensor);
/**
 * @brief Convert pressure from Pa to hPa
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_pascalToHPa(Sensor* sensor);
/**
 * @brief Converting pressure from pascals to inHg.
 * 
 * @param sensor Pointer to sensor
 */
void unitemp_pascalToInHg(Sensor* sensor);

/**
 * @brief Saving settings to SD card
 * 
 * @return True if save is successful
 */
bool unitemp_saveSettings(void);
/**
 * @brief Loading settings from SD card
 * 
 * @return True if upload is successful
 */
bool unitemp_loadSettings(void);

extern Unitemp* app;
#endif
