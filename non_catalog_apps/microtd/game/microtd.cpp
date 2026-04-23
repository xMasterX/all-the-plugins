/**
  @file   microtd.ino
  @author Miloslav Ciz
  @brief  tower defense game for Arduboy

  MicroTD - Tower Defense Game for Arduboy

  version: 1.4

  Miloslav "drummyfish" Ciz, 2018, license: CC0 (public domain)

  This file can be compiled both with Arduino IDE for the final release, or
  with gcc (or maybe even other compiler) for quick debug/tests on the PC.
*/

#ifdef ARDUINO
  #include <stdint.h>
  #include "lib/Arduboy2.h"
#else
  // for the PC
  #include <iostream>
  #include <string>
  #include <string.h>
  #include <cstdint>
  using namespace std;

  #define PROGMEM 

  #define A_BUTTON             0
  #define B_BUTTON             1
  #define UP_BUTTON            2
  #define RIGHT_BUTTON         3
  #define LEFT_BUTTON          4
  #define DOWN_BUTTON          5

  #define min(x,y) ((x) < (y) ? (x) : (y))
  #define max(x,y) ((x) > (y) ? (x) : (y))
#endif

#define DISPLAY_WIDTH          128
#define DISPLAY_HEIGHT         64
#define FRAMERATE              60

#define DIRECTION_N            0
#define DIRECTION_E            1
#define DIRECTION_S            2
#define DIRECTION_W            3

#define B_DIRECTION_N          0b00000000
#define B_DIRECTION_E          0b01000000
#define B_DIRECTION_S          0b10000000
#define B_DIRECTION_W          0b11000000

#define GAME_SPEEDUP           5   ///< How many times the game can be sped up.

#define MAX_SEGMENTS           10  ///< Max number of segments per creep path.
#define MAX_PATHS              2   ///< Max number of paths per game map.

#define TILE_EMPTY             0   ///< Empty tile, towers can be built here.
#define TILE_CREEP_START       1   ///< Creeps spawn here.
#define TILE_CREEP_EXIT        2   ///< Creeps exit here.
#define TILE_PATH_NS           3   ///< path: ║
#define TILE_PATH_WE           4   ///< path: ═
#define TILE_PATH_NE           5   ///< path: ╚
#define TILE_PATH_SE           6   ///< path: ╔
#define TILE_PATH_NW           7   ///< path: ╝
#define TILE_PATH_SW           8   ///< path: ╗
#define TILE_PATH_NES          9   ///< path: ╠
#define TILE_PATH_ESW          10  ///< path: ╦
#define TILE_PATH_NSW          11  ///< path: ╣
#define TILE_PATH_NEW          12  ///< path: ╩
#define TILE_PATH_NESW         13  ///< path: ╬
#define TILE_TOWER             14  ///< Tile with tower on it.

#define TILE_WIDTH             8   ///< Tile width in pixels.
#define TILE_HEIGHT            8   ///< Tile height in pixels.
#define TILE_WIDTH_HALF        (TILE_WIDTH / 2)
#define TILE_HEIGHT_HALF       (TILE_HEIGHT / 2)

#define TILE_PIXELS            (TILE_WIDTH * TILE_HEIGHT)
#define TILE_SIZE              (TILE_PIXELS / 8)

#define TILES_X                (DISPLAY_WIDTH  / TILE_WIDTH)
#define TILES_Y                (DISPLAY_HEIGHT / TILE_HEIGHT)
#define TILES_TOTAL            (TILES_X * TILES_Y)

#define CREEP_WIDTH            8
#define CREEP_HEIGHT           8
#define CREEP_PIXELS           (CREEP_WIDTH * CREEP_HEIGHT)
#define CREEP_SIZE             (CREEP_PIXELS / 8)

#define CREEP_SPIDER           0
#define CREEP_LIZARD           1
#define CREEP_SNAKE            2
#define CREEP_WOLF             3   ///< Freeze is less effective.
#define CREEP_BAT              4   ///< Immune to cannon.
#define CREEP_ENT              5
#define CREEP_SPIDER_BIG       6   ///< Spawns two small spiders when killed.
#define CREEP_GHOST            7   ///< Immune to physical attack.
#define CREEP_OGRE             8
#define CREEP_DINO             9
#define CREEP_DEMON            10  ///< Only attackable by fire/water.
#define CREEPS_TOTAL           11

#define TOWER_GUARD            0
#define TOWER_CANNON           1
#define TOWER_ICE              2
#define TOWER_ELECTRO          3
#define TOWER_SNIPER           4
#define TOWER_MAGIC            5
#define TOWER_WATER            6
#define TOWER_FIRE             7
#define TOWERS_TOTAL           8

#define UPGRADE_DAMAGE         0
#define UPGRADE_SPEED          1
#define UPGRADE_RANGE          2
#define UPGRADE_SHOCK          3   ///< Chance to freeze.
#define UPGRADE_SPEED_AURA     4

#define SPLASH_RANGE           12

#define WAVE_BASE_REWARD       5

#define UPGRADE_RANGE_FRACTION_INCREASE  4  /**< Fractional increase (+ 1/x) of
                                                 range by upgrade. */
#define UPGRADE_DAMAGE_FRACTION_INCREASE 2  /**< Fractional increase (+ 1/x) of
                                                 damage by upgrade. */

#define BUTTON_REPEAT_DELAY    20 ///< Initial button repeat delay, in frames.
#define BUTTON_REPEAT_PERIOD   6  ///< Button repeat period in frames.

#define CHEAT_NONE             0
#define CHEAT_MONEY            1

#define STATE_MENU             0  ///< In main menu.
#define STATE_PLAYING_MENU     1  ///< In game, game menu open.
#define STATE_PLAYING_BUILDING 2  ///< In game, building stage.
#define STATE_PLAYING_WAVE     3  ///< In game, wave in progress.
#define STATE_CONFIRM          4  ///< Confirming chosen action.
#define STATE_GAME_OVER        5  ///< Game over.

#define ITEM_STATE_AVAILABLE   0  ///< Game menu item state: available.
#define ITEM_STATE_UNAVAILABLE 1  ///< Game menu item state: unavailable.
#define ITEM_STATE_NULL        2  ///< Game menu item state: no item.

#define LOGO_X                 51
#define LOGO_Y                 5

#define GAME_MENU_UPGRADE1     8
#define GAME_MENU_UPGRADE2     9
#define GAME_MENU_DESTROY      10
#define GAME_MENU_NEXT_WAVE    11
#define GAME_MENU_QUIT         12

#define MAX_CREEPS             50  ///< Maximum number of creeps in one round.

#define INFOBAR_Y              1

#define TARGET_NONE            255 ///< No target for a tower.

#define MAIN_MENU_ITEMS        6

#define SOUND_PUT              0
#define SOUND_FALL             1
#define SOUND_BAD              2
#define SOUND_LOST             3

#define EEPROM_START           202 ///< Random address to record data to.
#define EEPROM_VALID_VALUE     95  ///< Random value to confirm data validity.

#define MAPS_TOTAL             5   ///< Total number of playable maps.

#define getter(dataType,memberVar)\
  dataType get##memberVar() const\
  { return m##memberVar; }

#define setter(dataType,memberVar)\
  void set##memberVar(dataType v)\
  { m##memberVar = v; }

#define getterSetter(dataType,memberVar)\
  getter(dataType,memberVar)\
  setter(dataType,memberVar)

#define absDiff(a,b) (a > b ? a - b : b - a)   ///< Absolute difference.

typedef uint8_t Direction;
typedef uint8_t TileType;
typedef uint8_t IndexPointer; ///< Array index, used instead of pointers (smaller)
typedef uint8_t GameState;
typedef uint8_t MenuItemState;

#ifdef ARDUINO
BeepPin1 beep1;

void playSound(IndexPointer sound)
{
  switch (sound)
  {
    case SOUND_PUT:
      beep1.tone(beep1.freq(532),2);
      break;

    case SOUND_FALL:
      beep1.tone(beep1.freq(293),2);
      break;

    case SOUND_BAD:
      beep1.tone(beep1.freq(185),5);
      break;

    case SOUND_LOST:
      beep1.tone(beep1.freq(110),15);
      break;

    default:
      break;
  }
}
#else
void playSound(IndexPointer sound)
{
  cout << "playing sound: " << sound << endl;
}
#endif

// A function is a simple way to store strings in PROGMEM :)
void getUpgradeName(IndexPointer upgrade, char *dst)
{
  dst[0] = '+';

  switch (upgrade)
  {
    case UPGRADE_DAMAGE:
      dst[1] = 'd'; dst[2] = 'm'; dst[3] = 'g'; dst[4] = 0;
      break;

    case UPGRADE_SPEED:
      dst[1] = 's'; dst[2] = 'p'; dst[3] = 'd'; dst[4] = 0;
      break;

    case UPGRADE_RANGE:
      dst[1] = 'r'; dst[2] = 'n'; dst[3] = 'g'; dst[4] = 0;
      break;

    case UPGRADE_SHOCK:
      dst[0] = 's'; dst[1] = 'h'; dst[2] = 'o'; dst[3] = 'c';
      dst[4] = 'k'; dst[5] = 0;
      break;

    case UPGRADE_SPEED_AURA:
      dst[0] = 's'; dst[1] = 'p'; dst[2] = 'd'; dst[3] = ' ';
      dst[4] = 'a'; dst[5] = 'u'; dst[6] = 'r'; dst[7] = 'a';
      dst[8] = 0;
      break;

    default:
      dst[0] = 0;
      break;
  }
}

/// 8 x 8 dithering pattern.
const uint8_t PROGMEM ditherImage[] =
{
0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55
};

/// 15 x 5 wave decoration image.
const uint8_t PROGMEM waveImage[] =
{
0x19, 0x1c, 0x1c, 0x1c, 0x1c, 0x19,
0x13, 0x13, 0x07, 0x07, 0x07, 0x07,
0x13, 0x13, 0x19 
};

/// 25 x 18 logo image
const uint8_t PROGMEM logoImage[] =
{
0xf7, 0x63, 0xb7, 0x5f, 0x5b, 0x41, 0x40, 0x41, 0x5b, 0x5f, 0x58, 0x41, 0x40,
0x41, 0x58, 0x5f, 0x5b, 0x41, 0x40, 0x41, 0x5b, 0x5f, 0xaf, 0x47, 0xef, 0x80,
0x55, 0x80, 0x3e, 0x7f, 0x41, 0x6f, 0x6f, 0x71, 0x7f, 0x3e, 0x00, 0x03, 0x01,
0x7f, 0x01, 0x03, 0x00, 0x7f, 0x41, 0x63, 0x3e, 0x80, 0x7f, 0x80, 0x03, 0x03,
0x02, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x03
};

/// 8 x 8 map tiles, correspond to TILE_x tile types (NOT including TILE_TOWER)
const uint8_t PROGMEM tileImages[] =
{
#if 0 // change to 0/1 for an alternate tileset!
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // TILE_EMPTY 
0xff, 0x83, 0xc1, 0xe1, 0xe1, 0xc1, 0x83, 0xff, // TILE_CREEP_START
0xff, 0x83, 0xf9, 0xfd, 0xfd, 0xf9, 0x83, 0xff, // TILE_CREEP_EXIT
0xaa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x55, // TILE_PATH_NS
0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, // TILE_PATH_WE  
0xaa, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, // TILE_PATH_NE  
0xaa, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0x7f, // TILE_PATH_SE  
0xfe, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x55, // TILE_PATH_NW  
0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0x55, // TILE_PATH_SW  
0xaa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, // TILE_PATH_NES 
0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0x7f, // TILE_PATH_ESW 
0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x55, // TILE_PATH_NSW 
0xfe, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, // TILE_PATH_NEW 
0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f  // TILE_PATH_NESW
#else
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // TILE_EMPTY 
0xff, 0x83, 0xc1, 0xe1, 0xe1, 0xc1, 0x83, 0xff, // TILE_CREEP_START
0xff, 0x83, 0xf9, 0xfd, 0xfd, 0xf9, 0x83, 0xff, // TILE_CREEP_EXIT
0xff, 0xff, 0xff, 0xaa, 0x55, 0xff, 0xff, 0xff, // TILE_PATH_NS
0xf7, 0xef, 0xf7, 0xef, 0xf7, 0xef, 0xf7, 0xef, // TILE_PATH_WE  
0xff, 0xff, 0xff, 0xfa, 0xf5, 0xeb, 0xf7, 0xef, // TILE_PATH_NE  
0xff, 0xff, 0xff, 0xbf, 0x5f, 0xaf, 0xd7, 0xef, // TILE_PATH_SE  
0xf7, 0xeb, 0xf5, 0xfa, 0xfd, 0xff, 0xff, 0xff, // TILE_PATH_NW  
0xf7, 0xef, 0xd7, 0xaf, 0x5f, 0xff, 0xff, 0xff, // TILE_PATH_SW  
0xff, 0xff, 0xff, 0xaa, 0x55, 0xef, 0xf7, 0xef, // TILE_PATH_NES 
0xf7, 0xef, 0xf7, 0xaf, 0x57, 0xef, 0xf7, 0xef, // TILE_PATH_ESW 
0xf7, 0xef, 0xf7, 0xaa, 0x55, 0xff, 0xff, 0xff, // TILE_PATH_NSW 
0xf7, 0xef, 0xf7, 0xea, 0xf5, 0xef, 0xf7, 0xef, // TILE_PATH_NEW 
0xf7, 0xef, 0xf7, 0xaa, 0x55, 0xef, 0xf7, 0xef  // TILE_PATH_NESW
#endif
};

/// 8 x 8 creep images, correspond to CREEP_x constants.
const uint8_t PROGMEM creepSprites[] =
{
8,8,  // width, height
0x00, 0x00, 0x00, 0x54, 0x00, 0x7c, 0x00, 0x38, // CREEP_SPIDER 
0x00, 0x38, 0x00, 0x7c, 0x00, 0x54, 0x00, 0x00, 
0x00, 0x0e, 0x00, 0x58, 0x00, 0x70, 0x00, 0x30, // CREEP_LIZARD
0x00, 0x38, 0x00, 0x78, 0x00, 0x58, 0x00, 0x10, 
0x00, 0x00, 0x00, 0x4c, 0x00, 0x5e, 0x00, 0x57, // CREEP_SNAKE
0x02, 0x57, 0x00, 0x77, 0x00, 0x22, 0x00, 0x00, 
0x00, 0x06, 0x00, 0x6c, 0x00, 0x38, 0x00, 0x18, // CREEP_WOLF
0x00, 0x1c, 0x00, 0x7e, 0x04, 0x4e, 0x00, 0x0c, 
0x00, 0x04, 0x00, 0x1e, 0x00, 0x7c, 0x00, 0x38, // CREEP_BAT 
0x00, 0x38, 0x00, 0x7c, 0x00, 0x1e, 0x00, 0x04, 
0x00, 0x10, 0x00, 0x98, 0x00, 0xcb, 0x00, 0x7f, // CREEP_ENT
0x00, 0x7f, 0x00, 0xcb, 0x00, 0x98, 0x00, 0x10, 
0x00, 0x92, 0x00, 0xd6, 0x00, 0x7c, 0x00, 0x7e, // CREEP_SPIDER_BIG
0x00, 0x7e, 0x00, 0x7c, 0x00, 0xd6, 0x00, 0x92, 
0x00, 0x0c, 0x00, 0x78, 0x00, 0x3e, 0x04, 0x7f, // CREEP_GHOST
0x04, 0x7f, 0x00, 0x3e, 0x00, 0x78, 0x00, 0x0c, 
0x00, 0x07, 0x00, 0x8c, 0x00, 0xfc, 0x00, 0x3f, // CREEP_OGRE
0x00, 0x3f, 0x00, 0xfc, 0x00, 0x8c, 0x00, 0x18, 
0x00, 0x0f, 0x00, 0x5c, 0x00, 0xf8, 0x00, 0xbc, // CREEP_DEMON
0x00, 0x7f, 0x04, 0xdf, 0x00, 0x8e, 0x00, 0x0c, 
0x00, 0x33, 0x00, 0x9a, 0x00, 0xff, 0x04, 0x7f, // CREEP_DEMON
0x04, 0x7f, 0x00, 0xff, 0x00, 0x9a, 0x00, 0x33
};

/// 8 x 8 tower images.
const uint8_t PROGMEM towerSmallImages[] = 
{
0xff, 0xe7, 0x83, 0x81, 0x81, 0x83, 0xe7, 0xff, // TOWER_GUARD full  
0xff, 0xe7, 0x8b, 0x8d, 0x8d, 0x8b, 0xe7, 0xff, // TOWER_GUARD upgrade 1
0xff, 0xe7, 0x83, 0xb1, 0xb1, 0x83, 0xe7, 0xff, // TOWER_GUARD upgrade 2
0xff, 0x99, 0x81, 0x81, 0x81, 0x81, 0x99, 0xff, // TOWER_CANNON full
0xff, 0x99, 0x85, 0x8d, 0x8d, 0x85, 0x99, 0xff, // TOWER_CANNON upgrade 1
0xff, 0x99, 0xa1, 0xb1, 0xb1, 0xa1, 0x99, 0xff, // TOWER_CANNON upgrade 2 
0xff, 0x8f, 0x83, 0x81, 0x81, 0x83, 0x8f, 0xff, // TOWER_ICE full
0xff, 0x8f, 0x83, 0x8d, 0x8d, 0x83, 0x8f, 0xff, // TOWER_ICE upgrade 1
0xff, 0x8f, 0xb3, 0xb1, 0xb1, 0xb3, 0x8f, 0xff, // TOWER_ICE upgrade 2
0xff, 0x93, 0x81, 0x83, 0x83, 0x81, 0x93, 0xff, // TOWER_ELECTRO full
0xff, 0x93, 0x8d, 0x8b, 0x8b, 0x8d, 0x93, 0xff, // TOWER_ELECTRO upgrade 1
0xff, 0x93, 0xa1, 0xb3, 0xb3, 0xa1, 0x93, 0xff, // TOWER_ELECTRO upgrade 2
0xff, 0xf1, 0x81, 0x83, 0x83, 0x81, 0xf1, 0xff, // TOWER_SNIPER full
0xff, 0xf1, 0x8d, 0x8b, 0x8b, 0x8d, 0xf1, 0xff, // TOWER_SNIPER upgrade 1
0xff, 0xf1, 0x81, 0xb3, 0xb3, 0x81, 0xf1, 0xff, // TOWER_SNIPER upgrade 2
0xff, 0xeb, 0x81, 0x81, 0x81, 0x81, 0xeb, 0xff, // TOWER_MAGIC full
0xff, 0xeb, 0x85, 0x8d, 0x8d, 0x85, 0xeb, 0xff, // TOWER_MAGIC upgrade 1
0xff, 0xeb, 0x91, 0xb1, 0xb1, 0x91, 0xeb, 0xff  // TOWER_MAGIC upgrade 2
};

/// 16 x 16 tower images.
const uint8_t PROGMEM towerBigSprites[] = 
{
16,16, // width, height
0xff, 0xff, 0x3f, 0x3f, 0x87, 0x93, 0xb9, 0xe5, // TOWER_WATER
0xe5, 0xb9, 0x93, 0x87, 0x3f, 0x3f, 0xff, 0xff, 
0xff, 0xff, 0xff, 0x8e, 0x96, 0xb0, 0x96, 0xbf,
0xbf, 0x96, 0xb0, 0x96, 0x8e, 0xff, 0xff, 0xff, 
0xff, 0xff, 0x3f, 0xbf, 0x87, 0x83, 0x81, 0x99, // TOWER_WATER upgraded
0x99, 0x81, 0x83, 0x87, 0xbf, 0x3f, 0xff, 0xff, 
0xff, 0xff, 0xff, 0x8e, 0xa6, 0x80, 0xa0, 0x80,
0x80, 0xa0, 0x80, 0xa6, 0x8e, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xe3, 0xc9, 0x1d, 0x73, 0xe7, // TOWER_FIRE
0xe7, 0x73, 0x1d, 0xc9, 0xe3, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xfb, 0x91, 0xa4, 0xb4, 0x95, 0xbf,
0xbf, 0x95, 0xb4, 0xa4, 0x91, 0xfb, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xe3, 0xc9, 0x19, 0x33, 0x07, // TOWER_FIRE upgraded
0x07, 0x33, 0x19, 0xc9, 0xe3, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xfb, 0x95, 0xa4, 0x84, 0xa4, 0x8e,
0x8e, 0xa4, 0x84, 0xa4, 0x95, 0xfb, 0xff, 0xff
};

/// Menu item 8 x 8 icons.
const uint8_t PROGMEM menuIcons[] =
{
0xff, 0xc3, 0x99, 0xe5, 0xe5, 0x99, 0xc3, 0xff, // water tower 
0xff, 0xc1, 0x99, 0xf3, 0xf3, 0x99, 0xc1, 0xff, // fire tower
0xff, 0xff, 0xfb, 0xf9, 0xfb, 0xef, 0x87, 0xff, // upgrade 1
0xff, 0xfb, 0xf9, 0xfb, 0xff, 0x97, 0xa7, 0xff, // upgrade 2
0xff, 0xbb, 0x93, 0xc7, 0xc7, 0x93, 0xbb, 0xff, // destroy tower
0xff, 0xff, 0xff, 0x81, 0xc3, 0xe7, 0xff, 0xff, // next wave
0xff, 0xc3, 0x99, 0xb5, 0xad, 0x99, 0xc3, 0xff  // quit to menu
};

uint16_t sqrtInt(uint16_t value)
{
  uint16_t result = 0;

  uint16_t a  = value;
  uint16_t b = 1u << 14;

  while (b > a)
    b >>= 2;

  while (b != 0)
  {
    if (a >= result + b)
    {
      a -= result + b;
      result = result +  2 * b;
    }

    b >>= 2;
    result >>= 1;
  }

  return result;
}

void getXYIncrement(Direction direction, char *xIncr, char *yIncr)
{
  *xIncr = 0;
  *yIncr = 0;

  switch (direction)
  {
    case DIRECTION_N: *yIncr = -1; break;
    case DIRECTION_E: *xIncr = 1;  break;
    case DIRECTION_S: *yIncr = 1;  break;
    case DIRECTION_W: *xIncr = -1; break;
    default: break;
  }
}

inline uint8_t interpolateBytes(uint8_t v1, uint8_t v2, uint8_t percentage)
{
  int16_t d = v2 - v1;
  return v1 + (d * percentage) / 100;
}

struct BytePosition
{
  uint8_t mX;
  uint8_t mY;

  BytePosition(uint8_t x=0, uint8_t y=0): mX(x), mY(y)
  {
  }

  BytePosition left()
  {
    return BytePosition(mX - 1,mY);
  }

  BytePosition up()
  {
    return BytePosition(mX,mY - 1);
  }

  BytePosition right()
  {
    return BytePosition(mX + 1,mY);
  }

  BytePosition down()
  {
    return BytePosition(mX,mY + 1);
  }
};

/// Checks if Euclidean distance between two points is greater than a limit.
bool distanceIsGreater(struct BytePosition p1, struct BytePosition p2,
  uint8_t limit)
{
  uint16_t dx = absDiff(p1.mX,p2.mX);
  uint16_t dy = absDiff(p1.mY,p2.mY);

  if (dx > limit || dy > limit)
    return true;

  // now compute the actual distance

  return sqrtInt(dx * dx + dy * dy) > limit;
}

class PathSegment
{
protected:
  uint8_t mData;

public:
  PathSegment(Direction direction=DIRECTION_N, uint8_t length=1)
  {
    mData = direction << 6 | (length & 0b00111111);
  }

  Direction getDirection()
  {
    return (mData & 0b11000000) >> 6;
  }

  uint8_t getLength()
  {
    return mData & 0b00111111;
  }
};

class CreepPath
{
protected:
  BytePosition mStart; ///< Where the path starts.
  uint8_t mNumSegments;   ///< Number of segments in mSegments.
  uint16_t mNumTiles;  ///< Length of the path in tiles.

  PathSegment mSegments[MAX_SEGMENTS];

public:
  CreepPath()
  {
    mStart.mX = 0;
    mStart.mY = 0;
    mNumSegments = 0;
    mNumTiles = 0;
  }

  /** For given position along the path (in pixels) returns a pixel position
      on the mPlayground. */
  BytePosition getPositionAlong(int16_t position) const
  {
    BytePosition result;

    result = getStart();
    result.mX = result.mX * TILE_WIDTH + TILE_WIDTH_HALF;
    result.mY = result.mY * TILE_HEIGHT + TILE_HEIGHT_HALF;
    
    uint8_t segment = 0;
    char incrX, incrY;

    if (position < 0)
      return result;

    while (true)      // keep substracting segments
    {
      PathSegment s = getSegment(segment);

      getXYIncrement(s.getDirection(),&incrX,&incrY);

      uint8_t length = s.getLength();

      int16_t diff = position - length * TILE_WIDTH;

      if (diff < 0) 
      {
        while (true)  // keep substracting tiles
        {
          diff = position - TILE_WIDTH;

          if (diff < 0)
          {
            result.mX += incrX * position;
            result.mY += incrY * position;
            break;
          }
          else
          {
            position = diff;
            result.mX += incrX * TILE_WIDTH;
            result.mY += incrY * TILE_HEIGHT;
          }
        }

        break;
      }
      else
      {
        result.mX += length * incrX * TILE_WIDTH;
        result.mY += length * incrY * TILE_HEIGHT;
        position = diff;
        segment++;
      }
    }

    return result;
  }

  void addSegment(PathSegment segment)
  {
    if (mNumSegments >= MAX_SEGMENTS)
      return;

    mSegments[mNumSegments] = segment;
    mNumTiles += segment.getLength();
    mNumSegments++;
  }

  PathSegment getSegment(uint8_t index) const
  {
    return mSegments[index < MAX_SEGMENTS ? index : MAX_SEGMENTS - 1];
  }

  getterSetter(BytePosition, Start)
  getter(uint16_t,     NumTiles)    ///< Returns the path length in tiles.
  getter(uint8_t,         NumSegments)
};

/// Represents a creep type.
struct CreepType
{
  uint8_t mSprite;    ///< Sprite number in the creep sprite sheet.
  uint8_t mMaxHealth;
  uint8_t mSpeed;     ///< Frequency of movement (lower => faster!).
  uint8_t mReward;    ///< Money for killing, also lives taken for exiting.

  CreepType(uint8_t sprite=0, uint8_t hp=1, uint8_t speed=1, uint8_t reward=1):
    mSprite(sprite), mMaxHealth(hp), mSpeed(speed), mReward(reward)
  {
  }
};

const CreepType creepTypes[CREEPS_TOTAL] =
{
// sprite            hp   spd reward
  {CREEP_SPIDER,     7,   2,  1},
  {CREEP_LIZARD,     8,   1,  1},
  {CREEP_SNAKE,      12,  2,  1},
  {CREEP_WOLF,       20,  2,  1},
  {CREEP_BAT,        13,  1,  1},
  {CREEP_ENT,        43,  4,  2},
  {CREEP_SPIDER_BIG, 20,  2,  2},
  {CREEP_GHOST,      30,  3,  2},
  {CREEP_OGRE,       58,  2,  3},
  {CREEP_DINO,       63,  1,  3},
  {CREEP_DEMON,      63,  1,  3}
};

/// Represents a concrete spawned creep
class CreepInstance
{
protected:
  IndexPointer     mTypeFreeze;    /**< Stores two values in a single uint8_t:
                                        - upper 4 bits: freeze counter
                                        - lower 4 bits: creep type index
                                   */

  uint8_t             mHealthLives;   /**< Stores two values in a single uint8_t:
                                        - upper 2 bits: lives (respawns)
                                        - lower 6 bits: health (hit point)
                                   */

  IndexPointer     mPath;     ///< Path this creep follows.   
  int16_t          mPosition; /**< Position along the path, in pixels. Can be
                               negative, which means the creep is waiting
                               to be spawned (e.g. -10 => will be spawned
                               after travelling 10 pixels. */

public:
  CreepInstance(IndexPointer type=0, IndexPointer path=0, int16_t position=0,
    uint8_t lives=1):
      mPath(path),
      mPosition(position)
    {
      setHealthLives(creepTypes[type].mMaxHealth,lives);
      setTypeFreeze(type,0);
    }

  bool isAlive()
  {
    return getHealth() > 0;
  }

  bool exited(const CreepPath *paths)
  {
    return mPosition >= ((int16_t) paths[mPath].getNumTiles()) * TILE_WIDTH;
  }

  bool entered()
  {
    return mPosition >= 0;
  }

  void update(uint16_t frameNumber)
  {
    if (isAlive() && frameNumber % (getSpeed() * 2) == 0)
      mPosition += 1;

    if (frameNumber % 3 == 0)
    {
      uint8_t freezeCounter = getFreezeCounter();

      if (freezeCounter > 0)
        setTypeFreeze(getTypeIndex(),freezeCounter - 1);
    }
  }

  uint8_t getSpeed()
  {
    return getType().mSpeed + (getFreezeCounter() == 0 ? 0 : 15);
  }

  void setTypeFreeze(IndexPointer creepType, uint8_t freezeCounter)
  {
    mTypeFreeze = (creepType & 0b00001111) |
      ((freezeCounter & 0b00001111) << 4);
  }

  BytePosition getPixelPosition(const CreepPath *paths)
  {
    return paths[mPath].getPositionAlong(mPosition);
  }

  void kill()
  {
    playSound(SOUND_FALL);

    if (getLives() == 0 || getTypeIndex() == CREEP_SPIDER_BIG)
    {                   // ^ big spiders have to be killed to spawn small ones
      setHealthLives(0,0);
    }
    else
    {
      // respawn
      setHealthLives(getType().mMaxHealth,getLives() - 1);
      mPosition = -10;
    }
  }

  /// Makes the creep take a hit, returns true if killed.
  bool hit(uint8_t damage, bool freeze)
  {
    uint8_t currentHealth = getHealth();

    if (damage >= currentHealth)
    {
      kill();
      return true;
    }

    setHealthLives(currentHealth - damage,getLives());

    if (freeze)
      setTypeFreeze(getTypeIndex(),getTypeIndex() == CREEP_WOLF ? 7 : 15);

    return false;
  }

  void setHealthLives(uint8_t health, uint8_t lives)
  {
    mHealthLives = (health & 0b00111111) | ((lives & 0b00111111) << 6);
  }

  const CreepType getType()
  {
    return creepTypes[getTypeIndex()];
  }

  IndexPointer getTypeIndex()
  {
    return mTypeFreeze & 0b00001111;
  }

  bool isFrozen()
  {
    return getFreezeCounter() > 0;
  }

  IndexPointer getFreezeCounter()
  {
    return (mTypeFreeze & 0b11110000) >> 4;
  }

  uint8_t getHealth()
  {
    return mHealthLives & 0b00111111;
  }

  uint8_t getLives()
  {
    return (mHealthLives & 0b11000000) >> 6;
  }

  getterSetter(IndexPointer,      Path)
  getterSetter(int16_t,           Position)
};

struct TowerType
{
  uint8_t mSprite;
  uint8_t mRange;       ///< Base range, in pixels.
  uint8_t mAttackSpeed; ///< Base attack speed (frequency, lower = faster)
  uint8_t mDamage;
  uint8_t mPrice;       ///< How much to build.
  IndexPointer mUpgrades[2];

  BytePosition getCenter(BytePosition tilePosition)
  {
    BytePosition result;
    
    result.mX = tilePosition.mX * TILE_WIDTH + TILE_WIDTH_HALF;
    result.mY = tilePosition.mY * TILE_HEIGHT + TILE_HEIGHT_HALF;

    if (isBig())
    {
      result.mX -= TILE_WIDTH;
      result.mY -= TILE_HEIGHT;
    }

    return result;
  }

  uint8_t getUpgrageCost()
  {
    return (mPrice * 2) / 3;
  }

  bool isBig()
  {
    return mSprite == TOWER_WATER || mSprite == TOWER_FIRE;
  }
};

const TowerType towerTypes[TOWERS_TOTAL] =
{
// sprite         rng spd dmg price  upgrade 1       upgrade 2
  {TOWER_GUARD,   30, 6,  2,  8,    {UPGRADE_RANGE,  UPGRADE_SPEED}},
  {TOWER_CANNON,  27, 8,  2,  8,    {UPGRADE_RANGE,  UPGRADE_DAMAGE}},
  {TOWER_ICE,     26, 7,  0,  17,   {UPGRADE_SPEED,  UPGRADE_RANGE}},
  {TOWER_ELECTRO, 35, 8,  4,  30,   {UPGRADE_DAMAGE, UPGRADE_SHOCK}},
  {TOWER_SNIPER,  60, 5,  3,  45,   {UPGRADE_SPEED,  UPGRADE_RANGE}},
  {TOWER_MAGIC,   25, 8,  2,  60,   {UPGRADE_DAMAGE, UPGRADE_SPEED_AURA}},
  {TOWER_WATER,   40, 4,  7,  100,  {UPGRADE_RANGE,  UPGRADE_SPEED}},
  {TOWER_FIRE,    38, 6,  8,  100,  {UPGRADE_RANGE,  UPGRADE_DAMAGE}}
};

/// Concrete instance of a tower.
class TowerInstance
{
protected:
  uint8_t             mData;           /**< Holds multiple data in a single uint8_t:
                                         - lower 3 bits: tower type (index)
                                         - upper 2 bits: upgrade info
                                         - remaining 3 bits: attack progress
                                    */
  IndexPointer     mTarget;         ///< Currently targeted creep, or 255

  /// Helper function for handling the creep hit.
  void hitCreep(CreepInstance *c, uint16_t *money, bool splash=false)
  {
    if (!canAttack(c->getTypeIndex()))
      return;

    if (c->hit(getDamage() / (splash ? 2 : 1), (getTypeIndex() == TOWER_ICE) ||
      (hasUpgraded(UPGRADE_SHOCK) && (rand() % 3) == 0)))
    {
      *money += c->getType().mReward;
      mTarget = TARGET_NONE;
    }

    if (getTypeIndex() == TOWER_WATER && (rand() % 10 == 0))
      c->setPosition(c->getPosition() - 10); // wave - knock back
  }

public:
  bool canAttack(IndexPointer creepType)
  {
    IndexPointer ti = getTypeIndex();

    if (creepType == CREEP_GHOST &&
        (ti == TOWER_GUARD || ti == TOWER_CANNON || ti == TOWER_SNIPER))
      return false; // ghosts are immune to physical attack

    if (creepType == CREEP_BAT && ti == TOWER_CANNON)
      return false; // bats are immune to cannon

    if (creepType == CREEP_DEMON && ti != TOWER_WATER && ti != TOWER_FIRE)
      return false; // demons are immune to everything else

    return true;
  }

  void update(CreepInstance *creepArray, uint8_t creepArraySize,
    const CreepPath *creepPaths, BytePosition tile, uint16_t frame,
    uint16_t *money)
  {
    BytePosition selfPosition = getCenter(tile);
   
    uint8_t range = getRange();

    if (mTarget == TARGET_NONE)  // no target => look for a new one
    {
      for (uint8_t i = 0; i < creepArraySize; ++i)
      {
        CreepInstance c = creepArray[i];

        if (!c.entered() || !c.isAlive() || !canAttack(c.getTypeIndex()))
          continue;

        BytePosition p = c.getPixelPosition(creepPaths);
        
        if (!distanceIsGreater(p,selfPosition,range))
        {
          mTarget = i;
          setAttackProgress(0);

          playSound(SOUND_PUT); // shooting sound

          if ((getTypeIndex() != TOWER_ICE) || !creepArray[mTarget].isFrozen())
          { // ^ ice tower keeps on looking for unfrozen targets
            break;
          }
        }
      }
    }
    else
    {
      // has target => check if still valid

      CreepInstance c = creepArray[mTarget];

      if (!c.isAlive() || !c.entered())
      {
        mTarget = TARGET_NONE;
      }
      else if (frame % getAttackSpeed() == 0)
      {
        setAttackProgress(getAttackProgress() + 1);

        if (getAttackProgress() == 0) // attack finished
        {
          hitCreep(&creepArray[mTarget],money);

          if (getTypeIndex() == TOWER_CANNON || getTypeIndex() == TOWER_FIRE)
          {
            // do splash damage

            BytePosition p = creepArray[mTarget].getPixelPosition(creepPaths);

            for (uint8_t i = 0; i < creepArraySize; ++i)
            {
              if (i == mTarget)
                continue;

              CreepInstance *c = &creepArray[i];

              if (c->isAlive() &&
               c->entered() &&
                 !distanceIsGreater(c->getPixelPosition(creepPaths),p,
                 SPLASH_RANGE))
              {
                hitCreep(c,money,true);
              }
            }
          }
          else if (getTypeIndex() == TOWER_ICE)
            mTarget = TARGET_NONE;
            // ^ ice tower will always look for a new target to freeze

          if (mTarget != TARGET_NONE)
          {
            // check if still in range         
            BytePosition p = c.getPixelPosition(creepPaths);
            
            if (distanceIsGreater(p,selfPosition,getRange()) || !c.entered())
              mTarget = TARGET_NONE;      // may have respawned ^
            else
              playSound(SOUND_PUT); // shooting sound
          }
        }
      }
    }
  }

  TowerInstance(const IndexPointer type=0): mTarget(TARGET_NONE)  
  {
    mData = type & 0b00000111;
  }

  /// Gets the tower center position in pixels.
  BytePosition getCenter(BytePosition tilePosition)
  {
    return getType().getCenter(tilePosition);
  }

  /// Sets given upgrade (0 or 1).
  void upgrade(uint8_t upgradeNumber)
  {
    mData |= (upgradeNumber == 0) ? 0b10000000 : 0b01000000;
  }

  bool upgraded(uint8_t upgradeNumber)
  {
    return mData & ((upgradeNumber == 0) ? 0b10000000 : 0b01000000);
  }

  TowerType getType()
  {
    return towerTypes[mData & getTypeIndex()];
  }

  IndexPointer getTypeIndex()
  {
    return mData & 0b00000111;
  }

  void setAttackProgress(uint8_t value)
  {
    mData = (mData & 0b11000111) | ((value & 0b00000111) << 3);
  }

  uint8_t getAttackProgress()
  {
    return (mData & 0b00111000) >> 3;
  }

  void clearTarget()
  {
    mTarget = TARGET_NONE;
  }
 
  bool hasUpgraded(IndexPointer upgradeType)
  {
    TowerType tt = getType();

    return 
      (tt.mUpgrades[0] == upgradeType && upgraded(0)) ||
      (tt.mUpgrades[1] == upgradeType && upgraded(1));
  }

  uint8_t getRange()
  {
    uint8_t r = getType().mRange;

    return r +
      (hasUpgraded(UPGRADE_RANGE) ? r / UPGRADE_RANGE_FRACTION_INCREASE : 0);
  }

  uint8_t getDamage()
  {
    uint8_t d = getType().mDamage;

    return d +
      (hasUpgraded(UPGRADE_DAMAGE) ? d / UPGRADE_DAMAGE_FRACTION_INCREASE : 0);
  }

  uint8_t getAttackSpeed()
  {
    uint8_t s = getType().mAttackSpeed;

    return s -
      (hasUpgraded(UPGRADE_SPEED) ? 2 : 0);
  }

  getter(IndexPointer, Target)
};

/// One tile on the mPlayground.
struct Tile
{
  TileType      mType;
  TowerInstance mTower; /// Only for type == TILE_TOWER.

  Tile(TileType type=TILE_EMPTY, TowerInstance tower=TowerInstance(0)):
    mType(type), mTower(tower)
  {
  }

  bool hasBigTower()
  {
    return mType == TILE_TOWER && mTower.getType().isBig();
  }
};

class Spawner; // forward decl

class GameMap
{
protected:
  CreepPath   mPaths[MAX_PATHS];
  uint8_t        mNumPaths;
  Spawner    *mSpawner;

  uint8_t        mStartLives;
  uint8_t        mStartMoney;

public:
  GameMap(): mNumPaths(0), mSpawner(0), mStartLives(20), mStartMoney(10)
  {
  }

  void setStartValues(uint8_t startLives, uint8_t startMoney)
  {
    mStartLives = startLives;
    mStartMoney = startMoney;
  }

  void addPath(CreepPath path)
  {
    if (mNumPaths >= MAX_PATHS)
      return;

    mPaths[mNumPaths] = path;
    mNumPaths++;
  }

  void clear()
  {
    mNumPaths = 0;
  }
 
  const CreepPath *getPath(uint8_t index) const
  {
    return &(mPaths[index < MAX_PATHS ? index : MAX_PATHS - 1]);
  }

  getter(uint8_t, StartLives)
  getter(uint8_t, StartMoney)
  getter(uint8_t, NumPaths)
  getter(const CreepPath *, Paths)
  getterSetter(Spawner *, Spawner)
};

/** Spawns creeps, is meant to be subclassed to implement the concrete spawn
    methods. */
class Spawner
{
protected:
  bool addCreep(CreepInstance c, CreepInstance *array, uint16_t *count)
  {
    if (*count >= MAX_CREEPS)
      return false;

    array[*count] = c;
    (*count)++;

    return true;
  }

  /// Helper function to cycle creeps in given range of levels.
  IndexPointer cycleCreeps(uint8_t index, IndexPointer levelFrom,
    IndexPointer levelTo)
  {
    levelFrom = min(CREEPS_TOTAL -1,levelFrom);
    levelTo   = min(CREEPS_TOTAL -1,levelTo);

    return levelFrom + (index % (levelTo - levelFrom + 1));
  }

  void cyclingSpawn(
    uint8_t total,           // total number of creeps to spawn
    uint8_t levelFrom,       // minimum level of a creep
    uint8_t levelTo,         // maximum level of a creep
    uint8_t maxLives,        // maximum number of lives for a creep
    uint8_t positionOffset,  // gaps between creeps
    uint8_t groupSize,       // affects additional gaps and creep lives
    const GameMap *gameMap, CreepInstance *creepArray, uint16_t *creepCount)
  {
    total = min(MAX_CREEPS,total);
    int16_t position = -positionOffset;

    for (uint8_t i = 0; i < total; ++i)
    {
      if (i % groupSize == 0)
        position -= positionOffset;

      CreepInstance c = CreepInstance(
        cycleCreeps(i,levelFrom,levelTo),
        i % gameMap->getNumPaths(),
        position,
        i % groupSize == 0 ? min(3,maxLives) : 0);

      position -= positionOffset;

      addCreep(c,creepArray,creepCount);
    }
  }

public:
  /// Spawns creeps into given array.
  virtual void spawnCreeps(uint16_t roundNumber, const GameMap *gameMap,
    CreepInstance *creepArray, uint16_t *creepCount)=0;
};

/// Spawner for maps 0 and 1.
class Spawner01: public Spawner
{
public:
  virtual void spawnCreeps(uint16_t roundNumber, const GameMap *gameMap,
    CreepInstance *creepArray, uint16_t *creepCount) override
  {
    cyclingSpawn
    (
      1 + roundNumber,  // number of creeps
      roundNumber / 4,  // level from
      roundNumber < 15 ? min(CREEP_DINO - 1,roundNumber) : roundNumber, // l to
      roundNumber / 5,  // max lives
      10,               // position offset
      5,                // group size
      gameMap, creepArray, creepCount
    );
  }
};

class Spawner2: public Spawner
{
public:
  virtual void spawnCreeps(uint16_t roundNumber, const GameMap *gameMap,
    CreepInstance *creepArray, uint16_t *creepCount) override
  {
    uint8_t path1Total = min(1 + roundNumber,(MAX_CREEPS * 2) / 3);
    int16_t position = -10;

    for (uint8_t i = 0; i < path1Total; ++i)
    {
      CreepInstance c = CreepInstance(
        cycleCreeps(i,roundNumber / 3,(roundNumber * 2) / 3),
        0,
        position,
        i % 3 == 0 ? min(3,roundNumber / 7) : 0);

      position -= 10;

      addCreep(c,creepArray,creepCount);

      if (gameMap->getNumPaths() > 1 && i % 2 == 0)
      {
        c.setPath(1);
        addCreep(c,creepArray,creepCount);
      }
    }
  }
};

class Spawner3: public Spawner
{
public:
  virtual void spawnCreeps(uint16_t roundNumber, const GameMap *gameMap,
    CreepInstance *creepArray, uint16_t *creepCount) override
  {
    cyclingSpawn
    (
      3 + roundNumber *  2,      // number of creeps
      roundNumber / 3,           // level from
      (roundNumber * 4) / 5,     // level to
      roundNumber / 8,           // max lives
      7,                         // position offset
      4,                         // group size
      gameMap, creepArray, creepCount
    );
  }
};

class Spawner4: public Spawner
{
public:
  virtual void spawnCreeps(uint16_t roundNumber, const GameMap *gameMap,
    CreepInstance *creepArray, uint16_t *creepCount) override
  {
    cyclingSpawn
    (
      8 + roundNumber * 2,       // number of creeps
      roundNumber / 3,           // level from
      2 + (roundNumber * 4) / 5, // level to
      roundNumber / 8,           // max lives
      10,                        // position offset
      4,                         // group size
      gameMap, creepArray, creepCount
    );
  }
};

/// Represents the currently loaded map that's being played.
class PlayGround
{
protected:
  Tile mTiles[TILES_TOTAL];

  uint16_t index(uint8_t x, uint8_t y)
  {
    return ((uint16_t) x) + ((uint16_t) y) * TILES_X;
  }

  /// Creates an appropriate path tile when paths cross etc.
  TileType makePathTile(Direction curDir, Direction prevDir, TileType curTile)
  {
    #define helperExpr(d1,d2,t1,t2,t3,t4,t5,t6,n) \
      (curDir == DIRECTION_##d1 || prevDir == DIRECTION_##d2 || \
       curTile == TILE_PATH_##t1 || \
       curTile == TILE_PATH_##t2 || \
       curTile == TILE_PATH_##t3 || \
       curTile == TILE_PATH_##t4 || \
       curTile == TILE_PATH_##t5 || \
       curTile == TILE_PATH_##t6 || \
       curTile == TILE_PATH_NESW    \
       ? n : 0)

    uint8_t tileFeatures = 
      helperExpr(N,S,NS,NE,NW,NEW,NSW,NEW,8) | // N
      helperExpr(E,W,WE,NE,SE,NES,ESW,NEW,4) | // E
      helperExpr(S,N,NS,SE,SW,NES,NSW,ESW,2) | // S
      helperExpr(W,E,WE,NW,SW,ESW,NSW,NEW,1);  // W

    #undef helperExpr

    switch (tileFeatures)
    {     // NESW
      case 0b0011: return TILE_PATH_SW;   break;
      case 0b0101: return TILE_PATH_WE;   break;
      case 0b0110: return TILE_PATH_SE;   break;
      case 0b0111: return TILE_PATH_ESW;  break;
      case 0b1001: return TILE_PATH_NW;   break;
      case 0b1010: return TILE_PATH_NS;   break;
      case 0b1011: return TILE_PATH_NSW;  break;
      case 0b1100: return TILE_PATH_NE;   break;
      case 0b1101: return TILE_PATH_NEW;  break;
      case 0b1110: return TILE_PATH_NES;  break;
      case 0b1111: return TILE_PATH_NESW; break;

      // these shouldn't happen:
      case 0b1000:
      case 0b0100:
      case 0b0000:
      case 0b0001:
      case 0b0010:
      default:
        return TILE_EMPTY; break;
    }
  }

public:
  PlayGround()
  {
    clear();
  }

  /// Initializes self based on given map.
  void loadMap(const GameMap *gameMap)
  {
    clear();

    // draw the paths on the mPlayground
    for (uint8_t i = 0; i < gameMap->getNumPaths(); ++i)
    {
      const CreepPath *p = gameMap->getPath(i);
      BytePosition pos   = p->getStart();

      char xIncr, yIncr;
      bool first = true;
      TileType previousDirection;

      for (uint8_t j = 0; j < p->getNumSegments(); ++j)
      {
        PathSegment s = p->getSegment(j);
        
        getXYIncrement(s.getDirection(),&xIncr,&yIncr);

        if (first)
        {
          previousDirection = s.getDirection();
          first = false;
        }

        for (uint8_t k = 0; k < s.getLength(); ++k)  
        {
          TileType newTile =
            makePathTile(s.getDirection(),previousDirection,
            getTile(pos)->mType);

          setTile(pos,newTile);
          previousDirection = s.getDirection();

          pos.mX += xIncr;
          pos.mY += yIncr;
        }
      }

      setTile(p->getStart(),Tile(TILE_CREEP_START)); 
      setTile(pos,Tile(TILE_CREEP_EXIT));
    }
  }

  void clear()
  {
    for (uint8_t i = 0; i < TILES_TOTAL; ++i)
      mTiles[i] = Tile(TILE_EMPTY);
  }

  Tile *getTile(BytePosition position)
  {
    return &(mTiles[index(position.mX,position.mY)]);
  }

  void setTile(BytePosition position, Tile tile)
  {
    mTiles[index(position.mX,position.mY)] = tile;
  }
};

struct GameMenuItem
{
  MenuItemState  mState; ///< ITEM_STATE_x
  const uint8_t    *mIcon;  ///< Pointer to an icon image.
  uint8_t           mPrice;
  char           mText[32];
};

struct MainMenuItem
{
  MenuItemState mState;
  char          mText[16];
  char          mSubText[16];
};

/**
  Puts together other components to represent the whole game state. This is
  not a class but rather a struct in order for the main program functions to be
  able to access the game state without tons of setters/getters.
 */
struct Game
{
  GameState mState;

  GameMap mMap;

  Spawner01 mSpawner01;
  Spawner2  mSpawner2;
  Spawner3  mSpawner3;
  Spawner4  mSpawner4;

  uint8_t mCheat;
  uint8_t mMapNumber;

  PlayGround mPlayground;

  CreepInstance mCreepArray[MAX_CREEPS];
  uint16_t mCreepCount;

  uint16_t mFrame;

  uint16_t mMoney;
  uint16_t mLives;
  uint8_t     mRound;

  BytePosition mCursor;

  uint8_t mSelectedMenuItem;

  bool mSound;

  uint8_t mRecords[MAPS_TOTAL]; ///< Record round for each map.

  Game()
  {
    mState = STATE_MENU;
    mFrame = 0;
    mCreepCount = 0;
    mSelectedMenuItem = 0;
    mCheat = CHEAT_NONE;

    mSound = false;

    for (uint8_t i = 0; i < MAPS_TOTAL; ++i)
      mRecords[i] = 0;

    mMapNumber = 0;
  }

  /// Checks whether a record high-score has been achieved.
  bool isRecord()
  {
    return mRound > mRecords[mMapNumber];
  }

  /// Call when a specific button has been pressed.
  void buttonDown(uint8_t button)
  {
    switch (mState)
    {
      case STATE_PLAYING_BUILDING:
        if (button == A_BUTTON)
        {
          mState = STATE_PLAYING_MENU;

          // potentially adjust the cursor to a nearby big tower

          if (mCursor.mX < TILES_X - 1 && mCursor.mY < TILES_Y - 1)
          {
            if (mPlayground.getTile(mCursor.right())->hasBigTower())
              mCursor.mX += 1;
            else if (mPlayground.getTile(mCursor.down())->hasBigTower())
              mCursor.mY += 1;
            else if (mPlayground.getTile(
                mCursor.right().down())->hasBigTower())
              mCursor = mCursor.right().down();
          }
        }
        [[fallthrough]];

      case STATE_PLAYING_WAVE:
        switch (button)
        {
          case DOWN_BUTTON:
            mCursor.mY = min(mCursor.mY + 1, TILES_Y - 1);
            break;

          case UP_BUTTON:
            mCursor.mY = mCursor.mY == 0 ? 0 : mCursor.mY - 1;
            break;

          case RIGHT_BUTTON:
            mCursor.mX = min(mCursor.mX + 1, TILES_X - 1);
            break;

          case LEFT_BUTTON:
            mCursor.mX = mCursor.mX == 0 ? 0 : mCursor.mX - 1;
            break;

          default:
            break;
        }

        break;

      case STATE_PLAYING_MENU:
        switch (button)
        {
          case A_BUTTON:
            gameMenuConfirm();
            break;          

          case B_BUTTON:
            mState = STATE_PLAYING_BUILDING;
            mSelectedMenuItem = 0;
            break;          

          case RIGHT_BUTTON:
            mSelectedMenuItem = (mSelectedMenuItem + 1) % 13;
            break;

          case LEFT_BUTTON:
            mSelectedMenuItem =
              mSelectedMenuItem == 0 ? 12 : mSelectedMenuItem - 1;
            break;
 
          default:
            break;
        }

        break;

      case STATE_MENU:
        switch (button)
        {
          case RIGHT_BUTTON:
            mSelectedMenuItem = (mSelectedMenuItem + 1) % MAIN_MENU_ITEMS;
            break;

          case LEFT_BUTTON:
            mSelectedMenuItem =
              mSelectedMenuItem == 0 ? (MAIN_MENU_ITEMS - 1) :
                mSelectedMenuItem - 1;
            break;

          case A_BUTTON:
            if (mSelectedMenuItem < MAPS_TOTAL)
            {
              // new map
              mCreepCount = 0;
              createMap(mSelectedMenuItem);
              mPlayground.loadMap(&mMap);
              mMapNumber = mSelectedMenuItem;
              mRound = 0;
              mMoney = mMap.getStartMoney();
              mLives = mMap.getStartLives();
              mCursor.mX = 8;
              mCursor.mY = 4;
              mSelectedMenuItem = 0;
              srand(0); // make the game deterministic

              mState = STATE_PLAYING_BUILDING;
            }
            else if (mSelectedMenuItem == MAPS_TOTAL)
              mSound = !mSound;

            break;

          default:
            break;
        }

        break;

      case STATE_CONFIRM:
        if (button == B_BUTTON)
        {
          mState = STATE_PLAYING_MENU;
        }
        else if (button == A_BUTTON)
        {
          if (mSelectedMenuItem == GAME_MENU_QUIT)
          {
            mState = STATE_MENU;
            mSelectedMenuItem = 0;
          }
          else if (mSelectedMenuItem == GAME_MENU_DESTROY)
          {
            Tile *tile = mPlayground.getTile(mCursor);
            mMoney += tile->mTower.getType().mPrice / 2;
            // ^ give back half the original price
            mPlayground.setTile(mCursor,Tile(TILE_EMPTY));
            playSound(SOUND_BAD);

            mState = STATE_PLAYING_BUILDING;
          }
        }

        break;

      case STATE_GAME_OVER:
        if (button == A_BUTTON || button == B_BUTTON)
        {
          if (isRecord())
            mRecords[mMapNumber] = mRound;

          mState = STATE_MENU;
          mSelectedMenuItem = 0;
        }  

        break;

      default:
        break;
    }
  }

  /// Call when in game menu and a confirm button was pressed.
  void gameMenuConfirm()
  {
    if (getGameMenuItem(mSelectedMenuItem).mState != ITEM_STATE_AVAILABLE)
      return;

    if (buildSelectedTower())
      return;

    switch (mSelectedMenuItem)
    {
      case GAME_MENU_UPGRADE1:
      case GAME_MENU_UPGRADE2:
      {
        Tile *tile = mPlayground.getTile(mCursor);
        uint8_t cost = tile->mTower.getType().getUpgrageCost();

        if (mMoney >= cost || mCheat == CHEAT_MONEY)
        {
          mPlayground.getTile(mCursor)->mTower.upgrade(
            (mSelectedMenuItem == GAME_MENU_UPGRADE1) ? 0 : 1);        

          mMoney -= cost;
        }

        break;
      }

      case GAME_MENU_DESTROY:
        mState = STATE_CONFIRM;
        break;

      case GAME_MENU_NEXT_WAVE:
        mMap.getSpawner()->spawnCreeps(mRound,&mMap,mCreepArray,&mCreepCount);
        mState = STATE_PLAYING_WAVE;
        mSelectedMenuItem = 0;
        break;

      case GAME_MENU_QUIT:
        mState = STATE_CONFIRM;
        break;

      default:
        break; 
    }
  }

  /** If possible, builds a tower currently selected in the menu at the
      cursor position, substracts money, exits menu and returns true
      (otherwise false). */
  bool buildSelectedTower()
  {
    if (mSelectedMenuItem >= 8)
      return false;                   // no tower selected

    if (getGameMenuItem(mSelectedMenuItem).mState == ITEM_STATE_UNAVAILABLE)
      return false;                   // can't build
 
    TowerType tt = towerTypes[mSelectedMenuItem];

    if (tt.mPrice > mMoney && mCheat != CHEAT_MONEY)
      return false;                   // not enough money

    // build the tower

    mMoney -= tt.mPrice;

    mPlayground.setTile(mCursor,Tile(TILE_TOWER,mSelectedMenuItem)); 
    mState = STATE_PLAYING_BUILDING;  // cancel menu
    mSelectedMenuItem = 0;

    playSound(SOUND_PUT);
    return true;
  }

  MainMenuItem getMainMenuItem(uint8_t index)
  {
    MainMenuItem result;

    result.mText[0] = 0;
    result.mSubText[0] = 0;

    result.mState = ITEM_STATE_AVAILABLE;

    if (index < MAPS_TOTAL)
    {
      // saves memory by not saving strings into RAM
      result.mText[0] = ' ';
      result.mText[1] = ' ';
      result.mText[2] = 'm';
      result.mText[3] = 'a';
      result.mText[4] = 'p';
      result.mText[5] = ' ';
      result.mText[6] = '0' + index + 1;
      result.mText[7] = ' ';
      result.mText[8] = ' ';
      result.mText[9] = 0;

      result.mSubText[0] = 'b';
      result.mSubText[1] = 'e';
      result.mSubText[2] = 's';
      result.mSubText[3] = 't';
      result.mSubText[4] = ':';
      result.mSubText[5] = ' ';

      uint8_t record = mRecords[index];
      if (record >= 100)
      {
        result.mSubText[6] = '0' + (record / 100);
        result.mSubText[7] = '0' + ((record / 10) % 10);
        result.mSubText[8] = '0' + (record % 10);
        result.mSubText[9] = 0;
      }
      else if (record >= 10)
      {
        result.mSubText[6] = '0' + (record / 10);
        result.mSubText[7] = '0' + (record % 10);
        result.mSubText[8] = 0;
      }
      else
      {
        result.mSubText[6] = '0' + record;
        result.mSubText[7] = 0;
      }
    }
    else if (index == MAPS_TOTAL)
    {
      result.mText[0] = ' ';
      result.mText[1] = 's';
      result.mText[2] = 'n';
      result.mText[3] = 'd';
      result.mText[4] = ':';
      result.mText[5] = ' ';
      result.mText[6] = 'o';
      result.mText[7] = mSound ? 'n' : 'f';
      result.mText[8] = mSound ? ' ' : 'f';
      result.mText[9] = 0;
    }
    else
    {
      result.mState = ITEM_STATE_NULL;
    }

    return result;
  }

  GameMenuItem getGameMenuItem(uint8_t index)
  {
    GameMenuItem result;

    if (index < 13)  // total items
    {
      result.mState = ITEM_STATE_AVAILABLE;
      result.mPrice = 0;

      Tile *tile = mPlayground.getTile(mCursor);

      TowerType tt = tile->mType == TILE_TOWER ? tile->mTower.getType() :
        towerTypes[0];

      if (index < 6) // small tower icons
        result.mIcon = towerSmallImages + index * TILE_SIZE * 3;
      else           // big towers + other icons
        result.mIcon = menuIcons + (index - 6) * TILE_SIZE;

      if (index < GAME_MENU_UPGRADE1) // towers
      {
        if (tile->mType != TILE_EMPTY)
        {
          // can't build on non-empty tiles
          result.mState = ITEM_STATE_UNAVAILABLE;
        }
        else if (index >= 6)
        {
          // big towers need more space
          
          result.mState =
            mCursor.mX > 0 && mCursor.mY > 0 &&
            mPlayground.getTile(mCursor.left())->mType == TILE_EMPTY &&
            mPlayground.getTile(mCursor.up())->mType == TILE_EMPTY &&
            mPlayground.getTile(mCursor.left().up())->mType == TILE_EMPTY
            ?
            ITEM_STATE_AVAILABLE : ITEM_STATE_UNAVAILABLE;
        }
      }

      if (index < GAME_MENU_UPGRADE1)             // towers
      {
        result.mPrice = towerTypes[index].mPrice;
      }
      else if (index < GAME_MENU_DESTROY)
      {
        if (tile->mTower.upgraded((index == GAME_MENU_UPGRADE1) ? 0 : 1))
        {
          result.mState = ITEM_STATE_UNAVAILABLE; // already upgraded
          result.mPrice = 0;
        }
        else // upgrades
        {
          if (tile->mType == TILE_TOWER)
            result.mPrice = tt.getUpgrageCost();
          else
          {
            result.mState = ITEM_STATE_UNAVAILABLE;
            result.mPrice = 0;
          }
        }
      }
      else
        result.mPrice = 0;

      if (index == GAME_MENU_DESTROY && tile->mType != TILE_TOWER)
        result.mState = ITEM_STATE_UNAVAILABLE;

      if (index == GAME_MENU_UPGRADE2 &&
           (
             tile->mTower.getTypeIndex() == TOWER_WATER ||
             tile->mTower.getTypeIndex() == TOWER_FIRE
           )
         )
        result.mState = ITEM_STATE_UNAVAILABLE; // no upgrade 2 for big towers

      if (result.mPrice > mMoney && mCheat != CHEAT_MONEY)
        result.mState = ITEM_STATE_UNAVAILABLE;

      uint8_t pos = 0;

      switch (index)
      {
        // saves RAM
        #define C(c) result.mText[pos] = c; pos++;

        case TOWER_GUARD:   C('g')C('u')C('a')C('r')C('d')C(0)       break; 
        case TOWER_CANNON:  C('c')C('a')C('n')C('n')C('o')C('n')C(0) break;
        case TOWER_ICE:     C('i')C('c')C('e')C(0)                   break; 
        case TOWER_ELECTRO: C('e')C('l')C('e')C('c')C('t')C('r')C(0) break; 
        case TOWER_SNIPER:  C('s')C('n')C('i')C('p')C('e')C('r')C(0) break; 
        case TOWER_MAGIC:   C('m')C('a')C('g')C('i')C('c')C(0)       break; 
        case TOWER_WATER:   C('w')C('a')C('t')C('e')C('r')C(0)       break; 
        case TOWER_FIRE:    C('f')C('i')C('r')C('e')C(0)             break; 
        case GAME_MENU_UPGRADE1:
        case GAME_MENU_UPGRADE2:
          if (tile->mType == TILE_TOWER)
            getUpgradeName(
              tt.mUpgrades[index == GAME_MENU_UPGRADE1 ? 0 : 1],
              result.mText
            );
          else
            result.mText[0] = 0;

          break;
        case GAME_MENU_DESTROY:   C('s')C('e')C('l')C('l')C(0)      break; 
        case GAME_MENU_NEXT_WAVE: C('g')C('o')C('!')C(0)            break; 
        case GAME_MENU_QUIT:      C('q')C('u')C('i')C('t')C(0)      break; 
        default: result.mText[0] = 0;                               break;

        #undef C
      }
    }
    else
    {
      result.mState = ITEM_STATE_NULL; // no item here
      result.mText[0] = 0;
    }

    return result;
  }

  void createMap(uint8_t i)
  {
    mMoney = 0;
    mRound = 0;
    mLives = 0;

    CreepPath path1, path2;

    mMap.clear();

    #define newSeg(p,d,l) p.addSegment(PathSegment(DIRECTION_##d,l))

    switch (i)
    {
      case 0:
        path1.setStart(BytePosition(1,1));
        newSeg(path1,S,5);
        newSeg(path1,E,2);
        newSeg(path1,N,5);
        newSeg(path1,E,8);
        newSeg(path1,S,2);
        newSeg(path1,W,5);
        newSeg(path1,S,3);
        newSeg(path1,E,7);
        newSeg(path1,N,5);
        mMap.addPath(path1);
        mMap.setStartValues(20,10);
        mMap.setSpawner(&mSpawner01);
        break;

      case 1:
        path1.setStart(BytePosition(1,1));
        newSeg(path1,S,2);
        newSeg(path1,E,7);
        newSeg(path1,S,1);
        newSeg(path1,E,2);
        newSeg(path1,S,2);
        newSeg(path1,E,4);
        newSeg(path1,N,3);
        newSeg(path1,W,2);
        newSeg(path1,N,2);
        newSeg(path1,W,4);
        path2.setStart(BytePosition(1,6));
        newSeg(path2,N,3);
        newSeg(path2,E,3);
        newSeg(path2,S,2);
        newSeg(path2,E,1);
        newSeg(path2,S,1);
        newSeg(path2,E,9);
        newSeg(path2,N,3);
        newSeg(path2,W,2);
        newSeg(path2,N,2);
        newSeg(path2,W,4);
        mMap.addPath(path1);
        mMap.addPath(path2);
        mMap.setStartValues(25,10);
        mMap.setSpawner(&mSpawner01);
        break;
 
      case 2:
        path1.setStart(BytePosition(1,1));
        newSeg(path1,S,5);
        newSeg(path1,E,2);
        newSeg(path1,N,2);
        newSeg(path1,E,2);
        newSeg(path1,S,2);
        newSeg(path1,E,3);
        newSeg(path1,N,3);
        newSeg(path1,W,2);
        newSeg(path1,N,2);
        newSeg(path1,W,3);
        path2.setStart(BytePosition(14,1));
        newSeg(path2,W,4);
        newSeg(path2,S,5);
        newSeg(path2,E,2);
        newSeg(path2,N,3);
        newSeg(path2,E,2);
        newSeg(path2,S,3);
        mMap.addPath(path1);
        mMap.addPath(path2);
        mMap.setStartValues(20,10);
        mMap.setSpawner(&mSpawner2);
        break;

      case 3:
        path1.setStart(BytePosition(2,7));
        newSeg(path1,N,2);
        newSeg(path1,E,12);
        newSeg(path1,S,2);
        newSeg(path1,W,7);
        newSeg(path1,N,6);
        newSeg(path1,W,2);
        newSeg(path1,S,2);
        newSeg(path1,W,4);
        newSeg(path1,N,1);
        newSeg(path1,E,3);
        path2.setStart(BytePosition(2,7));
        newSeg(path2,N,2);
        newSeg(path2,E,12);
        newSeg(path2,S,2);
        newSeg(path2,W,7);
        newSeg(path2,N,6);
        newSeg(path2,E,2);
        newSeg(path2,S,2);
        newSeg(path2,E,4);
        newSeg(path2,N,1);
        newSeg(path2,W,3);
        mMap.addPath(path1);
        mMap.addPath(path2);
        mMap.setStartValues(10,8);
        mMap.setSpawner(&mSpawner3);
        break;

      case 4:
        path1.setStart(BytePosition(4,4));
        newSeg(path1,N,2);
        newSeg(path1,W,3);
        newSeg(path1,S,4);
        newSeg(path1,E,5);
        newSeg(path1,N,5);
        newSeg(path1,E,2);
        newSeg(path1,S,5);
        newSeg(path1,E,3);
        newSeg(path1,N,5);
        newSeg(path1,E,3);
        path2.setStart(BytePosition(13,6));
        newSeg(path2,N,3);
        newSeg(path2,W,5);
        newSeg(path2,S,3);
        newSeg(path2,W,2);
        newSeg(path2,N,3);
        newSeg(path2,W,1);
        newSeg(path2,N,2);
        newSeg(path2,W,3);
        newSeg(path2,S,3);
        mMap.addPath(path1);
        mMap.addPath(path2);
        mMap.setStartValues(30,22);
        mMap.setSpawner(&mSpawner4);
        break;

      default:
        break;
    }

    #undef newSeg
  }

  void applySpeedAura(BytePosition center)
  {
    uint8_t y0 = center.mY > 0 ? center.mY - 1 : 0;
    uint8_t y1 = min(center.mY + 1, TILES_Y - 1);
    uint8_t x0 = center.mX > 0 ? center.mX - 1 : 0;
    uint8_t x1 = min(center.mX + 1, TILES_X - 1);

    for (uint8_t y = y0; y <= y1; ++y)
      for (uint8_t x = x0; x <= x1; ++x)
      {
        if (x == center.mX && y == center.mY)
          continue;

        bool apply = false;

        Tile *t = mPlayground.getTile(BytePosition(x,y));

        if (t->mType == TILE_TOWER)
        { 
          apply = t->mTower.getTypeIndex() != TOWER_MAGIC;
        }
        else if (x == x1 && x < TILES_X - 1)
        {
          // check for big tower base
          t = mPlayground.getTile(BytePosition(x + 1,y));
          apply = t->mType == TILE_TOWER && t->mTower.getType().isBig();
        }

        if (apply)
          t->mTower.setAttackProgress(
            min(7, t->mTower.getAttackProgress() + 2));
      }
  }

  void update()
  {
    mFrame++;

    switch (mState)
    {
      case STATE_PLAYING_WAVE:
      {
        // update creeps
        for (uint8_t i = 0; i < mCreepCount; ++i)
        { 
          CreepInstance *creep = &(mCreepArray[i]);
          creep->update(mFrame);

          if (creep->isAlive() && creep->exited(mMap.getPaths()))
          {
            creep->kill();

            mLives = max(0,((int16_t) mLives) - creep->getType().mReward);

            if (mLives == 0)
            {
              mState = STATE_GAME_OVER;
              playSound(SOUND_LOST);
              break;
            }

            playSound(SOUND_BAD);

            continue;
          }

          if (!creep->isAlive() && creep->getTypeIndex() == CREEP_SPIDER_BIG &&
              !creep->exited(mMap.getPaths()))
          {
            // replace big spiders that die with two small ones

            mCreepArray[i] = CreepInstance(CREEP_SPIDER,
              mCreepArray[i].getPath(),mCreepArray[i].getPosition(),0);

            CreepInstance spider2 = CreepInstance(CREEP_SPIDER,
              mCreepArray[i].getPath(),mCreepArray[i].getPosition() - 10,0);

            bool secondAdded = false;

            if (mCreepCount < MAX_CREEPS)
            {
              mCreepArray[mCreepCount] = spider2;
              mCreepCount++;
              secondAdded = true;
            }
            else
            {
              // try to find a dead creep to replace with the spider

              for (uint8_t i = 0; i < mCreepCount; ++i)
                if (!mCreepArray[i].isAlive())
                {
                  mCreepArray[i] = spider2;
                  secondAdded = true;
                }  
            }            

            if (!secondAdded)
              mCreepArray[i].setHealthLives(mCreepArray[i].getLives(),1);
              // ^ if two spiders can't be made, make one with two lives 
          }
        }

        if (mLives == 0)
          break;

        // update towers

        for (uint8_t y = 0; y < TILES_Y; ++y)
          for (uint8_t x = 0; x < TILES_X; ++x)
           {
             Tile *t = mPlayground.getTile(BytePosition(x,y));

             if (t->mType == TILE_TOWER)
             {
               t->mTower.update(mCreepArray,mCreepCount,mMap.getPaths(),
                 BytePosition(x,y),mFrame, &mMoney);

               if (mFrame % 8==0 && t->mTower.hasUpgraded(UPGRADE_SPEED_AURA))
                 applySpeedAura(BytePosition(x,y));
             }
           }

        // check if the wave is over

        bool over = true;

        for (uint8_t i = 0; i < mCreepCount; ++i)
          if (mCreepArray[i].isAlive())
          {
            over = false;
            break;
          }

        if (over)
        {
          // wave over

          mMoney += WAVE_BASE_REWARD + mRound / 4;
 
          mRound++;

          mState = STATE_PLAYING_BUILDING;

          // clear potential left over targets

          for (uint8_t y = 0; y < TILES_Y; ++y)
            for (uint8_t x = 0; x < TILES_X; ++x)
             {
               Tile *t = mPlayground.getTile(BytePosition(x,y));
               t->mTower.clearTarget();
             }

          mCreepCount = 0; // clear creeps
        }

        break;
      }

      default:
        break;
    }
  }
};

Game game;

#ifdef ARDUINO

// ========= Arduboy code only ==========

bool microtd_handle_back_long()
{
  if (game.mState == STATE_MENU)
    return true; // long Back in main menu exits app

  // long Back in gameplay returns to main menu
  game.mState = STATE_MENU;
  game.mSelectedMenuItem = game.mMapNumber < MAPS_TOTAL ? game.mMapNumber : 0;
  game.mCheat = CHEAT_NONE;
  return false;
}

void drawConfirmDialog()
{
  const uint8_t padding = 10;

  arduboy.fillRect(padding,padding,DISPLAY_WIDTH - 2 * padding,
    DISPLAY_HEIGHT - 2 * padding,WHITE);

  arduboy.drawRect(padding,padding,DISPLAY_WIDTH - 2 * padding,
    DISPLAY_HEIGHT - 2 * padding,BLACK);
 
  arduboy.setTextColor(BLACK);
  arduboy.setTextBackground(WHITE);

  arduboy.setCursor(padding + 8, padding + 18);

  arduboy.print(F("Really? A=y B=n"));
}

void drawMenu()
{
  arduboy.fillRect(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,WHITE);

  arduboy.fillRect(LOGO_X,LOGO_Y,25,18,BLACK);
  arduboy.drawBitmap(LOGO_X,LOGO_Y,logoImage,25,18,WHITE);

  const uint8_t margin = 10;

  Sprites::drawOverwrite(LOGO_X - TILE_WIDTH * 2 - margin,LOGO_Y,
    towerBigSprites,0);

  Sprites::drawOverwrite(LOGO_X + 25 + margin,LOGO_Y,towerBigSprites,2);

  // draw decoration wave

  arduboy.fillRect(0,28,DISPLAY_WIDTH,5,BLACK);

  for (uint8_t i = 0; i < DISPLAY_WIDTH / 15 + 2; ++i)
    arduboy.drawBitmap(i * 15 - game.mFrame % 15,28,waveImage,15,5,WHITE);

  MainMenuItem item = game.getMainMenuItem(game.mSelectedMenuItem);

  arduboy.fillRect(0,38,DISPLAY_WIDTH,11,BLACK);

  arduboy.setTextColor(WHITE);
  arduboy.setTextBackground(BLACK);
  arduboy.setCursor(22,40);

  arduboy.write('<');
  arduboy.write(' ');
  arduboy.print(item.mText);
  arduboy.write(' ');
  arduboy.write('>');

  arduboy.setTextColor(BLACK);
  arduboy.setTextBackground(WHITE);
  arduboy.setCursor(2,55);
  arduboy.print(item.mSubText);

  if (game.mCheat != CHEAT_NONE && (game.mFrame / 4) % 2 == 0)
  {
    arduboy.setCursor(47,26);
    arduboy.print(F("cheat!"));
  }
}

void drawGameOver()
{
  arduboy.fillRect(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,WHITE);

  arduboy.setTextColor(BLACK);
  arduboy.setTextBackground(WHITE);
  arduboy.setCursor(20,10);

  arduboy.print(F("game over at "));
  arduboy.print(game.mRound);

  if (game.isRecord())
  {
    arduboy.setCursor(20,20);
    arduboy.print(F("new record!"));
  }
}

void drawMap()
{
  uint8_t rangeToDraw = 255; // tower range in pixels or 255 (don't draw range)
  BytePosition rangeToDrawCenter;
  bool drawAura = false;

  for (uint8_t y = 0; y < TILES_Y; ++y)
    for (uint8_t x = 0; x < TILES_X; ++x)
    {
      Tile* t = game.mPlayground.getTile(BytePosition(x,y));

      if (t->mType == TILE_TOWER ||  // real tower
          (
            // tower preview
            game.mState == STATE_PLAYING_MENU &&
            game.mCursor.mX == x &&
            game.mCursor.mY == y &&
            game.mSelectedMenuItem < 8 &&
            (game.mFrame >> 3) % 4 != 0 &&
            game.getGameMenuItem(game.mSelectedMenuItem).mState ==
              ITEM_STATE_AVAILABLE
          )
        )
      {
        TowerType tt;

        if (t->mType == TILE_TOWER)
        {
          tt = t->mTower.getType();
        }
        else
        {
          // tower preview
          tt = towerTypes[game.mSelectedMenuItem];
          rangeToDraw = tt.mRange;
          rangeToDrawCenter = tt.getCenter(BytePosition(x,y));
        }

        if (tt.mSprite >= TOWER_WATER)
        {
          // big (2 x 2 tiles) tower

          Sprites::drawOverwrite(
            (x - 1) * TILE_WIDTH,
            (y - 1) * TILE_HEIGHT,
            towerBigSprites,
              (tt.mSprite == TOWER_WATER ? 0 : 2) +
              (t->mTower.upgraded(0) ? 1 : 0));
        }
        else
        {
          // small (1 x 1 tile) tower

          for (uint8_t i = 0; i < 3; ++i)
            if (i == 0 || !t->mTower.upgraded(i == 1 ? 0 : 1))
              arduboy.drawBitmap(
                x * TILE_WIDTH,
                y * TILE_HEIGHT,
                towerSmallImages + ((uint16_t) tt.mSprite) * TILE_SIZE * 3 +
                  i * TILE_SIZE,
                TILE_WIDTH,
                TILE_HEIGHT,
                WHITE);
        }
      } 
      else
      {
        // non-tower tile

        arduboy.drawBitmap(
          x * TILE_WIDTH,
          y * TILE_HEIGHT,
          tileImages + t->mType * TILE_SIZE,
          TILE_WIDTH,
          TILE_HEIGHT,
          WHITE);
      }
    }

  // draw projectiles

  for (uint8_t y = 0; y < TILES_Y; ++y)
    for (uint8_t x = 0; x < TILES_X; ++x)
    {
      Tile* t = game.mPlayground.getTile(BytePosition(x,y));

      if (t->mType == TILE_TOWER)
      {
        IndexPointer target = t->mTower.getTarget();

        if (target != 255)
        {
          CreepInstance c = game.mCreepArray[target];

          if (!c.isAlive())
            continue;

          BytePosition p = c.getPixelPosition(game.mMap.getPaths());

          uint8_t attackProgress = t->mTower.getAttackProgress();
          uint8_t attackPercentage = attackProgress * 12;
                                  // ^ * 12 roughly converts to percents

          BytePosition selfPos = t->mTower.getCenter(BytePosition(x,y));

          uint8_t projX = interpolateBytes(selfPos.mX,p.mX,attackPercentage);
          uint8_t projY = interpolateBytes(selfPos.mY,p.mY,attackPercentage);

          switch (t->mTower.getTypeIndex())
          { 
            case TOWER_GUARD:
              // arrow
              if (absDiff(selfPos.mX,p.mX) > absDiff(selfPos.mY,p.mY))
                arduboy.drawFastHLine(projX,projY,2,BLACK);
              else
                arduboy.drawFastVLine(projX,projY,2,BLACK);

              break;

            case TOWER_CANNON:
              // cannon ball

              arduboy.drawFastVLine(projX - 1,projY,2,BLACK);
              arduboy.drawFastVLine(projX,    projY,2,BLACK);
              break;

            case TOWER_ICE:
              // ice
              arduboy.drawPixel(projX,projY,BLACK);
              arduboy.drawPixel(projX + 1,projY + 1,BLACK);
              arduboy.drawPixel(projX - 1,projY + 1,BLACK);
              arduboy.drawPixel(projX + 1,projY - 1,BLACK);
              arduboy.drawPixel(projX - 1,projY - 1,BLACK);
              break;

            case TOWER_SNIPER:
            {
              // bullet
              uint8_t x2 = interpolateBytes(projX,selfPos.mX,25);
              uint8_t y2 = interpolateBytes(projY,selfPos.mY,25);
              arduboy.drawLine(projX,projY,x2,y2,BLACK);
              break;
            }

            case TOWER_MAGIC:
              // spell
              arduboy.drawCircle(projX,projY,1,BLACK);
              break;

            case TOWER_ELECTRO:
              // lightning

              if (attackPercentage < 50)
                arduboy.drawLine(selfPos.mX,selfPos.mY,
                  p.mX -1 + game.mFrame % 3,p.mY -1 + game.mFrame % 4,BLACK);

              break;

            case TOWER_WATER:
              //water

              arduboy.fillCircle(projX,projY,attackProgress / 2 + 1,BLACK);

              break;

            case TOWER_FIRE:
            {
              // fire

              for (uint8_t i = 0; i < (attackProgress + 1) * 3; ++i)
                arduboy.drawPixel(
                  projX - 2 + rand() % (attackProgress + 2),
                  projY - 2 + rand() % (attackProgress + 2), BLACK);

              break;
            }

            default:
              break;
          }
        }
      }
    }

  // draw creeps

  for (uint8_t i = 0; i < game.mCreepCount; ++i)
  {  
    CreepInstance *creep = &(game.mCreepArray[i]);

    if (creep->entered() && creep->isAlive())
    {
      BytePosition p = creep->getPixelPosition(game.mMap.getPaths());
      CreepType ct = creep->getType();
      uint8_t bob = (creep->getPosition() >> 2) % 2;

      Sprites::drawPlusMask(
        p.mX - TILE_WIDTH_HALF, p.mY - TILE_HEIGHT_HALF + bob,
        creepSprites,ct.mSprite);
    }
  }

  // draw cursor

  BytePosition corner(
    game.mCursor.mX * TILE_WIDTH,game.mCursor.mY * TILE_HEIGHT);

  arduboy.drawRect(corner.mX,corner.mY,TILE_WIDTH,TILE_HEIGHT,BLACK);

  for (uint8_t i = 0; i < TILE_WIDTH; ++i)
  {
    if (((game.mFrame >> 3) + i) % 3 == 0)
    {
      arduboy.drawPixel(corner.mX + i,corner.mY,WHITE);
      arduboy.drawPixel(corner.mX + TILE_WIDTH - 1,corner.mY + i,WHITE);
      arduboy.drawPixel(corner.mX + TILE_WIDTH - i,corner.mY + TILE_HEIGHT - 1,
        WHITE);
      arduboy.drawPixel(corner.mX,corner.mY + TILE_HEIGHT - i,WHITE);
    }
  }

  if (game.mState == STATE_PLAYING_MENU || !arduboy.pressed(B_BUTTON))
  {
    // draw the infobar

    uint8_t y = (game.mState != STATE_PLAYING_MENU && game.mCursor.mY < 1) ?
      DISPLAY_HEIGHT - INFOBAR_Y - 7 : INFOBAR_Y;

    arduboy.setTextColor(BLACK);
    arduboy.setTextBackground(WHITE);

    arduboy.setCursor(1,y);
    arduboy.write('$');

    if (game.mCheat == CHEAT_MONEY)
    {
      arduboy.write('!');
      arduboy.write('!');
      arduboy.write('!');
    }
    else
      arduboy.print(game.mMoney);

    arduboy.setCursor(1 + 5 * 8,y);
    arduboy.write('L');
    arduboy.print(game.mLives);

    arduboy.setCursor(1 + 11 * 8,y);
    arduboy.write('R');
    arduboy.print(game.mRound);
  }

  if (game.mState == STATE_PLAYING_WAVE && arduboy.pressed(A_BUTTON))
  {
    // draw health bars

    for (uint8_t i = 0; i < game.mCreepCount; ++i)
    {  
      CreepInstance creep = game.mCreepArray[i];

      if (creep.entered() && creep.isAlive())
      {
        BytePosition p = creep.getPixelPosition(game.mMap.getPaths());
        CreepType ct = creep.getType();

        uint8_t healthFraction =
          ((uint16_t) creep.getHealth()) * 5 / ct.mMaxHealth;

        arduboy.fillRect(p.mX - 3, p.mY - 6, 6, 3,WHITE);
        arduboy.drawFastHLine(p.mX - 2, p.mY - 5,healthFraction,BLACK);
      }
    }
  }

  Tile *cursorTile = game.mPlayground.getTile(game.mCursor);

  if (game.mState == STATE_PLAYING_MENU && cursorTile->mType == TILE_TOWER)
  {
    rangeToDraw = cursorTile->mTower.getRange();
    drawAura = cursorTile->mTower.hasUpgraded(UPGRADE_SPEED_AURA);

    if (game.mState == STATE_PLAYING_MENU && (game.mFrame >> 3) % 2 == 0)
    {

      #define helperCond(u,i,upgrade)\
       (game.mSelectedMenuItem == GAME_MENU_UPGRADE##u &&\
       game.getGameMenuItem(GAME_MENU_UPGRADE##u).mState ==\
       ITEM_STATE_AVAILABLE &&\
       cursorTile->mTower.getType().mUpgrades[i] == upgrade)

      if (helperCond(1,0,UPGRADE_RANGE) || helperCond(2,1,UPGRADE_RANGE))
        // range upgrade preview
        rangeToDraw += rangeToDraw / UPGRADE_RANGE_FRACTION_INCREASE;
      else if (helperCond(1,0,UPGRADE_SPEED_AURA) ||
        helperCond(2,1,UPGRADE_SPEED_AURA))
        drawAura = true; // aura upgrade preview

      #undef helperCond
    }

    rangeToDrawCenter = cursorTile->mTower.getType().getCenter(game.mCursor);
  }

  if (rangeToDraw != 255)
  {
    // draw the tower range
    arduboy.drawCircle(rangeToDrawCenter.mX,rangeToDrawCenter.mY,
      rangeToDraw,BLACK);

    if (drawAura)
      arduboy.drawRect(
        rangeToDrawCenter.mX - TILE_WIDTH_HALF - TILE_WIDTH,
        rangeToDrawCenter.mY - TILE_HEIGHT_HALF - TILE_HEIGHT,
        TILE_WIDTH * 3, TILE_HEIGHT * 3, BLACK);
  }

  if (game.mState == STATE_PLAYING_MENU)
  {
    // draw game menu

    arduboy.fillRect(0,DISPLAY_HEIGHT - TILE_HEIGHT - 1,DISPLAY_WIDTH,
      TILE_HEIGHT + 1,WHITE);

    arduboy.drawFastHLine(0,DISPLAY_HEIGHT - TILE_HEIGHT - 1,DISPLAY_WIDTH,
      BLACK); 

    uint8_t y = DISPLAY_HEIGHT - TILE_HEIGHT;

    for (uint8_t i = 0; i < DISPLAY_WIDTH / TILE_WIDTH; ++i)
    {
      GameMenuItem item = game.getGameMenuItem(i);

      if (item.mState == ITEM_STATE_NULL)
        break;

      uint8_t x = i * TILE_WIDTH;

      if (game.mSelectedMenuItem == i)
      {
        arduboy.drawBitmap(x,y,item.mIcon,TILE_WIDTH,TILE_HEIGHT,BLACK);
        arduboy.setCursor(1,y - 9);
        arduboy.print(item.mText);

        if (item.mPrice > 0)
        {
          arduboy.write(' ');
          arduboy.write('(');
          arduboy.write('$');
          arduboy.print(item.mPrice);
          arduboy.write(')');
        }
      }
      else
      {
        arduboy.fillRect(x,y,TILE_WIDTH,TILE_HEIGHT,BLACK);
        arduboy.drawBitmap(x,y,item.mIcon,TILE_WIDTH,TILE_HEIGHT,WHITE);
      }

      if (item.mState == ITEM_STATE_UNAVAILABLE)
        arduboy.drawBitmap(x,y,ditherImage,TILE_WIDTH,TILE_HEIGHT,BLACK);
    }
  }
}

uint8_t buttonRepeatCounter = BUTTON_REPEAT_DELAY;

inline void checkButton(uint8_t button, bool checkRepeat=false)
{
  if (arduboy.justPressed(button))
  {
    game.buttonDown(button);
  }
  
  if (checkRepeat)
  {
    if (arduboy.pressed(button))
    {
      if (buttonRepeatCounter == 0)
      {
        if (arduboy.frameCount() % BUTTON_REPEAT_PERIOD == 0)
          game.buttonDown(button);
      }
      else
        buttonRepeatCounter--;
    }
    else if (arduboy.justReleased(button))
      buttonRepeatCounter = BUTTON_REPEAT_DELAY;
  }
}

void handleInputs()
{
  checkButton(A_BUTTON);
  checkButton(B_BUTTON);
  checkButton(LEFT_BUTTON,true);
  checkButton(RIGHT_BUTTON,true);
  checkButton(UP_BUTTON,true);
  checkButton(DOWN_BUTTON,true);

  // cheat

  if (arduboy.pressed(UP_BUTTON) && arduboy.pressed(B_BUTTON))
    game.mCheat = arduboy.pressed(DOWN_BUTTON) ? CHEAT_MONEY : CHEAT_NONE;  
}

bool recordWritten = false; ///< For detecting records and writing to EEPROM.

void setup()
{
  EEPROM.begin();
  game.mSound = arduboy.audio.enabled();

  beep1.begin();

  arduboy.clear();
  arduboy.setFrameRate(FRAMERATE);

  if (EEPROM.read(EEPROM_START) != EEPROM_VALID_VALUE)
  {
    // EEPROM not valid => initialize it

    for (uint8_t i = 0; i < MAPS_TOTAL; ++i)
      EEPROM.update(EEPROM_START + i + 1,0); // zero the records
   
    EEPROM.update(EEPROM_START,EEPROM_VALID_VALUE);
  }
  else
  {
    // load the records from EEPROM

    for (uint8_t i = 0; i < MAPS_TOTAL; ++i)
      game.mRecords[i] = EEPROM.read(EEPROM_START + i + 1);
  }
}

void loop()
{
  if (!(arduboy.nextFrame()))
    return;

  beep1.timer();

  arduboy.pollButtons();
  handleInputs();

  uint8_t updates =
    game.mState == STATE_PLAYING_WAVE && arduboy.pressed(A_BUTTON) &&
                   arduboy.pressed(B_BUTTON)? GAME_SPEEDUP : 1;
  
  for (uint8_t i = 0; i < updates; ++i)
    game.update();

  arduboy.clear();

  if (game.mState == STATE_MENU)
    drawMenu();
  else if (game.mState == STATE_GAME_OVER)
    drawGameOver();
  else
    drawMap();

  if (game.mState == STATE_CONFIRM)
    drawConfirmDialog();

  if (game.mState == STATE_GAME_OVER)
  {
    if (game.isRecord() && !recordWritten)
    {
      // new high score record to be written to EEPROM

      EEPROM.update(EEPROM_START + game.mMapNumber + 1,game.mRound);

      recordWritten = true;
    }
  }
  else
    recordWritten = false;

  if (arduboy.audio.enabled() != game.mSound)
  {
    if (game.mSound)
      arduboy.audio.on();
    else
      arduboy.audio.off();

    playSound(SOUND_PUT);
  }

  arduboy.display();
}

#else

// ========= PC debug code only =========

string toStr(Direction d)
{
  switch (d)
  {
    case DIRECTION_N: return "N"; break;
    case DIRECTION_E: return "E"; break;
    case DIRECTION_S: return "S"; break;
    case DIRECTION_W: return "W"; break;
    default:          return "?"; break;
  }
}

string toStr(BytePosition t)
{
  return "[" + to_string(t.mX) + "," + to_string(t.mY) + "]";
}

string toStr(PathSegment s)
{
  return toStr(s.getDirection()) + to_string(s.getLength()); 
}

string toStr(CreepPath p)
{
  string result = toStr(p.getStart()) + ":";

  for (uint8_t i = 0; i < p.getNumSegments(); ++i)
    result += " " + toStr(p.getSegment(i));

  return result;
}

string toStr(GameMap m)
{
  string result = "map:";

  for (uint8_t i = 0; i < m.getNumPaths(); ++i)
    result += "\n  path: " + toStr(*(m.getPath(i)));

  return result; 
}

string toStr(TowerInstance t)
{
  string result = "tower:\n  upgrades:";

  if (t.upgraded(0))
    result += "0";

  if (t.upgraded(1))
    result += "1";

  return result;
}

string toStr(Tile t)
{
  switch (t.mType)
  {
    case TILE_EMPTY:       return "."; break;
    case TILE_TOWER:       return "T"; break;
    case TILE_CREEP_START: return "S"; break;
    case TILE_CREEP_EXIT:  return "E"; break;
    
    case TILE_PATH_NS:     return "|"; break;
    case TILE_PATH_WE:     return "-"; break;     
    case TILE_PATH_NE:     return "L"; break;
    case TILE_PATH_SE:     return "F"; break;
    case TILE_PATH_NW:     return "/"; break;
    case TILE_PATH_SW:     return "\\"; break;
    case TILE_PATH_NES:    return ">"; break;
    case TILE_PATH_ESW:    return "v"; break;
    case TILE_PATH_NEW:    return "^"; break;
    case TILE_PATH_NSW:    return "<"; break;
    case TILE_PATH_NESW:   return "+"; break;

    default: return         "?";
  }
}

string toStr(PlayGround p)
{
  string result = "mPlayground:";

  for (uint8_t y = 0; y < TILES_Y; ++y)
  {
    result += "\n";

    for (uint8_t x = 0; x < TILES_X; ++x)
      result += toStr(*(p.getTile(BytePosition(x,y))));
  }


  return result;
}

string toStr(CreepInstance c)
{
  string result = "creep:\n";

  result += "  type: " + to_string((unsigned int) c.getTypeIndex()) + "\n";
  result += "  path: " + to_string((long unsigned int) c.getPath()) + "\n";
  result += "  position: " + to_string(c.getPosition()) + "\n";
  result += "  health: " + to_string(c.getHealth());

  return result;
}

int main()
{
  // test here

  return 0;
}

#endif
