#include "Controller.hpp"

namespace freecam {

FreecamController::FreecamController(Settings settings)
    : mSettings(normalized(settings)) {
  mCore.setMoveSpeed(mSettings.moveSpeed);
}

void FreecamController::applySettings(const Settings &settings) {
  std::lock_guard lock(mMutex);
  mSettings = normalized(settings);
  mCore.setMoveSpeed(mSettings.moveSpeed);
}

Settings FreecamController::settings() const {
  std::lock_guard lock(mMutex);
  return mSettings;
}

void FreecamController::setArmed(bool armed) {
  std::lock_guard lock(mMutex);
  mArmed = armed;
  if (!armed) {
    // The session itself ends on the next advance(); the floating button is
    // removed together with the module, so it cannot get out of sync.
    mWantActive = false;
  }
}

bool FreecamController::armed() const {
  std::lock_guard lock(mMutex);
  return mArmed;
}

void FreecamController::setActive(bool active) {
  std::lock_guard lock(mMutex);
  if (active && !mArmed) {
    return;
  }
  mWantActive = active;
}

bool FreecamController::requested() const {
  std::lock_guard lock(mMutex);
  return mArmed && mWantActive;
}

std::optional<CameraPose>
FreecamController::advance(double dtSeconds, const CameraPose &playerEye,
                           const MoveInput &input, float lookDeltaYawDeg,
                           float lookDeltaPitchDeg) {
  std::lock_guard lock(mMutex);
  if (!(mArmed && mWantActive)) {
    if (mCore.active()) {
      mCore.end();
    }
    return std::nullopt;
  }

  if (!mCore.active()) {
    mCore.begin(playerEye);
  }
  mCore.look(lookDeltaYawDeg, lookDeltaPitchDeg);
  mCore.step(dtSeconds, input);
  return mCore.pose();
}

bool FreecamController::suppressPlayerInput() const {
  std::lock_guard lock(mMutex);
  return mArmed && mWantActive;
}

bool FreecamController::shouldDrawNameTags() const {
  std::lock_guard lock(mMutex);
  const bool flying = mArmed && mWantActive;
  return !flying || mSettings.showNameTags;
}

} // namespace freecam
