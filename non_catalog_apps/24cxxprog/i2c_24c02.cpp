#include "i2c_24c02.hpp"
#include "furi_hal_i2c.h"
#include <furi.h>

EEPROM24C02::EEPROM24C02(uint8_t i2c_address_7bit)
    : _i2c_addr_8bit(i2c_address_7bit << 1) {
}

bool EEPROM24C02::init() {
    // Just check if device is responding
    return isAvailable();
}

bool EEPROM24C02::isAvailable() {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

    // Try to read a dummy byte to check if device responds
    uint8_t dummy_data;
    bool success = furi_hal_i2c_rx(
        &furi_hal_i2c_handle_external, _i2c_addr_8bit, &dummy_data, 1, EEPROM_I2C_TIMEOUT);

    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    return success;
}

bool EEPROM24C02::readByte(uint8_t memory_addr, uint8_t& data) {
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

    // Send memory address first
    bool success_tx = furi_hal_i2c_tx_ext(
        &furi_hal_i2c_handle_external,
        _i2c_addr_8bit,
        false,
        &memory_addr,
        1,
        FuriHalI2cBeginStart,
        FuriHalI2cEndAwaitRestart,
        EEPROM_I2C_TIMEOUT);

    // Read the data
    bool success_rx = false;
    if(success_tx) {
        success_rx = furi_hal_i2c_rx_ext(
            &furi_hal_i2c_handle_external,
            _i2c_addr_8bit,
            false,
            &data,
            1,
            FuriHalI2cBeginRestart,
            FuriHalI2cEndStop,
            EEPROM_I2C_TIMEOUT);
    }

    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    return success_tx && success_rx;
}

bool EEPROM24C02::writeByte(uint8_t memory_addr, uint8_t data) {
    uint8_t write_buffer[2];
    write_buffer[0] = memory_addr;
    write_buffer[1] = data;

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

    bool success = furi_hal_i2c_tx_ext(
        &furi_hal_i2c_handle_external,
        _i2c_addr_8bit,
        false,
        write_buffer,
        2,
        FuriHalI2cBeginStart,
        FuriHalI2cEndStop,
        EEPROM_I2C_TIMEOUT);

    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    if(success) {
        // Wait for write cycle to complete (typically 5ms for 24C02)
        furi_delay_ms(10);
    }

    return success;
}

bool EEPROM24C02::readBytes(uint8_t start_addr, uint8_t* buffer, uint8_t length) {
    if(length == 0 || buffer == nullptr) return false;

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

    // Send start address
    bool success_tx = furi_hal_i2c_tx_ext(
        &furi_hal_i2c_handle_external,
        _i2c_addr_8bit,
        false,
        &start_addr,
        1,
        FuriHalI2cBeginStart,
        FuriHalI2cEndAwaitRestart,
        EEPROM_I2C_TIMEOUT);

    // Sequential read
    bool success_rx = false;
    if(success_tx) {
        success_rx = furi_hal_i2c_rx_ext(
            &furi_hal_i2c_handle_external,
            _i2c_addr_8bit,
            false,
            buffer,
            length,
            FuriHalI2cBeginRestart,
            FuriHalI2cEndStop,
            EEPROM_I2C_TIMEOUT);
    }

    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    return success_tx && success_rx;
}

bool EEPROM24C02::writeBytes(uint8_t start_addr, const uint8_t* buffer, uint8_t length) {
    if(length == 0 || buffer == nullptr) return false;

    // 24C02 has 8-byte page size - we need to handle page boundaries
    uint8_t bytes_written = 0;

    while(bytes_written < length) {
        uint8_t current_addr = start_addr + bytes_written;
        uint8_t page_offset = current_addr % EEPROM_24C02_PAGE_SIZE;
        uint8_t bytes_in_page = EEPROM_24C02_PAGE_SIZE - page_offset;
        uint8_t bytes_to_write =
            (length - bytes_written < bytes_in_page) ? (length - bytes_written) : bytes_in_page;

        // Prepare write buffer for this page
        uint8_t write_buffer[EEPROM_24C02_PAGE_SIZE + 1]; // +1 for address
        write_buffer[0] = start_addr + bytes_written;

        for(uint8_t i = 0; i < bytes_to_write; i++) {
            write_buffer[i + 1] = buffer[bytes_written + i];
        }

        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

        bool success = furi_hal_i2c_tx_ext(
            &furi_hal_i2c_handle_external,
            _i2c_addr_8bit,
            false,
            write_buffer,
            bytes_to_write + 1,
            FuriHalI2cBeginStart,
            FuriHalI2cEndStop,
            EEPROM_I2C_TIMEOUT);

        furi_hal_i2c_release(&furi_hal_i2c_handle_external);

        if(!success) {
            return false;
        }

        // Wait for write cycle to complete
        furi_delay_ms(10);

        bytes_written += bytes_to_write;
    }

    return true;
}

bool EEPROM24C02::eraseAll() {
    // Fill entire memory with 0xFF
    uint8_t erase_buffer[EEPROM_24C02_PAGE_SIZE];
    for(uint8_t i = 0; i < EEPROM_24C02_PAGE_SIZE; i++) {
        erase_buffer[i] = 0xFF;
    }

    // Erase page by page
    for(uint8_t page = 0; page < EEPROM_24C02_SIZE / EEPROM_24C02_PAGE_SIZE; page++) {
        uint8_t start_addr = page * EEPROM_24C02_PAGE_SIZE;
        if(!writeBytes(start_addr, erase_buffer, EEPROM_24C02_PAGE_SIZE)) {
            return false;
        }
    }

    return true;
}

bool EEPROM24C02::eraseRange(uint8_t start_addr, uint8_t length) {
    if(length == 0) return false;

    // Check if range goes beyond memory
    uint16_t end_addr = (uint16_t)start_addr + length;
    if(end_addr > EEPROM_24C02_SIZE) {
        length = EEPROM_24C02_SIZE - start_addr;
    }

    // Fill range with 0xFF
    uint8_t erase_buffer[EEPROM_24C02_PAGE_SIZE];
    for(uint8_t i = 0; i < EEPROM_24C02_PAGE_SIZE; i++) {
        erase_buffer[i] = 0xFF;
    }

    return writeBytes(start_addr, erase_buffer, length);
}

void EEPROM24C02::setAddress(uint8_t i2c_address_7bit) {
    _i2c_addr_8bit = i2c_address_7bit << 1;
}

uint8_t EEPROM24C02::getAddress() {
    return _i2c_addr_8bit >> 1;
}
