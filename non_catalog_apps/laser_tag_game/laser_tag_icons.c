
#include "laser_tag_icons.h"
#include <gui/icon_i.h>

const uint8_t laser_gun_icon_data[] = {
    0,
    0b00000000,
    0b00000000,
    0b00000001,
    0b10000000,
    0b00000011,
    0b11000000,
    0b00000111,
    0b11100000,
    0b00001111,
    0b11110000,
    0b00011111,
    0b11111000,
    0b11111111,
    0b11111110,
    0b11111111,
    0b11111111,
};

const uint8_t health_icon_data[] = {
    0,
    0b00001100,
    0b00110000,
    0b00011110,
    0b01111000,
    0b00111111,
    0b11111100,
    0b01111111,
    0b11111110,
    0b01111111,
    0b11111110,
    0b00111111,
    0b11111100,
    0b00011111,
    0b11111000,
    0b00000111,
    0b11100000,
};

const uint8_t ammo_icon_data[] = {
    0,
    0b00011000,
    0b00011000,
    0b00111100,
    0b00111100,
    0b01111110,
    0b01111110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b01111110,
    0b00111100,
    0b00111100,
    0b00011000,
    0b00011000,
};

const uint8_t team_red_icon_data[] = {
    0,
    0b00011000,
    0b00011000,
    0b00111100,
    0b00111100,
    0b01111110,
    0b01111110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b01111110,
    0b00111100,
    0b00111100,
    0b00011000,
    0b00011000,
};

const uint8_t team_blue_icon_data[] = {
    0,
    0b11100111,
    0b11100111,
    0b11000011,
    0b11000011,
    0b10000001,
    0b10000001,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b10000001,
    0b10000001,
    0b11000011,
    0b11000011,
    0b11100111,
    0b11100111,
};

const uint8_t game_over_icon_data[] = {
    0,
    0b11111111,
    0b11111111,
    0b10000000,
    0b00000001,
    0b10111101,
    0b10111101,
    0b10100001,
    0b10100001,
    0b10100001,
    0b10100001,
    0b10111101,
    0b10111101,
    0b10000000,
    0b00000001,
    0b11111111,
    0b11111111,
};

const uint8_t* const laser_gun_icon_frames[] = {laser_gun_icon_data};
const uint8_t* const health_icon_frames[] = {health_icon_data};
const uint8_t* const ammo_icon_frames[] = {ammo_icon_data};
const uint8_t* const team_red_icon_frames[] = {team_red_icon_data};
const uint8_t* const team_blue_icon_frames[] = {team_blue_icon_data};
const uint8_t* const game_over_icon_frames[] = {game_over_icon_data};

const Icon I_laser_gun_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = laser_gun_icon_frames,
};

const Icon I_health_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = health_icon_frames,
};

const Icon I_ammo_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = ammo_icon_frames,
};

const Icon I_team_red_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = team_red_icon_frames,
};

const Icon I_team_blue_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = team_blue_icon_frames,
};

const Icon I_game_over_icon = {
    .width = 16,
    .height = 8,
    .frame_count = 1,
    .frame_rate = 0,
    .frames = game_over_icon_frames,
};
