// Copyright (c) 2026 ApertureFox Technology. MIT License.
//   c++ -std=c++17 -O2 -ffp-contract=off -o /tmp/hostgen tools/hostgen.cpp
//
// -ffp-contract=off is not optional: the firmware's arm-none-eabi-gcc emits
// plain vmul/vadd, so the device -- and tools/worldgen.py, which mirrors it --
// round every product separately. Let clang fuse a*b+c here and a column
// sitting exactly on a rounding boundary comes out one block higher.
//   /tmp/hostgen out.fcw 16 12345 0 1  # chunks per side: 16/32/64/128, the
//                                      # header flags byte, then the terrain
//                                      # preset 0..3 (plugin_api.h)
#include "../src/world/gen_core.h"

#include <cstdio>
#include <cstdlib>

static bool fileWriteAt(void* ctx, uint32_t offset, const void* data, size_t n) {
    FILE* f = reinterpret_cast<FILE*>(ctx);
    return fseek(f, (long)offset, SEEK_SET) == 0 && fwrite(data, 1, n, f) == n;
}

static void onProgress(void*, uint8_t percent) {
    fprintf(stderr, "\r%3u%%", percent);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(
            stderr, "usage: %s out.fcw [chunks=16] [seed=random] [flags=0] [type=0]\n", argv[0]);
        return 2;
    }
    int chunks = argc > 2 ? atoi(argv[2]) : 16;
    uint32_t seed = argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 0) : (uint32_t)rand();
    uint8_t flags = argc > 4 ? (uint8_t)strtoul(argv[4], nullptr, 0) : 0;
    uint8_t type = argc > 5 ? (uint8_t)strtoul(argv[5], nullptr, 0) : 0;

    FILE* f = fopen(argv[1], "wb");
    if(!f) {
        perror("fopen");
        return 1;
    }
    fcgen::Writer out = {fileWriteAt, f};
    bool ok = fcgen::generate(chunks, seed, flags, type, out, onProgress, nullptr);
    fclose(f);
    fprintf(
        stderr,
        "\n%s: chunks=%d seed=%u type=%u -> %s\n",
        ok ? "OK" : "FAIL",
        chunks,
        seed,
        type,
        argv[1]);
    return ok ? 0 : 1;
}
