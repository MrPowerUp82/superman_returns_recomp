#pragma once
// Title benchmark result: JSON/CSV files next to the executable. Pure, so
// tests/native covers it without the SDK.
#include "title_benchmark_policy.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace superman_returns::benchmark {

struct Result {
  Phase phase = Phase::WaitingScene;
  bool valid = false;
  std::string reason;
  Stats stats;
  std::string settings_text, json_path;
  bool saved = false;
};

inline std::string JsonEscape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (uint8_t(c) < 0x20) { char hex[8]; std::snprintf(hex, sizeof(hex), "\\u%04x", unsigned(uint8_t(c))); out += hex; }
    else out += c;
  }
  return out;
}

inline std::string FormatResultJson(const Result& r, std::string_view timestamp) {
  const auto number = [](double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return std::string(text);
  };
  const auto& s = r.stats;
  std::string json = "{\n";
  json += "  \"timestamp\": \"" + JsonEscape(timestamp) + "\",\n";
  json += std::string("  \"valid\": ") + (r.valid ? "true" : "false") + ",\n";
  json += "  \"reason\": \"" + JsonEscape(r.reason) + "\",\n";
  json += "  \"avg_fps\": " + number(s.avg_fps) + ",\n";
  json += "  \"min_fps\": " + number(s.min_fps) + ",\n";
  json += "  \"low1_fps\": " + number(s.low1_fps) + ",\n";
  json += "  \"low01_fps\": " + number(s.low01_fps) + ",\n";
  json += "  \"p50_ms\": " + number(s.p50_ms) + ",\n";
  json += "  \"p95_ms\": " + number(s.p95_ms) + ",\n";
  json += "  \"p99_ms\": " + number(s.p99_ms) + ",\n";
  json += "  \"max_ms\": " + number(s.max_ms) + ",\n";
  json += "  \"seconds\": " + number(s.seconds) + ",\n";
  json += "  \"frames\": " + std::to_string(s.frames) + ",\n";
  json += "  \"settings\": \"" + JsonEscape(r.settings_text) + "\"\n";
  json += "}\n";
  return json;
}

// Writes benchmark-<stamp>.json and .csv into `directory`. A failure leaves
// saved=false and json_path empty; the results screen still shows the numbers.
inline void SaveResult(Result& r, const std::filesystem::path& directory, std::string_view stamp,
                       std::span<const double> frame_ms) {
  r.saved = false;
  r.json_path.clear();
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) return;
  const auto base = directory / ("benchmark-" + std::string(stamp));
  const auto json_path = std::filesystem::path(base.string() + ".json");
  std::ofstream json(json_path, std::ios::binary);
  std::ofstream csv(base.string() + ".csv", std::ios::binary);
  if (!json || !csv) return;
  json << FormatResultJson(r, stamp);
  csv << "frame_ms\n";
  for (const double ms : frame_ms) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g\n", ms);
    csv << text;
  }
  if (!json.flush() || !csv.flush()) return;
  r.saved = true;
  r.json_path = json_path.string();
}

}  // namespace superman_returns::benchmark
