#include "FreecamCore.hpp"

#include <algorithm>
#include <cmath>

namespace freecam {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

double finiteOr(double value, double fallback) {
  return std::isfinite(value) ? value : fallback;
}

float clampUnit(float value) {
  return std::clamp(finiteOr(value, 0.0f), -1.0f, 1.0f);
}

// Wraps to [-180, 180).
float wrapYaw(float yawDeg) {
  double wrapped = std::fmod(static_cast<double>(yawDeg) + 180.0, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return static_cast<float>(wrapped - 180.0);
}

float clampPitch(float pitchDeg) { return std::clamp(pitchDeg, -90.0f, 90.0f); }

} // namespace

void FreecamCore::setMoveSpeed(double blocksPerSecond) noexcept {
  mSpeed = clampMoveSpeed(blocksPerSecond);
}

void FreecamCore::begin(const CameraPose &start) noexcept {
  mPose.position.x = finiteOr(start.position.x, 0.0);
  mPose.position.y = finiteOr(start.position.y, 0.0);
  mPose.position.z = finiteOr(start.position.z, 0.0);
  mPose.yawDeg = wrapYaw(finiteOr(start.yawDeg, 0.0f));
  mPose.pitchDeg = clampPitch(finiteOr(start.pitchDeg, 0.0f));
  mActive = true;
}

void FreecamCore::end() noexcept { mActive = false; }

void FreecamCore::look(float deltaYawDeg, float deltaPitchDeg) noexcept {
  if (!mActive) {
    return;
  }
  mPose.yawDeg = wrapYaw(mPose.yawDeg + finiteOr(deltaYawDeg, 0.0f));
  mPose.pitchDeg = clampPitch(mPose.pitchDeg + finiteOr(deltaPitchDeg, 0.0f));
}

void FreecamCore::step(double dtSeconds, const MoveInput &input) noexcept {
  if (!mActive || !std::isfinite(dtSeconds) || dtSeconds <= 0.0) {
    return;
  }
  const double dt = std::min(dtSeconds, kMaxStepSeconds);

  const double forward = clampUnit(input.forward);
  const double strafe = clampUnit(input.strafe);
  const double vertical = clampUnit(input.vertical);

  const double yaw = static_cast<double>(mPose.yawDeg) * kDegToRad;
  const double pitch = static_cast<double>(mPose.pitchDeg) * kDegToRad;
  const double sinYaw = std::sin(yaw);
  const double cosYaw = std::cos(yaw);
  const double sinPitch = std::sin(pitch);
  const double cosPitch = std::cos(pitch);

  // forward = (-sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch))
  // right   = (-cos(yaw), 0, -sin(yaw))
  double dx = forward * (-sinYaw * cosPitch) + strafe * (-cosYaw);
  double dy = forward * (-sinPitch) + vertical;
  double dz = forward * (cosYaw * cosPitch) + strafe * (-sinYaw);

  const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (length < 1e-9) {
    return;
  }
  // Diagonals must not be faster than a single axis.
  const double scale = (length > 1.0 ? 1.0 / length : 1.0) * mSpeed * dt;

  mPose.position.x += dx * scale;
  mPose.position.y += dy * scale;
  mPose.position.z += dz * scale;
}

} // namespace freecam
