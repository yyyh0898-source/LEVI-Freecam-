#pragma once

// JSON-schema metadata for freecam::Settings. The launcher's config page reads
// the generated config.schema.json to show titles, descriptions and ranges.

#include <optional>
#include <string_view>

#include <pl/Config.hpp>

#include "Settings.hpp"

namespace pl::config {

template <> struct Schema<freecam::Settings> {
  static constexpr std::string_view title = "Freecam";
  static constexpr std::string_view description =
      "Freecam settings. Values changed in the in-game Mod Menu are saved here.";

  static constexpr FieldSchema field(std::string_view name) {
    if (name == "version") {
      return {"Version", "Config schema version managed by the mod.",
              std::nullopt, std::nullopt, true};
    }
    if (name == "showNameTags") {
      return {"Show Name Tags",
              "Draw entity name tags while the camera is detached.",
              std::nullopt, std::nullopt, false};
    }
    if (name == "moveSpeed") {
      return {"Movement Speed", "Camera speed in blocks per second.",
              freecam::kMinMoveSpeed, freecam::kMaxMoveSpeed, false};
    }
    return {};
  }
};

} // namespace pl::config
