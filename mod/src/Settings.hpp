#pragma once

// Plain settings struct shared by the mod runtime, the config generator and the
// unit tests. It must stay a simple aggregate: pl::config reflects over its
// fields (Boost.PFR), and the first field has to be an integral `version`.
//
// No dependency on the preloader SDK lives here, so host tests stay light.
// Schema metadata (titles, ranges) is in ConfigSchema.hpp.

#include <cmath>

namespace freecam {

inline constexpr double kMinMoveSpeed = 0.5;
inline constexpr double kMaxMoveSpeed = 50.0;
inline constexpr double kDefaultMoveSpeed = 10.0; // blocks per second

struct Settings {
  int version = 1;
  // Draw entity name tags while the camera is detached.
  bool showNameTags = true;
  // Camera speed in blocks per second.
  double moveSpeed = kDefaultMoveSpeed;
};

inline double clampMoveSpeed(double value) {
  if (!std::isfinite(value)) {
    return kDefaultMoveSpeed;
  }
  if (value < kMinMoveSpeed) {
    return kMinMoveSpeed;
  }
  if (value > kMaxMoveSpeed) {
    return kMaxMoveSpeed;
  }
  return value;
}

inline Settings normalized(Settings settings) {
  settings.version = Settings{}.version;
  settings.moveSpeed = clampMoveSpeed(settings.moveSpeed);
  return settings;
}

} // namespace freecam
