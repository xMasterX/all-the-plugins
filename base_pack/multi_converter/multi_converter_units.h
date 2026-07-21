#pragma once

#include <input/input.h>
#include <gui/gui.h>

#include "multi_converter_definitions.h"

#define MULTI_CONVERTER_AVAILABLE_UNITS 31

#define multi_converter_get_unit(unit_type) multi_converter_available_units[unit_type]
#define multi_converter_get_unit_type_offset(unit_type, offset)                                   \
    (((unit_type + offset) % MULTI_CONVERTER_AVAILABLE_UNITS + MULTI_CONVERTER_AVAILABLE_UNITS) % \
     MULTI_CONVERTER_AVAILABLE_UNITS)
// the modulo operation will fail with extremely large values on the units array

// DEC / HEX / BIN
void multi_converter_unit_dec_hex_bin_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_dec_hex_bin_allowed(MultiConverterUnitType);

// CEL / FAR / KEL
void multi_converter_unit_temperature_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_temperature_allowed(MultiConverterUnitType);

// KM / M / CM / MILES / FEET / INCHES
void multi_converter_unit_distance_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_distance_allowed(MultiConverterUnitType);

// DEG / RAD
void multi_converter_unit_angle_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_angle_allowed(MultiConverterUnitType unit_type);

// TONNES / KG / G / MG / POUNDS / OUNCES
void multi_converter_unit_weight_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_weight_allowed(MultiConverterUnitType unit_type);

// BIT / BYTE / KB / MB / GB (decimal) + KiB / MiB / GiB (binary)
void multi_converter_unit_data_convert(MultiConverterState* const multi_converter_state);
uint8_t multi_converter_unit_data_allowed(MultiConverterUnitType unit_type);

//
// each unit is made of comma? + negative? + keyboard_length + mini_name + name + convert function + allowed function
// (setting functions as NULL will cause convert / select options to be ignored)
//
static const MultiConverterUnit multi_converter_unit_dec = {
    0,
    0,
    10,
    "DEC\0",
    "Decimal\0",
    multi_converter_unit_dec_hex_bin_convert,
    multi_converter_unit_dec_hex_bin_allowed};
static const MultiConverterUnit multi_converter_unit_hex = {
    0,
    0,
    16,
    "HEX\0",
    "Hexadecimal\0",
    multi_converter_unit_dec_hex_bin_convert,
    multi_converter_unit_dec_hex_bin_allowed};
static const MultiConverterUnit multi_converter_unit_bin = {
    0,
    0,
    2,
    "BIN\0",
    "Binary\0",
    multi_converter_unit_dec_hex_bin_convert,
    multi_converter_unit_dec_hex_bin_allowed};

static const MultiConverterUnit multi_converter_unit_cel = {
    1,
    1,
    10,
    "CEL\0",
    "Celsius\0",
    multi_converter_unit_temperature_convert,
    multi_converter_unit_temperature_allowed};
static const MultiConverterUnit multi_converter_unit_far = {
    1,
    1,
    10,
    "FAR\0",
    "Fahrenheit\0",
    multi_converter_unit_temperature_convert,
    multi_converter_unit_temperature_allowed};
static const MultiConverterUnit multi_converter_unit_kel = {
    1,
    1,
    10,
    "KEL\0",
    "Kelvin\0",
    multi_converter_unit_temperature_convert,
    multi_converter_unit_temperature_allowed};

static const MultiConverterUnit multi_converter_unit_km = {
    1,
    0,
    10,
    "KM\0",
    "Kilometers\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};
static const MultiConverterUnit multi_converter_unit_m = {
    1,
    0,
    10,
    "M\0",
    "Meters\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};
static const MultiConverterUnit multi_converter_unit_cm = {
    1,
    0,
    10,
    "CM\0",
    "Centimeters\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};
static const MultiConverterUnit multi_converter_unit_mi = {
    1,
    0,
    10,
    "MI\0",
    "Miles\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};
static const MultiConverterUnit multi_converter_unit_ft = {
    1,
    0,
    10,
    "FT\0",
    "Feet\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};
static const MultiConverterUnit multi_converter_unit_in = {
    1,
    0,
    10,
    " \"\0",
    "Inches\0",
    multi_converter_unit_distance_convert,
    multi_converter_unit_distance_allowed};

static const MultiConverterUnit multi_converter_unit_deg = {
    1,
    0,
    10,
    "DEG\0",
    "Degree\0",
    multi_converter_unit_angle_convert,
    multi_converter_unit_angle_allowed};
static const MultiConverterUnit multi_converter_unit_rad = {
    1,
    0,
    10,
    "RAD\0",
    "Radian\0",
    multi_converter_unit_angle_convert,
    multi_converter_unit_angle_allowed};

static const MultiConverterUnit multi_converter_unit_t = {
    1,
    0,
    10,
    "T\0",
    "Tonnes\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};
static const MultiConverterUnit multi_converter_unit_kg = {
    1,
    0,
    10,
    "KG\0",
    "Kilograms\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};
static const MultiConverterUnit multi_converter_unit_g = {
    1,
    0,
    10,
    "G\0",
    "Grams\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};
static const MultiConverterUnit multi_converter_unit_mg = {
    1,
    0,
    10,
    "MG\0",
    "Milligrams\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};
static const MultiConverterUnit multi_converter_unit_lb = {
    1,
    0,
    10,
    "LB\0",
    "Pounds\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};
static const MultiConverterUnit multi_converter_unit_oz = {
    1,
    0,
    10,
    "OZ\0",
    "Ounces\0",
    multi_converter_unit_weight_convert,
    multi_converter_unit_weight_allowed};

static const MultiConverterUnit multi_converter_unit_bit = {
    1,
    0,
    10,
    "b\0",
    "Bit\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_byte = {
    1,
    0,
    10,
    "B\0",
    "Byte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_kbit = {
    1,
    0,
    10,
    "kb\0",
    "Kilobit\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_kbyte = {
    1,
    0,
    10,
    "kB\0",
    "Kilobyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_mbit = {
    1,
    0,
    10,
    "Mb\0",
    "Megabit\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_mbyte = {
    1,
    0,
    10,
    "MB\0",
    "Megabyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_gbit = {
    1,
    0,
    10,
    "Gb\0",
    "Gigabit\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_gbyte = {
    1,
    0,
    10,
    "GB\0",
    "Gigabyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_kibyte = {
    1,
    0,
    10,
    "KiB\0",
    "Kibibyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_mibyte = {
    1,
    0,
    10,
    "MiB\0",
    "Mebibyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};
static const MultiConverterUnit multi_converter_unit_gibyte = {
    1,
    0,
    10,
    "GiB\0",
    "Gibibyte\0",
    multi_converter_unit_data_convert,
    multi_converter_unit_data_allowed};

// index order set by the MultiConverterUnitType enum element (multi_converter_definitions.h)
static const MultiConverterUnit multi_converter_available_units[MULTI_CONVERTER_AVAILABLE_UNITS] = {
    [UnitTypeDec] = multi_converter_unit_dec,
    [UnitTypeHex] = multi_converter_unit_hex,
    [UnitTypeBin] = multi_converter_unit_bin,

    [UnitTypeCelsius] = multi_converter_unit_cel,
    [UnitTypeFahernheit] = multi_converter_unit_far,
    [UnitTypeKelvin] = multi_converter_unit_kel,

    [UnitTypeKilometers] = multi_converter_unit_km,
    [UnitTypeMeters] = multi_converter_unit_m,
    [UnitTypeCentimeters] = multi_converter_unit_cm,
    [UnitTypeMiles] = multi_converter_unit_mi,
    [UnitTypeFeet] = multi_converter_unit_ft,
    [UnitTypeInches] = multi_converter_unit_in,

    [UnitTypeDegree] = multi_converter_unit_deg,
    [UnitTypeRadian] = multi_converter_unit_rad,

    [UnitTypeTonnes] = multi_converter_unit_t,
    [UnitTypeKilograms] = multi_converter_unit_kg,
    [UnitTypeGrams] = multi_converter_unit_g,
    [UnitTypeMilligrams] = multi_converter_unit_mg,
    [UnitTypePounds] = multi_converter_unit_lb,
    [UnitTypeOunces] = multi_converter_unit_oz,

    [UnitTypeBit] = multi_converter_unit_bit,
    [UnitTypeByte] = multi_converter_unit_byte,
    [UnitTypeKilobit] = multi_converter_unit_kbit,
    [UnitTypeKilobyte] = multi_converter_unit_kbyte,
    [UnitTypeMegabit] = multi_converter_unit_mbit,
    [UnitTypeMegabyte] = multi_converter_unit_mbyte,
    [UnitTypeGigabit] = multi_converter_unit_gbit,
    [UnitTypeGigabyte] = multi_converter_unit_gbyte,
    [UnitTypeKibibyte] = multi_converter_unit_kibyte,
    [UnitTypeMebibyte] = multi_converter_unit_mibyte,
    [UnitTypeGibibyte] = multi_converter_unit_gibyte,
};
