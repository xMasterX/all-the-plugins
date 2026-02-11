#include "src/utils/Arduboy2Ext.h"
#include "src/ArduboyTonesFX.h"
#include <lib/ArduboyFX.h>  

#include "src/utils/Constants.h"
#include "src/utils/Enums.h"
#include "src/utils/Stack.h"
#include "src/utils/FadeEffects.h"
#include "src/entities/Entities.h"
#include "src/fonts/Font3x5.h"
#include "src/utils/EEPROM_Utils.h"


#ifdef SAVE_MEMORY_USB
ARDUBOY_NO_USB
#endif

Arduboy2Ext arduboy;

#ifndef SAVE_MEMORY_SOUND
    
    uint16_t buffer[16]; 

    ArduboyTonesFX sound(arduboy.audio.enabled, buffer);
    
#endif

#if (defined(DEBUG) && defined(DEBUG_ONSCREEN_DETAILS)) or (defined(DEBUG) && defined(DEBUG_ONSCREEN_DETAILS_MIN))
    Font3x5 font3x5 = Font3x5();
#endif

#ifdef DEBUG_LEVELS
uint8_t startLevel = STARTING_LEVEL;
#endif

Cookie cookie;
Stack <int16_t, Constants::StackSize> princeStack;
Prince &prince = cookie.prince;
Stack <int16_t, Constants::StackSize> enemyStack;
Mouse mouse;
uint8_t bCounter = 0;

#ifndef SAVE_MEMORY_ENEMY
Enemy &enemy = cookie.enemy;
#endif

Level &level = cookie.level;
GamePlay &gamePlay = cookie.gamePlay;
TitleScreenVars titleScreenVars;
MenuItem menu;

#ifndef SAVE_MEMORY_OTHER
    FadeEffects fadeEffect;
#endif

void setup() {

    arduboy.boot();
    FX::display(CLEAR_BUFFER);
    #ifndef SAVE_MEMORY_SYSTEM_BUTTONS
    arduboy.systemButtons();
    #endif

    #ifdef USE_LED
    #ifndef MICROCADE
    arduboy.setRGBled(0, 0, 0);
    #endif
    #endif

    #ifndef SAVE_MEMORY_SOUND
        arduboy.audio.begin();
    #endif

    arduboy.setFrameRate(Constants::FrameRate);

  #ifdef SAVE_TO_FX

    FX::begin(FX_DATA_PAGE, FX_SAVE_PAGE);
    const bool hasSave = FX::loadGameState((uint8_t*)&cookie, sizeof(cookie));

  #else

    FX::begin(FX_DATA_PAGE);
    EEPROM_Utils::loadCookie(cookie);

  #endif

    prince.setStack(&princeStack);

    #ifndef SAVE_MEMORY_ENEMY
        enemy.setStack(&enemyStack);
    #endif

    #ifdef SAVE_TO_FX
    if(hasSave) {
        restoreRuntimeAfterLoad();
    }
    #endif

    #ifdef SAVE_MEMORY_OTHER
        gamePlay.gameState = GameState::Game_Init;
    #else
        #ifdef SAVE_MEMORY_PPOT
            gamePlay.gameState = GameState::Title_Init;
        #else
            gamePlay.gameState = GameState::SplashScreen_Init;
        #endif
    #endif

}

void loop() {

    if (!arduboy.nextFrame()) return; 
    arduboy.pollButtons();
    bindRuntimeStacks();

    #ifndef SAVE_MEMORY_SOUND
        sound.fillBufferFromFX();
    #endif

    switch (gamePlay.gameState) {

        #ifndef SAVE_MEMORY_PPOT
            
            case GameState::SplashScreen_Init:

                splashScreen_Init();
                titleScreenVars.counter = 0;
                [[fallthrough]];

            case GameState::SplashScreen:

                splashScreen();
                break;
        #endif
        
        #ifndef SAVE_MEMORY_OTHER

            case GameState::Title_Init:

                #ifndef SAVE_MEMORY_SOUND
                    setSound(SoundIndex::Theme);
                #endif

                #ifndef SAVE_MEMORY_OTHER
                    fadeEffect.complete();
                #endif

                gamePlay.gameState = GameState::Title;
                
                title_Init();
                [[fallthrough]];

            case GameState::Title:

                title();
                break;

        #endif

        case GameState::Game_Init:

            #ifndef SAVE_MEMORY_SOUND
                sound.noTone();
            #endif
            
            game_Init();
            [[fallthrough]];

        case GameState::Game_StartLevel:
            game_StartLevel();
            game();
            break;

        case GameState::Game:
        #ifndef SAVE_MEMORY_OTHER
        case GameState::Menu:
        #endif

            game();
            break;


        default: break;

    }

    // Invert screen during play ?
    
    { 

        bool invert = false;
        
        switch (prince.getStance()) {

            case Stance::Pickup_Sword_3:
            case Stance::Pickup_Sword_5:
            case Stance::Drink_Tonic_Small_12:
            case Stance::Drink_Tonic_Small_14:
            case Stance::Drink_Tonic_Large_12:
            case Stance::Drink_Tonic_Large_14:
            case Stance::Drink_Tonic_Poison_12:
            case Stance::Drink_Tonic_Poison_14:
            case Stance::Drink_Tonic_Float_12:
            case Stance::Drink_Tonic_Float_14:
                invert = true;
                break;

        }    


        // Invert screen when striking player / enemy in sword fight ?
    
        Flash &flash = level.getFlash();
    
        #ifndef SAVE_MEMORY_ENEMY

            if (flash.frame == 1 && flash.type == FlashType::MirrorLevel12) {
                enemy.setStatus(Status::Dormant);
            }

        #endif

        if ((flash.frame == 2 || flash.frame == 4) && (flash.type == FlashType::SwordFight || flash.type == FlashType::MirrorLevel12)) {

            invert = true;

        }

        FX::enableOLED();
        arduboy.invert(invert);

    }


    // Handle fade effects ..

    #ifndef SAVE_MEMORY_OTHER
    
        if (!fadeEffect.isComplete()) {

            fadeEffect.draw(arduboy);
            fadeEffect.update();

        }

    #endif

    FX::display(CLEAR_BUFFER);

}
