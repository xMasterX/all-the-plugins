#include "game/Defines.h"
#include "game/Platform.h"
#include "game/Menu.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/Draw.h"
#include "game/Textures.h"
#include "game/Generated/SpriteTypes.h"
#include "game/Map.h"
#include "game/FixedMath.h"
#include "lib/EEPROM.h"

#include <stdio.h>
#include <string.h>

constexpr uint8_t EEPROM_BASE_ADDR = 0;

namespace {
constexpr uint8_t MENU_ITEMS_COUNT = 2;
constexpr uint8_t MENU_LINE = 6;
constexpr uint8_t MENU_LINE_Y = MENU_LINE * 8;
constexpr uint8_t MENU_START_X = 30;
constexpr uint8_t MENU_SOUND_X = 73;
constexpr uint8_t CURSOR_OFFSET_X = 10;
constexpr uint8_t CURSOR_OFFSET_Y = 0;

constexpr uint8_t kMenuStarCount = 16;
const uint8_t kStarX[kMenuStarCount] = {
    7, 15, 22, 30, 41, 50, 60, 71, 80, 89, 98, 106, 114, 119, 123, 126};
const uint8_t kStarY[kMenuStarCount] = {
    8, 18, 11, 24, 6, 20, 13, 27, 9, 22, 7, 32, 12, 35, 5, 38};
const uint8_t kStarPhase[kMenuStarCount] = {
    0, 5, 11, 17, 3, 9, 15, 1, 7, 13, 19, 4, 10, 16, 2, 8};
const uint8_t kStarRadius[kMenuStarCount] = {
    1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1};

static uint8_t Wrap(int v, int n) {
    v %= n;
    if(v < 0) v += n;
    return (uint8_t)v;
}

void DrawMenuRoom() {
    Renderer::SetFullScreenViewport();
    Platform::FillScreen(COLOUR_BLACK);
    for(uint8_t n = 0; n < DISPLAY_WIDTH; n++) {
        Renderer::wBuffer[n] = 0;
        Renderer::horizonBuffer[n] = HORIZON;
    }

    for(uint8_t i = 0; i < kMenuStarCount; i++) {
        const uint8_t cycle = (uint8_t)(((Game::globalTickFrame >> 2) + kStarPhase[i]) % 20);
        if(cycle >= 5) continue;

        const uint8_t stage = (cycle == 0 || cycle == 4) ? 0 : ((cycle == 2) ? 5 : 1);
        if(stage == 0) continue;

        const uint8_t x = kStarX[i];
        const uint8_t y = kStarY[i];
        Platform::PutPixel(x, y, COLOUR_WHITE);
        if(stage == 5) {
            const uint8_t r = kStarRadius[i];
            if(x >= r) Platform::PutPixel((uint8_t)(x - r), y, COLOUR_WHITE);
            if(x + r < DISPLAY_WIDTH) Platform::PutPixel((uint8_t)(x + r), y, COLOUR_WHITE);
            if(y >= r) Platform::PutPixel(x, (uint8_t)(y - r), COLOUR_WHITE);
            if(y + r < DISPLAY_HEIGHT) Platform::PutPixel(x, (uint8_t)(y + r), COLOUR_WHITE);
        }
    }

    const uint8_t menuBottomH = pgm_read_byte(&menuBottomSpriteData[1]);
    Platform::DrawSprite(0, DISPLAY_HEIGHT - menuBottomH, menuBottomSpriteData, 0);
}

uint8_t GetSpriteWidth(const uint8_t* sprite) {
    return pgm_read_byte(&sprite[0]);
}

void DrawMainMenuBackground() {
    DrawMenuRoom();

    const uint8_t logoW = GetSpriteWidth(logoSpriteData);
    const uint8_t logoX = (uint8_t)((DISPLAY_WIDTH - logoW) / 2);
    const uint8_t logoY = 5;
    Platform::DrawSprite(logoX, logoY, logoSpriteData, 0);
}
}

void Menu::Draw() {
    DrawMainMenuBackground();
    PrintItem(0, 0);
    PrintItem(1, 0);

    const uint8_t cursorX = (m_selection == 0) ? (uint8_t)(MENU_START_X - CURSOR_OFFSET_X) :
                                               (uint8_t)(MENU_SOUND_X - CURSOR_OFFSET_X);
    Platform::DrawSprite(cursorX, MENU_LINE_Y + CURSOR_OFFSET_Y, skullSpriteData, 0);
}

void Menu::PrintItem(uint8_t idx, uint8_t row) {
    (void)row;
    switch(idx) {
    case 0:
        Font::PrintString(PSTR("START"), MENU_LINE, MENU_START_X, COLOUR_WHITE);
        break;
    case 1:
        Font::PrintString(PSTR("SOUND"), MENU_LINE, MENU_SOUND_X, COLOUR_WHITE);
        Font::PrintString(
            Platform::IsAudioEnabled() ? PSTR("ON") : PSTR("OFF"),
            MENU_LINE,
            MENU_SOUND_X + 21,
            COLOUR_WHITE);
        break;
    }
}

void Menu::Init() {
    m_selection = 0;
    m_topIndex = 0;
    m_cursorPos = 0;
}

void Menu::DrawEnteringLevel() {
    DrawMenuRoom();
    Font::PrintString(PSTR("Entering floor"), 3, 30, COLOUR_BLACK);
    Font::PrintInt(Game::floor, 3, 90, COLOUR_BLACK);
}

static int CountCharsInt(int v) {
    int n = 1;
    if(v < 0) {
        n++;
        v = -v;
    } // минус тоже символ
    while(v >= 10) {
        v /= 10;
        n++;
    }
    return n;
}

void PrintScoreCentered(int finalScore) {
    const int screenW = 128;

    int n = CountCharsInt(finalScore);
    int textW = 4 * n - 1;

    int x = (screenW - textW) / 2;

    Font::PrintInt(finalScore, 2, x, COLOUR_BLACK);
}

void Menu::DrawGameOver() {
    uint16_t finalScore = 0;
    constexpr int finishBonus = 500;
    constexpr int levelBonus = 20;
    constexpr int chestBonus = 15;
    constexpr int crownBonus = 10;
    constexpr int scrollBonus = 8;
    constexpr int coinsBonus = 4;
    constexpr int skeletonKillBonus = 10;
    constexpr int mageKillBonus = 10;
    constexpr int batKillBonus = 5;
    constexpr int spiderKillBonus = 4;

    finalScore += (Game::floor - 1) * levelBonus;
    if(Game::stats.killedBy == EnemyType::None) finalScore += finishBonus;
    finalScore += Game::stats.chestsOpened * chestBonus;
    finalScore += Game::stats.crownsCollected * crownBonus;
    finalScore += Game::stats.scrollsCollected * scrollBonus;
    finalScore += Game::stats.coinsCollected * coinsBonus;
    finalScore += Game::stats.enemyKills[(int)EnemyType::Skeleton] * skeletonKillBonus;
    finalScore += Game::stats.enemyKills[(int)EnemyType::Mage] * mageKillBonus;
    finalScore += Game::stats.enemyKills[(int)EnemyType::Bat] * batKillBonus;
    finalScore += Game::stats.enemyKills[(int)EnemyType::Spider] * spiderKillBonus;

    DrawMenuRoom();
    PrintScoreCentered(finalScore);

    switch(Game::stats.killedBy) {
    case EnemyType::Exit:
        Font::PrintString(PSTR("You have left the game."), 1, 18, COLOUR_BLACK);
        break;
    case EnemyType::None:
        Font::PrintString(PSTR("You escaped the catacombs!"), 1, 12, COLOUR_BLACK);
        break;
    case EnemyType::Mage:
        Font::PrintString(PSTR("Killed by a mage on level"), 1, 8, COLOUR_BLACK);
        Font::PrintInt(Game::floor, 1, 112, COLOUR_BLACK);
        break;
    case EnemyType::Skeleton:
        Font::PrintString(PSTR("Killed by a knight on level"), 1, 4, COLOUR_BLACK);
        Font::PrintInt(Game::floor, 1, 116, COLOUR_BLACK);
        break;
    case EnemyType::Bat:
        Font::PrintString(PSTR("Killed by a bat on level"), 1, 10, COLOUR_BLACK);
        Font::PrintInt(Game::floor, 1, 110, COLOUR_BLACK);
        break;
    case EnemyType::Spider:
        Font::PrintString(PSTR("Killed by a spider on level"), 1, 4, COLOUR_BLACK);
        Font::PrintInt(Game::floor, 1, 116, COLOUR_BLACK);
        break;
    }

    constexpr uint8_t firstRow = 21;
    constexpr uint8_t secondRow = 38;

    int offset = (Game::globalTickFrame & 8) == 0 ? 32 : 0;

    Renderer::DrawScaled(chestSpriteData, 6, firstRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.chestsOpened, 4, 24, COLOUR_BLACK);

    Renderer::DrawScaled(crownSpriteData, 6, secondRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.crownsCollected, 6, 24, COLOUR_BLACK);

    Renderer::DrawScaled(scrollSpriteData, 36, firstRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.scrollsCollected, 4, 54, COLOUR_BLACK);

    Renderer::DrawScaled(coinsSpriteData, 36, secondRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.coinsCollected, 6, 54, COLOUR_BLACK);

    Renderer::DrawScaled(skeletonSpriteData + offset, 72, firstRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.enemyKills[(int)EnemyType::Skeleton], 4, 90, COLOUR_BLACK);

    Renderer::DrawScaled(mageSpriteData + offset, 72, secondRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.enemyKills[(int)EnemyType::Mage], 6, 90, COLOUR_BLACK);

    Renderer::DrawScaled(batSpriteData + offset, 102, firstRow, 9, 255, true, COLOUR_BLACK);
    Font::PrintInt(Game::stats.enemyKills[(int)EnemyType::Bat], 4, 120, COLOUR_BLACK);

    Renderer::DrawScaled(spiderSpriteData + offset, 102, secondRow, 9, 255, false, COLOUR_WHITE);
    Font::PrintInt(Game::stats.enemyKills[(int)EnemyType::Spider], 6, 120, COLOUR_BLACK);

    m_save[0] = Game::stats.chestsOpened;
    m_save[1] = Game::stats.crownsCollected;
    m_save[2] = Game::stats.scrollsCollected;
    m_save[3] = Game::stats.coinsCollected;
    m_save[4] = Game::stats.enemyKills[(int)EnemyType::Skeleton];
    m_save[5] = Game::stats.enemyKills[(int)EnemyType::Mage];
    m_save[6] = Game::stats.enemyKills[(int)EnemyType::Bat];
    m_save[7] = Game::stats.enemyKills[(int)EnemyType::Spider];
    m_save[8] = 0;

    if(Game::floor > 0) {
        m_save[8] = Game::floor - 1;
    } 

    SetScore(finalScore);
}

void Menu::Tick() {
    static uint8_t lastInput = 0;
    uint8_t input = Platform::GetInput();

    if(((input & INPUT_RIGHT) && !(lastInput & INPUT_RIGHT)) ||
       ((input & INPUT_DOWN) && !(lastInput & INPUT_DOWN))) {
        m_selection = Wrap((int)m_selection + 1, MENU_ITEMS_COUNT);
    }

    if(((input & INPUT_LEFT) && !(lastInput & INPUT_LEFT)) ||
       ((input & INPUT_UP) && !(lastInput & INPUT_UP))) {
        m_selection = Wrap((int)m_selection - 1, MENU_ITEMS_COUNT);
    }

    m_topIndex = 0;
    m_cursorPos = m_selection;

    if((input & INPUT_B) && !(lastInput & INPUT_B)) {
        switch(m_selection) {
        case 0:
            Game::StartGame();
            break;
        case 1:
            Platform::SetAudioEnabled(!Platform::IsAudioEnabled());
            break;
        default:
            break;
        }
    }

    lastInput = input;
}

void Menu::TickEnteringLevel() {
    constexpr uint8_t showTime = 45;
    if(timer < showTime) timer++;
    if(timer == showTime && Platform::GetInput() == 0) {
        Game::StartLevel();
    }
}

void Menu::TickGameOver() {
    constexpr uint8_t minShowTime = 30;

    if(timer < minShowTime) timer++;

    if(timer == minShowTime && (Platform::GetInput() & INPUT_B)) {
        timer++;
    } else if(timer == minShowTime + 1 && Platform::GetInput() == 0) {
        Game::SwitchState(Game::State::Menu);
    }
}

static inline void DrawEraseTile8x8(int16_t x, int16_t y, const uint8_t* frame8bytes) {
    for(uint8_t row = 0; row < 8; row++) {
        uint8_t rowMask = pgm_read_byte(frame8bytes + row);
        while(rowMask) {
            uint8_t b = (uint8_t)__builtin_ctz((unsigned)rowMask);
            uint8_t col = 7 - b;
            Platform::PutPixel(x + col, y + row, COLOUR_WHITE);
            rowMask &= (uint8_t)(rowMask - 1);
        }
    }
}

void Menu::DrawTransitionFrame(uint8_t frameIndex) {
    const uint8_t w = pgm_read_byte(transitionSet + 0);
    const uint8_t h = pgm_read_byte(transitionSet + 1);
    (void)w;
    (void)h;

    const uint8_t* framePtr = transitionSet + 2 + (uint16_t)frameIndex * 8;
    int16_t tileX = 120;
    int16_t tileY = 56;
    while(true) {
        DrawEraseTile8x8(tileX, tileY, framePtr);

        tileX -= 8;
        if(tileX < 0) {
            tileX = 120;
            tileY -= 8;
            if(tileY < 0) break;
        }
    }
}

void Menu::ResetTimer() {
    timer = 0;
}

static constexpr uint8_t TOTAL_TIME = 40;
static constexpr uint8_t TOTAL_FRAMES = 8;

void Menu::RunTransition(Menu* menu, uint8_t& t, TransitionNextFn next) {
    uint8_t frameIndex = (uint16_t)t * TOTAL_FRAMES / TOTAL_TIME;
    if(frameIndex >= TOTAL_FRAMES) frameIndex = TOTAL_FRAMES - 1;
    menu->DrawTransitionFrame(frameIndex);
    if(frameIndex >= TOTAL_FRAMES - 2) {
        t = 0;
        next();
        return;
    }
    ++t;
}

void Menu::FadeOut() {
    static uint8_t t = 0;
    RunTransition(this, t, +[]() { Game::SwitchState(Game::State::GameOver); });
}

void Menu::ReadSave() {
    uint8_t addr = EEPROM_BASE_ADDR;
    m_score = (uint16_t)EEPROM.read(addr) | ((uint16_t)EEPROM.read(addr + 1) << 8); addr += 2;
    m_high = (uint16_t)EEPROM.read(addr) | ((uint16_t)EEPROM.read(addr + 1) << 8);  addr += 2;
    m_storedHigh = m_high;
    
    for(int i = 0; i < 9; i++) {
        m_save[i] = EEPROM.read(addr++);
    }
}

void Menu::SetScore(uint16_t score) {
    if(score == 0) return;
    m_high = (score > m_storedHigh) ? score : m_storedHigh;
    m_score = score;
}

void Menu::WriteSave() {
    uint8_t addr = EEPROM_BASE_ADDR;
    EEPROM.update(addr++, (uint8_t)(m_score & 0xFF));
    EEPROM.update(addr++, (uint8_t)(m_score >> 8));
    EEPROM.update(addr++, (uint8_t)(m_high & 0xFF));
    EEPROM.update(addr++, (uint8_t)(m_high >> 8));

    for(int i = 0; i < 9; i++) {
        EEPROM.update(addr++, m_save[i]);
    }
    EEPROM.commit();
}
