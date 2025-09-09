#include "menu.h"
#include "confirmation_view.h"
#include "ghost_esp_ep.h"
#include "settings_def.h"
#include "settings_storage.h"
#include "uart_utils.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* label; // Display label in menu
    const char* command; // UART command to send
    const char* capture_prefix; // Prefix for capture files (NULL if none)
    const char* file_ext; // File extension for captures (NULL if none)
    const char* folder; // Folder for captures (NULL if none)
    bool needs_input; // Whether command requires text input
    const char* input_text; // Text to show in input box (NULL if none)
    bool needs_confirmation; // Whether command needs confirmation
    const char* confirm_header; // Confirmation dialog header
    const char* confirm_text; // Confirmation dialog text
    const char* details_header; // Header for details view
    const char* details_text; // Detailed description/info text
} MenuCommand;

typedef struct {
    const char* label;
    const char* command;
    const char* capture_prefix;
} SniffCommandDef;

typedef struct {
    const char* label;
    const char* command;
} BeaconSpamDef;

typedef struct {
    AppState* state;
    const MenuCommand* command;
} MenuCommandContext;

typedef struct {
    const char* label;
    const char* command;
    const char* details_header;
    const char* details_text;
    bool needs_input;
    const char* input_text;
} CyclingMenuDef;

// Forward declarations of static functions
static void show_menu(
    AppState* state,
    const MenuCommand* commands,
    size_t command_count,
    const char* header,
    Submenu* menu,
    uint8_t view_id);
static void show_command_details(AppState* state, const MenuCommand* command);
static bool menu_input_handler(InputEvent* event, void* context);
static void text_input_result_callback(void* context);
static void confirmation_ok_callback(void* context);
static void confirmation_cancel_callback(void* context);
static void app_info_ok_callback(void* context);
static void execute_menu_command(AppState* state, const MenuCommand* command);
static void error_callback(void* context);

// Sniff command definitions
static const SniffCommandDef sniff_commands[] = {
    {"< Sniff WPS >", "capture -wps\n", "wps_capture"},
    {"< Sniff Raw Packets >", "capture -raw\n", "raw_capture"},
    {"< Sniff Probes >", "capture -p\n", "probe_capture"},
    {"< Sniff Deauth >", "capture -deauth\n", "deauth_capture"},
    {"< Sniff Beacons >", "capture -beacon\n", "beacon_capture"},
    {"< Sniff EAPOL >", "capture -eapol\n", "eapol_capture"},
    {"< Sniff Pwn >", "capture -pwn\n", "pwn_capture"},
};

// Beacon spam command definitions
static const CyclingMenuDef beacon_spam_commands[] = {
    {"< Beacon Spam (List) >",
     "beaconspam -l\n",
     "Beacon Spam (List)",
     "Spam SSIDs from list.",
     false,
     NULL},
    {"< Beacon Spam (Random) >",
     "beaconspam -r\n",
     "Beacon Spam (Random)",
     "Spam random SSIDs.",
     false,
     NULL},
    {"< Beacon Spam (Rickroll) >",
     "beaconspam -rr\n",
     "Beacon Spam (Rickroll)",
     "Spam Rickroll SSIDs.",
     false,
     NULL},
    {"< Beacon Spam (Custom) >",
     "beaconspam",
     "Beacon Spam (Custom)",
     "Spam custom SSID.",
     true,
     "SSID Name"},
};

// BLE spam command definitions
static const CyclingMenuDef ble_spam_commands[] = {
    {"< BLE Spam (Apple) >",
     "blespam -apple\n",
     "BLE Spam (Apple)",
     "Spam Apple BLE devices.",
     false,
     NULL},
    {"< BLE Spam (Microsoft) >",
     "blespam -ms\n",
     "BLE Spam (Microsoft)",
     "Spam Microsoft BLE devices.",
     false,
     NULL},
    {"< BLE Spam (Samsung) >",
     "blespam -samsung\n",
     "BLE Spam (Samsung)",
     "Spam Samsung BLE devices.",
     false,
     NULL},
    {"< BLE Spam (Google) >",
     "blespam -google\n",
     "BLE Spam (Google)",
     "Spam Google BLE devices.",
     false,
     NULL},
    {"< BLE Spam (Random) >",
     "blespam -random\n",
     "BLE Spam (Random)",
     "Spam random BLE devices.",
     false,
     NULL},
};

static size_t current_rgb_index = 0;

static const CyclingMenuDef rgbmode_commands[] = {
    {"< LED: Rainbow >", "rgbmode rainbow\n", "LED: Rainbow", "Cycle rainbow colors.", false, NULL},
    {"< LED: Police >", "rgbmode police\n", "LED: Police", "Police light effect.", false, NULL},
    {"< LED: Strobe >", "rgbmode strobe\n", "LED: Strobe", "Strobe light effect.", false, NULL},
    {"< LED: Off >", "rgbmode off\n", "LED: Off", "Turn off LED.", false, NULL},
    {"< LED: Red >", "rgbmode red\n", "LED: Red", "Set LED to red.", false, NULL},
    {"< LED: Green >", "rgbmode green\n", "LED: Green", "Set LED to green.", false, NULL},
    {"< LED: Blue >", "rgbmode blue\n", "LED: Blue", "Set LED to blue.", false, NULL},
    {"< LED: Yellow >", "rgbmode yellow\n", "LED: Yellow", "Set LED to yellow.", false, NULL},
    {"< LED: Purple >", "rgbmode purple\n", "LED: Purple", "Set LED to purple.", false, NULL},
    {"< LED: Cyan >", "rgbmode cyan\n", "LED: Cyan", "Set LED to cyan.", false, NULL},
    {"< LED: Orange >", "rgbmode orange\n", "LED: Orange", "Set LED to orange.", false, NULL},
    {"< LED: White >", "rgbmode white\n", "LED: White", "Set LED to white.", false, NULL},
    {"< LED: Pink >", "rgbmode pink\n", "LED: Pink", "Set LED to pink.", false, NULL},
};

static const CyclingMenuDef wifi_scan_modes[] = {
    {"< Scan: (APs) >", "scanap\n", "WiFi AP Scanner", "Scans for WiFi APs...", false, NULL},
    {"< Scan: (APs Live) >",
     "scanap -live\n",
     "Live AP Scanner",
     "Continuously updates as APs are found\n- SSID names\n- Signal levels\n- "
     "Security type\n- Channel info\n",
     false,
     NULL},
    {"< Scan: (Stations) >", "scansta\n", "Station Scanner", "Scans for clients...", false, NULL},
    {"< Scan: (AP+STA) >", "scanall\n", "Scan All", "Combined AP/Station scan...", false, NULL},
};

static const CyclingMenuDef wifi_list_modes[] = {
    {"< List: (APs) >",
     "list -a\n",
     "List Access Points",
     "Shows list of APs found during last scan.",
     false,
     NULL},
    {"< List: (Stations) >",
     "list -s\n",
     "List Stations",
     "Shows list of clients found during last scan.",
     false,
     NULL},
};

static const CyclingMenuDef wifi_select_modes[] = {
    {"< Select: (AP) >",
     "select -a",
     "Select Access Point",
     "Select an AP by number from the scanned list.",
     true,
     "AP Number"},
    {"< Select: (Station) >",
     "select -s",
     "Select Station",
     "Target a station by number from the scan list for attacks.",
     true,
     "Station Number"},
};

static const CyclingMenuDef wifi_listen_modes[] = {
    {"< Listen Probes (Hop) >",
     "listenprobes\n",
     "Listen for Probes",
     "Listen for and log probe requests\nwhile hopping channels.",
     false,
     NULL},
    {"< Listen Probes (Chan) >",
     "listenprobes",
     "Listen on Channel",
     "Listen for probe requests on a\nspecific channel.",
     true,
     "Channel (1-165)"},
};

static size_t current_sniff_index = 0;
static size_t current_beacon_index = 0;
static size_t current_ble_spam_index = 0;
static size_t current_wifi_scan_index = 0;
static size_t current_wifi_list_index = 0;
static size_t current_wifi_select_index = 0;
static size_t current_wifi_listen_index = 0;

// WiFi menu command definitions
static const MenuCommand wifi_scanning_commands[] = {
    {
        .label = "< Scan: (APs) >", // Initial label
        .command = wifi_scan_modes[0].command,
        .details_header = wifi_scan_modes[0].details_header,
        .details_text = wifi_scan_modes[0].details_text,
    },
    {
        .label = "< List: (APs) >", // Initial label
        .command = wifi_list_modes[0].command,
        .details_header = wifi_list_modes[0].details_header,
        .details_text = wifi_list_modes[0].details_text,
    },
    {
        .label = "< Select: (AP) >", // Initial label
        .command = wifi_select_modes[0].command,
        .needs_input = wifi_select_modes[0].needs_input,
        .input_text = wifi_select_modes[0].input_text,
        .details_header = wifi_select_modes[0].details_header,
        .details_text = wifi_select_modes[0].details_text,
    },
    {
        .label = "< Listen Probes (Hop) >", // Initial label
        .command = wifi_listen_modes[0].command,
        .needs_input = wifi_listen_modes[0].needs_input,
        .input_text = wifi_listen_modes[0].input_text,
        .details_header = wifi_listen_modes[0].details_header,
        .details_text = wifi_listen_modes[0].details_text,
    },
    {
        .label = "Pineapple Detect",
        .command = "pineap\n",
        .details_header = "Pineapple Detection",
        .details_text = "Detects WiFi Pineapple devices\n",
    },
    {
        .label = "Channel Congestion",
        .command = "congestion\n",
        .details_header = "Channel Congestion",
        .details_text = "Display Wi-Fi channel\n"
                        "congestion chart.\n",
    },
    {
        .label = "Scan Ports",
        .command = "scanports",
        .needs_input = true,
        .input_text = "local or IP [options]",
        .details_header = "Port Scanner",
        .details_text = "Scan ports on local net\n"
                        "or specific IP.\n"
                        "Options: -C, -A, range\n"
                        "Ex: local -C\n"
                        "Ex: 192.168.1.1 80-1000",
    },
    {
        .label = "ARP Scan",
        .command = "scanarp\n",
        .details_header = "ARP Scan",
        .details_text = "Initiates an ARP scan on the local network to discover hosts:\n"
                        "- Sends ARP requests across the subnet\n"
                        "- Shows responding IP/MAC pairs\n"
                        "Requires WiFi connection.\n",
    },
    {
        .label = "Scan SSH",
        .command = "scanssh",
        .needs_input = true,
        .input_text = "IP",
        .details_header = "SSH Scan",
        .details_text = "Initiate an SSH port/service scan against the target IP:\n"
                        "- Provide an IP address (e.g., 192.168.1.10)\n"
                        "- Scans common SSH ports and reports responses\n"
                        "- Requires network connectivity\n\n",
    },
    {
        .label = "Stop Listen Probes",
        .command = "listenprobes stop\n",
        .details_header = "Stop Listening",
        .details_text = "Stops the probe listener.",
    },
    {
        .label = "Stop Scan",
        .command = "stopscan\n",
        .details_header = "Stop Scan",
        .details_text = "Stops AP or Station scan.",
    },
};

static const MenuCommand wifi_capture_commands[] = {
    {
        .label = "< Sniff WPS >",
        .command = "capture -wps\n",
        .capture_prefix = "wps_capture",
        .file_ext = "pcap",
        .folder = GHOST_ESP_APP_FOLDER_PCAPS,
        .details_header = "Variable Sniff",
        .details_text = "Use Left/Right to change:\n"
                        "- WPS traffic\n"
                        "- Raw packets\n"
                        "- Probe requests\n"
                        "- Deauth frames\n"
                        "- Beacon frames\n"
                        "- EAPOL/Handshakes\n",
    },
};

static const MenuCommand wifi_attack_commands[] = {
    {
        .label = "< Beacon Spam (List) >",
        .command = "beaconspam -l\n",
        .needs_input = false,
        .input_text = "SSID Name",
        .details_header = "Variable Beacon Spam",
        .details_text = "Use Left/Right to change:\n"
                        "- List mode\n"
                        "- Random names\n"
                        "- Rickroll mode\n"
                        "- Custom SSID\n"
                        "Range: ~50-100m\n",
    },
    {
        .label = "Deauth",
        .command = "attack -d\n",
        .details_header = "Deauth Attack",
        .details_text = "Sends deauth frames to\n"
                        "disconnect clients from\n"
                        "selected network.\n"
                        "Range: ~50-100m\n",
    },
    {
        .label = "EAPOL Logoff",
        .command = "attack -e\n",
        .details_header = "EAPOL Logoff Attack",
        .details_text = "Sends EAPOL logoff frames to\n"
                        "disconnect clients.",
    },
    {
        .label = "SAE Handshake Flood",
        .command = "saeflood\n",
        .details_header = "SAE Flood Attack",
        .details_text = "Floods WPA3 networks with\nSAE handshakes. Select a "
                        "WPA3 AP first.",
    },

    {
        .label = "DHCP Starve Start",
        .command = "dhcpstarve",
        .needs_input = true,
        .input_text = "start [threads]",
        .details_header = "DHCP Starve Attack",
        .details_text = "Exhausts DHCP server's IP pool.\n"
                        "Input: start [threads]\n"
                        "e.g., 'start' or 'start 5'",
    },
    {
        .label = "DHCP Starve Stop",
        .command = "dhcpstarve stop\n",
        .details_header = "Stop DHCP Starve",
        .details_text = "Stops the DHCP starvation attack.",
    },
    {
        .label = "Stop Deauth/SAE/EAPOL",
        .command = "stopdeauth\n",
        .details_header = "Stop Attacks",
        .details_text = "Stops Deauth, SAE Flood,\n"
                        "and EAPOL Logoff attacks.",
    },
    {
        .label = "Add SSID to Beacon List",
        .command = "beaconadd",
        .needs_input = true,
        .input_text = "SSID",
        .details_header = "Add to Beacon List",
        .details_text = "Add an SSID to the list used\n"
                        "by Beacon List Spam.",
    },
    {
        .label = "Remove SSID from Beacon List",
        .command = "beaconremove",
        .needs_input = true,
        .input_text = "SSID",
        .details_header = "Remove from Beacon List",
        .details_text = "Remove an SSID from the\n"
                        "beacon spam list.",
    },
    {
        .label = "Clear Beacon List",
        .command = "beaconclear\n",
        .details_header = "Clear Beacon List",
        .details_text = "Clears all SSIDs from the\n"
                        "beacon spam list.",
    },
    {
        .label = "Show Beacon List",
        .command = "beaconshow\n",
        .details_header = "Show Beacon List",
        .details_text = "Displays all SSIDs in the\n"
                        "beacon spam list.",
    },
    {
        .label = "Start Beacon List Spam",
        .command = "beaconspamlist\n",
        .details_header = "Beacon List Spam",
        .details_text = "Starts beacon spam using the\n"
                        "custom list of SSIDs.",
    },
    {
        .label = "Stop Beacon Spam",
        .command = "stopspam\n",
        .details_header = "Stop Beacon Spam",
        .details_text = "Stops any active beacon spam.",
    },
};

static const MenuCommand wifi_network_commands[] = {
    {
        .label = "Evil Portal",
        .command = "startportal",
        .needs_input = true,
        .input_text = "<filepath> <SSID> <PSK (leave blank for open)>",
        .details_header = "Evil Portal",
        .details_text = "Captive portal for\n"
                        "credential harvest.\n"
                        "Configure in WebUI:\n"
                        "- Portal settings\n"
                        "- Landing page\n",
    },
    {
        .label = "List Portals",
        .command = "listportals\n",
        .details_header = "List Portals",
        .details_text = "Show all available HTML portals\non the SD card.",
    },
    {
        .label = "Set Evil Portal HTML",
        .command = "set_evil_portal_html",
        .needs_input = true,
        .input_text = "HTML File",
        .details_header = "Set Evil Portal HTML",
        .details_text = "Select and send an HTML\n"
                        "file to the ESP32 for\n"
                        "the evil portal.\n\n",
    },
    {
        .label = "Connect To WiFi",
        .command = "connect",
        .needs_input = true,
        .input_text = "SSID",
        .details_header = "WiFi Connect",
        .details_text = "Connect ESP to WiFi:\n"
                        "Enter SSID followed by password.\n\n",
    },
    {
        .label = "Connect to Saved WiFi",
        .command = "connect\n",
        .details_header = "Connect (Saved)",
        .details_text = "Connect to the previously saved WiFi credentials on the ESP.\n"
                        "No input required.\n\n",
    },
    {
        .label = "Disconnect WiFi",
        .command = "disconnect\n",
        .details_header = "Disconnect",
        .details_text = "Disconnects from the current WiFi network on the ESP.\n"
                        "No input required.\n",
    },

    {
        .label = "Cast Random Video",
        .command = "dialconnect\n",
        .needs_confirmation = true,
        .confirm_header = "Cast Video",
        .confirm_text = "Make sure you've connected\nto WiFi first via "
                        "the\n'Connect to WiFi' option.\n",
        .details_header = "Video Cast",
        .details_text = "Casts random videos\n"
                        "to nearby Cast/DIAL\n"
                        "enabled devices.\n"
                        "Range: ~50m\n\n",
    },
    {
        .label = "Printer Power",
        .command = "powerprinter\n",
        .needs_confirmation = true,
        .confirm_header = "Printer Power",
        .confirm_text = "You need to configure\n settings in the WebUI\n for "
                        "this command.\n",
        .details_header = "WiFi Printer",
        .details_text = "Control power state\n"
                        "of network printers.\n"
                        "Configure in WebUI:\n"
                        "- Printer IP/Port\n"
                        "- Protocol type\n\n",
    },
    {
        .label = "Scan Local Network",
        .command = "scanlocal\n",
        .needs_confirmation = true,
        .confirm_header = "Local Network Scan",
        .confirm_text = "Make sure you've connected\nto WiFi first via "
                        "the\n'Connect to WiFi' option.\n",
        .details_header = "Network Scanner",
        .details_text = "Scans local network for:\n"
                        "- Printers\n"
                        "- Smart devices\n"
                        "- Cast devices\n"
                        "- Requires WiFi connection\n\n",
    },
    {
        .label = "Set WebUI Creds",
        .command = "apcred",
        .needs_input = true,
        .input_text = "MySSID MyPassword",
        .details_header = "Set AP Credentials",
        .details_text = "Set custom WebUI AP:\n"
                        "Format:\nMySSID MyPassword\n"
                        "Example: GhostNet,spooky123\n",
    },
    {
        .label = "Reset WebUI Creds",
        .command = "apcred -r\n",
        .needs_confirmation = true,
        .confirm_header = "Reset AP Credentials",
        .confirm_text = "Reset WebUI AP to\n"
                        "default credentials?\n"
                        "SSID: GhostNet\n"
                        "Password: GhostNet\n",
        .details_header = "Reset AP Credentials",
        .details_text = "Restores default WebUI AP:\n"
                        "SSID: GhostNet\n"
                        "Password: GhostNet\n"
                        "Requires ESP reboot\n",
    },
    {
        .label = "Stop Evil Portal",
        .command = "stopportal\n",
        .details_header = "Stop Evil Portal",
        .details_text = "Stops the Evil Portal.",
    },
    {
        .label = "TP-Link Smart Plug",
        .command = "tplinktest",
        .needs_input = true,
        .input_text = "on | off | loop",
        .details_header = "TP-Link Control",
        .details_text = "Control TP-Link smart plugs\n"
                        "on the local network.",
    },
};

static const MenuCommand wifi_settings_commands[] = {
    {
        .label = "< LED: Rainbow >",
        .command = "rgbmode rainbow\n",
        .confirm_header = "LED Effects",
        .details_header = "LED Effects",
        .details_text = "Control LED effects:\n"
                        "- rainbow, police, strobe, off, or fixed colors\n"
                        "Cycle with Left/Right to select an effect\n",
    },
    {
        .label = "Set RGB Pins",
        .command = "setrgbpins",
        .needs_input = true,
        .input_text = "<red> <green> <blue>",
        .details_header = "Set RGB Pins",
        .details_text = "Change RGB LED pins.\n"
                        "Requires restart.\n"
                        "Use same value for all\n"
                        "pins for single-pin LED.",
    },
    {.label = "Chip Info",
     .command = "chipinfo\n",
     .details_header = "Chip Info",
     .details_text = "Displays chip information from the ESP\n"},
    {
        .label = "Show SD Pin Config",
        .command = "sd_config",
        .details_header = "SD Pin Config",
        .details_text = "Show current SD GPIO\n"
                        "pin configuration for\n"
                        "MMC and SPI modes.",
    },
    {
        .label = "Set SD Pins (MMC)",
        .command = "sd_pins_mmc",
        .needs_input = true,
        .input_text = "<clk> <cmd> <d0..d3>",
        .details_header = "Set SD Pins (MMC)",
        .details_text = "Set GPIO pins for SDMMC.\n"
                        "Requires restart.\n"
                        "Only if firmware built\n"
                        "for SDMMC mode.",
    },
    {
        .label = "Set SD Pins (SPI)",
        .command = "sd_pins_spi",
        .needs_input = true,
        .input_text = "<cs> <clk> <miso> <mosi>",
        .details_header = "Set SD Pins (SPI)",
        .details_text = "Set GPIO pins for SPI.\n"
                        "Requires restart.\n"
                        "Only if firmware built\n"
                        "for SPI mode.",
    },
    {
        .label = "Save SD Pin Config",
        .command = "sd_save_config",
        .needs_confirmation = true,
        .confirm_header = "Save SD Config",
        .confirm_text = "Save current SD pin\n"
                        "config to SD card?\n"
                        "Requires SD mounted.",
        .details_header = "Save SD Pin Config",
        .details_text = "Save current SD pin\n"
                        "config (both modes) to\n"
                        "SD card (sd_config.conf).",
    },
    {
        .label = "Set Timezone",
        .command = "timezone",
        .needs_input = true,
        .input_text = "TZ String",
        .details_header = "Set Timezone",
        .details_text = "Set timezone for the clock.\n"
                        "e.g. 'EST5EDT,M3.2.0,M11.1.0'",
    },
    {
        .label = "Set Web Auth",
        .command = "webauth",
        .needs_input = true,
        .input_text = "on | off",
        .details_header = "Set Web Auth",
        .details_text = "Enable or disable Web\n"
                        "UI authentication.",
    },
    {
        .label = "Set WiFi Country",
        .command = "setcountry",
        .needs_input = true,
        .input_text = "Country Code (e.g. US)",
        .details_header = "Set WiFi Country",
        .details_text = "Set the WiFi country code.\n"
                        "May require ESP32-C5.",
    },
    {
        .label = "Show Help",
        .command = "help\n",
        .details_header = "Help",
        .details_text = "Show complete command list.",
    },
    {
        .label = "Reboot Device",
        .command = "reboot\n",
        .needs_confirmation = true,
        .confirm_header = "Reboot Device",
        .confirm_text = "Are you sure you want to reboot?",
        .details_header = "Reboot",
        .details_text = "Restart the ESP device.",
    },
    {
        .label = "Enable/Disable AP",
        .command = "apenable",
        .needs_input = true,
        .input_text = "on | off",
        .details_header = "AP Enable/Disable",
        .details_text = "Enable or disable the Access Point\nacross reboots.",
    },
    {
        .label = "Show Chip Info",
        .command = "chipinfo\n",
        .details_header = "Chip Info",
        .details_text = "Show chip and memory info.",
    },
};

static const MenuCommand wifi_stop_command = {
    .label = "Stop All WiFi",
    .command = "stop\n",
    .details_header = "Stop WiFi Operations",
    .details_text = "Stops all active WiFi\n"
                    "operations including:\n"
                    "- Scanning\n"
                    "- Beacon Spam\n"
                    "- Deauth Attacks\n"
                    "- Packet Captures\n"
                    "- Evil Portal\n",
};

// BLE menu command definitions
static const MenuCommand ble_scanning_commands[] = {
    {
        .label = "Skimmer Detection",
        .command = "capture -skimmer\n",
        .capture_prefix = "skimmer_scan",
        .file_ext = "pcap",
        .folder = GHOST_ESP_APP_FOLDER_PCAPS,
        .details_header = "Skimmer Scanner",
        .details_text = "Detects potential\n"
                        "card skimmers by\n"
                        "analyzing BLE\n"
                        "signatures and\n"
                        "known patterns.\n",
    },
    {
        .label = "Find the Flippers",
        .command = "blescan -f\n",
        .details_header = "Flipper Scanner",
        .details_text = "Scans for Flippers:\n"
                        "- Device name\n"
                        "- BT address\n"
                        "- Signal level\n"
                        "Range: ~50m\n",
    },
    {
        .label = "AirTag Scanner",
        .command = "blescan -a\n",
        .details_header = "AirTag Scanner",
        .details_text = "Detects nearby Apple\n"
                        "AirTags and shows:\n"
                        "- Device ID\n"
                        "- Signal strength\n"
                        "- Last seen time\n",
    },
    {
        .label = "List AirTags",
        .command = "listairtags\n",
        .details_header = "List AirTags",
        .details_text = "List discovered AirTags.",
    },
    {
        .label = "Select AirTag",
        .command = "select -airtag",
        .needs_input = true,
        .input_text = "AirTag Number",
        .details_header = "Select AirTag",
        .details_text = "Target an AirTag by number\nfrom the scan list.",
    },
    {
        .label = "List Flippers",
        .command = "listflippers\n",
        .details_header = "List Flippers",
        .details_text = "List discovered Flipper Devices\n"
                        "in range.",
    },
    {
        .label = "Select Flipper",
        .command = "selectflipper",
        .needs_input = true,
        .input_text = "Flipper Number",
        .details_header = "Select Flipper",
        .details_text = "Select a Flipper by number.",
    },
    {
        .label = "Detect BLE Spam",
        .command = "blescan -ds\n",
        .details_header = "BLE Spam Detection",
        .details_text = "Detects Bluetooth spam devices\nin the area.",
    },
    {
        .label = "View All BLE Traffic",
        .command = "blescan -r\n",
        .details_header = "BLE Raw Traffic",
        .details_text = "View all Bluetooth Low Energy\ntraffic in range.",
    },
    {
        .label = "Stop BLE Scanning",
        .command = "blescan -s\n",
        .details_header = "Stop BLE Scan",
        .details_text = "Stops any active BLE scanning.",
    },
};

static const MenuCommand ble_capture_commands[] = {
    {
        .label = "BLE Raw Capture",
        .command = "capture -ble\n",
        .capture_prefix = "ble_raw_capture",
        .file_ext = "pcap",
        .folder = GHOST_ESP_APP_FOLDER_PCAPS,
        .details_header = "BLE Raw Capture",
        .details_text = "Captures raw BLE\n"
                        "traffic and data.\n"
                        "Range: ~10-30m\n",
    },
};

static const MenuCommand ble_attack_commands[] = {
    {
        .label = "< BLE Spam (Apple) >",
        .command = "blespam -apple\n",
        .details_header = "Variable BLE Spam",
        .details_text = "Use Left/Right to change:\n"
                        "- Apple device spam\n"
                        "- Microsoft Swift Pair\n"
                        "- Samsung Galaxy Watch\n"
                        "- Google Fast Pair\n"
                        "- Random spam (all types)\n"
                        "Range: ~50m\n",
    },
    {
        .label = "Spoof Selected AirTag",
        .command = "spoofairtag\n",
        .details_header = "Spoof AirTag",
        .details_text = "Spoof the selected AirTag.",
    },
    {
        .label = "Stop BLE Spam",
        .command = "blespam -s\n",
        .details_header = "Stop BLE Spam",
        .details_text = "Stops BLE advertisement\n"
                        "spam attacks.",
    },
    {
        .label = "Stop AirTag Spoof",
        .command = "stopspoof\n",
        .details_header = "Stop Spoofing",
        .details_text = "Stops AirTag spoofing.",
    },
};

static const MenuCommand ble_stop_command = {
    .label = "Stop All BLE",
    .command = "stop\n",
    .details_header = "Stop BLE Operations",
    .details_text = "Stops all active BLE\n"
                    "operations including:\n"
                    "- BLE Scanning\n"
                    "- Skimmer Detection\n"
                    "- Packet Captures\n"
                    "- Device Detection\n",
};

// GPS menu command definitions
static const MenuCommand gps_commands[] = {
    {
        .label = "GPS Info",
        .command = "gpsinfo\n",
        .details_header = "GPS Information",
        .details_text = "Shows GPS details:\n"
                        "- Position (Lat/Long)\n"
                        "- Altitude & Speed\n"
                        "- Direction & Quality\n"
                        "- Satellite Status\n",
    },
    {
        .label = "Start Wardriving",
        .command = "startwd\n",
        .details_header = "Wardrive Mode",
        .details_text = "Maps WiFi networks:\n"
                        "- Network info\n"
                        "- GPS location\n"
                        "- Signal levels\n"
                        "Saves as CSV\n",
    },
    {
        .label = "BLE Wardriving",
        .command = "blewardriving\n",
        .details_header = "BLE Wardriving",
        .details_text = "Maps BLE devices:\n"
                        "- Device info\n"
                        "- GPS location\n"
                        "- Signal levels\n"
                        "Saves as CSV\n",
    },
    {
        .label = "Stop All GPS",
        .command = "stop\n",
        .details_header = "Stop GPS Operations",
        .details_text = "Stops all active GPS\n"
                        "operations including:\n"
                        "- GPS Info Updates\n"
                        "- WiFi Wardriving\n"
                        "- BLE Wardriving\n",
    },
};

static bool cycle_menu_item(
    CyclingMenuDef* cycling_array,
    size_t cycling_count,
    size_t* current_index,
    MenuCommand* menu_commands,
    size_t menu_index,
    Submenu* menu,
    InputEvent* event) {
    if(event->key == InputKeyRight) {
        *current_index = (*current_index + 1) % cycling_count;
    } else {
        *current_index = (*current_index == 0) ? (cycling_count - 1) : (*current_index - 1);
    }
    submenu_change_item_label(menu, menu_index, cycling_array[*current_index].label);

    // Update menu command fields
    MenuCommand* cmd = &menu_commands[menu_index];
    cmd->command = cycling_array[*current_index].command;
    cmd->needs_input = cycling_array[*current_index].needs_input;
    cmd->input_text = cycling_array[*current_index].input_text;
    cmd->details_header = cycling_array[*current_index].details_header;
    cmd->details_text = cycling_array[*current_index].details_text;

    return true;
}

void send_uart_command(const char* command, void* state) {
    AppState* app_state = (AppState*)state;
    uart_send(app_state->uart_context, (uint8_t*)command, strlen(command));
}

void send_uart_command_with_text(const char* command, char* text, AppState* state) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s %s\n", command, text);
    uart_send(state->uart_context, (uint8_t*)buffer, strlen(buffer));
}

void send_uart_command_with_bytes(
    const char* command,
    const uint8_t* bytes,
    size_t length,
    AppState* state) {
    send_uart_command(command, state);
    uart_send(state->uart_context, bytes, length);
}

static void confirmation_ok_callback(void* context) {
    MenuCommandContext* cmd_ctx = context;
    if(cmd_ctx && cmd_ctx->state && cmd_ctx->command) {
        bool file_opened = false;

        // Handle capture commands
        if(cmd_ctx->command->capture_prefix || cmd_ctx->command->file_ext ||
           cmd_ctx->command->folder) {
            FURI_LOG_I("Capture", "Attempting to open PCAP file before sending capture command.");
            file_opened = uart_receive_data(
                cmd_ctx->state->uart_context,
                cmd_ctx->state->view_dispatcher,
                cmd_ctx->state,
                cmd_ctx->command->capture_prefix ? cmd_ctx->command->capture_prefix : "",
                cmd_ctx->command->file_ext ? cmd_ctx->command->file_ext : "",
                cmd_ctx->command->folder ? cmd_ctx->command->folder : "");

            if(!file_opened) {
                FURI_LOG_E("Capture", "Failed to open PCAP file. Aborting capture command.");
                free(cmd_ctx);
                return;
            }

            // Send capture command
            send_uart_command(cmd_ctx->command->command, cmd_ctx->state);
            FURI_LOG_I("Capture", "Capture command sent to firmware.");
        } else {
            // For non-capture confirmation commands, send command and switch to text
            // view
            send_uart_command(cmd_ctx->command->command, cmd_ctx->state);
            uart_receive_data(
                cmd_ctx->state->uart_context,
                cmd_ctx->state->view_dispatcher,
                cmd_ctx->state,
                "",
                "",
                ""); // No capture files needed
        }
    }
    free(cmd_ctx);
}

static void confirmation_cancel_callback(void* context) {
    MenuCommandContext* cmd_ctx = context;
    if(cmd_ctx && cmd_ctx->state) {
        switch(cmd_ctx->state->previous_view) {
        case 1:
            show_wifi_menu(cmd_ctx->state);
            break;
        case 2:
            show_ble_menu(cmd_ctx->state);
            break;
        case 3:
            show_gps_menu(cmd_ctx->state);
            break;
        default:
            show_main_menu(cmd_ctx->state);
            break;
        }
    }
    free(cmd_ctx);
}

// Add at top with other declarations:
static void app_info_ok_callback(void* context) {
    AppState* state = context;
    if(!state) return;

    view_dispatcher_switch_to_view(state->view_dispatcher, state->previous_view);
    state->current_view = state->previous_view;
}

static void show_command_details(AppState* state, const MenuCommand* command) {
    if(!command->details_header || !command->details_text) return;

    // Save current view before switching
    state->previous_view = state->current_view;

    // Setup confirmation view to show details
    confirmation_view_set_header(state->confirmation_view, command->details_header);
    confirmation_view_set_text(state->confirmation_view, command->details_text);

    // Set up callbacks for OK/Cancel to return to previous view
    confirmation_view_set_ok_callback(
        state->confirmation_view,
        app_info_ok_callback, // Reuse app info callback since it does the same
        // thing
        state);
    confirmation_view_set_cancel_callback(state->confirmation_view, app_info_ok_callback, state);

    // Switch to confirmation view
    view_dispatcher_switch_to_view(state->view_dispatcher, 7);
    state->current_view = 7;
}

static void error_callback(void* context) {
    AppState* state = (AppState*)context;
    if(!state) return;
    view_dispatcher_switch_to_view(state->view_dispatcher, state->previous_view);
    state->current_view = state->previous_view;
}

// Text input callback implementation
static void text_input_result_callback(void* context) {
    AppState* input_state = (AppState*)context;
    if(input_state->connect_input_stage == 1) {
        size_t len = strlen(input_state->input_buffer);
        if(len >= sizeof(input_state->connect_ssid)) len = sizeof(input_state->connect_ssid) - 1;
        memcpy(input_state->connect_ssid, input_state->input_buffer, len);
        input_state->connect_ssid[len] = '\0';
        input_state->connect_input_stage = 2;
        if(input_state->input_buffer) memset(input_state->input_buffer, 0, INPUT_BUFFER_SIZE);
        text_input_reset(input_state->text_input);
        text_input_set_header_text(input_state->text_input, "PASSWORD");
        text_input_set_result_callback(
            input_state->text_input,
            text_input_result_callback,
            input_state,
            input_state->input_buffer,
            INPUT_BUFFER_SIZE,
            true);
        view_dispatcher_switch_to_view(input_state->view_dispatcher, 6);
        return;
    }
    if(input_state->connect_input_stage == 2) {
        char buffer[256];
        snprintf(
            buffer,
            sizeof(buffer),
            "connect \"%s\" \"%s\"\n",
            input_state->connect_ssid,
            input_state->input_buffer);
        uart_send(input_state->uart_context, (uint8_t*)buffer, strlen(buffer));
        input_state->connect_input_stage = 0;
        input_state->connect_ssid[0] = '\0';
    } else {
        send_uart_command_with_text(
            input_state->uart_command, input_state->input_buffer, input_state);
    }
    uart_receive_data(
        input_state->uart_context, input_state->view_dispatcher, input_state, "", "", "");
    if(input_state->input_buffer) memset(input_state->input_buffer, 0, INPUT_BUFFER_SIZE);
}

static void send_evil_portal_html(AppState* state) {
    uint8_t* the_html = NULL;
    size_t html_size = 0;

    if(ghost_esp_ep_read_html_file(state, &the_html, &html_size)) {
        if(the_html != NULL) {
            // Send the command first
            const char* command_str = "evilportal -c sethtmlstr\n";
            uart_send(state->uart_context, (const uint8_t*)command_str, strlen(command_str));

            // Begin HTML block
            const char* html_begin_marker = "[HTML/BEGIN]";
            uart_send(state->uart_context, (const uint8_t*)html_begin_marker, 12);

            // Send HTML content
            uart_send(state->uart_context, the_html, html_size);

            // End HTML block
            const char* html_close_marker = "[HTML/CLOSE]";
            uart_send(state->uart_context, (const uint8_t*)html_close_marker, 12);

            free(the_html);
        }
    }
}

static void execute_menu_command(AppState* state, const MenuCommand* command) {
    // Special handler for setting the evil portal HTML
    if(strcmp(command->command, "set_evil_portal_html") == 0) {
        send_evil_portal_html(state);
        return;
    }
    if(!uart_is_esp_connected(state->uart_context)) {
        state->previous_view = state->current_view;
        confirmation_view_set_header(state->confirmation_view, "Connection Error");
        confirmation_view_set_text(
            state->confirmation_view,
            "No response from ESP!\nIs a command running?\nRestart the "
            "app.\nRestart ESP.\nCheck UART Pins.\nReflash if issues persist.\nYou "
            "can disable this check in the settings menu.\n\n");
        confirmation_view_set_ok_callback(state->confirmation_view, error_callback, state);
        confirmation_view_set_cancel_callback(state->confirmation_view, error_callback, state);

        view_dispatcher_switch_to_view(state->view_dispatcher, 7);
        state->current_view = 7;
        return;
    }

    if(command->needs_input && strcmp(command->command, "connect") == 0) {
        state->connect_input_stage = 1;
        state->uart_command = command->command;
        state->previous_view = state->current_view;
        text_input_reset(state->text_input);
        if(state->input_buffer) memset(state->input_buffer, 0, INPUT_BUFFER_SIZE);
        text_input_set_header_text(state->text_input, "SSID");
        text_input_set_result_callback(
            state->text_input,
            text_input_result_callback,
            state,
            state->input_buffer,
            INPUT_BUFFER_SIZE,
            true);
        view_dispatcher_switch_to_view(state->view_dispatcher, 6);
        state->current_view = 6;
        return;
    }

    // For commands needing input
    if(command->needs_input) {
        state->uart_command = command->command;
        state->previous_view = state->current_view;
        text_input_reset(state->text_input);
        if(state->input_buffer) memset(state->input_buffer, 0, INPUT_BUFFER_SIZE);
        text_input_set_header_text(state->text_input, command->input_text);
        text_input_set_result_callback(
            state->text_input,
            text_input_result_callback,
            state,
            state->input_buffer,
            INPUT_BUFFER_SIZE,
            true);
        view_dispatcher_switch_to_view(state->view_dispatcher, 6);
        state->current_view = 6;
        return;
    }

    // For commands needing confirmation
    if(command->needs_confirmation) {
        MenuCommandContext* cmd_ctx = malloc(sizeof(MenuCommandContext));
        cmd_ctx->state = state;
        cmd_ctx->command = command;
        confirmation_view_set_header(state->confirmation_view, command->confirm_header);
        confirmation_view_set_text(state->confirmation_view, command->confirm_text);
        confirmation_view_set_ok_callback(
            state->confirmation_view, confirmation_ok_callback, cmd_ctx);
        confirmation_view_set_cancel_callback(
            state->confirmation_view, confirmation_cancel_callback, cmd_ctx);

        view_dispatcher_switch_to_view(state->view_dispatcher, 7);
        return;
    }

    // Handle variable sniff command
    if(state->current_view == 11 && state->current_index == 0) {
        const SniffCommandDef* current_sniff = &sniff_commands[current_sniff_index];
        // Handle capture commands
        if(current_sniff->capture_prefix) {
            // Save current view for proper back navigation
            state->previous_view = state->current_view;
            bool file_opened = uart_receive_data(
                state->uart_context,
                state->view_dispatcher,
                state,
                current_sniff->capture_prefix,
                "pcap",
                GHOST_ESP_APP_FOLDER_PCAPS);

            if(!file_opened) {
                FURI_LOG_E("Capture", "Failed to open capture file");
                return;
            }

            furi_delay_ms(10);
            send_uart_command(current_sniff->command, state);
            state->current_view = 5;
            return;
        }

        // Save view and show terminal log
        state->previous_view = state->current_view;
        uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
        state->current_view = 5;

        furi_delay_ms(5);
        send_uart_command(current_sniff->command, state);
        return;
    }

    // Handle variable beacon spam command
    if(state->current_view == 12 && state->current_index == 0) {
        const CyclingMenuDef* current_beacon = &beacon_spam_commands[current_beacon_index];

        // If it's custom mode (last index), handle text input
        if(current_beacon_index == COUNT_OF(beacon_spam_commands) - 1) {
            state->uart_command = current_beacon->command;
            // Save current view for proper back navigation
            state->previous_view = state->current_view;
            text_input_reset(state->text_input);
            text_input_set_header_text(state->text_input, "SSID Name");
            text_input_set_result_callback(
                state->text_input,
                text_input_result_callback,
                state,
                state->input_buffer,
                INPUT_BUFFER_SIZE,
                true);
            view_dispatcher_switch_to_view(state->view_dispatcher, 6);
            state->current_view = 6;
            return;
        }

        // Save view and show terminal log
        state->previous_view = state->current_view;
        uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
        state->current_view = 5;
        furi_delay_ms(5);
        send_uart_command(current_beacon->command, state);
        return;
    }

    // Handle variable rgbmode command (new branch for index 17)
    if(state->current_view == 14 && state->current_index == 0) {
        const CyclingMenuDef* current_rgb = &rgbmode_commands[current_rgb_index];
        // Save view and show terminal log
        state->previous_view = state->current_view;
        uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
        state->current_view = 5;
        furi_delay_ms(5);
        send_uart_command(current_rgb->command, state);
        return;
    }

    // Handle variable BLE spam command
    if(state->current_view == 22 && state->current_index == 0) {
        const CyclingMenuDef* current_ble_spam = &ble_spam_commands[current_ble_spam_index];
        // Save view and show terminal log
        state->previous_view = state->current_view;
        uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
        state->current_view = 5;
        furi_delay_ms(5);
        send_uart_command(current_ble_spam->command, state);
        return;
    }

    // Handle capture commands
    if(command->capture_prefix || command->file_ext || command->folder) {
        // Save view and show terminal log
        state->previous_view = state->current_view;
        bool file_opened = uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            command->capture_prefix ? command->capture_prefix : "",
            command->file_ext ? command->file_ext : "",
            command->folder ? command->folder : "");

        if(!file_opened) {
            FURI_LOG_E("Capture", "Failed to open capture file");
            return;
        }

        furi_delay_ms(10);
        send_uart_command(command->command, state);
        state->current_view = 5;
        return;
    }

    // Save view and show terminal log
    state->previous_view = state->current_view;
    uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");

    furi_delay_ms(5);
    send_uart_command(command->command, state);
}

// Menu display function implementation
static void show_menu(
    AppState* state,
    const MenuCommand* commands,
    size_t command_count,
    const char* header,
    Submenu* menu,
    uint8_t view_id) {
    submenu_reset(menu);
    submenu_set_header(menu, header);

    for(size_t i = 0; i < command_count; i++) {
        submenu_add_item(menu, commands[i].label, i, submenu_callback, state);
    }

    // Set up view with input handler
    View* menu_view = submenu_get_view(menu);
    view_set_context(menu_view, state);
    view_set_input_callback(menu_view, menu_input_handler);

    // Restore last selection based on menu type
    uint32_t last_index = 0;
    switch(view_id) {
    case 1: // WiFi categories
        last_index = state->last_wifi_category_index;
        break;
    case 10: // WiFi Scanning
        last_index = state->last_wifi_scanning_index;
        break;
    case 11: // WiFi Capture
        last_index = state->last_wifi_capture_index;
        break;
    case 12: // WiFi Attack
        last_index = state->last_wifi_attack_index;
        break;
    case 13: // WiFi Network
        last_index = state->last_wifi_network_index;
        break;
    case 14: // WiFi Settings
        last_index = state->last_wifi_settings_index;
        break;
    case 2: // BLE categories
        last_index = state->last_ble_category_index;
        break;
    case 20: // BLE Scanning
        last_index = state->last_ble_scanning_index;
        break;
    case 21: // BLE Capture
        last_index = state->last_ble_capture_index;
        break;
    case 22: // BLE Attack
        last_index = state->last_ble_attack_index;
        break;
    case 3: // GPS
        last_index = state->last_gps_index;
        break;
    }
    if(last_index < command_count) {
        submenu_set_selected_item(menu, last_index);
    }

    view_dispatcher_switch_to_view(state->view_dispatcher, view_id);
    state->current_view = view_id;
    state->previous_view = view_id;
}

// Menu display functions
void show_wifi_scanning_menu(AppState* state) {
    show_menu(
        state,
        wifi_scanning_commands,
        COUNT_OF(wifi_scanning_commands),
        "Scanning & Probing",
        state->wifi_scanning_menu,
        10);
}

void show_wifi_capture_menu(AppState* state) {
    show_menu(
        state,
        wifi_capture_commands,
        COUNT_OF(wifi_capture_commands),
        "Packet Capture",
        state->wifi_capture_menu,
        11);
}

void show_wifi_attack_menu(AppState* state) {
    show_menu(
        state,
        wifi_attack_commands,
        COUNT_OF(wifi_attack_commands),
        "Attacks",
        state->wifi_attack_menu,
        12);
}

void show_wifi_network_menu(AppState* state) {
    show_menu(
        state,
        wifi_network_commands,
        COUNT_OF(wifi_network_commands),
        "Portal & Network",
        state->wifi_network_menu,
        13);
}

void show_wifi_settings_menu(AppState* state) {
    show_menu(
        state,
        wifi_settings_commands,
        COUNT_OF(wifi_settings_commands),
        "Settings & Hardware",
        state->wifi_settings_menu,
        14);
}

void show_ble_scanning_menu(AppState* state) {
    show_menu(
        state,
        ble_scanning_commands,
        COUNT_OF(ble_scanning_commands),
        "Scanning & Detection",
        state->ble_scanning_menu,
        20);
}

void show_ble_capture_menu(AppState* state) {
    show_menu(
        state,
        ble_capture_commands,
        COUNT_OF(ble_capture_commands),
        "Packet Capture",
        state->ble_capture_menu,
        21);
}

void show_ble_attack_menu(AppState* state) {
    show_menu(
        state,
        ble_attack_commands,
        COUNT_OF(ble_attack_commands),
        "Attacks & Spoofing",
        state->ble_attack_menu,
        22);
}

void show_wifi_menu(AppState* state) {
    submenu_reset(state->wifi_menu);
    submenu_set_header(state->wifi_menu, "WiFi Commands");
    submenu_add_item(state->wifi_menu, "Scanning & Probing > ", 0, submenu_callback, state);
    submenu_add_item(state->wifi_menu, "Packet Capture > ", 1, submenu_callback, state);
    submenu_add_item(state->wifi_menu, "Attacks > ", 2, submenu_callback, state);
    submenu_add_item(state->wifi_menu, "Evil Portal & Network >", 3, submenu_callback, state);
    submenu_add_item(state->wifi_menu, wifi_stop_command.label, 4, submenu_callback, state);
    // Restore last selected WiFi category
    submenu_set_selected_item(state->wifi_menu, state->last_wifi_category_index);

    view_dispatcher_switch_to_view(state->view_dispatcher, 1);
    state->current_view = 1;
}

void show_ble_menu(AppState* state) {
    submenu_reset(state->ble_menu);
    submenu_set_header(state->ble_menu, "BLE Commands");
    submenu_add_item(state->ble_menu, "Scanning & Detection >", 0, submenu_callback, state);
    submenu_add_item(state->ble_menu, "Packet Capture >", 1, submenu_callback, state);
    submenu_add_item(state->ble_menu, "Attacks & Spoofing >", 2, submenu_callback, state);
    submenu_add_item(state->ble_menu, ble_stop_command.label, 3, submenu_callback, state);
    // Restore last selected BLE category
    submenu_set_selected_item(state->ble_menu, state->last_ble_category_index);

    view_dispatcher_switch_to_view(state->view_dispatcher, 2);
    state->current_view = 2;
}

void show_gps_menu(AppState* state) {
    state->came_from_settings = false;
    show_menu(state, gps_commands, COUNT_OF(gps_commands), "GPS Commands:", state->gps_menu, 3);
}

// Menu command handlers
void handle_wifi_menu(AppState* state, uint32_t index) {
    // This function is now for sub-category menus
    const MenuCommand* command = NULL;
    switch(state->current_view) {
    case 10: // Scanning
        if(index < COUNT_OF(wifi_scanning_commands)) {
            command = &wifi_scanning_commands[index];
            state->last_wifi_scanning_index = index;
        }
        break;
    case 11: // Capture
        if(index < COUNT_OF(wifi_capture_commands)) {
            command = &wifi_capture_commands[index];
            state->last_wifi_capture_index = index;
        }
        break;
    case 12: // Attack
        if(index < COUNT_OF(wifi_attack_commands)) {
            command = &wifi_attack_commands[index];
            state->last_wifi_attack_index = index;
        }
        break;
    case 13: // Network
        if(index < COUNT_OF(wifi_network_commands)) {
            command = &wifi_network_commands[index];
            state->last_wifi_network_index = index;
        }
        break;
    case 14: // Settings
        if(index < COUNT_OF(wifi_settings_commands)) {
            command = &wifi_settings_commands[index];
            state->last_wifi_settings_index = index;
        }
        break;
    }

    if(command) {
        execute_menu_command(state, command);
    }
}

void handle_ble_menu(AppState* state, uint32_t index) {
    // This function is now for sub-category menus
    const MenuCommand* command = NULL;
    switch(state->current_view) {
    case 20: // Scanning
        if(index < COUNT_OF(ble_scanning_commands)) {
            command = &ble_scanning_commands[index];
            state->last_ble_scanning_index = index;
        }
        break;
    case 21: // Capture
        if(index < COUNT_OF(ble_capture_commands)) {
            command = &ble_capture_commands[index];
            state->last_ble_capture_index = index;
        }
        break;
    case 22: // Attack
        if(index < COUNT_OF(ble_attack_commands)) {
            command = &ble_attack_commands[index];
            state->last_ble_attack_index = index;
        }
        break;
    }

    if(command) {
        execute_menu_command(state, command);
    }
}

void handle_gps_menu(AppState* state, uint32_t index) {
    if(index < COUNT_OF(gps_commands)) {
        state->last_gps_index = index; // Save the selection
        execute_menu_command(state, &gps_commands[index]);
    }
}

void submenu_callback(void* context, uint32_t index) {
    AppState* state = (AppState*)context;
    state->current_index = index; // Track current selection

    switch(state->current_view) {
    case 0: // Main Menu
        switch(index) {
        case 0:
            show_wifi_menu(state);
            break;
        case 1:
            show_ble_menu(state);
            break;
        case 2:
            show_gps_menu(state);
            break;
        case 3: // Settings button
            view_dispatcher_switch_to_view(state->view_dispatcher, 8);
            state->current_view = 8;
            state->previous_view = 8;
            break;
        }
        break;
    case 1: // WiFi Categories
        // Save selected category
        state->last_wifi_category_index = index;
        switch(index) {
        case 0:
            show_wifi_scanning_menu(state);
            break;
        case 1:
            show_wifi_capture_menu(state);
            break;
        case 2:
            show_wifi_attack_menu(state);
            break;
        case 3:
            show_wifi_network_menu(state);
            break;
        case 4:
            execute_menu_command(state, &wifi_stop_command);
            break;
        }
        break;
    case 2: // BLE Categories
        // Save selected category
        state->last_ble_category_index = index;
        switch(index) {
        case 0:
            show_ble_scanning_menu(state);
            break;
        case 1:
            show_ble_capture_menu(state);
            break;
        case 2:
            show_ble_attack_menu(state);
            break;
        case 3:
            execute_menu_command(state, &ble_stop_command);
            break;
        }
        break;
    case 3:
        handle_gps_menu(state, index);
        break;
    case 20:
    case 21:
    case 22:
        handle_ble_menu(state, index);
        break;
    }
}

static void show_menu_help(void* context, uint32_t index) {
    UNUSED(index);
    AppState* state = context;

    // Save current view
    state->previous_view = state->current_view;

    // Define help text with essential actions only
    const char* help_text = "=== Controls ===\n"
                            "Hold [Ok]\n"
                            "    Show command details\n"
                            "Back button returns to\n"
                            "previous menu\n"
                            "\n"
                            "=== File Locations ===\n"
                            "/apps_data/ghost_esp/\n"
                            "\n"
                            "\n"
                            "=== Tips ===\n"
                            "- One capture at a time\n"
                            "- Hold OK on any command\n"
                            "  to see range & details\n"
                            "\n"
                            "=== Settings ===\n"
                            "Configure options in\n"
                            "SET menu including:\n"
                            "- Auto-stop behavior\n"
                            "- LED settings\n"
                            "\n"
                            "Join the Discord\n"
                            "for support and\n"
                            "to stay updated!\n";

    // Set header and help text in the confirmation view
    confirmation_view_set_header(state->confirmation_view, "Quick Help");
    confirmation_view_set_text(state->confirmation_view, help_text);

    // Set callbacks for user actions
    confirmation_view_set_ok_callback(state->confirmation_view, app_info_ok_callback, state);
    confirmation_view_set_cancel_callback(state->confirmation_view, app_info_ok_callback, state);

    // Switch to confirmation view to display help
    view_dispatcher_switch_to_view(state->view_dispatcher, 7);
    state->current_view = 7;
}

bool back_event_callback(void* context) {
    AppState* state = (AppState*)context;
    if(!state) return false;

    uint32_t current_view = state->current_view;

    // Allow confirmation view to handle its own back button
    if(current_view == 7) {
        return false;
    }

    // Handle text box view (view 5)
    if(current_view == 5) {
        // send stop on exit if enabled
        if(state->settings.stop_on_back_index) {
            send_uart_command(wifi_stop_command.command, state);
        }
        FURI_LOG_D("Ghost ESP", "Handling text box view exit");

        // Cleanup text buffer
        if(state->textBoxBuffer) {
            free(state->textBoxBuffer);
            state->textBoxBuffer = malloc(1);
            if(state->textBoxBuffer) {
                state->textBoxBuffer[0] = '\0';
            }
            state->buffer_length = 0;
        }

        // Return to previous menu with selection restored
        if(state->previous_view == 8 || state->previous_view == 4) {
            // if we came from settings or configuration view, go back there
            view_dispatcher_switch_to_view(state->view_dispatcher, state->previous_view);
            state->current_view = state->previous_view;
        } else {
            switch(state->previous_view) {
            case 1:
                show_wifi_menu(state);
                submenu_set_selected_item(state->wifi_menu, state->last_wifi_category_index);
                break;
            case 10:
                show_wifi_scanning_menu(state);
                submenu_set_selected_item(
                    state->wifi_scanning_menu, state->last_wifi_scanning_index);
                break;
            case 11:
                show_wifi_capture_menu(state);
                submenu_set_selected_item(
                    state->wifi_capture_menu, state->last_wifi_capture_index);
                break;
            case 12:
                show_wifi_attack_menu(state);
                submenu_set_selected_item(state->wifi_attack_menu, state->last_wifi_attack_index);
                break;
            case 13:
                show_wifi_network_menu(state);
                submenu_set_selected_item(
                    state->wifi_network_menu, state->last_wifi_network_index);
                break;
            case 14:
                show_wifi_settings_menu(state);
                submenu_set_selected_item(
                    state->wifi_settings_menu, state->last_wifi_settings_index);
                break;
            case 2:
                show_ble_menu(state);
                submenu_set_selected_item(state->ble_menu, state->last_ble_category_index);
                break;
            case 20:
                show_ble_scanning_menu(state);
                submenu_set_selected_item(
                    state->ble_scanning_menu, state->last_ble_scanning_index);
                break;
            case 21:
                show_ble_capture_menu(state);
                submenu_set_selected_item(state->ble_capture_menu, state->last_ble_capture_index);
                break;
            case 22:
                show_ble_attack_menu(state);
                submenu_set_selected_item(state->ble_attack_menu, state->last_ble_attack_index);
                break;
            case 3:
                show_gps_menu(state);
                submenu_set_selected_item(state->gps_menu, state->last_gps_index);
                break;
            default:
                show_main_menu(state);
                break;
            }
        }
        // do not overwrite previous_view here to preserve original navigation
        // context
    }
    // Handle settings menu (view 8)
    else if(current_view == 8) {
        show_main_menu(state);
        state->current_view = 0;
    }
    // Handle settings submenu (view 4)
    else if(current_view == 4) {
        view_dispatcher_switch_to_view(state->view_dispatcher, 8);
        state->current_view = 8;
    }
    // Handle submenu views (1-3)
    else if(current_view >= 1 && current_view <= 3) {
        show_main_menu(state);
        state->current_view = 0;
    }
    // Handle WiFi sub-category menus
    else if(current_view >= 10 && current_view <= 14) {
        if(state->came_from_settings) {
            // came from settings hardware menu; return to settings actions
            view_dispatcher_switch_to_view(state->view_dispatcher, 8);
            state->current_view = 8;
        } else {
            show_wifi_menu(state);
            submenu_set_selected_item(state->wifi_menu, state->last_wifi_category_index);
            state->current_view = 1;
        }
    }
    // Handle BLE sub-category menus
    else if(current_view >= 20 && current_view <= 22) {
        show_ble_menu(state);
        submenu_set_selected_item(state->ble_menu, state->last_ble_category_index);
        state->current_view = 2;
    }
    // Handle text input view (view 6)
    else if(current_view == 6) {
        // send stop on exit if enabled
        if(state->settings.stop_on_back_index) {
            send_uart_command(wifi_stop_command.command, state);
        }
        // Clear any command setup state
        state->uart_command = NULL;
        state->connect_input_stage = 0;
        state->connect_ssid[0] = '\0';
        if(state->text_input) text_input_reset(state->text_input);
        if(state->input_buffer) memset(state->input_buffer, 0, INPUT_BUFFER_SIZE);

        switch(state->previous_view) {
        case 1:
            show_wifi_menu(state);
            submenu_set_selected_item(state->wifi_menu, state->last_wifi_category_index);
            break;
        case 10:
            show_wifi_scanning_menu(state);
            submenu_set_selected_item(state->wifi_scanning_menu, state->last_wifi_scanning_index);
            break;
        case 11:
            show_wifi_capture_menu(state);
            submenu_set_selected_item(state->wifi_capture_menu, state->last_wifi_capture_index);
            break;
        case 12:
            show_wifi_attack_menu(state);
            submenu_set_selected_item(state->wifi_attack_menu, state->last_wifi_attack_index);
            break;
        case 13:
            show_wifi_network_menu(state);
            submenu_set_selected_item(state->wifi_network_menu, state->last_wifi_network_index);
            break;
        case 14:
            show_wifi_settings_menu(state);
            submenu_set_selected_item(state->wifi_settings_menu, state->last_wifi_settings_index);
            break;
        case 2:
            show_ble_menu(state);
            submenu_set_selected_item(state->ble_menu, state->last_ble_category_index);
            break;
        case 20:
            show_ble_scanning_menu(state);
            submenu_set_selected_item(state->ble_scanning_menu, state->last_ble_scanning_index);
            break;
        case 21:
            show_ble_capture_menu(state);
            submenu_set_selected_item(state->ble_capture_menu, state->last_ble_capture_index);
            break;
        case 22:
            show_ble_attack_menu(state);
            submenu_set_selected_item(state->ble_attack_menu, state->last_ble_attack_index);
            break;
        case 3:
            show_gps_menu(state);
            submenu_set_selected_item(state->gps_menu, state->last_gps_index);
            break;
        default:
            show_main_menu(state);
            break;
        }

        // do not overwrite previous_view here to preserve original navigation
        // context
    }
    // Handle main menu (view 0)
    else if(current_view == 0) {
        view_dispatcher_stop(state->view_dispatcher);
    }

    return true;
}

void show_main_menu(AppState* state) {
    main_menu_reset(state->main_menu);
    main_menu_set_header(state->main_menu, "");
    main_menu_add_item(state->main_menu, "WiFi", 0, submenu_callback, state);
    main_menu_add_item(state->main_menu, "BLE", 1, submenu_callback, state);
    main_menu_add_item(state->main_menu, "GPS", 2, submenu_callback, state);
    main_menu_add_item(state->main_menu, " SET", 3, submenu_callback, state);

    // Set up help callback
    main_menu_set_help_callback(state->main_menu, show_menu_help, state);

    state->came_from_settings = false;
    view_dispatcher_switch_to_view(state->view_dispatcher, 0);
    state->current_view = 0;
}

static bool menu_input_handler(InputEvent* event, void* context) {
    AppState* state = (AppState*)context;
    bool consumed = false;

    if(!state || !event) return false;

    const MenuCommand* commands = NULL;
    size_t commands_count = 0;
    Submenu* current_menu = NULL;

    // Determine current menu context
    switch(state->current_view) {
    case 1:
        current_menu = state->wifi_menu;
        // No commands here, just categories
        return false;
    case 2:
        current_menu = state->ble_menu;
        // No commands here, just categories
        return false;
    case 20:
        current_menu = state->ble_scanning_menu;
        commands = ble_scanning_commands;
        commands_count = COUNT_OF(ble_scanning_commands);
        break;
    case 21:
        current_menu = state->ble_capture_menu;
        commands = ble_capture_commands;
        commands_count = COUNT_OF(ble_capture_commands);
        break;
    case 22:
        current_menu = state->ble_attack_menu;
        commands = ble_attack_commands;
        commands_count = COUNT_OF(ble_attack_commands);
        break;
    case 3:
        current_menu = state->gps_menu;
        commands = gps_commands;
        commands_count = COUNT_OF(gps_commands);
        break;
    case 10:
        current_menu = state->wifi_scanning_menu;
        commands = wifi_scanning_commands;
        commands_count = COUNT_OF(wifi_scanning_commands);
        break;
    case 11:
        current_menu = state->wifi_capture_menu;
        commands = wifi_capture_commands;
        commands_count = COUNT_OF(wifi_capture_commands);
        break;
    case 12:
        current_menu = state->wifi_attack_menu;
        commands = wifi_attack_commands;
        commands_count = COUNT_OF(wifi_attack_commands);
        break;
    case 13:
        current_menu = state->wifi_network_menu;
        commands = wifi_network_commands;
        commands_count = COUNT_OF(wifi_network_commands);
        break;
    case 14:
        current_menu = state->wifi_settings_menu;
        commands = wifi_settings_commands;
        commands_count = COUNT_OF(wifi_settings_commands);
        break;
    default:
        return false;
    }

    if(!current_menu || !commands) return false;

    uint32_t current_index = submenu_get_selected_item(current_menu);

    switch(event->type) {
    case InputTypeShort:
        switch(event->key) {
        case InputKeyUp:
            if(current_index > 0) {
                submenu_set_selected_item(current_menu, current_index - 1);
            } else {
                // Wrap to bottom
                submenu_set_selected_item(current_menu, commands_count - 1);
            }
            consumed = true;
            break;

        case InputKeyDown:
            if(current_index < commands_count - 1) {
                submenu_set_selected_item(current_menu, current_index + 1);
            } else {
                // Wrap to top
                submenu_set_selected_item(current_menu, 0);
            }
            consumed = true;
            break;

        case InputKeyOk:
            if(current_index < commands_count) {
                state->current_index = current_index;
                // Save last selection for proper restore on exit
                if(state->current_view >= 10 && state->current_view <= 14) {
                    switch(state->current_view) {
                    case 10:
                        state->last_wifi_scanning_index = current_index;
                        break;
                    case 11:
                        state->last_wifi_capture_index = current_index;
                        break;
                    case 12:
                        state->last_wifi_attack_index = current_index;
                        break;
                    case 13:
                        state->last_wifi_network_index = current_index;
                        break;
                    case 14:
                        state->last_wifi_settings_index = current_index;
                        break;
                    }
                } else if(state->current_view >= 20 && state->current_view <= 22) {
                    switch(state->current_view) {
                    case 20:
                        state->last_ble_scanning_index = current_index;
                        break;
                    case 21:
                        state->last_ble_capture_index = current_index;
                        break;
                    case 22:
                        state->last_ble_attack_index = current_index;
                        break;
                    }
                } else if(state->current_view == 3) {
                    state->last_gps_index = current_index;
                }
                execute_menu_command(state, &commands[current_index]);
                consumed = true;
            }
            break;

        case InputKeyBack:
            // Back from WiFi subcategory menus returns to WiFi categories
            if(state->current_view >= 10 && state->current_view <= 14) {
                if(state->came_from_settings) {
                    // came from settings hardware menu; return to settings actions
                    view_dispatcher_switch_to_view(state->view_dispatcher, 8);
                    state->current_view = 8;
                } else {
                    show_wifi_menu(state);
                    submenu_set_selected_item(state->wifi_menu, state->last_wifi_category_index);
                    state->current_view = 1;
                }
            }
            // Back from BLE subcategory menus returns to BLE categories
            else if(state->current_view >= 20 && state->current_view <= 22) {
                show_ble_menu(state);
                submenu_set_selected_item(state->ble_menu, state->last_ble_category_index);
                state->current_view = 2;
            } else if(state->current_view >= 1 && state->current_view <= 3) {
                // Back from a top-level menu returns to main menu
                show_main_menu(state);
                state->current_view = 0;
            }
            consumed = true;
            break;

        case InputKeyRight:
        case InputKeyLeft:
            // Handle sniff command cycling
            if(state->current_view == 11 && current_index == 0) {
                // sniff_commands is not CyclingMenuDef, so keep legacy logic for now
                if(event->key == InputKeyRight) {
                    current_sniff_index = (current_sniff_index + 1) % COUNT_OF(sniff_commands);
                } else {
                    current_sniff_index = (current_sniff_index == 0) ?
                                              (size_t)(COUNT_OF(sniff_commands) - 1) :
                                              (current_sniff_index - 1);
                }
                submenu_change_item_label(
                    current_menu, current_index, sniff_commands[current_sniff_index].label);
                consumed = true;
            }
            // Handle beacon spam command cycling
            else if(state->current_view == 12 && current_index == 0) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)beacon_spam_commands,
                    COUNT_OF(beacon_spam_commands),
                    &current_beacon_index,
                    (MenuCommand*)wifi_attack_commands,
                    0,
                    current_menu,
                    event);
            }
            // Handle rgbmode command cycling (new branch for index 17)
            else if(state->current_view == 14 && current_index == 0) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)rgbmode_commands,
                    COUNT_OF(rgbmode_commands),
                    &current_rgb_index,
                    (MenuCommand*)wifi_settings_commands,
                    0,
                    current_menu,
                    event);
            }
            // Handle BLE spam command cycling
            else if(state->current_view == 22 && current_index == 0) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)ble_spam_commands,
                    COUNT_OF(ble_spam_commands),
                    &current_ble_spam_index,
                    (MenuCommand*)ble_attack_commands,
                    0,
                    current_menu,
                    event);
            }
            // Handle WiFi scan mode cycling
            else if(state->current_view == 10 && current_index == 0) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)wifi_scan_modes,
                    COUNT_OF(wifi_scan_modes),
                    &current_wifi_scan_index,
                    (MenuCommand*)wifi_scanning_commands,
                    0,
                    current_menu,
                    event);
            }
            // List mode cycling
            else if(state->current_view == 10 && current_index == 1) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)wifi_list_modes,
                    COUNT_OF(wifi_list_modes),
                    &current_wifi_list_index,
                    (MenuCommand*)wifi_scanning_commands,
                    1,
                    current_menu,
                    event);
            }
            // Select mode cycling
            else if(state->current_view == 10 && current_index == 2) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)wifi_select_modes,
                    COUNT_OF(wifi_select_modes),
                    &current_wifi_select_index,
                    (MenuCommand*)wifi_scanning_commands,
                    2,
                    current_menu,
                    event);
            }
            // Handle listen mode cycling
            else if(state->current_view == 10 && current_index == 3) {
                consumed = cycle_menu_item(
                    (CyclingMenuDef*)wifi_listen_modes,
                    COUNT_OF(wifi_listen_modes),
                    &current_wifi_listen_index,
                    (MenuCommand*)wifi_scanning_commands,
                    3,
                    current_menu,
                    event);
            }
            break;
        case InputKeyMAX:
            break;
        }
        break;

    case InputTypeLong:
        switch(event->key) {
        case InputKeyUp:
        case InputKeyDown:
        case InputKeyRight:
        case InputKeyLeft:
        case InputKeyBack:
        case InputKeyMAX:
            break;

        case InputKeyOk:
            if(current_index < commands_count) {
                const MenuCommand* command = &commands[current_index];
                if(command->details_header && command->details_text) {
                    show_command_details(state, command);
                    consumed = true;
                }
            }
            break;
        }
        break;

    case InputTypeRepeat:
        switch(event->key) {
        case InputKeyUp:
            if(current_index > 0) {
                submenu_set_selected_item(current_menu, current_index - 1);
            } else {
                // Wrap to bottom
                submenu_set_selected_item(current_menu, commands_count - 1);
            }
            consumed = true;
            break;

        case InputKeyDown:
            if(current_index < commands_count - 1) {
                submenu_set_selected_item(current_menu, current_index + 1);
            } else {
                // Wrap to top
                submenu_set_selected_item(current_menu, 0);
            }
            consumed = true;
            break;

        case InputKeyRight:
        case InputKeyLeft:
        case InputKeyOk:
        case InputKeyBack:
        case InputKeyMAX:
            break;
        }
        break;

    case InputTypePress:
    case InputTypeRelease:
    case InputTypeMAX:
        break;
    }

    return consumed;
}

// 6675636B796F7564656B69
