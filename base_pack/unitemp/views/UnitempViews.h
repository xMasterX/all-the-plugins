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
#ifndef UNITEMP_SCENES
#define UNITEMP_SCENES

#include "../unitemp.h"

//Types of menus
typedef enum UnitempViews {
    UnitempViewGeneral,
    UnitempViewMainMenu,
    UnitempViewSettings,
    UnitempViewSensorsList,
    UnitempViewSensorEdit,
    UnitempViewSensorNameEdit,
    UnitempViewSensorActions,
    UnitempViewWidget,
    UnitempViewPopup,

    UnitempViewsCount
} UnitempViews;

/**
 * @brief Display a popup window
 * 
 * @param icon Pointer to icon
 * @param header The header
 * @param message Message
 * @param prev_view_id ID of the view where you want to return to
 */
void unitemp_popup(const Icon* icon, char* header, char* message, uint32_t prev_view_id);

/* General view of the sensors */
void unitemp_General_alloc(void);
void unitemp_General_switch(void);
void unitemp_General_free(void);

/* Main menu */
void unitemp_MainMenu_alloc(void);
void unitemp_MainMenu_switch(void);
void unitemp_MainMenu_free(void);

/* Settings */
void unitemp_Settings_alloc(void);
void unitemp_Settings_switch(void);
void unitemp_Settings_free(void);

/* List of sensors */
void unitemp_SensorsList_alloc(void);
void unitemp_SensorsList_switch(void);
void unitemp_SensorsList_free(void);

/* Sensor editor */
void unitemp_SensorEdit_alloc(void);
//sensor - pointer to the edited sensor
void unitemp_SensorEdit_switch(Sensor* sensor);
void unitemp_SensorEdit_free(void);

/* Sensor name editor */
void unitemp_SensorNameEdit_alloc(void);
void unitemp_SensorNameEdit_switch(Sensor* sensor);
void unitemp_SensorNameEdit_free(void);

/* List of actions with the sensor */
void unitemp_SensorActions_alloc(void);
void unitemp_SensorActions_switch(Sensor* sensor);
void unitemp_SensorActions_free(void);

/* Widgets */
void unitemp_widgets_alloc(void);
void unitemp_widgets_free(void);

/* Deletion confirmation */
void unitemp_widget_delete_switch(Sensor* sensor);
/* Help */
void unitemp_widget_help_switch(void);
/* About the application */
void unitemp_widget_about_switch(void);
#endif
