#pragma once

// Pure camera maths for Freecam. No game, Android or preloader dependency, so it
// is unit-tested on the host (see tests/CoreTests.cpp).
//
// Conventions follow Minecraft: +X east, +Y up, +Z south. Yaw 0 faces +Z,
// yaw 90 faces -X (west). Pitch is positive when looking down (-90 = straight
// up, +90 = straight down).

#include "Settings.hpp"

namespace freecam {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct CameraPose {
  Vec3 position;
  float yawDeg = 0.0f;
  float pitchDeg = 0.0f;
};

// Normalised movement input, each axis in [-1, 1].
struct MoveInput {
  float forward = 0.0f;  // +1 = along the view direction (pitch included)
  float strafe = 0.0f;   // +1 = to the right
  float vertical = 0.0f; // +1 = world up
};

class FreecamCore {
public:
  // A long frame (app resumed, loading hitch) must not teleport the camera.
  static constexpr double kMaxStepSeconds = 0.25;

  [[nodiscard]] bool active() const noexcept { return mActive; }
  [[nodiscard]] const CameraPose &pose() const noexcept { return mPose; }
  [[nodiscard]] double moveSpeed() const noexcept { return mSpeed; }

  // Blocks per second, clamped to [kMinMoveSpeed, kMaxMoveSpeed].
  void setMoveSpeed(double blocksPerSecond) noexcept;

  // Starts a session from `start` (normally the player's eye pose).
  void begin(const CameraPose &start) noexcept;
  void end() noexcept;

  // Rotates the camera. No-op while inactive.
  void look(float deltaYawDeg, float deltaPitchDeg) noexcept;

  // Moves the camera by `input` for `dtSeconds`. No-op while inactive.
  void step(double dtSeconds, const MoveInput &input) noexcept;

private:
  bool mActive = false;
  CameraPose mPose;
  double mSpeed = kDefaultMoveSpeed;
};

} // namespace freecam
