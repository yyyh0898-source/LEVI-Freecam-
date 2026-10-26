#pragma once

// Thread-safe glue between the Mod Menu (UI thread) and the game hooks (game
// thread). It owns the Freecam state and has no Android/preloader dependency,
// so it is unit-tested on the host.
//
// Two switches, matching the Mod Menu UX:
//   armed  - the "Freecam" module is enabled in the Mod Menu. This is also what
//            makes the floating camera button visible.
//   active - the floating button is toggled on: the camera is (or is about to
//            be) detached from the player.

#include <mutex>
#include <optional>

#include "FreecamCore.hpp"
#include "Settings.hpp"

namespace freecam {

class FreecamController {
public:
  explicit FreecamController(Settings settings = {});

  // ---- UI / lifecycle side -------------------------------------------------

  void applySettings(const Settings &settings);
  [[nodiscard]] Settings settings() const;

  // Module enabled/disabled in the Mod Menu. Disarming also leaves freecam.
  void setArmed(bool armed);
  [[nodiscard]] bool armed() const;

  // Floating button toggled. Ignored while not armed.
  void setActive(bool active);

  // True while freecam is requested (armed && active).
  [[nodiscard]] bool requested() const;

  // ---- Game-hook side ------------------------------------------------------

  // Call once per frame from the camera/input hook. `playerEye` is the local
  // player's real eye pose; it only seeds the camera when a session starts.
  // `lookDelta*` is the look input of this frame in degrees, in the game's own
  // yaw/pitch sign convention.
  //
  // Returns the pose the renderer should use, or nullopt when the vanilla
  // camera must be left alone.
  [[nodiscard]] std::optional<CameraPose>
  advance(double dtSeconds, const CameraPose &playerEye, const MoveInput &input,
          float lookDeltaYawDeg, float lookDeltaPitchDeg);

  // While true, the hook must zero the movement/look input handed to the real
  // player so the character stands still while the camera flies.
  [[nodiscard]] bool suppressPlayerInput() const;

  // False only while freecam is requested and "Show Name Tags" is off.
  [[nodiscard]] bool shouldDrawNameTags() const;

private:
  mutable std::mutex mMutex;
  Settings mSettings;
  bool mArmed = true;
  bool mWantActive = false;
  FreecamCore mCore;
};

} // namespace freecam
