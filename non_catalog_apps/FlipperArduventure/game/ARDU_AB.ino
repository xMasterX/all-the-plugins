/*
  ARDUVENTURE: http://www.team-arg.org/ardu-manual.html

  Arduboy version 1.0.1:  http://www.team-arg.org/ardu-downloads.html

  MADE by TEAM a.r.g. : http://www.team-arg.org/more-about.html

  2017 - 2018 - JO3RI GANTOIS - GAVIN ATKIN - OLIVIER HUARD - SIEGFRIED CROES 

  Additional Level Design - Jace Atkin

  Game License: MIT : https://opensource.org/licenses/MIT

  Story, characters, sprites, tiles, design and art: copyrighted to TEAM a.r.g.
*/

//determine the game
#define GAME_ID 46

#include "game/globals.h"
#include "game/songs.h"
#include "game/menu.h"
#include "game/game.h"
#include "game/inputs.h"
#include "game/text.h"
#include "game/inventory.h"
#include "game/items.h"
#include "game/player.h"
#include "game/enemies.h"
#include "game/battles.h"


typedef void (*FunctionPointer) ();

const FunctionPointer PROGMEM mainGameLoop[] = {
  stateMenuIntro,
  stateMenuMain,
  stateMenuContinue,
  stateMenuNew,
  stateMenuSound,
  stateMenuCredits,
  stateGamePlaying,
  stateGameInventory,
  stateGameEquip,
  stateGameStats,
  stateGameMap,
  stateGameOver,
  showSubMenuStuff,
  showSubMenuStuff,
  showSubMenuStuff,
  showSubMenuStuff,
  walkingThroughDoor,
  stateGameBattle,
  stateGameBoss,
  stateGameIntro,
  stateGameNew,
  stateGameSaveSoundEnd,
  stateGameSaveSoundEnd,
  stateGameSaveSoundEnd,
  stateGameObjects,
  stateGameShop,
  stateGameInn,
  battleGiveRewards,
  stateMenuReboot,
};

int main() {
  arduboy.mainNoUSB();
  return 0;
}

void setup() {
  arduboy.boot();
  arduboy.audio.begin();
  ATM.play(titleSong);
  arduboy.setFrameRate(60);                                 // set the frame rate of the game at 60 fps
}


void loop() {
  if (!(arduboy.nextFrame())) return;
  //arduboy.fillScreen(1);
  arduboy.pollButtons();
  //arduboy.clear();
  drawTiles();
  updateEyes();
  
  ((FunctionPointer) pgm_read_word(&mainGameLoop[gameState]))();
  checkInputs();
  if (question) drawQuestion();
  if (yesNo) drawYesNo();
  if (flashBlack) flashScreen(BLACK);     // Set in battleStart
  else if (flashWhite) flashScreen(WHITE);
  //Serial.write(arduboy.getBuffer(), 128 * 64 / 8);
  arduboy.display();
}

