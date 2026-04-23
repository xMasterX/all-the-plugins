#include <stdint.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "lodepng.h"

namespace fs = std::filesystem;

enum class ImageColour {
    Transparent,
    Black,
    White,
};

enum class AssetKind {
    Sprite3D,
    Sprite2D,
    Texture,
    HudElement,
};

struct AssetTask {
    AssetKind kind;
    const char* inputPath;
    const char* variableName;
};

struct Config {
    fs::path root = fs::current_path();
    fs::path imagesDir;
    fs::path outputDir;
};

static const AssetTask kAssetTasks[] = {
    {AssetKind::Sprite3D, "enemy.png", "skeletonSpriteData"},
    {AssetKind::Sprite3D, "mage.png", "mageSpriteData"},
    {AssetKind::Sprite3D, "torchalt1.png", "torchSpriteData1"},
    {AssetKind::Sprite3D, "torchalt2.png", "torchSpriteData2"},
    {AssetKind::Sprite3D, "fireball2.png", "projectileSpriteData"},
    {AssetKind::Sprite3D, "fireball.png", "enemyProjectileSpriteData"},
    {AssetKind::Sprite3D, "entrance.png", "entranceSpriteData"},
    {AssetKind::Sprite3D, "exit.png", "exitSpriteData"},
    {AssetKind::Sprite3D, "urn.png", "urnSpriteData"},
    {AssetKind::Sprite3D, "sign.png", "signSpriteData"},
    {AssetKind::Sprite3D, "crown.png", "crownSpriteData"},
    {AssetKind::Sprite3D, "coins.png", "coinsSpriteData"},
    {AssetKind::Sprite3D, "scroll.png", "scrollSpriteData"},
    {AssetKind::Sprite3D, "chest.png", "chestSpriteData"},
    {AssetKind::Sprite3D, "chestopen.png", "chestOpenSpriteData"},
    {AssetKind::Sprite3D, "potion.png", "potionSpriteData"},
    {AssetKind::Sprite3D, "bat.png", "batSpriteData"},
    {AssetKind::Sprite3D, "spider.png", "spiderSpriteData"},
    {AssetKind::Sprite2D, "hand1.png", "handSpriteData1"},
    {AssetKind::Sprite2D, "hand2.png", "handSpriteData2"},
    {AssetKind::Sprite2D, "aim.png", "aimSpriteData"},
    {AssetKind::Sprite2D, "avatar.png", "avatarSpriteData"},
    {AssetKind::Sprite2D, "background_bottom.png", "backgroundBottomSpriteData"},
    {AssetKind::Sprite2D, "background_bottom_dark.png", "backgroundBottomDarkSpriteData"},
    {AssetKind::Sprite2D, "background_top.png", "backgroundTopSpriteData"},
    {AssetKind::Sprite2D, "logo.png", "logoSpriteData"},
    {AssetKind::Sprite2D, "menu_bottom.png", "menuBottomSpriteData"},
    {AssetKind::Sprite2D, "skull.png", "skullSpriteData"},
    {AssetKind::Sprite2D, "gun.png", "gunSpriteData"},
    {AssetKind::Sprite2D, "skeleton.png", "skeletonAltSpriteData"},
    {AssetKind::Sprite2D, "torch1.png", "torchSpriteLegacyData1"},
    {AssetKind::Sprite2D, "torch2.png", "torchSpriteLegacyData2"},
    {AssetKind::Sprite2D, "torch3.png", "torchSpriteLegacyData3"},
    {AssetKind::Sprite2D, "urn2.png", "urn2SpriteData"},
    {AssetKind::Texture, "textures.png", "wallTextureData"},
    {AssetKind::HudElement, "font.png", "fontPageData"},
    {AssetKind::HudElement, "heart.png", "heartSpriteData"},
    {AssetKind::HudElement, "mana.png", "manaSpriteData"},
};

static void PrintUsage(const char* exe) {
    std::cout
        << "Usage: " << exe << " [--root <repo_root>] [--images <images_dir>] [--out-dir <generated_dir>]\n"
        << "Defaults:\n"
        << "  root    = current working directory\n"
        << "  images  = <root>/Images\n"
        << "  out-dir = <root>/game/Generated\n";
}

static bool ParseArgs(int argc, char* argv[], Config& config) {
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        }

        if(i + 1 >= argc) {
            std::cerr << "Missing value for argument: " << arg << '\n';
            PrintUsage(argv[0]);
            return false;
        }

        fs::path value = argv[++i];
        if(arg == "--root") {
            config.root = value;
        } else if(arg == "--images") {
            config.imagesDir = value;
        } else if(arg == "--out-dir") {
            config.outputDir = value;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            PrintUsage(argv[0]);
            return false;
        }
    }

    if(config.imagesDir.empty()) {
        config.imagesDir = config.root / "Images";
    }
    if(config.outputDir.empty()) {
        config.outputDir = config.root / "game" / "Generated";
    }

    return true;
}

static ImageColour CalculateColour(const std::vector<uint8_t>& pixels, unsigned int x, unsigned int y, unsigned int pitch) {
    const unsigned int index = (y * pitch + x) * 4;
    if(pixels[index] == 0 && pixels[index + 1] == 0 && pixels[index + 2] == 0 && pixels[index + 3] == 255) {
        return ImageColour::Black;
    }
    if(pixels[index] == 255 && pixels[index + 1] == 255 && pixels[index + 2] == 255 && pixels[index + 3] == 255) {
        return ImageColour::White;
    }

    return ImageColour::Transparent;
}

static bool DecodeImage(
    const fs::path& inputPath,
    std::vector<uint8_t>& pixels,
    unsigned& width,
    unsigned& height) {
    const unsigned error = lodepng::decode(pixels, width, height, inputPath.string());
    if(error) {
        std::cerr << inputPath << " : decoder error " << error << ": " << lodepng_error_text(error) << '\n';
        return false;
    }

    return true;
}

static bool EncodeSprite3D(
    std::ofstream& typefs,
    std::ofstream& fsout,
    const fs::path& inputPath,
    const char* relativePath,
    const char* variableName) {
    std::vector<uint8_t> pixels;
    unsigned width = 0;
    unsigned height = 0;
    if(!DecodeImage(inputPath, pixels, width, height)) {
        return false;
    }

    if(height != 16) {
        std::cerr << inputPath << " : sprite must be 16 pixels high\n";
        return false;
    }
    if((width % 16) != 0) {
        std::cerr << inputPath << " : sprite width must be multiple of 16 pixels\n";
        return false;
    }

    std::vector<uint16_t> colourMasks;
    std::vector<uint16_t> transparencyMasks;

    for(unsigned x = 0; x < width; ++x) {
        uint16_t colour = 0;
        uint16_t transparency = 0;

        for(unsigned y = 0; y < height; ++y) {
            const uint16_t mask = static_cast<uint16_t>(1u << y);
            const ImageColour pixelColour = CalculateColour(pixels, x, y, width);

            if(pixelColour == ImageColour::Black) {
                transparency |= mask;
            } else if(pixelColour == ImageColour::White) {
                transparency |= mask;
                colour |= mask;
            }
        }

        colourMasks.push_back(colour);
        transparencyMasks.push_back(transparency);
    }

    const unsigned int numFrames = width / 16;

    typefs << "// Generated from Images/" << relativePath << '\n';
    typefs << "constexpr uint8_t " << variableName << "_numFrames = " << std::dec << numFrames << ";\n";
    typefs << "extern const uint16_t " << variableName << "[];\n";

    fsout << "// Generated from Images/" << relativePath << '\n';
    fsout << "constexpr uint8_t " << variableName << "_numFrames = " << std::dec << numFrames << ";\n";
    fsout << "extern const uint16_t " << variableName << "[] PROGMEM =\n";
    fsout << "{\n\t";

    for(unsigned x = 0; x < width; ++x) {
        fsout << "0x" << std::hex << transparencyMasks[x] << ",0x" << std::hex << colourMasks[x];
        if(x + 1 != width) {
            fsout << ",";
        }
    }

    fsout << "\n};\n";
    return true;
}

static bool EncodeTextures(
    std::ofstream& typefs,
    std::ofstream& fsout,
    const fs::path& inputPath,
    const char* relativePath,
    const char* variableName) {
    std::vector<uint8_t> pixels;
    unsigned width = 0;
    unsigned height = 0;
    if(!DecodeImage(inputPath, pixels, width, height)) {
        return false;
    }

    if(height != 16) {
        std::cerr << inputPath << " : texture must be 16 pixels high\n";
        return false;
    }
    if((width % 16) != 0) {
        std::cerr << inputPath << " : texture width must be multiple of 16 pixels\n";
        return false;
    }

    std::vector<uint16_t> colourMasks;

    for(unsigned x = 0; x < width; ++x) {
        uint16_t colour = 0;

        for(unsigned y = 0; y < height; ++y) {
            const uint16_t mask = static_cast<uint16_t>(1u << y);
            const ImageColour pixelColour = CalculateColour(pixels, x, y, width);

            if(pixelColour != ImageColour::Black) {
                colour |= mask;
            }
        }

        colourMasks.push_back(colour);
    }

    const unsigned int numTextures = width / 16;

    typefs << "// Generated from Images/" << relativePath << '\n';
    typefs << "constexpr uint8_t " << variableName << "_numTextures = " << std::dec << numTextures << ";\n";
    typefs << "extern const uint16_t " << variableName << "[];\n";

    fsout << "// Generated from Images/" << relativePath << '\n';
    fsout << "constexpr uint8_t " << variableName << "_numTextures = " << std::dec << numTextures << ";\n";
    fsout << "extern const uint16_t " << variableName << "[] PROGMEM =\n";
    fsout << "{\n\t";

    for(unsigned x = 0; x < width; ++x) {
        fsout << "0x" << std::hex << colourMasks[x];
        if(x + 1 != width) {
            fsout << ",";
        }
    }

    fsout << "\n};\n";
    return true;
}

static bool EncodeHUDElement(
    std::ofstream& typefs,
    std::ofstream& fsout,
    const fs::path& inputPath,
    const char* relativePath,
    const char* variableName) {
    std::vector<uint8_t> pixels;
    unsigned width = 0;
    unsigned height = 0;
    if(!DecodeImage(inputPath, pixels, width, height)) {
        return false;
    }

    if(height != 8) {
        std::cerr << inputPath << " : height must be 8 pixels\n";
        return false;
    }

    std::vector<uint8_t> colourMasks;

    for(unsigned y = 0; y < height; y += 8) {
        for(unsigned x = 0; x < width; ++x) {
            uint8_t colour = 0;

            for(unsigned z = 0; z < 8 && y + z < height; ++z) {
                const uint8_t mask = static_cast<uint8_t>(1u << z);
                const ImageColour pixelColour = CalculateColour(pixels, x, y + z, width);

                if(pixelColour == ImageColour::White) {
                    colour |= mask;
                }
            }

            colourMasks.push_back(colour);
        }
    }

    typefs << "// Generated from Images/" << relativePath << '\n';
    typefs << "extern const uint8_t " << variableName << "[];\n";

    fsout << "// Generated from Images/" << relativePath << '\n';
    fsout << "extern const uint8_t " << variableName << "[] PROGMEM =\n";
    fsout << "{\n";

    for(size_t x = 0; x < colourMasks.size(); ++x) {
        fsout << "0x" << std::hex << static_cast<int>(colourMasks[x]);
        if(x + 1 != colourMasks.size()) {
            fsout << ",";
        }
    }

    fsout << "\n};\n";
    return true;
}

static bool HasOpaquePixel(
    const std::vector<uint8_t>& pixels,
    unsigned width,
    unsigned x1,
    unsigned y1,
    unsigned x2,
    unsigned y2) {
    for(unsigned y = y1; y < y2; ++y) {
        for(unsigned x = x1; x < x2; ++x) {
            if(CalculateColour(pixels, x, y, width) != ImageColour::Transparent) {
                return true;
            }
        }
    }
    return false;
}

static bool EncodeSprite2D(
    std::ofstream& typefs,
    std::ofstream& fsout,
    const fs::path& inputPath,
    const char* relativePath,
    const char* variableName) {
    std::vector<uint8_t> pixels;
    unsigned width = 0;
    unsigned height = 0;
    if(!DecodeImage(inputPath, pixels, width, height)) {
        return false;
    }

    unsigned x1 = 0;
    unsigned y1 = 0;
    unsigned x2 = width;
    unsigned y2 = height;

    while(x1 < x2 && !HasOpaquePixel(pixels, width, x1, y1, x1 + 1, y2)) {
        ++x1;
    }
    while(x2 > x1 && !HasOpaquePixel(pixels, width, x2 - 1, y1, x2, y2)) {
        --x2;
    }
    while(y1 < y2 && !HasOpaquePixel(pixels, width, x1, y1, x2, y1 + 1)) {
        ++y1;
    }
    while(y2 > y1 && !HasOpaquePixel(pixels, width, x1, y2 - 1, x2, y2)) {
        --y2;
    }

    if(x1 == x2 || y1 == y2) {
        std::cerr << inputPath << " : sprite is fully transparent\n";
        return false;
    }

    std::vector<uint8_t> colourMasks;
    std::vector<uint8_t> transparencyMasks;

    for(unsigned y = y1; y < y2; y += 8) {
        for(unsigned x = x1; x < x2; ++x) {
            uint8_t colour = 0;
            uint8_t transparency = 0;

            for(unsigned z = 0; z < 8 && y + z < height; ++z) {
                const uint8_t mask = static_cast<uint8_t>(1u << z);
                const ImageColour pixelColour = CalculateColour(pixels, x, y + z, width);

                if(pixelColour == ImageColour::Black) {
                    transparency |= mask;
                } else if(pixelColour == ImageColour::White) {
                    transparency |= mask;
                    colour |= mask;
                }
            }

            colourMasks.push_back(colour);
            transparencyMasks.push_back(transparency);
        }
    }

    typefs << "// Generated from Images/" << relativePath << '\n';
    typefs << "extern const uint8_t " << variableName << "[];\n";

    fsout << "// Generated from Images/" << relativePath << '\n';
    fsout << "extern const uint8_t " << variableName << "[] PROGMEM =\n";
    fsout << "{\n";
    fsout << "\t" << std::dec << (x2 - x1) << ", " << std::dec << (y2 - y1) << ",\n\t";

    for(size_t x = 0; x < colourMasks.size(); ++x) {
        fsout << "0x" << std::hex << static_cast<int>(colourMasks[x]) << ",";
        fsout << "0x" << std::hex << static_cast<int>(transparencyMasks[x]);
        if(x + 1 != colourMasks.size()) {
            fsout << ",";
        }
    }

    fsout << "\n};\n";
    return true;
}

int main(int argc, char* argv[]) {
    Config config;
    if(!ParseArgs(argc, argv, config)) {
        return (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) ? 0 : 1;
    }

    if(!fs::exists(config.imagesDir)) {
        std::cerr << "Images directory not found: " << config.imagesDir << '\n';
        return 1;
    }

    std::error_code ec;
    fs::create_directories(config.outputDir, ec);
    if(ec) {
        std::cerr << "Failed to create output directory " << config.outputDir << ": " << ec.message() << '\n';
        return 1;
    }

    const fs::path spriteDataHeaderOutputPath = config.outputDir / "SpriteData.inc.h";
    const fs::path spriteTypesHeaderOutputPath = config.outputDir / "SpriteTypes.h";

    std::ofstream dataFile(spriteDataHeaderOutputPath);
    std::ofstream typeFile(spriteTypesHeaderOutputPath);

    if(!dataFile.is_open()) {
        std::cerr << "Failed to open output file: " << spriteDataHeaderOutputPath << '\n';
        return 1;
    }
    if(!typeFile.is_open()) {
        std::cerr << "Failed to open output file: " << spriteTypesHeaderOutputPath << '\n';
        return 1;
    }

    int failures = 0;
    for(const AssetTask& task : kAssetTasks) {
        const fs::path inputPath = config.imagesDir / task.inputPath;
        bool ok = false;

        switch(task.kind) {
        case AssetKind::Sprite3D:
            ok = EncodeSprite3D(typeFile, dataFile, inputPath, task.inputPath, task.variableName);
            break;
        case AssetKind::Sprite2D:
            ok = EncodeSprite2D(typeFile, dataFile, inputPath, task.inputPath, task.variableName);
            break;
        case AssetKind::Texture:
            ok = EncodeTextures(typeFile, dataFile, inputPath, task.inputPath, task.variableName);
            break;
        case AssetKind::HudElement:
            ok = EncodeHUDElement(typeFile, dataFile, inputPath, task.inputPath, task.variableName);
            break;
        }

        if(!ok) {
            ++failures;
        }
    }

    dataFile.close();
    typeFile.close();

    if(failures != 0) {
        std::cerr << "Asset generation finished with " << failures << " error(s)\n";
        return 1;
    }

    std::cout << "Generated:\n";
    std::cout << "  " << spriteDataHeaderOutputPath << '\n';
    std::cout << "  " << spriteTypesHeaderOutputPath << '\n';
    return 0;
}
