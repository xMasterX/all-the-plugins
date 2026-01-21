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
#include "unitemp.h"
#include "Sensors.h"
#include "./views/UnitempViews.h"
#include "math.h"

#include <furi_hal_power.h>

/* Variables */
// Application data
Unitemp* app;

void unitemp_celsiusToFahrenheit(Sensor* sensor) {
    sensor->temp = sensor->temp * (9.0 / 5.0) + 32;
    sensor->heat_index = sensor->heat_index * (9.0 / 5.0) + 32;
}

static float heat_index_consts[9] = {
    -42.379f,
    2.04901523f,
    10.14333127f,
    -0.22475541f,
    -0.00683783f,
    -0.05481717f,
    0.00122874f,
    0.00085282f,
    -0.00000199f};
void unitemp_calculate_heat_index(Sensor* sensor) {
    // temp should be in Celsius, heat index will be in Celsius
    float temp = sensor->temp * (9.0 / 5.0) + 32.0f;
    float hum = sensor->hum;
    sensor->heat_index =
        (heat_index_consts[0] + heat_index_consts[1] * temp + heat_index_consts[2] * hum +
         heat_index_consts[3] * temp * hum + heat_index_consts[4] * temp * temp +
         heat_index_consts[5] * hum * hum + heat_index_consts[6] * temp * temp * hum +
         heat_index_consts[7] * temp * hum * hum + heat_index_consts[8] * temp * temp * hum * hum -
         32.0f) *
        (5.0 / 9.0);
}

float calculateDewPoint(float temperature, float relativeHumidity);

void unitemp_rhToDewpointC(Sensor* sensor) {
    sensor->hum = calculateDewPoint(sensor->temp, sensor->hum);
}

void unitemp_rhToDewpointF(Sensor* sensor) {
    sensor->hum = calculateDewPoint(sensor->temp, sensor->hum) * (9.0 / 5.0) + 32;
}

float calculateDewPoint(float temperature, float relativeHumidity) {
    float a = 17.27f;
    float b = 237.7f;
    float tempCalc = (a * temperature) / (b + temperature) + (float)log(relativeHumidity / 100.0f);
    float dewPoint = (b * tempCalc) / (a - tempCalc);
    return dewPoint;
}

void unitemp_pascalToMmHg(Sensor* sensor) {
    sensor->pressure = sensor->pressure * 0.007500638;
}
void unitemp_pascalToKPa(Sensor* sensor) {
    sensor->pressure = sensor->pressure / 1000.0f;
}
void unitemp_pascalToHPa(Sensor* sensor) {
    sensor->pressure = sensor->pressure / 100.0f;
}
void unitemp_pascalToInHg(Sensor* sensor) {
    sensor->pressure = sensor->pressure * 0.0002953007;
}

bool unitemp_saveSettings(void) {
    // Allocate memory for the stream
    app->file_stream = file_stream_alloc(app->storage);

    // Variable for the file path
    FuriString* filepath = furi_string_alloc();
    // Compose the file path
    furi_string_printf(filepath, "%s/%s", APP_PATH_FOLDER, APP_FILENAME_SETTINGS);
    // Create the plugin folder
    storage_common_mkdir(app->storage, APP_PATH_FOLDER);
    // Open the stream
    if(!file_stream_open(
           app->file_stream, furi_string_get_cstr(filepath), FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(
            APP_NAME,
            "An error occurred while saving the settings file: %d",
            file_stream_get_error(app->file_stream));
        // Close the stream and free memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        return false;
    }

    // Save settings
    stream_write_format(
        app->file_stream, "INFINITY_BACKLIGHT %d\n", app->settings.infinityBacklight);
    stream_write_format(app->file_stream, "TEMP_UNIT %d\n", app->settings.temp_unit);
    stream_write_format(app->file_stream, "HUMIDITY_UNIT %d\n", app->settings.humidity_unit);
    stream_write_format(app->file_stream, "PRESSURE_UNIT %d\n", app->settings.pressure_unit);
    stream_write_format(app->file_stream, "HEAT_INDEX %d\n", app->settings.heat_index);

    // Close the stream and free memory
    file_stream_close(app->file_stream);
    stream_free(app->file_stream);

    FURI_LOG_I(APP_NAME, "Settings have been successfully saved");
    return true;
}

bool unitemp_loadSettings(void) {
    UNITEMP_DEBUG("Loading settings...");

    // Allocate memory for the stream
    app->file_stream = file_stream_alloc(app->storage);

    // Variable for the file path
    FuriString* filepath = furi_string_alloc();
    // Compose the file path
    furi_string_printf(filepath, "%s/%s", APP_PATH_FOLDER, APP_FILENAME_SETTINGS);

    // Open the stream to the settings file
    if(!file_stream_open(
           app->file_stream, furi_string_get_cstr(filepath), FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        // Save default settings if the file is missing
        if(file_stream_get_error(app->file_stream) == FSE_NOT_EXIST) {
            FURI_LOG_W(APP_NAME, "Missing settings file. Setting defaults and saving...");
            // Close the stream and free memory
            file_stream_close(app->file_stream);
            stream_free(app->file_stream);
            // Save the default config
            unitemp_saveSettings();
            return false;
        } else {
            FURI_LOG_E(
                APP_NAME,
                "An error occurred while loading the settings file: %d. Standard values have been applied",
                file_stream_get_error(app->file_stream));
            // Close the stream and free memory
            file_stream_close(app->file_stream);
            stream_free(app->file_stream);
            return false;
        }
    }

    // Calculate the file size
    uint8_t file_size = stream_size(app->file_stream);
    // If the file is empty then:
    if(file_size == (uint8_t)0) {
        FURI_LOG_W(APP_NAME, "Settings file is empty");
        // Close the stream and free memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        // Save the default config
        unitemp_saveSettings();
        return false;
    }
    // Allocate memory for loading the file
    uint8_t* file_buf = malloc(file_size);
    // Clear the file buffer
    memset(file_buf, 0, file_size);
    // Load the file
    if(stream_read(app->file_stream, file_buf, file_size) != file_size) {
        // Exit on read error
        FURI_LOG_E(APP_NAME, "Error reading settings file");
        // Close the stream and free memory
        file_stream_close(app->file_stream);
        stream_free(app->file_stream);
        free(file_buf);
        return false;
    }
    // Read the file line by line
    // Pointer to the start of the line
    FuriString* file = furi_string_alloc_set_str((char*)file_buf);
    // How many bytes to the end of the line
    size_t line_end = 0;

    while(line_end != ((size_t)-1) && line_end != (size_t)(file_size - 1)) {
        char buff[20] = {0};
        sscanf(((char*)(file_buf + line_end)), "%s", buff);

        if(!strcmp(buff, "INFINITY_BACKLIGHT")) {
            // Read the parameter value
            int p = 0;
            sscanf(((char*)(file_buf + line_end)), "INFINITY_BACKLIGHT %d", &p);
            app->settings.infinityBacklight = p;
        } else if(!strcmp(buff, "TEMP_UNIT")) {
            // Read the parameter value
            int p = 0;
            sscanf(((char*)(file_buf + line_end)), "\nTEMP_UNIT %d", &p);
            app->settings.temp_unit = p;
        } else if(!strcmp(buff, "HUMIDITY_UNIT")) {
            int p = 9;
            sscanf(((char*)(file_buf + line_end)), "\nHUMIDITY_UNIT %d", &p);
            app->settings.humidity_unit = p;
        } else if(!strcmp(buff, "PRESSURE_UNIT")) {
            // Read the parameter value
            int p = 0;
            sscanf(((char*)(file_buf + line_end)), "\nPRESSURE_UNIT %d", &p);
            app->settings.pressure_unit = p;
        } else if(!strcmp(buff, "HEAT_INDEX")) {
            // Read the parameter value
            int p = 0;
            sscanf(((char*)(file_buf + line_end)), "\nHEAT_INDEX %d", &p);
            app->settings.heat_index = p;
        } else {
            FURI_LOG_W(APP_NAME, "Unknown settings parameter: %s", buff);
        }

        // Calculate the end of the line
        line_end = furi_string_search_char(file, '\n', line_end + 1);
    }
    free(file_buf);
    file_stream_close(app->file_stream);
    stream_free(app->file_stream);

    FURI_LOG_I(APP_NAME, "Settings have been successfully loaded");
    return true;
}

static void view_dispatcher_tick_event_callback(void* context) {
    UNUSED(context);

    if((app->sensors_ready) && (app->sensors_update)) {
        unitemp_sensors_updateValues();
    }
}

/**
 * @brief Allocate memory for plugin variables
 * 
 * @return true If everything completed successfully
 * @return false If an error occurred during loading
 */
static bool unitemp_alloc(void) {
    // Allocate memory for application data
    app = malloc(sizeof(Unitemp));

    app->sensors_ready = false;

    // Open storage
    app->storage = furi_record_open(RECORD_STORAGE);

    // Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Set default values
    app->settings.infinityBacklight = true; // The backlight is always on
    app->settings.temp_unit = UT_TEMP_CELSIUS; // Temperature unit - degrees Celsius
    app->settings.humidity_unit = UT_HUMIDITY_RELATIVE;
    app->settings.pressure_unit = UT_PRESSURE_MM_HG; // Pressure unit - mm Hg
    app->settings.heat_index = false;

    app->gui = furi_record_open(RECORD_GUI);

    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();

    app->sensors = NULL;

    app->buff = malloc(BUFF_SIZE);

    unitemp_General_alloc();

    unitemp_MainMenu_alloc();
    unitemp_Settings_alloc();
    unitemp_SensorsList_alloc();
    unitemp_SensorEdit_alloc();
    unitemp_SensorNameEdit_alloc();
    unitemp_SensorActions_alloc();
    unitemp_widgets_alloc();

    // Popup
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, UnitempViewPopup, popup_get_view(app->popup));

    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, view_dispatcher_tick_event_callback, furi_ms_to_ticks(100));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return true;
}

/**
 * @brief Free memory after the application finishes
 */
static void unitemp_free(void) {
    popup_free(app->popup);
    // Remove the view after processing
    view_dispatcher_remove_view(app->view_dispatcher, UnitempViewPopup);
    unitemp_widgets_free();

    unitemp_SensorActions_free();
    unitemp_SensorNameEdit_free();
    unitemp_SensorEdit_free();
    unitemp_SensorsList_free();
    unitemp_Settings_free();
    unitemp_MainMenu_free();
    unitemp_General_free();

    free(app->buff);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    // Clear sensors
    // Free sensor data
    unitemp_sensors_free();
    free(app->sensors);

    // Close notifications
    furi_record_close(RECORD_NOTIFICATION);
    // Close storage
    furi_record_close(RECORD_STORAGE);
    // Delete last
    free(app);
}

/**
 * @brief Application entry point
 * 
 * @return Error code
 */
int32_t unitemp_app() {
    // Allocate memory for variables
    // Exit if an error occurred
    if(unitemp_alloc() == false) {
        // Free memory
        unitemp_free();
        // Exit
        return 0;
    }

    // Load settings from the SD card
    unitemp_loadSettings();

    // Apply settings
    if(app->settings.infinityBacklight == true) {
        // Force the backlight to stay on
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    }

    app->settings.lastOTGState = furi_hal_power_is_otg_enabled();

    // Load sensors from the SD card
    unitemp_sensors_load();

    // Initialize sensors
    unitemp_sensors_init();

    unitemp_General_switch();

    view_dispatcher_run(app->view_dispatcher);

    // Deinitialize sensors
    unitemp_sensors_deInit();

    // Automatic backlight control
    if(app->settings.infinityBacklight == true)
        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);

    // Free memory
    unitemp_free();

    // Exit
    return 0;
}
