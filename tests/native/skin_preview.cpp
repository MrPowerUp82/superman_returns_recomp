// sr_skin_preview <dump dir> <out dir>: paints the Kingdom Come skin over the
// matching <key>.png files of a texture dump (--sr_texture_dump_dir) and writes
// the results under the same names. The pixels come from the user's own game.
#include "graphics/texture_mods/kingdom_come.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace superman_returns::graphics::texture_mods;

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: sr_skin_preview <dump dir> <out dir>\n");
    return 2;
  }
  const std::filesystem::path dump = argv[1], out = argv[2];
  std::filesystem::create_directories(out);
  int painted = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dump)) {
    const auto stem = entry.path().stem().string();
    if (entry.path().extension() != ".png" || stem.size() != 16) continue;
    const uint64_t key = std::strtoull(stem.c_str(), nullptr, 16);
    if (!IsKingdomComeKey(key)) continue;
    Image image;
    if (!ReadPng(entry.path(), image) || !PaintKingdomCome(key, image)) continue;
    if (WritePng(out / entry.path().filename(), image)) {
      std::printf("painted %s\n", stem.c_str());
      ++painted;
    }
  }
  return painted ? 0 : 1;
}
