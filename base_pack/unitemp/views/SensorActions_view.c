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
#include "UnitempViews.h"
#include <gui/modules/variable_item_list.h>
#include <stdio.h>

//Current view
static View* view;
//List
static VariableItemList* variable_item_list;
//Current sensor
static Sensor* current_sensor;

typedef enum carousel_info {
    CAROUSEL_VALUES, //Displaying sensor values
    CAROUSEL_INFO, //Displaying sensor information
} carousel_info;
extern carousel_info carousel_info_selector;

#define VIEW_ID UnitempViewSensorActions

/**
 * @brief Back button click handling function
 *
 * @param context Pointer to application data
 * @return ID of the view to switch to
 */
static uint32_t _exit_callback(void* context) {
    UNUSED(context);

    //Return to previous view
    return UnitempViewGeneral;
}
/**
 * @brief Middle button click handling function
 *
 * @param context Pointer to application data
 * @param index Which list item the button was clicked on
 */
static void _enter_callback(void* context, uint32_t index) {
    UNUSED(context);
    switch(index) {
    case 0:
        carousel_info_selector = CAROUSEL_INFO;
        unitemp_General_switch();
        return;
    case 1:
        unitemp_SensorEdit_switch(current_sensor);
        break;
    case 2:
        unitemp_widget_delete_switch(current_sensor);
        break;
    case 3:
        unitemp_SensorsList_switch();
        break;
    case 4:
        unitemp_Settings_switch();
        break;
    case 5:
        unitemp_widget_help_switch();
        break;
    case 6:
        unitemp_widget_about_switch();
        break;
    }
}

/**
 * @brief Creating a sensor action menu
 */
void unitemp_SensorActions_alloc(void) {
    variable_item_list = variable_item_list_alloc();
    //Reset all menu items
    variable_item_list_reset(variable_item_list);

    variable_item_list_add(variable_item_list, "Info", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Edit", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Delete", 1, NULL, NULL);

    variable_item_list_add(variable_item_list, "Add new sensor", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Settings", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "Help", 1, NULL, NULL);
    variable_item_list_add(variable_item_list, "About", 1, NULL, NULL);

    //Adding a callback for pressing the middle button
    variable_item_list_set_enter_callback(variable_item_list, _enter_callback, app);
    //Creating a View from a List
    view = variable_item_list_get_view(variable_item_list);
    //Adding a callback for pressing the "Back" button
    view_set_previous_callback(view, _exit_callback);
    //Adding a View to the Manager
    view_dispatcher_add_view(app->view_dispatcher, VIEW_ID, view);
}

void unitemp_SensorActions_switch(Sensor* sensor) {
    current_sensor = sensor;
    //Resetting the last selected item
    variable_item_list_set_selected_item(variable_item_list, 0);

    view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_ID);
}

void unitemp_SensorActions_free(void) {
    //Clearing the list of elements
    variable_item_list_free(variable_item_list);
    //Clearing a view
    view_free(view);
    //Deleting a view after processing
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_ID);
}
