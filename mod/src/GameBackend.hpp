#pragma once

// Android-only layer that attaches Freecam to the running game. It is the one
// part of this project that depends on the Minecraft binary.
//
// STATUS: the signature lookup is implemented; the hook detours are NOT. They
// need function addresses (and prototypes / struct offsets) from the exact
// Minecraft build, which this repository does not ship. Until signatures and
// detours exist, attach() reports why Freecam cannot detach the camera and the
// rest of the mod (menu, settings, floating button) keeps working harmlessly.
//
// The three hook points and the contract each detour has to fulfil, expressed
// through FreecamController:
//
//   cameraPose  Runs where the game builds the render camera. After the vanilla
//               code ran, call controller.advance(dt, playerEye, input, look).
//               If it returns a pose, write it into the camera the renderer
//               will use.
//   moveInput   Runs where the game samples touch/keyboard movement for the
//               local player. Forward the sampled axes to advance(); while
//               controller.suppressPlayerInput() is true, zero what the game
//               applies to the player.
//   nameTag     Runs where entity name tags are drawn. Skip the original call
//               when controller.shouldDrawNameTags() is false.

#include <filesystem>
#include <string>

#include "Controller.hpp"

namespace freecam {

struct BackendStatus {
  bool hooksInstalled = false;
  // Human readable; shown in the Mod Menu description and in logcat.
  std::string summary = "Not attached.";
};

class GameBackend {
public:
  explicit GameBackend(FreecamController &controller)
      : mController(controller) {}
  GameBackend(const GameBackend &) = delete;
  GameBackend &operator=(const GameBackend &) = delete;
  ~GameBackend() { detach(); }

  // Reads `signaturesFile`, resolves the first matching rule inside the game
  // module and installs the hooks. Never throws; failure is reported through
  // the returned status.
  BackendStatus attach(const std::filesystem::path &signaturesFile);

  // Removes installed hooks. Safe to call repeatedly.
  void detach();

  [[nodiscard]] const BackendStatus &status() const { return mStatus; }

private:
  FreecamController &mController;
  BackendStatus mStatus;
};

} // namespace freecam
