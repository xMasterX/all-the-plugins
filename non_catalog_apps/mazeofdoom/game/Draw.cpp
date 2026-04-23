#include <stdint.h>
#include "game/Draw.h"
#include "game/Defines.h"
#include "game/Game.h"
#include "game/Particle.h"
#include "game/FixedMath.h"
#include "game/Map.h"
#include "game/Projectile.h"
#include "game/Platform.h"
#include "game/Enemy.h"
#include "game/Font.h"
#include "game/LUT.h"
#include "game/Generated/SpriteData.inc.h"
#if WITH_VECTOR_TEXTURES
#include "game/Textures.h"
#endif
#if WITH_SPRITE_OUTLINES
#define DrawScaledInner DrawScaledOutline
#else
#define DrawScaledInner DrawScaledNoOutline
#endif
Camera Renderer::camera;
uint8_t Renderer::wBuffer[DISPLAY_WIDTH];
int8_t Renderer::horizonBuffer[DISPLAY_WIDTH];
uint8_t Renderer::globalRenderFrame = 0;
uint8_t Renderer::numBufferSlicesFilled = 0;
QueuedDrawable Renderer::queuedDrawables[MAX_QUEUED_DRAWABLES];
uint8_t Renderer::numQueuedDrawables = 0;
int16_t Renderer::viewX = 0;
int16_t Renderer::viewWidth = DISPLAY_WIDTH;
int16_t Renderer::viewRight = DISPLAY_WIDTH;
int16_t Renderer::viewCenterX = DISPLAY_WIDTH / 2;
int16_t Renderer::nearPlane = DISPLAY_WIDTH * NEAR_PLANE_MULTIPLIER / 256;
namespace {
constexpr uint8_t kMinWallDrawDistance = 3;
constexpr int16_t kSidebarMapX = 2;
constexpr int16_t kSidebarMapY = DISPLAY_HEIGHT - 16 - 2;
constexpr int16_t kSidebarMapWidth = 19;
constexpr int16_t kSidebarMapHeight = 16;
constexpr uint8_t kNightStarCount = 18;
constexpr int8_t kNightSkyFloorGap = 0;
constexpr uint8_t kNightStaticStarChance = 77; // ~30% of 256

struct BackgroundParallaxState {
    uint16_t levelSeed = 0;
    int16_t skySampleX = 0;
};

BackgroundParallaxState g_backgroundParallax;

uint16_t AdvanceNoise(uint16_t value) {
    return (uint16_t)(value * 2053u + 13849u);
}

uint8_t WrapSpriteColumn(int16_t sampleX, uint8_t tileWidth) {
    if(tileWidth == 0) return 0;
    sampleX %= tileWidth;
    if(sampleX < 0) {
        sampleX += tileWidth;
    }
    return (uint8_t)sampleX;
}

uint8_t WrapSpriteRow(int16_t sampleY, uint8_t tileHeight) {
    if(tileHeight == 0) return 0;
    sampleY %= tileHeight;
    if(sampleY < 0) {
        sampleY += tileHeight;
    }
    return (uint8_t)sampleY;
}

void ResetBackgroundParallaxOrigin() {
    g_backgroundParallax.levelSeed = Game::levelThemeSeed;
    g_backgroundParallax.skySampleX = 0;
}

void UpdateBackgroundParallax() {
    if(g_backgroundParallax.levelSeed != Game::levelThemeSeed) {
        ResetBackgroundParallaxOrigin();
    }
    // Sky should pan opposite to yaw and slower than walls because it is distant.
    g_backgroundParallax.skySampleX = (int16_t)(-(int16_t)Renderer::camera.angle / 2);
}

void DrawStaticNightStar(uint8_t x, uint8_t y, uint8_t size) {
    Platform::PutPixel(x, y, COLOUR_WHITE);
    if(size < 2) return;

    if(x + 1 < DISPLAY_WIDTH) Platform::PutPixel((uint8_t)(x + 1), y, COLOUR_WHITE);
    if(y + 1 < DISPLAY_HEIGHT) Platform::PutPixel(x, (uint8_t)(y + 1), COLOUR_WHITE);
    if(x + 1 < DISPLAY_WIDTH && y + 1 < DISPLAY_HEIGHT) {
        Platform::PutPixel((uint8_t)(x + 1), (uint8_t)(y + 1), COLOUR_WHITE);
    }
}

void DrawAnimatedNightStar(uint8_t x, uint8_t y, uint8_t phase, uint8_t radius) {
    const uint8_t cycle = (uint8_t)(((Game::globalTickFrame >> 2) + phase) % 20);
    Platform::PutPixel(x, y, COLOUR_WHITE);
    if(cycle >= 5 || cycle == 0 || cycle == 4) return;

    if(x >= radius) Platform::PutPixel((uint8_t)(x - radius), y, COLOUR_WHITE);
    if(x + radius < DISPLAY_WIDTH) Platform::PutPixel((uint8_t)(x + radius), y, COLOUR_WHITE);
    if(y >= radius) Platform::PutPixel(x, (uint8_t)(y - radius), COLOUR_WHITE);
    if(y + radius < DISPLAY_HEIGHT) Platform::PutPixel(x, (uint8_t)(y + radius), COLOUR_WHITE);
}

void DrawNightSkyStars(int16_t topY, int16_t bottomY) {
    if(bottomY <= topY) return;

    uint16_t noise = (uint16_t)(Game::levelThemeSeed ^ ((uint16_t)Game::floor << 8));
    const uint8_t skyHeight = (uint8_t)(bottomY - topY);
    for(uint8_t i = 0; i < kNightStarCount; i++) {
        noise = AdvanceNoise(noise);
        const uint8_t starX = (uint8_t)(
            GAME_VIEW_X +
            WrapSpriteColumn(
                (int16_t)(noise % GAME_VIEW_WIDTH) + g_backgroundParallax.skySampleX,
                GAME_VIEW_WIDTH));
        noise = AdvanceNoise(noise);
        const uint8_t y = (uint8_t)(topY + (noise % skyHeight));
        noise = AdvanceNoise(noise);
        const bool isStatic = (uint8_t)(noise & 0xffu) < kNightStaticStarChance;
        noise = AdvanceNoise(noise);
        const uint8_t radius = (uint8_t)((noise & 1u) + 1u);
        noise = AdvanceNoise(noise);
        const uint8_t phase = (uint8_t)(noise % 20u);

        if(isStatic) {
            DrawStaticNightStar(starX, y, radius);
        } else {
            DrawAnimatedNightStar(starX, y, phase, radius);
        }
    }
}

bool ReadSpritePixel(
    const uint8_t* spriteData,
    uint8_t sampleX,
    uint8_t sampleY,
    uint8_t& colour,
    uint8_t& mask) {
    const uint8_t tileWidth = pgm_read_byte(&spriteData[0]);
    const uint8_t tileHeight = pgm_read_byte(&spriteData[1]);
    if(tileWidth == 0 || tileHeight == 0) return false;

    const uint8_t* data = spriteData + 2;
    const uint8_t page = (uint8_t)(sampleY >> 3);
    const uint16_t srcIndex = (uint16_t)((page * tileWidth + sampleX) * 2u);
    colour = pgm_read_byte(&data[srcIndex]);
    mask = pgm_read_byte(&data[srcIndex + 1]);
    return true;
}

void DrawPerspectiveFloorBand(const uint8_t* spriteData, int16_t y) {
    const uint8_t tileWidth = pgm_read_byte(&spriteData[0]);
    const uint8_t tileHeight = pgm_read_byte(&spriteData[1]);
    if(tileWidth == 0 || tileHeight == 0) return;

    const int16_t bandBottom = y + tileHeight;
    const int16_t forwardX = FixedCos(Renderer::camera.angle);
    const int16_t forwardY = FixedSin(Renderer::camera.angle);
    const int16_t rightX = FixedCos(Renderer::camera.angle + FIXED_ANGLE_90);
    const int16_t rightY = FixedSin(Renderer::camera.angle + FIXED_ANGLE_90);

    for(int16_t dx = Renderer::viewX; dx < Renderer::viewRight; dx++) {
        const int16_t horizon = Renderer::GetHorizon(dx);
        for(int16_t dstY = y; dstY < bandBottom && dstY < DISPLAY_HEIGHT; dstY++) {
            const int16_t screenDepth = dstY - horizon;
            if(screenDepth <= 0) continue;

            const int32_t viewZ =
                ((int32_t)(CELL_SIZE / 2) * Renderer::nearPlane * CAMERA_SCALE) / screenDepth;
            const int32_t viewX =
                ((int32_t)(dx - Renderer::viewCenterX) * viewZ) / Renderer::nearPlane;
            const int32_t worldX =
                Renderer::camera.x +
                (((int32_t)forwardX * viewZ + (int32_t)rightX * viewX) >> 8);
            const int32_t worldY =
                Renderer::camera.y +
                (((int32_t)forwardY * viewZ + (int32_t)rightY * viewX) >> 8);

            const uint8_t sx = WrapSpriteColumn((int16_t)((worldX * tileWidth) >> 8), tileWidth);
            const uint8_t sy = WrapSpriteRow((int16_t)((worldY * tileHeight) >> 8), tileHeight);

            uint8_t colourBits = 0;
            uint8_t maskBits = 0;
            if(!ReadSpritePixel(spriteData, sx, sy, colourBits, maskBits)) continue;

            const uint8_t bit = (uint8_t)(1u << (sy & 7u));
            if((maskBits & bit) == 0u) continue;

            Platform::PutPixel(
                (uint8_t)dx,
                (uint8_t)dstY,
                (colourBits & bit) != 0u ? COLOUR_WHITE : COLOUR_BLACK);
        }
    }
}

void FillColumn(int16_t x, int16_t topY, int16_t bottomY, uint8_t colour) {
    if(topY > bottomY) return;
    if(x < 0 || x >= DISPLAY_WIDTH) return;

    if(topY < 0) topY = 0;
    if(bottomY >= DISPLAY_HEIGHT) bottomY = DISPLAY_HEIGHT - 1;

    for(int16_t y = topY; y <= bottomY; y++) {
        Platform::PutPixel((uint8_t)x, (uint8_t)y, colour);
    }
}

void DrawTiltedSprite(int16_t x, int16_t y, const uint8_t* bmp, int8_t skew, bool invert = false) {
    if(!bmp) return;
    const uint8_t width = pgm_read_byte(&bmp[0]);
    const uint8_t height = pgm_read_byte(&bmp[1]);
    if(!width || !height) return;
    const int16_t center = (int16_t)(height - 1) / 2;
    const uint8_t* data = bmp + 2;
    for(uint8_t sy = 0; sy < height; sy++) {
        const int16_t rowOffset = center ? (int16_t)(((int16_t)sy - center) * skew / center) : 0;
        const uint8_t page = sy >> 3;
        const uint8_t bit = (uint8_t)(1u << (sy & 7u));
        for(uint8_t sx = 0; sx < width; sx++) {
            const uint16_t srcIndex = (uint16_t)((page * width + sx) * 2u);
            const uint8_t mask = pgm_read_byte(&data[srcIndex + 1]);
            if((mask & bit) == 0) continue;
            const uint8_t src = pgm_read_byte(&data[srcIndex]);
            const uint8_t colour = (uint8_t)(((src & bit) != 0) ^ invert);
            Platform::PutPixel(
                (uint8_t)(x + sx + rowOffset), (uint8_t)(y + sy), colour);
        }
    }
}

void ClearSidebar() {
    uint8_t* screenBuffer = Platform::GetScreenBuffer();
    if(!screenBuffer) return;

    for(uint8_t page = 0; page < DISPLAY_HEIGHT / 8; page++) {
        for(uint8_t x = 0; x < SIDEBAR_WIDTH; x++) {
            screenBuffer[(uint16_t)page * DISPLAY_WIDTH + x] = 0x00;
        }
    }
}

void ClearPixelNumberArea(int16_t x, int16_t y) {
    for(int16_t py = y; py < y + Font::glyphHeight; py++) {
        for(int16_t px = x; px < x + Font::glyphWidth * 2 + 1; px++) {
            Platform::PutPixel((uint8_t)px, (uint8_t)py, COLOUR_BLACK);
        }
    }
}

void DrawPixelGlyph(int16_t x, int16_t y, char c, uint8_t colour) {
    const uint8_t uc = (uint8_t)c;
    if(uc < Font::firstGlyphIndex) return;

    const uint8_t* glyph = fontPageData + Font::glyphWidth * (uc - Font::firstGlyphIndex);
    for(uint8_t col = 0; col < Font::glyphWidth; col++) {
        const uint8_t bits = (uint8_t)~pgm_read_byte(&glyph[col]);
        for(uint8_t row = 0; row < Font::glyphHeight; row++) {
            if(bits & (1u << row)) {
                Platform::PutPixel((uint8_t)(x + col), (uint8_t)(y + row), colour);
            }
        }
    }
}

void DrawPixelNumber(int16_t x, int16_t y, uint8_t value, uint8_t colour) {
    const uint8_t clamped = value > 99 ? 99 : value;
    ClearPixelNumberArea(x, y);

    if(clamped >= 10) {
        DrawPixelGlyph(x + 1, y, (char)('0' + clamped / 10), colour);
        DrawPixelGlyph(x + Font::glyphWidth + 1, y, (char)('0' + clamped % 10), colour);
    } else {
        DrawPixelGlyph(x + Font::glyphWidth + 1, y, (char)('0' + clamped), colour);
    }
}

void DrawScreenFrame() {
    for(int16_t x = 0; x < DISPLAY_WIDTH; x++) {
        Platform::PutPixel((uint8_t)x, 0, COLOUR_BLACK);
        Platform::PutPixel((uint8_t)x, DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    }

    for(int16_t y = 0; y < DISPLAY_HEIGHT; y++) {
        Platform::PutPixel(0, (uint8_t)y, COLOUR_BLACK);
        Platform::PutPixel(DISPLAY_WIDTH - 1, (uint8_t)y, COLOUR_BLACK);
    }
}

void DrawGameWindowMask() {
    Platform::PutPixel((uint8_t)GAME_VIEW_X, 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 2), 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, 2, COLOUR_BLACK);

    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 4), 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), 0, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), 2, COLOUR_BLACK);

    Platform::PutPixel((uint8_t)GAME_VIEW_X, DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 2), DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, DISPLAY_HEIGHT - 2, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), DISPLAY_HEIGHT - 2, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, DISPLAY_HEIGHT - 3, COLOUR_BLACK);

    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 4), DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), DISPLAY_HEIGHT - 1, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), DISPLAY_HEIGHT - 2, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), DISPLAY_HEIGHT - 2, COLOUR_BLACK);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), DISPLAY_HEIGHT - 3, COLOUR_BLACK);

    for(int16_t x = GAME_VIEW_X + 3; x <= GAME_VIEW_RIGHT - 4; x++) {
        Platform::PutPixel((uint8_t)x, 1, COLOUR_WHITE);
        Platform::PutPixel((uint8_t)x, DISPLAY_HEIGHT - 2, COLOUR_WHITE);
    }

    for(int16_t y = 4; y <= DISPLAY_HEIGHT - 4; y++) {
        Platform::PutPixel((uint8_t)GAME_VIEW_X, (uint8_t)y, COLOUR_WHITE);
        Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), (uint8_t)y, COLOUR_WHITE);
    }

    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 2), 1, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), 2, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, 3, COLOUR_WHITE);

    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 4), 1, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), 2, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), 3, COLOUR_WHITE);

    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 2), DISPLAY_HEIGHT - 2, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_X + 1), DISPLAY_HEIGHT - 3, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)GAME_VIEW_X, DISPLAY_HEIGHT - 4, COLOUR_WHITE);

    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 4), DISPLAY_HEIGHT - 2, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 3), DISPLAY_HEIGHT - 3, COLOUR_WHITE);
    Platform::PutPixel((uint8_t)(GAME_VIEW_RIGHT - 2), DISPLAY_HEIGHT - 4, COLOUR_WHITE);
}

void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour) {
    for(int16_t py = y; py < y + h; py++) {
        for(int16_t px = x; px < x + w; px++) {
            Platform::PutPixel((uint8_t)px, (uint8_t)py, colour);
        }
    }
}

const uint16_t* GetPickupIconSprite(CellType cellType) {
    switch(cellType) {
    case CellType::Potion:
        return potionSpriteData;
    case CellType::Coins:
        return coinsSpriteData;
    case CellType::Crown:
        return crownSpriteData;
    case CellType::Scroll:
        return scrollSpriteData;
    default:
        return nullptr;
    }
}

void DrawPickupIcon(const uint16_t* spriteData) {
    if(!spriteData) return;

    const int16_t boxCenterX = kSidebarMapX + kSidebarMapWidth / 2;
    const int16_t boxCenterY = kSidebarMapY + kSidebarMapHeight / 2;
    constexpr uint8_t halfSize = 8;
    const int16_t oldViewX = Renderer::viewX;
    const int16_t oldViewWidth = Renderer::viewWidth;
    const int16_t oldViewRight = Renderer::viewRight;
    const int16_t oldViewCenterX = Renderer::viewCenterX;
    const int16_t oldNearPlane = Renderer::nearPlane;
    uint8_t savedBuffer[kSidebarMapWidth];

    for(uint8_t i = 0; i < kSidebarMapWidth; i++) {
        savedBuffer[i] = Renderer::wBuffer[kSidebarMapX + i];
        Renderer::wBuffer[kSidebarMapX + i] = 0;
    }

    Renderer::SetFullScreenViewport();
    Renderer::DrawScaled(
        spriteData,
        (int8_t)(boxCenterX - halfSize),
        (int8_t)(boxCenterY - halfSize),
        halfSize,
        255,
        false,
        COLOUR_WHITE);

    for(uint8_t i = 0; i < kSidebarMapWidth; i++) {
        Renderer::wBuffer[kSidebarMapX + i] = savedBuffer[i];
    }
    Renderer::viewX = oldViewX;
    Renderer::viewWidth = oldViewWidth;
    Renderer::viewRight = oldViewRight;
    Renderer::viewCenterX = oldViewCenterX;
    Renderer::nearPlane = oldNearPlane;
}

void DrawSidebarMinimap() {
    FillRect(kSidebarMapX, kSidebarMapY, kSidebarMapWidth, kSidebarMapHeight, COLOUR_WHITE);

    const uint8_t playerCellX = Game::player.x / CELL_SIZE;
    const uint8_t playerCellY = Game::player.y / CELL_SIZE;
    const uint8_t startCellX = playerCellX - kSidebarMapWidth / 2;
    const uint8_t startCellY = playerCellY - kSidebarMapHeight / 2;

    for(uint8_t outX = 0; outX < kSidebarMapWidth; outX++) {
        for(uint8_t outY = 0; outY < kSidebarMapHeight; outY++) {
            const uint8_t cellX = startCellX + outX;
            const uint8_t cellY = startCellY + outY;
            const bool isPlayer = (cellX == playerCellX) && (cellY == playerCellY);
            const uint8_t colour =
                isPlayer ? ((Game::globalTickFrame & 3) ? COLOUR_BLACK : COLOUR_WHITE) :
                           (cellX < Map::width && cellY < Map::height && Map::IsSolid(cellX, cellY) ?
                                COLOUR_BLACK :
                                COLOUR_WHITE);
            Platform::PutPixel(
                (uint8_t)(kSidebarMapX + outX), (uint8_t)(kSidebarMapY + outY), colour);
        }
    }
}

void DrawSidebarPanel() {
    FillRect(kSidebarMapX, kSidebarMapY, kSidebarMapWidth, kSidebarMapHeight, COLOUR_WHITE);
    if(Game::hudPickupIconTime > 0) {
        DrawPickupIcon(GetPickupIconSprite(Game::hudPickupIcon));
    } else {
        DrawSidebarMinimap();
    }
}

void DrawGameOverlayFrame() {
    DrawScreenFrame();
    DrawGameWindowMask();
}

}

void Renderer::SetFullScreenViewport() {
    viewX = 0;
    viewWidth = DISPLAY_WIDTH;
    viewRight = DISPLAY_WIDTH;
    viewCenterX = DISPLAY_WIDTH / 2;
    nearPlane = DISPLAY_WIDTH * NEAR_PLANE_MULTIPLIER / 256;
}

void Renderer::SetGameViewport() {
    viewX = GAME_VIEW_X;
    viewRight = GAME_VIEW_RIGHT;
    viewWidth = viewRight - viewX;
    viewCenterX = viewX + viewWidth / 2;
    nearPlane = viewWidth * NEAR_PLANE_MULTIPLIER / 256;
}
const uint8_t scaleDrawWriteMasks[] PROGMEM =
    {(1), (1 << 1), (1 << 2), (1 << 3), (1 << 4), (1 << 5), (1 << 6), (1 << 7)};
const uint16_t scaleDrawReadMasks[] PROGMEM = {
    (1),
    (1 << 1),
    (1 << 2),
    (1 << 3),
    (1 << 4),
    (1 << 5),
    (1 << 6),
    (1 << 7),
    (1 << 8),
    (1 << 9),
    (1 << 10),
    (1 << 11),
    (1 << 12),
    (1 << 13),
    (1 << 14),
    (1 << 15)};
#if WITH_VECTOR_TEXTURES
void Renderer::DrawWallLine(
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2,
    uint8_t clipLeft,
    uint8_t clipRight,
    uint8_t col) {
    if(x1 > x2) return;
    if(y1 < 0) {
        if(y2 < 0) return;
        if(y2 != y1) x1 += (0 - y1) * (x2 - x1) / (y2 - y1);
        y1 = 0;
    }
    if(y2 > DISPLAY_HEIGHT - 1) {
        if(y1 > DISPLAY_HEIGHT - 1) return;
        if(y2 != y1) x2 += (((DISPLAY_HEIGHT - 1) - y2) * (x1 - x2)) / (y1 - y2);
        y2 = DISPLAY_HEIGHT - 1;
    }
    if(x1 < clipLeft) {
        if(x2 != x1) {
            y1 += ((clipLeft - x1) * (y2 - y1)) / (x2 - x1);
        }
        x1 = clipLeft;
    }
    int16_t dx = x2 - x1;
    int16_t yerror = dx / 2;
    int16_t y = y1;
    int16_t dy;
    int8_t ystep;
    if(y1 < y2) {
        dy = y2 - y1;
        ystep = 1;
    } else {
        dy = y1 - y2;
        ystep = -1;
    }
    for(int x = x1; x <= x2 && x <= clipRight; x++) {
        int8_t horizon = horizonBuffer[x] - HORIZON;
        Platform::PutPixel(x, horizon + y, col);
        yerror -= dy;
        while(yerror < 0) {
            y += ystep;
            //if(y < 0 || y >= DISPLAY_HEIGHT)
            //	return;
            yerror += dx;
            if(yerror < 0) {
                Platform::PutPixel(x, horizon + y, col);
            }
            if(x == x2 && y == y2) break;
        }
    }
}
#endif
#if WITH_IMAGE_TEXTURES
void Renderer::DrawWallSegment(
    const uint16_t* texture,
    int16_t x1,
    int16_t w1,
    int16_t x2,
    int16_t w2,
    uint8_t u1clip,
    uint8_t u2clip,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#elif WITH_VECTOR_TEXTURES
void Renderer::DrawWallSegment(
    const uint8_t* texture,
    int16_t x1,
    int16_t w1,
    int16_t x2,
    int16_t w2,
    uint8_t u1clip,
    uint8_t u2clip,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#else
void Renderer::DrawWallSegment(
    int16_t x1,
    int16_t w1,
    int16_t x2,
    int16_t w2,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#endif
{
    UNUSED(shadeEdge);
    if(x2 < viewX || x1 >= viewRight) return;
    if(x1 < viewX) {
        const int16_t clipDelta = viewX - x1;
#if WITH_TEXTURES
        u1clip += ((int32_t)clipDelta * (int32_t)(u2clip - u1clip)) / (x2 - x1);
#endif
        w1 += ((int32_t)clipDelta * (int32_t)(w2 - w1)) / (x2 - x1);
        x1 = viewX;
        edgeLeft = false;
    }
    int16_t dx = x2 - x1;
    if(dx <= 0) return;
    int16_t werror = dx / 2;
    int16_t w = w1;
    int16_t dw;
    int8_t wstep;
#if WITH_IMAGE_TEXTURES
    uint8_t du = (uint8_t)(u2clip - u1clip);
    int16_t uerror = werror;
    uint8_t u = u1clip;
#endif
    if(w1 < w2) {
        dw = w2 - w1;
        wstep = 1;
    } else {
        dw = w1 - w2;
        wstep = -1;
    }
    constexpr uint8_t edgeColour = COLOUR_BLACK;
    uint8_t segmentClipLeft = (uint8_t)x1;
    uint8_t segmentClipRight = (x2 < viewRight) ? (uint8_t)x2 : (uint8_t)(viewRight - 1);
    for(int x = x1; x < viewRight; x++) {
        // NOTE: x is >= x1 and x1 is clamped to >=0 above, so x>=0 always here.
        bool drawSlice = (wBuffer[x] < w);
        bool shadeSlice = false;
        // Use wider type for safe math
        int16_t horizon = (int16_t)horizonBuffer[x];
        if(drawSlice) {
            uint8_t sliceMask = 0xff;
            const bool isEdgeColumn = (edgeLeft && x == x1) || (edgeRight && x == x2);
            if(shadeSlice) {
                sliceMask = 0x55;
            }
#if WITH_IMAGE_TEXTURES
            {
                // Clip vertical extents (already safe here)
                uint8_t y1s = (w > horizon) ? 0 : (uint8_t)(horizon - w);
                uint8_t y2s = (horizon + w > DISPLAY_HEIGHT) ? DISPLAY_HEIGHT : (uint8_t)(horizon + w);
                DrawVLine(x, y1s, y2s, sliceMask);
                uint16_t textureData = pgm_read_word(&texture[u % 16]);
                const uint16_t wallSize = (uint16_t)(w * 2);
                uint16_t wallPos = (uint16_t)(y1s - (horizon - w));
                for(uint8_t y = y1s; y < y2s; y++) {
                    uint8_t v = (uint8_t)((16u * wallPos) / wallSize);
                    uint16_t mask = pgm_read_word(&scaleDrawReadMasks[v]);
                    if((textureData & mask) == 0) {
                        Platform::PutPixel((uint8_t)x, y, 0);
                    }
                    wallPos++;
                }
            }
#else
            // -------- FIX: hard clip Y to screen to prevent OOB writes --------
            // Original code used horizon±extent without clipping; with extent=64 this goes outside 0..63.
            int16_t extent = (w > 64) ? 64 : w;
            int16_t yTop = horizon - extent;
            int16_t yBot = horizon + extent;
            if(yTop < 0) yTop = 0;
            if(yBot > (DISPLAY_HEIGHT - 1)) yBot = (DISPLAY_HEIGHT - 1);
            if(yTop <= yBot) {
                Platform::DrawVLine((uint8_t)x, (int16_t)yTop, (int16_t)yBot, sliceMask);
                if(isEdgeColumn) {
                    for(int16_t py = yTop; py <= yBot; py++) {
                        Platform::PutPixel((uint8_t)x, (uint8_t)py, edgeColour);
                    }
                }
                Platform::PutPixel((uint8_t)x, (uint8_t)yTop, edgeColour);
                Platform::PutPixel((uint8_t)x, (uint8_t)yBot, edgeColour);
            }
#endif
            if(wBuffer[x] == 0) {
                numBufferSlicesFilled++;
            }
            wBuffer[x] = (w > 255) ? 255 : (uint8_t)w;
        } else {
            if(x == segmentClipLeft) {
                segmentClipLeft++;
            } else if(x < segmentClipRight) {
                segmentClipRight = (uint8_t)x;
                break;
            }
        }
        if(x == x2) break;
        werror -= dw;
        while(werror < 0) {
            w += wstep;
            werror += dx;
            // These pixels can also go OOB. Clip them too.
            if(drawSlice && werror < 0 && w <= DISPLAY_HEIGHT / 2) {
                int16_t yA = horizon + w - 1;
                int16_t yB = horizon - w;
                if((uint16_t)yA < DISPLAY_HEIGHT) Platform::PutPixel((uint8_t)x, (uint8_t)yA, edgeColour);
                if((uint16_t)yB < DISPLAY_HEIGHT) Platform::PutPixel((uint8_t)x, (uint8_t)yB, edgeColour);
            }
        }
#if WITH_IMAGE_TEXTURES
        uerror -= du;
        while(uerror < 0) {
            u++;
            uerror += dx;
        }
#endif
    }
    if(segmentClipLeft == segmentClipRight) return;
#if WITH_VECTOR_TEXTURES
    UNUSED(texture);
    UNUSED(u1clip);
    UNUSED(u2clip);
#endif
}
bool Renderer::isFrustrumClipped(int16_t x, int16_t y) {
    if((camera.clipCos * (x - camera.cellX) - camera.clipSin * (y - camera.cellY)) < -512)
        return true;
    if((camera.clipSin * (x - camera.cellX) + camera.clipCos * (y - camera.cellY)) < -512)
        return true;
    return false;
}
void Renderer::TransformToViewSpace(int16_t x, int16_t y, int16_t& outX, int16_t& outY) {
    int32_t relX = x - camera.x;
    int32_t relY = y - camera.y;
    outY = (int16_t)((camera.rotCos * relX) >> 8) - (int16_t)((camera.rotSin * relY) >> 8);
    outX = (int16_t)((camera.rotSin * relX) >> 8) + (int16_t)((camera.rotCos * relY) >> 8);
}
void Renderer::TransformToScreenSpace(int16_t viewX, int16_t viewZ, int16_t& outX, int16_t& outW) {
    // apply perspective projection
    outX = (int16_t)((int32_t)viewX * nearPlane * CAMERA_SCALE / viewZ);
    outW = (int16_t)((CELL_SIZE / 2 * nearPlane * CAMERA_SCALE) / viewZ);
    // transform into screen space
    outX = (int16_t)(viewCenterX + outX);
}
#if WITH_IMAGE_TEXTURES
void Renderer::DrawWall(
    const uint16_t* texture,
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#elif WITH_VECTOR_TEXTURES
void Renderer::DrawWall(
    const uint8_t* texture,
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#else
void Renderer::DrawWall(
    int16_t x1,
    int16_t y1,
    int16_t x2,
    int16_t y2,
    bool edgeLeft,
    bool edgeRight,
    bool shadeEdge)
#endif
{
    int16_t viewX1, viewZ1, viewX2, viewZ2;
#if WITH_VECTOR_TEXTURES
    uint8_t u1 = 0, u2 = 128;
#elif WITH_IMAGE_TEXTURES
    uint8_t u1 = 0, u2 = 16;
#endif
    TransformToViewSpace(x1, y1, viewX1, viewZ1);
    TransformToViewSpace(x2, y2, viewX2, viewZ2);
    // Frustum cull
    if(viewX2 < 0 && -2 * viewZ2 > viewX2) return;
    if(viewX1 > 0 && 2 * viewZ1 < viewX1) return;
    // clip to the front pane
    if((viewZ1 < CLIP_PLANE) && (viewZ2 < CLIP_PLANE)) return;
    if(viewZ1 < CLIP_PLANE) {
#if WITH_TEXTURES
        u1 += (CLIP_PLANE - viewZ1) * (u2 - u1) / (viewZ2 - viewZ1);
#endif
        viewX1 += (CLIP_PLANE - viewZ1) * (viewX2 - viewX1) / (viewZ2 - viewZ1);
        viewZ1 = CLIP_PLANE;
        edgeLeft = false;
    } else if(viewZ2 < CLIP_PLANE) {
#if WITH_TEXTURES
        u2 += (CLIP_PLANE - viewZ2) * (u1 - u2) / (viewZ1 - viewZ2);
#endif
        viewX2 += (CLIP_PLANE - viewZ2) * (viewX1 - viewX2) / (viewZ1 - viewZ2);
        viewZ2 = CLIP_PLANE;
        edgeRight = false;
    }
    // apply perspective projection
    int16_t vx1 = (int16_t)((int32_t)viewX1 * nearPlane * CAMERA_SCALE / viewZ1);
    int16_t vx2 = (int16_t)((int32_t)viewX2 * nearPlane * CAMERA_SCALE / viewZ2);
    // transform the end points into screen space
    int16_t sx1 = (int16_t)(viewCenterX + vx1);
    int16_t sx2 = (int16_t)(viewCenterX + vx2) - 1;
    if(sx1 >= sx2 || sx2 < viewX || sx1 >= viewRight) return;
    int16_t w1 = (int16_t)((CELL_SIZE / 2 * nearPlane * CAMERA_SCALE) / viewZ1);
    int16_t w2 = (int16_t)((CELL_SIZE / 2 * nearPlane * CAMERA_SCALE) / viewZ2);
#if WITH_TEXTURES
    DrawWallSegment(texture, sx1, w1, sx2, w2, u1, u2, edgeLeft, edgeRight, shadeEdge);
#else
    DrawWallSegment(sx1, w1, sx2, w2, edgeLeft, edgeRight, shadeEdge);
#endif
}
void swap(int16_t& a, int16_t& b) {
    int16_t temp = a;
    a = b;
    b = temp;
}
void Renderer::DrawFloorLineInner(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    uint8_t color = COLOUR_BLACK;
    bool steep = ABS(y1 - y0) > ABS(x1 - x0);
    if(steep) {
        swap(x0, y0);
        swap(x1, y1);
    }
    if(x0 > x1) {
        swap(x0, x1);
        swap(y0, y1);
    }
    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);
    int16_t err = dx / 2;
    int8_t ystep;
    if(y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }
    for(; x0 <= x1; x0++) {
        if(steep) {
            if(y0 >= viewX && y0 < viewRight && x0 >= 0 && x0 < DISPLAY_HEIGHT &&
               x0 > GetHorizon(y0) + wBuffer[y0] && x0 > GetHorizon(y0) + 8) {
                Platform::PutPixel((uint8_t)y0, (uint8_t)x0 + horizonBuffer[y0] - HORIZON, color);
            }
        } else {
            if(x0 >= viewX && x0 < viewRight && y0 >= 0 && y0 < DISPLAY_HEIGHT &&
               y0 > GetHorizon(x0) + wBuffer[x0] && y0 > GetHorizon(x0) + 8) {
                Platform::PutPixel((uint8_t)x0, (uint8_t)y0 + horizonBuffer[x0] - HORIZON, color);
            }
        }
        err -= dy;
        if(err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}
void Renderer::DrawFloorLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    int16_t viewX1, viewZ1, viewX2, viewZ2;
    TransformToViewSpace(x1, y1, viewX1, viewZ1);
    TransformToViewSpace(x2, y2, viewX2, viewZ2);
    //if(viewX1 > viewX2)
    //{
    //	swap(viewX1, viewX2);
    //	swap(viewZ1, viewZ2);
    //}
    // Frustum cull
    //	if (viewX2 < 0 && -2 * viewZ2 > viewX2)
    //		return;
    //	if (viewX1 > 0 && 2 * viewZ1 < viewX1)
    //		return;
    // clip to the front pane
    if((viewZ1 < CLIP_PLANE) && (viewZ2 < CLIP_PLANE)) return;
    if(viewZ1 < CLIP_PLANE) {
        viewX1 += (CLIP_PLANE - viewZ1) * (viewX2 - viewX1) / (viewZ2 - viewZ1);
        viewZ1 = CLIP_PLANE;
    } else if(viewZ2 < CLIP_PLANE) {
        viewX2 += (CLIP_PLANE - viewZ2) * (viewX1 - viewX2) / (viewZ1 - viewZ2);
        viewZ2 = CLIP_PLANE;
    }
    // apply perspective projection
    int16_t vx1 = (int16_t)((int32_t)viewX1 * nearPlane * CAMERA_SCALE / viewZ1);
    int16_t vx2 = (int16_t)((int32_t)viewX2 * nearPlane * CAMERA_SCALE / viewZ2);
    // transform the end points into screen space
    int16_t sx1 = (int16_t)(viewCenterX + vx1);
    int16_t sx2 = (int16_t)(viewCenterX + vx2) - 1;
    //if (sx2 < GAME_VIEW_X || sx1 >= GAME_VIEW_RIGHT)
    //	return;
    int16_t w1 = (int16_t)((CELL_SIZE / 2 * nearPlane * CAMERA_SCALE) / viewZ1);
    int16_t w2 = (int16_t)((CELL_SIZE / 2 * nearPlane * CAMERA_SCALE) / viewZ2);
    DrawFloorLineInner(sx1, HORIZON + w1, sx2, HORIZON + w2);
}
void Renderer::DrawFloorLines() {
    constexpr int size = 10;
    int16_t baseX = (Game::player.x - CELL_SIZE * size / 2) & 0xff00;
    int16_t baseY = (Game::player.y - CELL_SIZE * size / 2) & 0xff00;
    for(int n = 0; n < 10; n++) {
        DrawFloorLine(
            baseX,
            baseY + n * CELL_SIZE,
            baseX + CELL_SIZE * 10 - n * CELL_SIZE,
            baseY + CELL_SIZE * 10);
        DrawFloorLine(baseX, baseY + n * CELL_SIZE, baseX + n * CELL_SIZE, baseY);
    }
    for(int n = 1; n < 10; n++) {
        DrawFloorLine(
            baseX + n * CELL_SIZE,
            baseY,
            baseX + CELL_SIZE * 10,
            baseY + CELL_SIZE * 10 - n * CELL_SIZE);
        DrawFloorLine(
            baseX + n * CELL_SIZE,
            baseY + 10 * CELL_SIZE,
            baseX + 10 * CELL_SIZE,
            baseY + n * CELL_SIZE);
    }
}
void Renderer::DrawCell(uint8_t x, uint8_t y) {
    CellType cellType = Map::GetCellSafe(x, y);
    if(isFrustrumClipped(x, y)) {
        return;
    }
    switch(cellType) {
    case CellType::Torch: {
        const uint16_t* torchSpriteData = Game::globalTickFrame & 4 ? torchSpriteData1 :
                                                                      torchSpriteData2;
        constexpr uint8_t torchScale = 75;
        if(Map::IsSolid(x - 1, y)) {
            DrawObject(
                torchSpriteData,
                x * CELL_SIZE + CELL_SIZE / 7,
                y * CELL_SIZE + CELL_SIZE / 2,
                torchScale,
                AnchorType::Center);
        } else if(Map::IsSolid(x + 1, y)) {
            DrawObject(
                torchSpriteData,
                x * CELL_SIZE + 6 * CELL_SIZE / 7,
                y * CELL_SIZE + CELL_SIZE / 2,
                torchScale,
                AnchorType::Center);
        } else if(Map::IsSolid(x, y - 1)) {
            DrawObject(
                torchSpriteData,
                x * CELL_SIZE + CELL_SIZE / 2,
                y * CELL_SIZE + CELL_SIZE / 7,
                torchScale,
                AnchorType::Center);
        } else if(Map::IsSolid(x, y + 1)) {
            DrawObject(
                torchSpriteData,
                x * CELL_SIZE + CELL_SIZE / 2,
                y * CELL_SIZE + 6 * CELL_SIZE / 7,
                torchScale,
                AnchorType::Center);
        }
    }
        return;
    case CellType::Entrance:
        DrawObject(
            entranceSpriteData,
            x * CELL_SIZE + CELL_SIZE / 2,
            y * CELL_SIZE + CELL_SIZE / 2,
            96,
            AnchorType::Ceiling);
        return;
    case CellType::Exit:
        DrawObject(
            exitSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 96);
        return;
    case CellType::Urn:
        DrawObject(
            urnSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 80);
        return;
    case CellType::Potion:
        DrawObject(
            potionSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 64);
        return;
    case CellType::Scroll:
        DrawObject(
            scrollSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 64);
        return;
    case CellType::Coins:
        DrawObject(
            coinsSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 64);
        return;
    case CellType::Crown:
        DrawObject(
            crownSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 64);
        return;
    case CellType::Sign:
        DrawObject(
            signSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 80);
        return;
    case CellType::Chest:
        DrawObject(
            chestSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 75);
        return;
    case CellType::ChestOpened:
        DrawObject(
            chestOpenSpriteData, x * CELL_SIZE + CELL_SIZE / 2, y * CELL_SIZE + CELL_SIZE / 2, 75);
        return;
    default:
        break;
    }
    if(numBufferSlicesFilled >= viewWidth) {
        return;
    }
    if(!Map::IsSolid(x, y)) {
        return;
    }
    int16_t x1 = x * CELL_SIZE;
    int16_t y1 = y * CELL_SIZE;
    int16_t x2 = x1 + CELL_SIZE;
    int16_t y2 = y1 + CELL_SIZE;
    bool blockedLeft = Map::IsSolid(x - 1, y);
    bool blockedRight = Map::IsSolid(x + 1, y);
    bool blockedUp = Map::IsSolid(x, y - 1);
    bool blockedDown = Map::IsSolid(x, y + 1);
#if WITH_IMAGE_TEXTURES
    const uint16_t* texture = wallTextureData + (16 * (cellType - 1));
#elif WITH_VECTOR_TEXTURES
    const uint8_t* texture =
        vectorTexture0; // (const uint8_t*) pgm_read_ptr(&textures[(uint8_t)cellType - (uint8_t)CellType::FirstSolidCell]);
#endif
    if(!blockedLeft && camera.x < x1) {
        const bool topContinues = Map::IsSolid(x, y - 1) && !Map::IsSolid(x - 1, y - 1);
        const bool bottomContinues = Map::IsSolid(x, y + 1) && !Map::IsSolid(x - 1, y + 1);
        const bool topEdge = !topContinues;
        const bool bottomEdge = !bottomContinues;
#if WITH_TEXTURES
        DrawWall(
            texture,
            x1,
            y1,
            x1,
            y2,
            topEdge,
            bottomEdge,
            true);
#else
        DrawWall(x1, y1, x1, y2, topEdge, bottomEdge, true);
#endif
    }
    if(!blockedDown && camera.y > y2) {
        const bool leftContinues = Map::IsSolid(x - 1, y) && !Map::IsSolid(x - 1, y + 1);
        const bool rightContinues = Map::IsSolid(x + 1, y) && !Map::IsSolid(x + 1, y + 1);
        const bool leftEdge = !blockedLeft && camera.x >= x1 && !leftContinues;
        const bool rightEdge = !blockedRight && camera.x <= x2 && !rightContinues;
#if WITH_TEXTURES
        DrawWall(
            texture,
            x1,
            y2,
            x2,
            y2,
            leftEdge,
            rightEdge,
            false);
#else
        DrawWall(x1, y2, x2, y2, leftEdge, rightEdge, false);
#endif
    }
    if(!blockedRight && camera.x > x2) {
        const bool bottomContinues = Map::IsSolid(x, y + 1) && !Map::IsSolid(x + 1, y + 1);
        const bool topContinues = Map::IsSolid(x, y - 1) && !Map::IsSolid(x + 1, y - 1);
        const bool bottomEdge = !bottomContinues;
        const bool topEdge = !topContinues;
#if WITH_TEXTURES
        DrawWall(
            texture,
            x2,
            y2,
            x2,
            y1,
            bottomEdge,
            topEdge,
            true);
#else
        DrawWall(x2, y2, x2, y1, bottomEdge, topEdge, true);
#endif
    }
    if(!blockedUp && camera.y < y1) {
        const bool rightContinues = Map::IsSolid(x + 1, y) && !Map::IsSolid(x + 1, y - 1);
        const bool leftContinues = Map::IsSolid(x - 1, y) && !Map::IsSolid(x - 1, y - 1);
        const bool rightEdge = !blockedRight && camera.x <= x2 && !rightContinues;
        const bool leftEdge = !blockedLeft && camera.x >= x1 && !leftContinues;
#if WITH_TEXTURES
        DrawWall(
            texture,
            x2,
            y1,
            x1,
            y1,
            rightEdge,
            leftEdge,
            false);
#else
        DrawWall(x2, y1, x1, y1, rightEdge, leftEdge, false);
#endif
    }
}
void Renderer::DrawCells() {
    constexpr int8_t MAP_BUFFER_WIDTH = 16;
    constexpr int8_t MAP_BUFFER_HEIGHT = 16;
    int16_t cosAngle = FixedCos(camera.angle);
    int16_t sinAngle = FixedSin(camera.angle);
    int8_t bufferX = (int8_t)((camera.x + cosAngle * 7) >> 8) - MAP_BUFFER_WIDTH / 2;
    int8_t bufferY = (int8_t)((camera.y + sinAngle * 7) >> 8) - MAP_BUFFER_WIDTH / 2;
    ;
    if(bufferX < 0) bufferX = 0;
    if(bufferY < 0) bufferY = 0;
    if(bufferX > Map::width - MAP_BUFFER_WIDTH) bufferX = Map::width - MAP_BUFFER_WIDTH;
    if(bufferY > Map::height - MAP_BUFFER_HEIGHT) bufferY = Map::height - MAP_BUFFER_HEIGHT;
    // This should make cells draw front to back
    int8_t xd, yd;
    int8_t x1, y1, x2, y2;
    if(camera.rotCos > 0) {
        x1 = bufferX;
        x2 = x1 + MAP_BUFFER_WIDTH;
        xd = 1;
    } else {
        x2 = bufferX - 1;
        x1 = x2 + MAP_BUFFER_WIDTH;
        xd = -1;
    }
    if(camera.rotSin < 0) {
        y1 = bufferY;
        y2 = y1 + MAP_BUFFER_HEIGHT;
        yd = 1;
    } else {
        y2 = bufferY - 1;
        y1 = y2 + MAP_BUFFER_HEIGHT;
        yd = -1;
    }
    if(ABS(camera.rotCos) < ABS(camera.rotSin)) {
        for(int8_t y = y1; y != y2; y += yd) {
            for(int8_t x = x1; x != x2; x += xd) {
                DrawCell(x, y);
            }
        }
    } else {
        for(int8_t x = x1; x != x2; x += xd) {
            for(int8_t y = y1; y != y2; y += yd) {
                DrawCell(x, y);
            }
        }
    }
}
void DrawScaledOutline(
    const uint16_t* data,
    int8_t x,
    int8_t y,
    uint8_t halfSize,
    uint8_t inverseCameraDistance,
    uint8_t shiftAmount,
    bool invert) {
    uint8_t size = 2 * halfSize;
    const uint8_t* lut =
        scaleLUT + (((halfSize - 1) >> shiftAmount) * ((halfSize - 1) >> shiftAmount));
    uint8_t i0 = x < Renderer::viewX ? (uint8_t)(Renderer::viewX - x) : 0;
    uint8_t i1 = x + size > Renderer::viewRight ? Renderer::viewRight - x : size;
    uint8_t j0 = y < 0 ? -y : 0;
    uint8_t j1 = y + size > DISPLAY_HEIGHT ? DISPLAY_HEIGHT - y : size;
    int8_t outX = x >= Renderer::viewX ? x : (int8_t)Renderer::viewX;
    uint16_t invertMask = invert ? 0xffff : 0;
    uint16_t leftTransparencyAndColourColumn = 0;
    uint16_t middleTransparencyColumn = 0;
    uint16_t rightTransparencyColumn = 0;
    uint16_t middleColourColumn = 0;
    uint16_t rightColourColumn = 0;
    bool wasVisible = false;
    for(uint8_t i = i0; i < i1; i++) {
        const bool isVisible = Renderer::wBuffer[outX] < inverseCameraDistance;
        if(isVisible) {
            uint16_t leftRightOutlineColumn = 0;
            if(i >= i1 - 2) {
                if(wasVisible) {
                    leftTransparencyAndColourColumn = middleColourColumn &
                                                      middleTransparencyColumn;
                    middleColourColumn = rightColourColumn;
                    middleTransparencyColumn = rightTransparencyColumn;
                    rightTransparencyColumn = 0;
                    rightColourColumn = 0;
                    leftRightOutlineColumn = leftTransparencyAndColourColumn;
                } else {
                    break;
                }
            } else {
                const uint8_t u = pgm_read_byte(&lut[i >> shiftAmount]);
                if(wasVisible) {
                    leftTransparencyAndColourColumn = middleColourColumn &
                                                      middleTransparencyColumn;
                    middleColourColumn = rightColourColumn;
                    middleTransparencyColumn = rightTransparencyColumn;
                    rightTransparencyColumn = pgm_read_word(&data[u * 2]);
                    rightColourColumn = pgm_read_word(&data[u * 2 + 1]) ^ invertMask;
                    leftRightOutlineColumn = leftTransparencyAndColourColumn |
                                             (rightColourColumn & rightTransparencyColumn);
                } else {
                    leftTransparencyAndColourColumn = 0;
                    rightTransparencyColumn = pgm_read_word(&data[u * 2]);
                    rightColourColumn = pgm_read_word(&data[u * 2 + 1]) ^ invertMask;
                    middleColourColumn = rightColourColumn;
                    middleTransparencyColumn = rightTransparencyColumn;
                    leftRightOutlineColumn = (rightColourColumn & rightTransparencyColumn);
                }
            }
            int8_t outY = y >= 0 ? y : 0;
            uint8_t bufferPos = (outY & 7);
            uint8_t* screenBuffer = Platform::GetScreenBuffer() + outX + ((outY & 0x38) << 4);
            uint8_t localBuffer = *screenBuffer;
            uint8_t writeMask = pgm_read_byte(&scaleDrawWriteMasks[bufferPos]);
            bool upIsOpaqueAndWhite = false;
            bool middleIsOpaque = false;
            bool downIsOpaque = false;
            bool middleIsWhite = false;
            bool downIsWhite = false;
            bool leftOrRightIsOutline = false;
            bool downLeftOrRightIsOutline = false;
            for(uint8_t j = j0; j < j1; j++) {
                upIsOpaqueAndWhite = middleIsOpaque && middleIsWhite;
                middleIsOpaque = downIsOpaque;
                middleIsWhite = downIsWhite;
                leftOrRightIsOutline = downLeftOrRightIsOutline;
                if(j >= j1 - 2) {
                    downIsOpaque = false;
                    downIsWhite = false;
                    downLeftOrRightIsOutline = false;
                } else {
                    uint8_t v = pgm_read_byte(&lut[j >> shiftAmount]);
                    uint16_t mask = pgm_read_word(&scaleDrawReadMasks[v]);
                    downLeftOrRightIsOutline = (leftRightOutlineColumn & mask) != 0;
                    downIsOpaque = (middleTransparencyColumn & mask) != 0;
                    downIsWhite = (middleColourColumn & mask) != 0;
                }
                if(middleIsOpaque && j < j1) {
                    if(middleIsWhite) {
                        localBuffer |= writeMask;
                    } else {
                        localBuffer &= ~writeMask;
                    }
                } else if(
                    leftOrRightIsOutline || (upIsOpaqueAndWhite) ||
                    (downIsOpaque && downIsWhite)) {
                    localBuffer &= ~writeMask;
                }
                outY++;
                bufferPos++;
                writeMask <<= 1;
                if(bufferPos == 8) {
                    bufferPos = 0;
                    writeMask = 1;
                    *screenBuffer = localBuffer;
                    if(outY < DISPLAY_HEIGHT) {
                        screenBuffer += 128;
                    }
                    localBuffer = *screenBuffer;
                }
            }
            *screenBuffer = localBuffer;
        }
        outX++;
        wasVisible = isVisible;
    }
}
template <int scaleMultiplier>
inline void DrawScaledNoOutline(
    const uint16_t* data,
    int8_t x,
    int8_t y,
    uint8_t halfSize,
    uint8_t inverseCameraDistance) {
    uint8_t size = 2 * halfSize;
    const uint8_t* lut = scaleLUT + ((halfSize / scaleMultiplier) * (halfSize / scaleMultiplier));
    uint8_t i0 = x < Renderer::viewX ? (uint8_t)(Renderer::viewX - x) : 0;
    uint8_t i1 = x + size > Renderer::viewRight ? Renderer::viewRight - x : size;
    uint8_t j0 = y < 0 ? -y : 0;
    uint8_t j1 = y + size > DISPLAY_HEIGHT ? DISPLAY_HEIGHT - y : size;
    int8_t outX = x >= Renderer::viewX ? x : (int8_t)Renderer::viewX;
    for(uint8_t i = i0; i < i1; i++) {
        const bool isVisible = Renderer::wBuffer[outX] < inverseCameraDistance;
        if(isVisible) {
            const uint8_t u = pgm_read_byte(&lut[i / scaleMultiplier]);
            int8_t outY = y >= 0 ? y : 0;
            uint8_t bufferPos = (outY & 7);
            uint8_t* screenBuffer = Platform::GetScreenBuffer() + outX + ((outY & 0x38) << 4);
            uint8_t localBuffer = *screenBuffer;
            uint8_t writeMask = pgm_read_byte(&scaleDrawWriteMasks[bufferPos]);
            uint16_t transparencyColumn = pgm_read_word(&data[u * 2]);
            uint16_t colourColumn = pgm_read_word(&data[u * 2 + 1]);
            for(uint8_t j = j0; j < j1; j += scaleMultiplier) {
                uint8_t v = pgm_read_byte(&lut[j / scaleMultiplier]);
                uint16_t mask = pgm_read_word(&scaleDrawReadMasks[v]);
                for(uint8_t k = 0; k < scaleMultiplier; k++) {
                    bool isOpaque = (transparencyColumn & mask) != 0;
                    if(isOpaque) {
                        bool isWhite = (colourColumn & mask) != 0;
                        if(isWhite) {
                            localBuffer |= writeMask;
                        } else {
                            localBuffer &= ~writeMask;
                        }
                    }
                    outY++;
                    bufferPos++;
                    writeMask <<= 1;
                    if(bufferPos == 8) {
                        bufferPos = 0;
                        writeMask = 1;
                        *screenBuffer = localBuffer;
                        if(outY < DISPLAY_HEIGHT) {
                            screenBuffer += 128;
                        }
                        localBuffer = *screenBuffer;
                    }
                }
            }
            *screenBuffer = localBuffer;
        }
        outX++;
    }
}
void Renderer::DrawScaled(
    const uint16_t* data,
    int8_t x,
    int8_t y,
    uint8_t halfSize,
    uint8_t inverseCameraDistance,
    bool invert,
    uint8_t color) {
    if(halfSize > MAX_SPRITE_SIZE * 2) {
        return;
    } else if(halfSize > MAX_SPRITE_SIZE) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 2, invert);
    } else if(halfSize * 2 > MAX_SPRITE_SIZE) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 1, invert);
    } else if(halfSize > 2) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 0, invert);
    } else if(halfSize == 2) {
        const int16_t x0 = x;
        const int16_t x1 = x + 1;
        if(x0 >= viewX && Renderer::wBuffer[x0] < inverseCameraDistance) {
            Platform::PutPixel(x, y, color);
            Platform::PutPixel(x, y + 1, color);
        }
        if(x1 >= viewX && Renderer::wBuffer[x1] < inverseCameraDistance) {
            Platform::PutPixel(x + 1, y, color);
            Platform::PutPixel(x + 1, y + 1, color);
        }
    } else {
        const int16_t x0 = x;
        if(x0 >= viewX && Renderer::wBuffer[x0] < inverseCameraDistance) {
            Platform::PutPixel(x, y, color);
        }
    }
}
QueuedDrawable* Renderer::CreateQueuedDrawable(uint8_t inverseCameraDistance) {
    uint8_t insertionPoint = MAX_QUEUED_DRAWABLES;
    for(uint8_t n = 0; n < numQueuedDrawables; n++) {
        if(inverseCameraDistance < queuedDrawables[n].inverseCameraDistance) {
            if(numQueuedDrawables < MAX_QUEUED_DRAWABLES) {
                insertionPoint = n;
                numQueuedDrawables++;
                for(uint8_t i = numQueuedDrawables - 1; i > n; i--) {
                    queuedDrawables[i] = queuedDrawables[i - 1];
                }
            } else {
                if(n == 0) {
                    // List is full and this is smaller than the first element so just cull
                    return nullptr;
                }
                // Drop the smallest element to make a space
                for(uint8_t i = 0; i < n - 1; i++) {
                    queuedDrawables[i] = queuedDrawables[i + 1];
                }
                insertionPoint = n - 1;
            }
            break;
        }
    }
    if(insertionPoint == MAX_QUEUED_DRAWABLES) {
        if(numQueuedDrawables < MAX_QUEUED_DRAWABLES) {
            insertionPoint = numQueuedDrawables;
            numQueuedDrawables++;
        } else if(
            inverseCameraDistance >
            queuedDrawables[numQueuedDrawables - 1].inverseCameraDistance) {
            // Drop the smallest element to make a space
            for(uint8_t i = 0; i < numQueuedDrawables - 1; i++) {
                queuedDrawables[i] = queuedDrawables[i + 1];
            }
            insertionPoint = numQueuedDrawables - 1;
        } else {
            return nullptr;
        }
    }
    return &queuedDrawables[insertionPoint];
}
void Renderer::QueueSprite(
    const uint16_t* data,
    int8_t x,
    int8_t y,
    uint8_t halfSize,
    uint8_t inverseCameraDistance,
    bool invert) {
    if(x < -halfSize * 2) return;
    //if(x >= DISPLAY_WIDTH)
    //	return;
    //if(halfSize <= 2)
    //	return;
    QueuedDrawable* drawable = CreateQueuedDrawable(inverseCameraDistance);
    if(drawable != nullptr) {
        drawable->type = DrawableType::Sprite;
        drawable->spriteData = data;
        drawable->x = x;
        drawable->y = y;
        drawable->halfSize = halfSize;
        drawable->inverseCameraDistance = inverseCameraDistance;
        drawable->invert = invert;
    }
}
void Renderer::RenderQueuedDrawables() {
    for(uint8_t n = 0; n < numQueuedDrawables; n++) {
        QueuedDrawable& drawable = queuedDrawables[n];
        if(drawable.type == DrawableType::Sprite) {
            DrawScaled(
                drawable.spriteData,
                drawable.x,
                drawable.y,
                drawable.halfSize,
                drawable.inverseCameraDistance,
                drawable.invert);
        } else {
            drawable.particleSystem->Draw(drawable.x, drawable.inverseCameraDistance);
        }
    }
}
int8_t Renderer::GetHorizon(int16_t x) {
    if(x < 0) x = 0;
    if(x < viewX) x = viewX;
    if(x >= viewRight) x = viewRight - 1;
    return horizonBuffer[x];
}
bool Renderer::TransformAndCull(
    int16_t worldX,
    int16_t worldY,
    int16_t& outScreenX,
    int16_t& outScreenW) {
    int16_t relX, relZ;
    TransformToViewSpace(worldX, worldY, relX, relZ);
    // Frustum cull
    if(relZ < CLIP_PLANE) return false;
    if(relX < 0 && -2 * relZ > relX) return false;
    if(relX > 0 && 2 * relZ < relX) return false;
    TransformToScreenSpace(relX, relZ, outScreenX, outScreenW);
    return true;
}
void Renderer::DrawObject(
    const uint16_t* spriteData,
    int16_t x,
    int16_t y,
    uint8_t scale,
    AnchorType anchor,
    bool invert) {
    int16_t screenX, screenW;
    if(TransformAndCull(x, y, screenX, screenW)) {
        // Bit of a hack: nudge sorting closer to the camera
        uint8_t inverseCameraDistance = (uint8_t)(screenW + 1);
        int16_t spriteSize = (screenW * scale) / 128;
        int8_t outY = GetHorizon(screenX);
        switch(anchor) {
        case AnchorType::Floor:
            outY += screenW - 2 * spriteSize;
            break;
        case AnchorType::Center:
            outY -= spriteSize;
            break;
        case AnchorType::BelowCenter:
            break;
        case AnchorType::Ceiling:
            outY -= screenW;
            break;
        }
        QueueSprite(
            spriteData,
            screenX - spriteSize,
            outY,
            (uint8_t)spriteSize,
            inverseCameraDistance,
            invert);
    }
}
void Renderer::DrawWeapon() {
    constexpr uint8_t gunWidth = 40;
    constexpr uint8_t gunHeight = 29;
    int x = DISPLAY_WIDTH - gunWidth + 2 + camera.tilt / 6;
    int y = DISPLAY_HEIGHT - gunHeight - camera.bob;
    uint8_t reloadTime = Game::player.reloadTime;
    if(reloadTime > 0) {
        Platform::DrawSprite(x + reloadTime / 2, y + reloadTime / 3, gunSpriteData, 0);
    } else {
        Platform::DrawSprite(x, y, gunSpriteData, 0);
    }
}
void Renderer::DrawBackground() {
    constexpr int16_t centerOffset = 2;
    UpdateBackgroundParallax();

#if !LEVEL_THEME_DAY
        const uint8_t bottomTileH = pgm_read_byte(&backgroundBottomDarkSpriteData[1]);
        const int16_t bottomY = DISPLAY_HEIGHT - bottomTileH - centerOffset;
        Platform::FillScreen(COLOUR_WHITE);

        for(int16_t x = viewX; x < viewRight; x++) {
            int16_t skyBottom = horizonBuffer[x] + kNightSkyFloorGap;
            if(skyBottom < centerOffset) {
                skyBottom = centerOffset;
            }
            if(skyBottom >= bottomY) {
                skyBottom = bottomY - 1;
            }
            FillColumn(x, centerOffset, skyBottom, COLOUR_BLACK);
        }

        DrawNightSkyStars(centerOffset, bottomY - 1);
        DrawPerspectiveFloorBand(backgroundBottomDarkSpriteData, bottomY);
        return;
#else
    const uint8_t bottomTileH = pgm_read_byte(&backgroundTopSpriteData[1]);
    Platform::FillScreen(COLOUR_WHITE);
    DrawTiledSpriteSampled(backgroundBottomSpriteData, centerOffset, g_backgroundParallax.skySampleX, 0);
    DrawPerspectiveFloorBand(backgroundTopSpriteData, DISPLAY_HEIGHT - bottomTileH - centerOffset);
#endif
}
void Renderer::DrawBar(uint8_t* screenPtr, const uint8_t* iconData, uint8_t amount, uint8_t max) {
    constexpr uint8_t iconWidth = 8;
    constexpr uint8_t barWidth = 32;
    constexpr uint8_t unfilledBar = 0xfe;
    constexpr uint8_t filledBar = 0xc6;
    uint8_t fillAmount = (amount * barWidth) / max;
    uint8_t x = 0;
    while(x < iconWidth) {
        screenPtr[x] = pgm_read_byte(&iconData[x]);
        x++;
    }
    while(fillAmount--) {
        screenPtr[x++] = filledBar;
    }
    while(x < barWidth + iconWidth) {
        screenPtr[x++] = unfilledBar;
    }
    screenPtr[x++] = unfilledBar;
    screenPtr[x] = 0;
}
void Renderer::DrawDamageIndicator() {
}
void Renderer::DrawHUD() {
    constexpr uint8_t aimWidth = 13;
    constexpr uint8_t aimHeight = 13;
    int8_t aimSkew = camera.tilt / 28;
    const bool invertAim = (Game::player.damageTime > 0);
    if(aimSkew < -2) aimSkew = -2;
    if(aimSkew > 2) aimSkew = 2;
    DrawTiltedSprite(
        viewCenterX - aimWidth / 2,
        (DISPLAY_HEIGHT - aimHeight) / 2 - camera.bob / 2,
        aimSpriteData,
        aimSkew,
        invertAim);
    Platform::DrawSprite(0, 0, avatarSpriteData, 0);
    DrawSidebarPanel();
    DrawPixelNumber(11, 23, Game::player.hp, COLOUR_WHITE);
    DrawPixelNumber(11, 35, Game::player.mana, COLOUR_WHITE);
}
void Renderer::Render() {
    SetGameViewport();
    globalRenderFrame++;
    camera.cellX = camera.x / CELL_SIZE;
    camera.cellY = camera.y / CELL_SIZE;
    camera.rotCos = FixedCos(-camera.angle);
    camera.rotSin = FixedSin(-camera.angle);
    camera.clipCos = FixedCos(-camera.angle + CLIP_ANGLE);
    camera.clipSin = FixedSin(-camera.angle + CLIP_ANGLE);
    for(uint8_t n = 0; n < DISPLAY_WIDTH; n++) {
        const bool isGameColumn = (n >= viewX);
        wBuffer[n] = isGameColumn ? 0 : 255;
        horizonBuffer[n] = HORIZON + (((viewCenterX - n) * camera.tilt) >> 8) + camera.bob;
    }
    DrawBackground();
    ClearSidebar();
    numBufferSlicesFilled = 0;
    numQueuedDrawables = 0;
    DrawCells();
    //DrawFloorLines();
    EnemyManager::Draw();
    ProjectileManager::Draw();
    ParticleSystemManager::Draw();
    RenderQueuedDrawables();
    DrawWeapon();
    DrawHUD();
    DrawGameOverlayFrame();
    //Map::DrawMinimap();
}
