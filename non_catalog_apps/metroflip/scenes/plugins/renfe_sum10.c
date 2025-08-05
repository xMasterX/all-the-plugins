#include <flipper_application.h>
#include "../../metroflip_i.h"

#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include "../../api/metroflip/metroflip_api.h"

#include <dolphin/dolphin.h>
#include <bit_lib.h>
#include <furi_hal.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>
#include <storage/storage.h>
#include <ctype.h>
#include "../../api/metroflip/metroflip_api.h"
#include "../../metroflip_plugins.h"

#define TAG "Metroflip:Scene:RenfeSum10"
typedef struct {
    uint8_t block_data[16];  
    uint32_t timestamp;     
    int block_number;       
} HistoryEntry;

// Station name cache structure
#define MAX_STATION_NAME_LENGTH 28  // Reduced from 32 for memory efficiency
#define MAX_CACHED_STATIONS 150      // Reduced from 200 for memory efficiency
typedef struct {
    uint16_t code;
    char name[MAX_STATION_NAME_LENGTH];
} StationEntry;

typedef struct {
    StationEntry stations[MAX_CACHED_STATIONS];
    size_t count;
    bool loaded;
    char current_region[32];
} StationCache;

// Global station cache
static StationCache station_cache = {0};

// Forward declarations for helper functions
static bool renfe_sum10_is_history_entry(const uint8_t* block_data);
static void renfe_sum10_parse_history_entry(FuriString* parsed_data, const uint8_t* block_data, int entry_num);
static void renfe_sum10_parse_travel_history(FuriString* parsed_data, const MfClassicData* data);
static uint32_t renfe_sum10_extract_timestamp(const uint8_t* block_data);
static void renfe_sum10_sort_history_entries(HistoryEntry* entries, int count);
static bool renfe_sum10_has_history_data(const MfClassicData* data);
static bool renfe_sum10_load_station_file(const char* region);
static const char* renfe_sum10_get_station_name_from_cache(uint16_t station_code);
static void renfe_sum10_clear_station_cache(void);
static const char* renfe_sum10_get_station_name_dynamic(uint16_t station_code, const char* region);
static const char* renfe_sum10_map_region_to_file(const char* region_name);
static bool renfe_sum10_is_travel_history_block(const uint8_t* block_data, int block_number);
static bool renfe_sum10_is_default_station_code(uint16_t station_code);
static bool renfe_sum10_is_valid_timestamp(const uint8_t* block_data);
// Keys for RENFE Suma 10 cards - specific keys found in real dumps
const MfClassicKeyPair renfe_sum10_keys[16] = {
    {.a = 0xA8844B0BCA06, .b = 0xffffffffffff}, // Sector 0 - RENFE specific key
    {.a = 0xCB5ED0E57B08, .b = 0xffffffffffff}, // Sector 1 - RENFE specific key 
    {.a = 0x749934CC8ED3, .b = 0xffffffffffff}, // Sector 2
    {.a = 0xAE381EA0811B, .b = 0xffffffffffff}, // Sector 3
    {.a = 0x40454EE64229, .b = 0xffffffffffff}, // Sector 4 - Contains block 18 (history)
    {.a = 0x66A4932816D3, .b = 0xffffffffffff}, // Sector 5 - Contains block 22 (history)
    {.a = 0xB54D99618ADC, .b = 0xffffffffffff}, // Sector 6
    {.a = 0x08D6A7765640, .b = 0xffffffffffff}, // Sector 7 - Contains blocks 28,29,30 (history)
    {.a = 0x3E0557273982, .b = 0xffffffffffff}, // Sector 8
    {.a = 0xC0C1C2C3C4C5, .b = 0xffffffffffff}, // Sector 9
    {.a = 0xC0C1C2C3C4C5, .b = 0xffffffffffff}, // Sector 10
    {.a = 0xC0C1C2C3C4C5, .b = 0xffffffffffff}, // Sector 11 - Contains blocks 44,45,46 (history)
    {.a = 0xffffffffffff, .b = 0xffffffffffff}, // Sector 12
    {.a = 0xffffffffffff, .b = 0xffffffffffff}, // Sector 13
    {.a = 0xffffffffffff, .b = 0xffffffffffff}, // Sector 14
    {.a = 0xffffffffffff, .b = 0xffffffffffff}, // Sector 15
};

// Alternative common keys for RENFE Suma 10
const MfClassicKeyPair renfe_sum10_alt_keys[16] = {
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 0
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 1
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 2
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 3
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 4
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 5 - Value blocks
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 6 - Value blocks
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 7
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 8
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 9
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 10
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 11
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 12
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 13
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 14
    {.a = 0xa0a1a2a3a4a5, .b = 0xa0a1a2a3a4a5}, // Sector 15
};

// Check if a station code is problematic (appears in empty cards)
static bool renfe_sum10_is_default_station_code(uint16_t station_code) {
    // Codes that appear in new/empty cards - these should be filtered out
    return (station_code == 0x2000 || // La Fonteta (appears in empty cards)
            station_code == 0x0000 || // Null code
            station_code == 0xFFFF);  // Fill pattern
}

// Check if timestamp indicates a real transaction (not initialization data)
static bool renfe_sum10_is_valid_timestamp(const uint8_t* block_data) {
    if(!block_data) return false;
    
    // Check if timestamp is all zeros (initialization pattern)
    if(block_data[2] == 0x00 && block_data[3] == 0x00 && block_data[4] == 0x00) {
        return false;
    }
    
    // Check if timestamp is all 0xFF (empty pattern)
    if(block_data[2] == 0xFF && block_data[3] == 0xFF && block_data[4] == 0xFF) {
        return false;
    }
    
    return true;
}

// Check if a block contains a valid history entry with stricter validation
static bool renfe_sum10_is_history_entry(const uint8_t* block_data) {
    // Check for null pointer
    if(!block_data) {
        return false;
    }
    
    // Extract station code for validation
    uint16_t station_code = (block_data[9] << 8) | block_data[10];
    
    // Filter out problematic station codes that appear in empty cards
    if(renfe_sum10_is_default_station_code(station_code)) {
        return false;
    }
    
    // Validate timestamp - must not be initialization data
    if(!renfe_sum10_is_valid_timestamp(block_data)) {
        return false;
    }
    
    // Primary pattern: second byte is 0x98 and first byte is not 0x00
    if(block_data[1] == 0x98 && block_data[0] != 0x00) {
        return true;
    }
    
    // Stricter validation: Only accept CONFIRMED transaction types from real cards
    uint8_t transaction_type = block_data[0];
    bool is_confirmed_transaction = (transaction_type == 0x13 || // ENTRY - confirmed
                                   transaction_type == 0x1A || // EXIT - confirmed  
                                   transaction_type == 0x1E || // TRANSFER - confirmed
                                   transaction_type == 0x16 || // VALIDATION - confirmed
                                   transaction_type == 0x33 || // TOP-UP - confirmed
                                   transaction_type == 0x3A);  // CHARGE - confirmed
    
    // Only accept if it's a confirmed transaction type AND has valid data
    if(is_confirmed_transaction && 
       (block_data[1] != 0xFF && block_data[2] != 0xFF) &&
       station_code != 0x0000) {
        return true;
    }
    
    return false;
}
// Extract timestamp from block data for sorting purposes
static uint32_t renfe_sum10_extract_timestamp(const uint8_t* block_data) {
    if(!block_data) return 0;
    
    // Extract timestamp from bytes 2-4 and combine into a single value for comparison
    uint32_t timestamp = ((uint32_t)block_data[2] << 16) | 
                        ((uint32_t)block_data[3] << 8) | 
                        (uint32_t)block_data[4];
    
    return timestamp;
}
// Sort history entries manually (newest first) - using bubble sort since qsort is not available
static void renfe_sum10_sort_history_entries(HistoryEntry* entries, int count) {
    if(!entries || count <= 1) return;
    
    for(int i = 0; i < count - 1; i++) {
        for(int j = 0; j < count - 1 - i; j++) {
            bool should_swap = false;
            
            if(entries[j].timestamp < entries[j + 1].timestamp) {
                should_swap = true;
            } else if(entries[j].timestamp == entries[j + 1].timestamp) {
                if(entries[j].block_number < entries[j + 1].block_number) {
                    should_swap = true;
                }
            }
            
            if(should_swap) {
                // Swap entries
                HistoryEntry temp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = temp;
            }
        }
    }
}

// Clear the station cache
static void renfe_sum10_clear_station_cache(void) {
    station_cache.count = 0;
    station_cache.loaded = false;
    memset(station_cache.current_region, 0, sizeof(station_cache.current_region));
    memset(station_cache.stations, 0, sizeof(station_cache.stations));
}

// Load station names from file for a specific region
static bool renfe_sum10_load_station_file(const char* region) {
    if(!region) {
        return false;
    }
    
    // Check if we already have this region loaded
    if(station_cache.loaded && strcmp(station_cache.current_region, region) == 0) {
        return true;
    }
    
    // Clear existing cache
    renfe_sum10_clear_station_cache();
    
    // Build file path
    FuriString* file_path = furi_string_alloc();
    furi_string_printf(file_path, "/ext/apps_assets/metroflip/renfe/stations/%s.txt", region);
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    
    bool success = false;
    if(storage_file_open(file, furi_string_get_cstr(file_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line_buffer[128];
        station_cache.count = 0;
        
        // Read file line by line using simple character reading
        while(station_cache.count < MAX_CACHED_STATIONS) {
            size_t line_pos = 0;
            bool end_of_file = false;
            
            // Read line character by character
            while(line_pos < sizeof(line_buffer) - 1) {
                char c;
                size_t bytes_read = storage_file_read(file, &c, 1);
                if(bytes_read == 0) {
                    // End of file
                    end_of_file = true;
                    break;
                }
                
                if(c == '\n') {
                    // End of line
                    break;
                }
                
                if(c != '\r') { // Skip carriage returns
                    line_buffer[line_pos++] = c;
                }
            }
            
            if(end_of_file && line_pos == 0) {
                // End of file with no more data
                break;
            }
            
            line_buffer[line_pos] = '\0';
            
            // Skip comments and empty lines
            if(line_buffer[0] == '#' || line_buffer[0] == '\0' || strlen(line_buffer) < 3) {
                if(end_of_file) break;
                continue;
            }
            
            // Parse format: 0xCODE,Station Name
            char* comma = strchr(line_buffer, ',');
            if(comma && station_cache.count < MAX_CACHED_STATIONS) {
                *comma = '\0';
                char* code_str = line_buffer;
                char* name_str = comma + 1;
                
                // Parse hex code
                uint16_t code = 0;
                if(sscanf(code_str, "0x%hX", &code) == 1) {
                    // Store in cache
                    station_cache.stations[station_cache.count].code = code;
                    strncpy(station_cache.stations[station_cache.count].name, name_str, MAX_STATION_NAME_LENGTH - 1);
                    station_cache.stations[station_cache.count].name[MAX_STATION_NAME_LENGTH - 1] = '\0';
                    station_cache.count++;
                }
            }
            
            if(end_of_file) break;
        }
        
        if(station_cache.count > 0) {
            strncpy(station_cache.current_region, region, sizeof(station_cache.current_region) - 1);
            station_cache.current_region[sizeof(station_cache.current_region) - 1] = '\0';
            station_cache.loaded = true;
            success = true;
        }
        
        storage_file_close(file);
    }
    
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(file_path);
    
    return success;
}

// Get station name from cache
static const char* renfe_sum10_get_station_name_from_cache(uint16_t station_code) {
    if(!station_cache.loaded) {
        return "Unknown";
    }
    
    for(size_t i = 0; i < station_cache.count; i++) {
        if(station_cache.stations[i].code == station_code) {
            return station_cache.stations[i].name;
        }
    }
    
    return "Unknown";
}

// Map region names to station file names
static const char* renfe_sum10_map_region_to_file(const char* region_name) {
    if(!region_name) {
        return "valencia"; // Default fallback
    }
    
    // Convert region name to lowercase for comparison
    char region_lower[32];
    strncpy(region_lower, region_name, sizeof(region_lower) - 1);
    region_lower[sizeof(region_lower) - 1] = '\0';
    
    for(int i = 0; region_lower[i]; i++) {
        region_lower[i] = tolower(region_lower[i]);
    }
    
    // Map Spanish region names to file names
    if(strcmp(region_lower, "valencia") == 0 || strcmp(region_lower, "comunidad valenciana") == 0) {
        return "valencia";
    } else if(strcmp(region_lower, "madrid") == 0 || strcmp(region_lower, "comunidad de madrid") == 0) {
        return "madrid";
    } else if(strcmp(region_lower, "cataluña") == 0 || strcmp(region_lower, "catalunya") == 0 || strcmp(region_lower, "catalonia") == 0) {
        return "cataluna";
    } else if(strcmp(region_lower, "andalucía") == 0 || strcmp(region_lower, "andalucia") == 0) {
        return "andalucia";
    } else if(strcmp(region_lower, "país vasco") == 0 || strcmp(region_lower, "pais vasco") == 0 || strcmp(region_lower, "euskadi") == 0) {
        return "pais_vasco";
    } else if(strcmp(region_lower, "galicia") == 0) {
        return "galicia";
    } else if(strcmp(region_lower, "asturias") == 0 || strcmp(region_lower, "principado de asturias") == 0) {
        return "asturias";
    } else if(strcmp(region_lower, "cantabria") == 0) {
        return "cantabria";
    } else if(strcmp(region_lower, "castilla y león") == 0 || strcmp(region_lower, "castilla y leon") == 0) {
        return "castilla_leon";
    } else if(strcmp(region_lower, "castilla-la mancha") == 0 || strcmp(region_lower, "castilla la mancha") == 0) {
        return "castilla_mancha";
    } else if(strcmp(region_lower, "extremadura") == 0) {
        return "extremadura";
    } else if(strcmp(region_lower, "aragon") == 0 || strcmp(region_lower, "aragon") == 0) {
        return "aragon";
    } else if(strcmp(region_lower, "murcia") == 0 || strcmp(region_lower, "región de murcia") == 0) {
        return "murcia";
    } else if(strcmp(region_lower, "navarra") == 0 || strcmp(region_lower, "comunidad foral de navarra") == 0) {
        return "navarra";
    } else if(strcmp(region_lower, "la rioja") == 0) {
        return "la_rioja";
    } else if(strcmp(region_lower, "islas baleares") == 0 || strcmp(region_lower, "baleares") == 0) {
        return "baleares";
    } else if(strcmp(region_lower, "islas canarias") == 0 || strcmp(region_lower, "canarias") == 0) {
        return "canarias";
    }
    
    // Default fallback to Valencia for unknown regions
    return "valencia";
}

// Dynamic station name lookup with region-based file loading
static const char* renfe_sum10_get_station_name_dynamic(uint16_t station_code, const char* region) {
    // Handle special case
    if(station_code == 0x0000) {
        return "";
    }
    
    // Get primary file for the region
    const char* primary_file = renfe_sum10_map_region_to_file(region);
    
    // Try primary file first
    if(renfe_sum10_load_station_file(primary_file)) {
        const char* station_name = renfe_sum10_get_station_name_from_cache(station_code);
        if(strcmp(station_name, "Unknown") != 0) {
            return station_name;
        }
    }
    
    // For Valencia, try additional files as fallback
    if(strcmp(region, "Valencia") == 0 || strcmp(region, "valencia") == 0) {
        const char* fallback_files[] = {
            "cercanias_valencia",
            "metro_valencia", 
            "tranvia_valencia",
            NULL
        };
        
        for(int i = 0; fallback_files[i] != NULL; i++) {
            if(renfe_sum10_load_station_file(fallback_files[i])) {
                const char* station_name = renfe_sum10_get_station_name_from_cache(station_code);
                if(strcmp(station_name, "Unknown") != 0) {
                    return station_name;
                }
            }
        }
    }
    
    // Try common fallback files for any region
    const char* common_files[] = {"general", "cercanias", "metro", "tranvia", NULL};
    for(int i = 0; common_files[i] != NULL; i++) {
        if(renfe_sum10_load_station_file(common_files[i])) {
            const char* station_name = renfe_sum10_get_station_name_from_cache(station_code);
            if(strcmp(station_name, "Unknown") != 0) {
                return station_name;
            }
        }
    }
    
    return "Unknown";
}

// Global variable to store detected region for station lookup
static char g_detected_region[32] = "valencia"; // Default to Valencia

// Updated wrapper function to maintain API compatibility
static const char* renfe_sum10_get_station_name(uint16_t station_code) {
    // Use the detected region, defaulting to Valencia
    return renfe_sum10_get_station_name_dynamic(station_code, g_detected_region);
}
// Extract zone code from Block 5 data
static uint16_t renfe_sum10_extract_zone_code(const uint8_t* block5_data) {
    if(!block5_data) {
        return 0x0000;
    }
    
    // Zone is stored in bytes 5-6 of Block 5 (big-endian)
    uint16_t zone_code = (block5_data[5] << 8) | block5_data[6];
    
    return zone_code;
}

// Get zone name from zone code (based on real RENFE Suma 10 data analysis)
static const char* renfe_sum10_get_zone_name(uint16_t zone_code) {
    switch(zone_code) {
        // ========== ZONA A - TODAS LAS VARIANTES ==========
        case 0x6C16: case 0x6C17: case 0x6C18: case 0x6C19: case 0x6C1A: case 0x6C1B: case 0x6C1C: case 0x6C1D: case 0x6C1E: case 0x6C1F:
        case 0x6000: case 0x6001: case 0x6002: case 0x6003: case 0x6004: case 0x6005:
        case 0x7100: case 0x7101: case 0x7102: case 0x7103: case 0x7104: case 0x7105:
            return "A";
            
        // ========== ZONA B - TODAS LAS VARIANTES ==========
        case 0x627C: case 0x627D: case 0x627E: case 0x627F:
        case 0x6010: case 0x6011: case 0x6012: case 0x6013: case 0x6014: case 0x6015:
            return "B";
            
        // ========== ZONA C - TODAS LAS VARIANTES ==========
        case 0x6A14: case 0x6A15: case 0x6A16: case 0x6A17: case 0x6A18: case 0x6A19: case 0x6A1A: case 0x6A1B: case 0x6A1C: case 0x6A1D:
        case 0x6020: case 0x6021: case 0x6022: case 0x6023: case 0x6024: case 0x6025:
            return "C";
            
        // ========== ZONA D - TODAS LAS VARIANTES ==========
        case 0x6815: case 0x6816: case 0x6817: case 0x6818: case 0x6819: case 0x681A: case 0x681B: case 0x681C: case 0x681D: case 0x681E:
        case 0x6030: case 0x6031: case 0x6032: case 0x6033: case 0x6034: case 0x6035:
            return "D";
            
        // ========== ZONA E ==========
        case 0x6612: case 0x6613: case 0x6614: case 0x6615: case 0x6616: case 0x6617: case 0x6618: case 0x6619: case 0x661A: case 0x661B:
            return "E";
            
        // ========== ZONA F ==========
        case 0x6410: case 0x6411: case 0x6412: case 0x6413: case 0x6414: case 0x6415: case 0x6416: case 0x6417: case 0x6418: case 0x6419:
            return "F";
            
        // ========== ZONAS NUMÉRICAS 1-9 - CONSOLIDADAS ==========
        case 0x6200: case 0x6201: case 0x6202: case 0x6203: case 0x6204: case 0x6205:
        case 0x8100: case 0x8101: case 0x8102: case 0x8103: case 0x8104: case 0x8105:
        case 0x8200: case 0x8201: case 0x8202: case 0x8203: case 0x8204: case 0x8205:
            return "1";
            
        case 0x6300: case 0x6301: case 0x6302: case 0x6303: case 0x6304: case 0x6305:
        case 0x8110: case 0x8111: case 0x8112: case 0x8113: case 0x8114: case 0x8115:
        case 0x8210: case 0x8211: case 0x8212: case 0x8213: case 0x8214: case 0x8215:
            return "2";
            
        case 0x6400: case 0x6401: case 0x6402: case 0x6403: case 0x6404: case 0x6405:
        case 0x8120: case 0x8121: case 0x8122: case 0x8123: case 0x8124: case 0x8125:
        case 0x8220: case 0x8221: case 0x8222: case 0x8223: case 0x8224: case 0x8225:
            return "3";
            
        case 0x6500: case 0x6501: case 0x6502: case 0x6503: case 0x6504: case 0x6505:
        case 0x8130: case 0x8131: case 0x8132: case 0x8133: case 0x8134: case 0x8135:
            return "4";
            
        case 0x6600: case 0x6601: case 0x6602: case 0x6603: case 0x6604: case 0x6605:
        case 0x8140: case 0x8141: case 0x8142: case 0x8143: case 0x8144: case 0x8145:
            return "5";
            
        case 0x6700: case 0x6701: case 0x6702: case 0x6703: case 0x6704: case 0x6705:
        case 0x8150: case 0x8151: case 0x8152: case 0x8153: case 0x8154: case 0x8155:
            return "6";
            
        case 0x6800: case 0x6801: case 0x6802: case 0x6803: case 0x6804: case 0x6805:
            return "7";
            
        case 0x6900: case 0x6901: case 0x6902: case 0x6903: case 0x6904: case 0x6905:
            return "8";
            
        case 0x6A00: case 0x6A01: case 0x6A02: case 0x6A03: case 0x6A04: case 0x6A05:
            return "9";
            
        // ========== SUBZONAS A1-A6 ==========
        case 0x6C50: case 0x6C51: case 0x6C52: case 0x6C53: case 0x6C54: case 0x6C55:
            return "A1";
        case 0x6C80: case 0x6C81: case 0x6C82: case 0x6C83: case 0x6C84: case 0x6C85:
            return "A2";
        case 0x6C90: case 0x6C91: case 0x6C92: case 0x6C93: case 0x6C94: case 0x6C95:
            return "A3";
        case 0x6CA0: case 0x6CA1: case 0x6CA2: case 0x6CA3: case 0x6CA4: case 0x6CA5:
            return "A4";
        case 0x6CB0: case 0x6CB1: case 0x6CB2: case 0x6CB3: case 0x6CB4: case 0x6CB5:
            return "A5";
        case 0x6CC0: case 0x6CC1: case 0x6CC2: case 0x6CC3: case 0x6CC4: case 0x6CC5:
            return "A6";
            
        // ========== SUBZONAS B1-B6 - CONSOLIDADAS ==========
        case 0x6250: case 0xEC16: case 0x6251: case 0x6252: case 0x6253: case 0x6254: case 0x6255: case 0x6256: case 0x6257: case 0x6258:
        case 0x6259: case 0x625A: case 0x625B: case 0x625C: case 0x625D: case 0x625E: case 0x625F: case 0x6270: case 0x6271: case 0x6272:
        case 0x6273: case 0x6274: case 0x6275: case 0x6276: case 0x6277: case 0x6278: case 0x6279: case 0x627A: case 0x627B:
        case 0x7150: case 0x7151: case 0x7152: case 0x7153: case 0x7154: case 0x7155:
            return "B1";
            
        case 0x6280: case 0x6281: case 0x6282: case 0x6283: case 0x6284: case 0x6285:
        case 0x7180: case 0x7181: case 0x7182: case 0x7183: case 0x7184: case 0x7185:
            return "B2";
            
        case 0x6290: case 0x6291: case 0x6292: case 0x6293: case 0x6294: case 0x6295:
        case 0x7190: case 0x7191: case 0x7192: case 0x7193: case 0x7194: case 0x7195:
            return "B3";
            
        case 0x62A0: case 0x62A1: case 0x62A2: case 0x62A3: case 0x62A4: case 0x62A5:
            return "B4";
        case 0x62B0: case 0x62B1: case 0x62B2: case 0x62B3: case 0x62B4: case 0x62B5:
            return "B5";
        case 0x62C0: case 0x62C1: case 0x62C2: case 0x62C3: case 0x62C4: case 0x62C5:
            return "B6";
            
        // ========== SUBZONAS C1-C7 - CONSOLIDADAS ==========
        case 0x6A50: case 0x6A51: case 0x6A52: case 0x6A53: case 0x6A54: case 0x6A55:
        case 0x7200: case 0x7201: case 0x7202: case 0x7203: case 0x7204: case 0x7205:
            return "C1";
            
        case 0x6A80: case 0x6A81: case 0x6A82: case 0x6A83: case 0x6A84: case 0x6A85:
        case 0x7210: case 0x7211: case 0x7212: case 0x7213: case 0x7214: case 0x7215:
            return "C2";
            
        case 0x6A90: case 0x6A91: case 0x6A92: case 0x6A93: case 0x6A94: case 0x6A95:
        case 0x7220: case 0x7221: case 0x7222: case 0x7223: case 0x7224: case 0x7225:
            return "C3";
            
        case 0x6AA0: case 0x6AA1: case 0x6AA2: case 0x6AA3: case 0x6AA4: case 0x6AA5:
        case 0x7230: case 0x7231: case 0x7232: case 0x7233: case 0x7234: case 0x7235:
            return "C4";
            
        case 0x6AB0: case 0x6AB1: case 0x6AB2: case 0x6AB3: case 0x6AB4: case 0x6AB5:
        case 0x7240: case 0x7241: case 0x7242: case 0x7243: case 0x7244: case 0x7245:
            return "C5";
            
        case 0x6AC0: case 0x6AC1: case 0x6AC2: case 0x6AC3: case 0x6AC4: case 0x6AC5:
        case 0x7250: case 0x7251: case 0x7252: case 0x7253: case 0x7254: case 0x7255:
            return "C6";
            
        case 0x7260: case 0x7261: case 0x7262: case 0x7263: case 0x7264: case 0x7265:
            return "C7";
            
        // ========== ZONAS COMBINADAS ==========
        case 0x6D00: case 0x6D01: case 0x6D02: case 0x6D03: case 0x6D04: case 0x6D05:
            return "AB";
        case 0x6E00: case 0x6E01: case 0x6E02: case 0x6E03: case 0x6E04: case 0x6E05:
            return "AC";
        case 0x6F00: case 0x6F01: case 0x6F02: case 0x6F03: case 0x6F04: case 0x6F05:
            return "BC";
        case 0x7000: case 0x7001: case 0x7002: case 0x7003: case 0x7004: case 0x7005:
            return "ABC";
            
        // ========== LÍNEAS L1-L9 - CONSOLIDADAS ==========
        case 0x9200: case 0x9201: case 0x9202: case 0x9203: case 0x9204: case 0x9205:
        case 0xA100: case 0xA101: case 0xA102: case 0xA103: case 0xA104: case 0xA105:
        case 0x3100: case 0x3101: case 0x3102: case 0x3103: case 0x3104: case 0x3105:
        case 0x4100: case 0x4101: case 0x4102: case 0x4103: case 0x4104: case 0x4105:
        case 0x0C00: case 0x0C01: case 0x0C02: case 0x0C03: case 0x0C04: case 0x0C05:
            return "L1";
            
        case 0x9210: case 0x9211: case 0x9212: case 0x9213: case 0x9214: case 0x9215:
        case 0xA110: case 0xA111: case 0xA112: case 0xA113: case 0xA114: case 0xA115:
        case 0x0C10: case 0x0C11: case 0x0C12: case 0x0C13: case 0x0C14: case 0x0C15:
            return "L2";
            
        case 0xA120: case 0xA121: case 0xA122: case 0xA123: case 0xA124: case 0xA125:
            return "L3";
            
        case 0x6040: case 0x6041: case 0x6042: case 0x6043: case 0x6044: case 0x6045:
            return "L4";
            
        case 0x6050: case 0x6051: case 0x6052: case 0x6053: case 0x6054: case 0x6055:
            return "L6";
            
        case 0x6060: case 0x6061: case 0x6062: case 0x6063: case 0x6064: case 0x6065:
            return "L8";
            
        case 0x6070: case 0x6071: case 0x6072: case 0x6073: case 0x6074: case 0x6075:
            return "L9";
            
        // ========== EUSKOTREN ==========
        case 0xA200: case 0xA201: case 0xA202: case 0xA203: case 0xA204: case 0xA205:
            return "E1";
        case 0xA210: case 0xA211: case 0xA212: case 0xA213: case 0xA214: case 0xA215:
            return "E2";
        case 0xA220: case 0xA221: case 0xA222: case 0xA223: case 0xA224: case 0xA225:
            return "E3";
            
        // ========== ZONAS ESPECIALES ==========
        case 0x9100: case 0x9101: case 0x9102: case 0x9103: case 0x9104: case 0x9105:
        case 0x9300: case 0x9301: case 0x9302: case 0x9303: case 0x9304: case 0x9305:
            return "Centro";
            
        case 0x9110: case 0x9111: case 0x9112: case 0x9113: case 0x9114: case 0x9115:
            return "Periférico";
            
        case 0x9310: case 0x9311: case 0x9312: case 0x9313: case 0x9314: case 0x9315:
            return "Norte";
            
        case 0x0B00: case 0x0B01: case 0x0B02: case 0x0B03: case 0x0B04: case 0x0B05:
            return "M1";
        case 0x0B10: case 0x0B11: case 0x0B12: case 0x0B13: case 0x0B14: case 0x0B15:
            return "M2";
            
        // ========== RENFE ESPECIALES ==========
        case 0xF000: case 0xF001: case 0xF002: case 0xF003: case 0xF004: case 0xF005:
            return "Cercanías Nacional";
        case 0xF100: case 0xF101: case 0xF102: case 0xF103: case 0xF104: case 0xF105:
            return "AVE Nacional";
        case 0xF200: case 0xF201: case 0xF202: case 0xF203: case 0xF204: case 0xF205:
            return "Media Distancia";
        case 0xF300: case 0xF301: case 0xF302: case 0xF303: case 0xF304: case 0xF305:
            return "Larga Distancia";
            
        // ========== CASOS ESPECIALES ==========
        case 0x0000:
            return "Not available";
        default:
            return "Unknown";
    }
}

// Get region/autonomous community from card data analysis
static const char* renfe_sum10_get_region(const MfClassicData* data) {
    if(!data) {
        return "Unknown";
    }
    
    // Extract key data from blocks (only use blocks we have read)
    const uint8_t* block4_data = NULL;
    const uint8_t* block5_data = NULL;
    
    // Check if blocks are available
    if(mf_classic_is_block_read(data, 4)) {
        block4_data = data->block[4].data;
    }
    
    if(mf_classic_is_block_read(data, 5)) {
        block5_data = data->block[5].data;
    }
    
    if(!block4_data || !block5_data) {
        return "Unknown";
    }
    
    // Extract zone code for region identification
    uint16_t zone_code = renfe_sum10_extract_zone_code(block5_data);
    
    // Extract additional identifiers for region detection (only use block 4, skip block 8)
    uint16_t block4_pattern = (block4_data[0] << 8) | block4_data[1];  // First 2 bytes of block 4
    
    // ========== LAS 17 COMUNIDADES AUTÓNOMAS DE ESPAÑA ==========
    
    // 1. VALENCIA (Comunidad Valenciana) - CONFIRMADO por dumps reales
    if((zone_code == 0x6C16 || zone_code == 0x627C || zone_code == 0xEC16) || 
       (block4_pattern == 0x3110 || block4_pattern == 0x1120) ||
       (zone_code >= 0x6000 && zone_code <= 0x6FFF)) {
        return "Valencia";
    }
    
    // 2. MADRID (Comunidad de Madrid)
    if((zone_code >= 0x7000 && zone_code <= 0x7FFF) || 
       (block4_pattern == 0x4110 || block4_pattern == 0x4120) ||
       (block4_pattern >= 0x7000 && block4_pattern <= 0x7FFF) ||
       // Patrones específicos de Metro de Madrid
       (block4_pattern >= 0x7100 && block4_pattern <= 0x72FF) ||
       (zone_code >= 0x7100 && zone_code <= 0x72FF)) {
        return "Madrid";
    }
    
    // 3. CATALUÑA (Catalunya)
    if((zone_code >= 0x8000 && zone_code <= 0x8FFF) || 
       (block4_pattern == 0x5110 || block4_pattern == 0x5120) ||
       (block4_pattern >= 0x8000 && block4_pattern <= 0x8FFF) ||
       // Patrones de TMB Barcelona y FGC
       (block4_pattern >= 0x8100 && block4_pattern <= 0x83FF) ||
       (zone_code >= 0x8100 && zone_code <= 0x83FF)) {
        return "Cataluña";
    }
    
    // 4. ANDALUCÍA 
    if((zone_code >= 0x9000 && zone_code <= 0x9FFF) || 
       (block4_pattern == 0x6110 || block4_pattern == 0x6120) ||
       (block4_pattern >= 0x9000 && block4_pattern <= 0x9FFF) ||
       // Patrones de Metro de Sevilla, Málaga, Granada
       (block4_pattern >= 0x9100 && block4_pattern <= 0x94FF) ||
       (zone_code >= 0x9100 && zone_code <= 0x94FF)) {
        return "Andalucía";
    }
    
    // 5. PAÍS VASCO (Euskadi)
    if((zone_code >= 0xA000 && zone_code <= 0xAFFF) || 
       (block4_pattern == 0x7110 || block4_pattern == 0x7120) ||
       (block4_pattern >= 0xA000 && block4_pattern <= 0xAFFF) ||
       // Patrones de Metro Bilbao y Euskotren
       (block4_pattern >= 0xA100 && block4_pattern <= 0xA3FF) ||
       (zone_code >= 0xA100 && zone_code <= 0xA3FF)) {
        return "País Vasco";
    }
    
    // 6. GALICIA
    if((zone_code >= 0xB000 && zone_code <= 0xBFFF) || 
       (block4_pattern == 0x8110 || block4_pattern == 0x8120) ||
       (block4_pattern >= 0xB000 && block4_pattern <= 0xBFFF) ||
       // Patrones de transporte gallego
       (block4_pattern >= 0xB100 && block4_pattern <= 0xB2FF) ||
       (zone_code >= 0xB100 && zone_code <= 0xB2FF)) {
        return "Galicia";
    }
    
    // 7. ASTURIAS (Principado de Asturias)
    if((zone_code >= 0xC000 && zone_code <= 0xCFFF) || 
       (block4_pattern == 0x9110 || block4_pattern == 0x9120) ||
       (block4_pattern >= 0xC000 && block4_pattern <= 0xCFFF) ||
       // Patrones de FEVE y Metrotranvía
       (block4_pattern >= 0xC100 && block4_pattern <= 0xC2FF) ||
       (zone_code >= 0xC100 && zone_code <= 0xC2FF)) {
        return "Asturias";
    }
    
    // 8. CANTABRIA
    if((zone_code >= 0xD000 && zone_code <= 0xDFFF) || 
       (block4_pattern == 0xA110 || block4_pattern == 0xA120) ||
       (block4_pattern >= 0xD000 && block4_pattern <= 0xDFFF) ||
       // Patrones de FEVE Cantabria
       (block4_pattern >= 0xD100 && block4_pattern <= 0xD1FF) ||
       (zone_code >= 0xD100 && zone_code <= 0xD1FF)) {
        return "Cantabria";
    }
    
    // 9. CASTILLA Y LEÓN
    if((zone_code >= 0xE000 && zone_code <= 0xEFFF) || 
       (block4_pattern == 0xB110 || block4_pattern == 0xB120) ||
       (block4_pattern >= 0xE000 && block4_pattern <= 0xEFFF) ||
       // Patrones de AVE y líneas de Castilla y León
       (block4_pattern >= 0xE100 && block4_pattern <= 0xE3FF) ||
       (zone_code >= 0xE100 && zone_code <= 0xE3FF)) {
        return "Castilla y León";
    }
    
    // 10. CASTILLA-LA MANCHA
    if((zone_code >= 0x1000 && zone_code <= 0x1FFF) || 
       (block4_pattern == 0xC110 || block4_pattern == 0xC120) ||
       (block4_pattern >= 0x1000 && block4_pattern <= 0x1FFF) ||
       // Patrones de AVE Madrid-Sevilla y líneas regionales
       (block4_pattern >= 0x1100 && block4_pattern <= 0x13FF) ||
       (zone_code >= 0x1100 && zone_code <= 0x13FF)) {
        return "Castilla-La Mancha";
    }
    
    // 11. EXTREMADURA
    if((zone_code >= 0x2000 && zone_code <= 0x2FFF) || 
       (block4_pattern == 0xD110 || block4_pattern == 0xD120) ||
       (block4_pattern >= 0x2000 && block4_pattern <= 0x2FFF) ||
       // Patrones de líneas a Badajoz y Cáceres
       (block4_pattern >= 0x2100 && block4_pattern <= 0x22FF) ||
       (zone_code >= 0x2100 && zone_code <= 0x22FF)) {
        return "Extremadura";
    }
    
    // 12. ARAGÓN
    if((zone_code >= 0x3000 && zone_code <= 0x3FFF) || 
       (block4_pattern == 0xE110 || block4_pattern == 0xE120) ||
       (block4_pattern >= 0x3000 && block4_pattern <= 0x3FFF) ||
       // Patrones de Tranvía de Zaragoza y líneas regionales
       (block4_pattern >= 0x3100 && block4_pattern <= 0x32FF) ||
       (zone_code >= 0x3100 && zone_code <= 0x32FF)) {
        return "Aragón";
    }
    
    // 13. MURCIA (Región de Murcia)
    if((zone_code >= 0x4000 && zone_code <= 0x4FFF) || 
       (block4_pattern == 0xF110 || block4_pattern == 0xF120) ||
       (block4_pattern >= 0x4000 && block4_pattern <= 0x4FFF) ||
       // Patrones de Tranvía de Murcia
       (block4_pattern >= 0x4100 && block4_pattern <= 0x41FF) ||
       (zone_code >= 0x4100 && zone_code <= 0x41FF)) {
        return "Murcia";
    }
    
    // 14. NAVARRA (Comunidad Foral de Navarra)
    if((zone_code >= 0x5000 && zone_code <= 0x5FFF) || 
       (block4_pattern >= 0x5000 && block4_pattern <= 0x5FFF) ||
       // Patrones específicos de transporte navarro
       (block4_pattern >= 0x5100 && block4_pattern <= 0x51FF) ||
       (zone_code >= 0x5100 && zone_code <= 0x51FF)) {
        return "Navarra";
    }
    
    // 15. LA RIOJA
    if((zone_code >= 0xF000) || 
       (block4_pattern >= 0xF000) ||
       // Patrones específicos de La Rioja
       (block4_pattern >= 0xF100 && block4_pattern <= 0xF1FF) ||
       (zone_code >= 0xF100 && zone_code <= 0xF1FF)) {
        return "La Rioja";
    }
    
    // 16. ISLAS BALEARES
    if((zone_code >= 0x0100 && zone_code <= 0x0FFF) || 
       (block4_pattern >= 0x0100 && block4_pattern <= 0x0FFF) ||
       // Patrones de Metro de Palma y Tren de Sóller
       (block4_pattern >= 0x0B00 && block4_pattern <= 0x0BFF) ||
       (zone_code >= 0x0B00 && zone_code <= 0x0BFF)) {
        return "Islas Baleares";
    }
    
    // 17. ISLAS CANARIAS
    if((zone_code >= 0x0010 && zone_code <= 0x00FF) || 
       (block4_pattern >= 0x0010 && block4_pattern <= 0x00FF) ||
       // Patrones de Metro de Tenerife y Guaguas
       (block4_pattern >= 0x0C00 && block4_pattern <= 0x0CFF) ||
       (zone_code >= 0x0C00 && zone_code <= 0x0CFF)) {
        return "Islas Canarias";
    }
    
    // Try to detect region by other patterns before defaulting
    if(block4_pattern >= 0x1000) {
        // If we have a valid pattern but couldn't identify the region,
        // try to guess based on ranges
        if(block4_pattern >= 0x7000 && block4_pattern <= 0x7FFF) return "Madrid";
        if(block4_pattern >= 0x8000 && block4_pattern <= 0x8FFF) return "Catalunya";
        if(block4_pattern >= 0x9000 && block4_pattern <= 0x9FFF) return "Andalucia";
        if(block4_pattern >= 0xA000 && block4_pattern <= 0xAFFF) return "Pais Vasco";
        if(block4_pattern >= 0xB000 && block4_pattern <= 0xBFFF) return "Galicia";
    }
    
    return "Unknown"; // Fallback when region cannot be identified
}

// Parse a single history entry
static void renfe_sum10_parse_history_entry(FuriString* parsed_data, const uint8_t* block_data, int entry_num) {
    // Check for null pointers
    if(!parsed_data || !block_data) {
        return;
    }
    
    // Extract transaction type from first byte
    uint8_t transaction_type = block_data[0];
    
    /* TRANSACTION TYPES MAPPING (based on analysis):
     * CONFIRMED TYPES:
     * 0x13 = ENTRY     - Entrada al sistema
     * 0x1A = EXIT      - Salida del sistema  
     * 0x1E = TRANSFER  - Transbordo entre lineas
     * 0x16 = VALIDATION- Validacion de titulo
     * 0x33 = TOP-UP    - Recarga de saldo
     * 0x3A = CHARGE    - Carga/cargo adicional
     * 0x18 = CHECK     - Verificacion
     * 0x2B = SPECIAL   - Operacion especial
     * 
     * DEDUCED TYPES (found in real cards):
     * 0x17 = INSPECTION- Posible control/revision de inspector
     * 0x23 = DISCOUNT  - Posible aplicacion de descuento
     * 0x2A = PENALTY   - Posible sancion o multa
     */
    
    // Extract timestamp data (bytes 2-4)
    uint8_t timestamp_1 = block_data[2];
    uint8_t timestamp_2 = block_data[3];
    uint8_t timestamp_3 = block_data[4];
    
    // Extract station codes (bytes 9-10) - Read as big-endian
    uint16_t station_code = (block_data[9] << 8) | block_data[10];
    
    // Extract transaction details
    uint8_t detail1 = block_data[5];
    uint8_t detail2 = block_data[6];
    
    // Format the entry
    furi_string_cat_printf(parsed_data, "%d. ", entry_num);
    
    // Interpret transaction type
    switch(transaction_type) {
        case 0x13:
            furi_string_cat_printf(parsed_data, "Entry");
            break;
        case 0x1A:
            furi_string_cat_printf(parsed_data, "Exit");
            break;
        case 0x1E:
            furi_string_cat_printf(parsed_data, "Transfer");
            break;
        case 0x16:
            furi_string_cat_printf(parsed_data, "Validation");
            break;
        case 0x17:
            furi_string_cat_printf(parsed_data, "Inspection");
            break;
        case 0x23:
            furi_string_cat_printf(parsed_data, "Discount");
            break;
        case 0x2A:
            furi_string_cat_printf(parsed_data, "Penalty");
            break;
        case 0x33:
            furi_string_cat_printf(parsed_data, "Top-up");
            break;
        case 0x3A:
            furi_string_cat_printf(parsed_data, "Charge");
            break;
        case 0x18:
            furi_string_cat_printf(parsed_data, "Check");
            break;
        case 0x2B:
            furi_string_cat_printf(parsed_data, "Special");
            break;
        default:
            furi_string_cat_printf(parsed_data, "Unknown (0x%02X)", transaction_type);
            break;
    }
    // Add station information
    const char* station_name = renfe_sum10_get_station_name(station_code);
    if(station_code != 0x0000 && strlen(station_name) > 0) {
        furi_string_cat_printf(parsed_data, " - %s", station_name);
        if(strcmp(station_name, "Unknown") == 0) {
            furi_string_cat_printf(parsed_data, " (Code: %04X)", station_code);
        }
    }
    
    // Add timestamp info in a cleaner format
    furi_string_cat_printf(parsed_data, " [%02X%02X%02X]", timestamp_1, timestamp_2, timestamp_3);
    
    // Add additional details if available
    if(detail1 != 0x00 || detail2 != 0x00) {
        furi_string_cat_printf(parsed_data, " (Details: %02X%02X)", detail1, detail2);
    }
    
    furi_string_cat_printf(parsed_data, "\n");
}
// Parse travel history from the card data (with sorting and strict validation)
static void renfe_sum10_parse_travel_history(FuriString* parsed_data, const MfClassicData* data) {
    // Check for null pointers
    if(!parsed_data || !data) {
        return;
    }
    
    // Define specific blocks that contain history based on manual analysis
    // These are the confirmed history blocks from the original analysis
    int history_blocks[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 28, 29, 30, 44, 45, 46};
    int num_blocks = sizeof(history_blocks) / sizeof(history_blocks[0]);
    
    // Array to store found history entries for sorting
    HistoryEntry* history_entries = malloc(sizeof(HistoryEntry) * num_blocks);
    if(!history_entries) {
        furi_string_cat_printf(parsed_data, "Memory allocation error\n");
        return;
    }
    
    int history_count = 0;
    
    int max_blocks = (data->type == MfClassicType1k) ? 64 : 256;
    
    // Collect all valid history entries with strict validation
    for(int i = 0; i < num_blocks; i++) {
        int block = history_blocks[i];
        
        // Check if block number is within valid range for this card type
        if(block >= max_blocks) {
            continue;
        }
        
        // Check if block was actually read
        if(!mf_classic_is_block_read(data, block)) {
            continue;
        }
        
        const uint8_t* block_data = data->block[block].data;
        
        // Use strict block-specific validation to avoid false positives
        if(renfe_sum10_is_travel_history_block(block_data, block)) {
            
            // Store the entry for sorting
            memcpy(history_entries[history_count].block_data, block_data, 16);
            history_entries[history_count].timestamp = renfe_sum10_extract_timestamp(block_data);
            history_entries[history_count].block_number = block;
            
            history_count++;
        }
    }
    
    if(history_count == 0) {
        // Don't add any message here - let the calling function handle it
    } else {
        // Sort the history entries by timestamp (newest first)
        renfe_sum10_sort_history_entries(history_entries, history_count);
        
        // Display the sorted history
        furi_string_cat_printf(parsed_data, "� Travel History (%d trips):\n", history_count);
        furi_string_cat_printf(parsed_data, "(Most recent first)\n\n");
        
        for(int i = 0; i < history_count; i++) {
            
            furi_string_cat_printf(parsed_data, "Trip %d:\n", i + 1);
            renfe_sum10_parse_history_entry(parsed_data, history_entries[i].block_data, i + 1);
            furi_string_cat_printf(parsed_data, "\n");
        }
    }
    
    // Free allocated memory
    free(history_entries);
}

// Parse only travel history (called when user presses LEFT button)
typedef struct {
    uint8_t data_sector;
    const MfClassicKeyPair* keys;
} RenfeSum10CardConfig;

static bool renfe_sum10_get_card_config(RenfeSum10CardConfig* config, MfClassicType type) {
    bool success = true;

    if(type == MfClassicType1k) {
        config->data_sector = 5; // Primary data sector for trips
        config->keys = renfe_sum10_keys;
    } else if(type == MfClassicType4k) {
        // Mifare Plus 2K/4K configured as 1K for RENFE Suma 10 compatibility
        // Treat it exactly like a 1K card - only use the first 1K sectors
        config->data_sector = 5; // Same as 1K - RENFE only uses first 1K sectors
        config->keys = renfe_sum10_keys;
    } else {
        success = false; 
    }

    return success;
}

// Parse only travel history (called when user presses LEFT button)
static bool renfe_sum10_parse_history_only(FuriString* parsed_data, const MfClassicData* data) {
    // Check for null pointers
    if(!parsed_data || !data) {
        return false;
    }
    
    // Clear existing data and show only travel history
    furi_string_reset(parsed_data);
    furi_string_cat_printf(parsed_data, "\e#RENFE SUMA 10\n");
    furi_string_cat_printf(parsed_data, "\e#Travel History\n\n");
    
    // Check if we have history data first using strict validation
    bool has_history = renfe_sum10_has_history_data(data);
    
    if(!has_history) {
        // Show a clear message when no history is found
        furi_string_cat_printf(parsed_data, "📅 No travel history found\n\n");
        furi_string_cat_printf(parsed_data, "This card appears to be:\n");
        furi_string_cat_printf(parsed_data, "• New or unused\n");
        furi_string_cat_printf(parsed_data, "• Recently reset\n");
        furi_string_cat_printf(parsed_data, "• Used only for balance\n\n");
        furi_string_cat_printf(parsed_data, "Note: The system filters out\n");
        furi_string_cat_printf(parsed_data, "false positives to ensure\n");
        furi_string_cat_printf(parsed_data, "only real trips are shown.\n");
    } else {
        // Parse travel history with strict validation
        renfe_sum10_parse_travel_history(parsed_data, data);
    }
    
    furi_string_cat_printf(parsed_data, "\n⬅️ Press LEFT to return");
    
    return true;
}

// Check if a specific block likely contains travel history (not configuration)
static bool renfe_sum10_is_travel_history_block(const uint8_t* block_data, int block_number) {
    if(!block_data) return false;
    
    // Blocks 4-5 are more likely to contain configuration than real travel history
    // Apply stricter validation for these blocks
    if(block_number <= 5) {
        // For configuration blocks, require confirmed transaction patterns
        uint8_t transaction_type = block_data[0];
        
        // Only accept very specific confirmed transaction types in config blocks
        bool is_definite_transaction = (transaction_type == 0x13 || // ENTRY
                                      transaction_type == 0x1A);   // EXIT
        
        // Must have valid timestamp AND confirmed transaction type
        return is_definite_transaction && 
               renfe_sum10_is_valid_timestamp(block_data) &&
               !renfe_sum10_is_default_station_code((block_data[9] << 8) | block_data[10]);
    }
    
    // For higher numbered blocks, use standard validation
    return renfe_sum10_is_history_entry(block_data);
}

// Simple check if we have enough data to show travel history
static bool renfe_sum10_has_history_data(const MfClassicData* data) {
    if(!data) {
        return false;
    }
    
    // Use the same block list as the parsing function to ensure consistency
    int history_blocks[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 28, 29, 30, 44, 45, 46};
    int num_blocks = sizeof(history_blocks) / sizeof(history_blocks[0]);
    
    int max_blocks = (data->type == MfClassicType1k) ? 64 : 256;
    int valid_entries_found = 0;
    
    for(int i = 0; i < num_blocks; i++) {
        int block = history_blocks[i];
        
        // Check if block number is within valid range for this card type
        if(block >= max_blocks) {
            continue;
        }
        
        // Check if block was actually read
        if(!mf_classic_is_block_read(data, block)) {
            continue;
        }
        
        const uint8_t* block_data = data->block[block].data;
        if(!block_data) {
            continue;
        }
        
        // Use block-specific validation
        if(renfe_sum10_is_travel_history_block(block_data, block)) {
            valid_entries_found++;
            
            // Require at least 1 valid entry, but be more strict about what counts as valid
            if(valid_entries_found >= 1) {
                return true;
            }
        }
    }
    
    return false;
}

// Parse function for RENFE Suma 10 cards
static bool renfe_sum10_parse(FuriString* parsed_data, const MfClassicData* data) {
    // Check for null pointers
    if(!parsed_data) {
        return false;
    }
    
    if(!data) {
        return false;
    }
    
    bool parsed = false;
    RenfeSum10CardConfig cfg;
    
    do {
        memset(&cfg, 0, sizeof(cfg));
        if(!renfe_sum10_get_card_config(&cfg, data->type)) {
            break;
        }

        furi_string_cat_printf(parsed_data, "\e#🚆 RENFE SUMA 10\n");
        
        // 1. Show card type info
        if(data->type == MfClassicType1k) {
            furi_string_cat_printf(parsed_data, "📱 Type: Mifare Classic 1K\n");
        } else if(data->type == MfClassicType4k) {
            furi_string_cat_printf(parsed_data, "📱 Type: Mifare Plus 2K\n");
        } else {
            furi_string_cat_printf(parsed_data, "📱 Type: Unknown\n");
        }
        
        // 2. Extract and show UID
        if(data->iso14443_3a_data) {
            const uint8_t* uid = data->iso14443_3a_data->uid;
            size_t uid_len = data->iso14443_3a_data->uid_len;
            
            furi_string_cat_printf(parsed_data, "🔢 UID: ");
            for(size_t i = 0; i < uid_len; i++) {
                furi_string_cat_printf(parsed_data, "%02X", uid[i]);
                if(i < uid_len - 1) furi_string_cat_printf(parsed_data, " ");
            }
            furi_string_cat_printf(parsed_data, "\n");
        } else {
            furi_string_cat_printf(parsed_data, "🔢 UID: Not available\n");
        }
        
        // 3. Extract and show region information
        const char* region_name = renfe_sum10_get_region(data);
        if(region_name) {
            furi_string_cat_printf(parsed_data, "🌍 Region: %s\n", region_name);
            
            // Set global region for station name lookup
            strncpy(g_detected_region, region_name, sizeof(g_detected_region) - 1);
            g_detected_region[sizeof(g_detected_region) - 1] = '\0';
            // Convert to lowercase for file lookup
            for(int i = 0; g_detected_region[i]; i++) {
                g_detected_region[i] = tolower(g_detected_region[i]);
            }
        } else {
            furi_string_cat_printf(parsed_data, "🌍 Region: Unknown\n");
        }
        
        // 4. Extract and show zone information from Block 5 (bytes 5-6)
        const uint8_t* block5 = data->block[5].data;
        if(block5) {
            uint16_t zone_code = renfe_sum10_extract_zone_code(block5);
            const char* zone_name = renfe_sum10_get_zone_name(zone_code);
            if(zone_code != 0x0000) {
                furi_string_cat_printf(parsed_data, "🎯 Zone: %s\n", zone_name);
                if(strcmp(zone_name, "Unknown") == 0) {
                    furi_string_cat_printf(parsed_data, "   Code: 0x%04X\n", zone_code);
                }
            } else {
                furi_string_cat_printf(parsed_data, "🎯 Zone: Not available\n");
            }
        } else {
            furi_string_cat_printf(parsed_data, "🎯 Zone: Block 5 not available\n");
        }
        
        // 5. Extract and show trips from Block 5 (based on real dump analysis)
        if(block5) {
            if(block5[0] == 0x01 && block5[1] == 0x00 && block5[2] == 0x00 && block5[3] == 0x00) {
                // Extract trip count from byte 4
                int viajes = (int)block5[4];
                furi_string_cat_printf(parsed_data, "🎫 Trips: %d\n", viajes);
            } else {
                furi_string_cat_printf(parsed_data, "🎫 Trips: Not available\n");
            }
        } else {
            furi_string_cat_printf(parsed_data, "🎫 Trips: Block 5 not available\n");
        }
        
        // Add travel history status prominently (visible above buttons)
        furi_string_cat_printf(parsed_data, "\n");
        if(renfe_sum10_has_history_data(data)) {
            furi_string_cat_printf(parsed_data, "📚 History: Available\n");
            furi_string_cat_printf(parsed_data, "   ⬅️ Press LEFT to view details\n");
        } else {
            furi_string_cat_printf(parsed_data, "📭 History: Empty/Not found\n");
            furi_string_cat_printf(parsed_data, "   (New card or cleared history)\n");
        }
        
        parsed = true;
        
    } while(false);

    return parsed;
}

// Widget callback function for handling button events (only for History button)
static void renfe_sum10_widget_callback(GuiButtonType result, InputType type, void* context) {
    if(!context) {
        return;
    }
    
    Metroflip* app = context;
    if(!app->view_dispatcher) {
        return;
    }
    
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

static NfcCommand renfe_sum10_poller_callback(NfcGenericEvent event, void* context) {
    // Check for null pointers instead of asserting
    if(!context) {
        return NfcCommandStop;
    }
    
    if(!event.event_data) {
        return NfcCommandStop;
    }
    
    if(event.protocol != NfcProtocolMfClassic) {
        return NfcCommandStop;
    }

    NfcCommand command = NfcCommandContinue;
    const MfClassicPollerEvent* mfc_event = event.event_data;
    Metroflip* app = context;

    if(mfc_event->type == MfClassicPollerEventTypeCardDetected) {
        // Ensure nfc_device is allocated
        if(!app->nfc_device) {
            app->nfc_device = nfc_device_alloc();
        }
        
        view_dispatcher_send_custom_event(app->view_dispatcher, MetroflipCustomEventCardDetected);
        command = NfcCommandContinue;
    } else if(mfc_event->type == MfClassicPollerEventTypeCardLost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MetroflipCustomEventCardLost);
        app->sec_num = 0;
        command = NfcCommandStop;
    } else if(mfc_event->type == MfClassicPollerEventTypeRequestMode) {
        mfc_event->data->poller_mode.mode = MfClassicPollerModeRead;

    } else if(mfc_event->type == MfClassicPollerEventTypeRequestReadSector) {
        // Set data during sector reading (like working version)
        nfc_device_set_data(app->nfc_device, NfcProtocolMfClassic, nfc_poller_get_data(app->poller));
        const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
        
        // Determine maximum sectors based on card type
        uint8_t max_sectors = 16; // Default for 1K
        if(mfc_data && mfc_data->type == MfClassicType4k) {
            max_sectors = 16; // RENFE Suma 10 only uses first 16 sectors even on 4K cards
        }
        
        // Check if we have more sectors to read
        if(app->sec_num < max_sectors) {
            MfClassicKey key = {0};
            MfClassicKeyType key_type = MfClassicKeyTypeA;
            
            // Verify sector number is within key array bounds
            if(app->sec_num >= sizeof(renfe_sum10_keys) / sizeof(renfe_sum10_keys[0])) {
                return NfcCommandStop;
            }
            
            // Use the correct key for this sector
            bit_lib_num_to_bytes_be(renfe_sum10_keys[app->sec_num].a, COUNT_OF(key.data), key.data);
            
            mfc_event->data->read_sector_request_data.sector_num = app->sec_num;
            mfc_event->data->read_sector_request_data.key = key;
            mfc_event->data->read_sector_request_data.key_type = key_type;
            mfc_event->data->read_sector_request_data.key_provided = true;
            
            app->sec_num++;
        } else {
            // No more sectors to read
            mfc_event->data->read_sector_request_data.key_provided = false;
            app->sec_num = 0;
        }
    } else if(mfc_event->type == MfClassicPollerEventTypeSuccess) {
        // Handle successful completion - parse and display immediately (like working version)
        
        if(!app->nfc_device) {
            return NfcCommandStop;
        }
        
        const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
        if(!mfc_data) {
            return NfcCommandStop;
        }
        
        FuriString* parsed_data = furi_string_alloc();
        if(!parsed_data) {
            return NfcCommandStop;
        }
        
        Widget* widget = app->widget;
        if(!widget) {
            furi_string_free(parsed_data);
            return NfcCommandStop;
        }
        
        if(!renfe_sum10_parse(parsed_data, mfc_data)) {
            furi_string_reset(app->text_box_store);
            furi_string_printf(parsed_data, "\e#Unknown card\n");
        }
        
        widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));
        
        // Always show History button - if no history found, we'll show a message
        // Use the standard Metroflip callbacks in the poller success
        widget_add_button_element(widget, GuiButtonTypeLeft, "History", renfe_sum10_widget_callback, app);
        widget_add_button_element(widget, GuiButtonTypeCenter, "Save", metroflip_save_widget_callback, app);
        widget_add_button_element(widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);

        furi_string_free(parsed_data);
        
        if(app->view_dispatcher) {
            view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        }
        metroflip_app_blink_stop(app);
        command = NfcCommandStop;
    } else if(mfc_event->type == MfClassicPollerEventTypeFail) {
        command = NfcCommandStop;
    }

    return command;
}

static void renfe_sum10_on_enter(Metroflip* app) {
    if(!app) {
        return;
    }
    
    dolphin_deed(DolphinDeedNfcRead);

    app->sec_num = 0;

    if(app->data_loaded) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FlipperFormat* ff = flipper_format_file_alloc(storage);
        if(flipper_format_file_open_existing(ff, app->file_path)) {
            MfClassicData* mfc_data = mf_classic_alloc();
            mf_classic_load(mfc_data, ff, 2);
            FuriString* parsed_data = furi_string_alloc();
            Widget* widget = app->widget;

            if(!widget) {
                mf_classic_free(mfc_data);
                furi_string_free(parsed_data);
                flipper_format_free(ff);
                furi_record_close(RECORD_STORAGE);
                return;
            }

            if(!app->text_box_store) {
                mf_classic_free(mfc_data);
                furi_string_free(parsed_data);
                flipper_format_free(ff);
                furi_record_close(RECORD_STORAGE);
                return;
            }

            furi_string_reset(app->text_box_store);
            if(!renfe_sum10_parse(parsed_data, mfc_data)) {
                furi_string_reset(app->text_box_store);
                furi_string_printf(parsed_data, "\e#Unknown card\n");
            }
            widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));

            // Use the standard Metroflip callbacks instead of custom ones
            widget_add_button_element(widget, GuiButtonTypeLeft, "History", renfe_sum10_widget_callback, app);
            widget_add_button_element(widget, GuiButtonTypeCenter, "Delete", metroflip_delete_widget_callback, app);
            widget_add_button_element(widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
            
            if(!app->view_dispatcher) {
                mf_classic_free(mfc_data);
                furi_string_free(parsed_data);
                flipper_format_free(ff);
                furi_record_close(RECORD_STORAGE);
                return;
            }
            
            mf_classic_free(mfc_data);
            furi_string_free(parsed_data);
            view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
        }
        flipper_format_free(ff);
    } else {
        // Setup view
        Popup* popup = app->popup;
        if(!popup) {
            return;
        }
        
        if(!app->view_dispatcher) {
            return;
        }
        
        popup_set_header(popup, "Apply\n card to\nthe back", 68, 30, AlignLeft, AlignTop);
        popup_set_icon(popup, 0, 3, &I_RFIDDolphinReceive_97x61);

        // Start worker
        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewPopup);
        
        app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfClassic);
        if(!app->poller) {
            return;
        }
        
        nfc_poller_start(app->poller, renfe_sum10_poller_callback, app);

        metroflip_app_blink_start(app);
    }
}

static bool renfe_sum10_on_event(Metroflip* app, SceneManagerEvent event) {
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MetroflipCustomEventCardDetected) {
            if(app->popup) {
                Popup* popup = app->popup;
                popup_set_header(popup, "DON'T\nMOVE", 68, 30, AlignLeft, AlignTop);
            }
            consumed = true;
        } else if(event.event == MetroflipCustomEventCardLost) {
            if(app->popup) {
                Popup* popup = app->popup;
                popup_set_header(popup, "Card \n lost", 68, 30, AlignLeft, AlignTop);
            }
            consumed = true;
        } else if(event.event == MetroflipCustomEventWrongCard) {
            if(app->popup) {
                Popup* popup = app->popup;
                popup_set_header(popup, "WRONG \n CARD", 68, 30, AlignLeft, AlignTop);
            }
            consumed = true;
        } else if(event.event == MetroflipCustomEventPollerFail) {
            if(app->popup) {
                Popup* popup = app->popup;
                popup_set_header(popup, "Failed", 68, 30, AlignLeft, AlignTop);
            }
            consumed = true;
        } else if(event.event == GuiButtonTypeLeft) {
            // Handle LEFT button press to show travel history
            if(app->data_loaded) {
                // Data loaded from file
                Storage* storage = furi_record_open(RECORD_STORAGE);
                FlipperFormat* ff = flipper_format_file_alloc(storage);
                if(flipper_format_file_open_existing(ff, app->file_path)) {
                    MfClassicData* mfc_data = mf_classic_alloc();
                    mf_classic_load(mfc_data, ff, 2);
                    
                    FuriString* parsed_data = furi_string_alloc();
                    Widget* widget = app->widget;

                    if(widget) {
                        // Reset widget and show only travel history
                        widget_reset(widget);
                        if(!renfe_sum10_parse_history_only(parsed_data, mfc_data)) {
                            furi_string_reset(parsed_data);
                            furi_string_printf(parsed_data, "\e#Travel History\nNo history found\n");
                        }
                        widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));
                        widget_add_button_element(widget, GuiButtonTypeRight, "Back", renfe_sum10_widget_callback, app);
                        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
                    }

                    mf_classic_free(mfc_data);
                    furi_string_free(parsed_data);
                }
                flipper_format_free(ff);
                furi_record_close(RECORD_STORAGE);
            } else {
                // Card is connected - use already read data
                const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
                if(mfc_data && app->widget) {
                    FuriString* parsed_data = furi_string_alloc();
                    Widget* widget = app->widget;
                    
                    // Reset widget and show travel history
                    widget_reset(widget);
                    if(!renfe_sum10_parse_history_only(parsed_data, mfc_data)) {
                        furi_string_reset(parsed_data);
                        furi_string_printf(parsed_data, "\e#Travel History\nNo history found\n");
                    }
                    widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));
                    widget_add_button_element(widget, GuiButtonTypeRight, "Back", renfe_sum10_widget_callback, app);
                    
                    furi_string_free(parsed_data);
                    view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
                }
            }
            consumed = true;
        } else if(event.event == GuiButtonTypeRight) {
            // Handle RIGHT button press - go back to main card view from history
            if(app->data_loaded) {
                // Data loaded from file - reload main view
                Storage* storage = furi_record_open(RECORD_STORAGE);
                FlipperFormat* ff = flipper_format_file_alloc(storage);
                if(flipper_format_file_open_existing(ff, app->file_path)) {
                    MfClassicData* mfc_data = mf_classic_alloc();
                    mf_classic_load(mfc_data, ff, 2);
                    FuriString* parsed_data = furi_string_alloc();
                    Widget* widget = app->widget;

                    if(widget) {
                        widget_reset(widget);
                        if(!renfe_sum10_parse(parsed_data, mfc_data)) {
                            furi_string_printf(parsed_data, "\e#Unknown card\n");
                        }
                        widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));

                        widget_add_button_element(widget, GuiButtonTypeLeft, "History", renfe_sum10_widget_callback, app);
                        widget_add_button_element(widget, GuiButtonTypeCenter, "Delete", metroflip_delete_widget_callback, app);
                        widget_add_button_element(widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
                        
                        view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
                    }
                    
                    mf_classic_free(mfc_data);
                    furi_string_free(parsed_data);
                }
                flipper_format_free(ff);
                furi_record_close(RECORD_STORAGE);
            } else {
                // Card is connected - use already read data
                const MfClassicData* mfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolMfClassic);
                if(mfc_data && app->widget) {
                    FuriString* parsed_data = furi_string_alloc();
                    Widget* widget = app->widget;
                    
                    widget_reset(widget);
                    if(!renfe_sum10_parse(parsed_data, mfc_data)) {
                        furi_string_printf(parsed_data, "\e#Unknown card\n");
                    }
                    widget_add_text_scroll_element(widget, 0, 0, 128, 52, furi_string_get_cstr(parsed_data));
                    
                    widget_add_button_element(widget, GuiButtonTypeLeft, "History", renfe_sum10_widget_callback, app);
                    widget_add_button_element(widget, GuiButtonTypeCenter, "Save", metroflip_save_widget_callback, app);
                    widget_add_button_element(widget, GuiButtonTypeRight, "Exit", metroflip_exit_widget_callback, app);
                    
                    view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
                    furi_string_free(parsed_data);
                }
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
        consumed = true;
    }

    return consumed;
}

static void renfe_sum10_on_exit(Metroflip* app) {
    if(!app) {
        FURI_LOG_E(TAG, "renfe_sum10_on_exit: app is NULL");
        return;
    }

    if(app->widget) {
        widget_reset(app->widget);
    }

    if(app->poller && !app->data_loaded) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }

    // Clear view
    if(app->popup) {
        popup_reset(app->popup);
    }

    // Clear station cache to free memory
    renfe_sum10_clear_station_cache();
    
    // Reset region to default
    strncpy(g_detected_region, "valencia", sizeof(g_detected_region) - 1);
    g_detected_region[sizeof(g_detected_region) - 1] = '\0';

    metroflip_app_blink_stop(app);
}

static const MetroflipPlugin renfe_sum10_plugin = {
    .card_name = "RENFE Suma 10",
    .plugin_on_enter = renfe_sum10_on_enter,
    .plugin_on_event = renfe_sum10_on_event,
    .plugin_on_exit = renfe_sum10_on_exit,
};

static const FlipperAppPluginDescriptor renfe_sum10_plugin_descriptor = {
    .appid = METROFLIP_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = METROFLIP_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &renfe_sum10_plugin,
};

const FlipperAppPluginDescriptor* renfe_sum10_plugin_ep(void) {
    return &renfe_sum10_plugin_descriptor;
}
