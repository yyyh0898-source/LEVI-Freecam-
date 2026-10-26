// Host unit tests for the parts of Freecam that do not touch the game:
// camera maths, controller state machine, settings clamping and the
// signatures.json parser. Plain asserts, no test framework needed.

#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "Controller.hpp"
#include "FreecamCore.hpp"
#include "Settings.hpp"
#include "SignatureRules.hpp"
#include "TestHarness.hpp"

namespace {

using testing_lite::gChecks;
using testing_lite::gFailures;

using freecam::CameraPose;
using freecam::FreecamController;
using freecam::FreecamCore;
using freecam::MoveInput;
using freecam::Settings;

CameraPose poseAt(double x, double y, double z, float yaw = 0, float pitch = 0) {
  CameraPose pose;
  pose.position = {x, y, z};
  pose.yawDeg = yaw;
  pose.pitchDeg = pitch;
  return pose;
}

// step() caps one frame at FreecamCore::kMaxStepSeconds, so longer movements are
// integrated in 50 ms frames, like a real 20 fps game loop would.
void run(FreecamCore &core, double seconds, const MoveInput &input) {
  constexpr double kFrame = 0.05;
  const long frames = std::lround(seconds / kFrame);
  for (long i = 0; i < frames; ++i) {
    core.step(kFrame, input);
  }
}

FreecamCore startedCore(float yaw, float pitch, double speed = 10.0) {
  FreecamCore core;
  core.setMoveSpeed(speed);
  core.begin(poseAt(0, 0, 0, yaw, pitch));
  return core;
}

// ---- FreecamCore ------------------------------------------------------------

void forwardFollowsYaw() {
  auto south = startedCore(0, 0);
  run(south, 1.0, {1, 0, 0});
  CHECK_NEAR(south.pose().position.x, 0.0);
  CHECK_NEAR(south.pose().position.y, 0.0);
  CHECK_NEAR(south.pose().position.z, 10.0);

  auto west = startedCore(90, 0);
  run(west, 1.0, {1, 0, 0});
  CHECK_NEAR(west.pose().position.x, -10.0);
  CHECK_NEAR(west.pose().position.z, 0.0);

  auto east = startedCore(-90, 0);
  run(east, 1.0, {1, 0, 0});
  CHECK_NEAR(east.pose().position.x, 10.0);

  auto north = startedCore(180, 0);
  run(north, 1.0, {1, 0, 0});
  CHECK_NEAR(north.pose().position.z, -10.0);
}

void forwardFollowsPitch() {
  auto down = startedCore(0, 90);
  run(down, 1.0, {1, 0, 0});
  CHECK_NEAR(down.pose().position.y, -10.0);
  CHECK_NEAR(down.pose().position.z, 0.0);

  auto up = startedCore(0, -90);
  run(up, 1.0, {1, 0, 0});
  CHECK_NEAR(up.pose().position.y, 10.0);

  auto slantedDown = startedCore(0, 45);
  run(slantedDown, 1.0, {1, 0, 0});
  CHECK(slantedDown.pose().position.y < 0.0);
  CHECK(slantedDown.pose().position.z > 0.0);
}

void strafeAndVertical() {
  // Facing south, the player's right hand points west (-X).
  auto core = startedCore(0, 0);
  run(core, 1.0, {0, 1, 0});
  CHECK_NEAR(core.pose().position.x, -10.0);

  // Facing west, right is north (-Z).
  auto west = startedCore(90, 0);
  run(west, 1.0, {0, 1, 0});
  CHECK_NEAR(west.pose().position.z, -10.0);

  // Vertical is world up whatever the view direction.
  auto looking = startedCore(33, 70);
  run(looking, 1.0, {0, 0, 1});
  CHECK_NEAR(looking.pose().position.x, 0.0);
  CHECK_NEAR(looking.pose().position.y, 10.0);
  CHECK_NEAR(looking.pose().position.z, 0.0);

  auto descending = startedCore(0, 0);
  run(descending, 1.0, {0, 0, -1});
  CHECK_NEAR(descending.pose().position.y, -10.0);
}

void speedScalesLinearly() {
  auto slow = startedCore(0, 0, 2.0);
  slow.step(3.0 * 0.05, {1, 0, 0});
  CHECK_NEAR(slow.pose().position.z, 2.0 * 3.0 * 0.05);

  auto half = startedCore(0, 0, 10.0);
  run(half, 1.0, {0.5f, 0, 0});
  CHECK_NEAR(half.pose().position.z, 5.0);

  auto stepped = startedCore(0, 0, 10.0);
  for (int i = 0; i < 100; ++i) {
    stepped.step(0.01, {1, 0, 0});
  }
  CHECK_NEAR(stepped.pose().position.z, 10.0);
}

void diagonalIsNotFaster() {
  auto core = startedCore(0, 0);
  run(core, 1.0, {1, 1, 1});
  const auto &p = core.pose().position;
  CHECK_NEAR(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), 10.0);
}

void inputOutOfRangeIsClamped() {
  auto core = startedCore(0, 0);
  run(core, 1.0, {50.0f, 0, 0});
  CHECK_NEAR(core.pose().position.z, 10.0);

  auto nan = startedCore(0, 0);
  run(nan, 1.0, {std::numeric_limits<float>::quiet_NaN(), 0, 0});
  CHECK_NEAR(nan.pose().position.z, 0.0);
}

void longFramesAreCapped() {
  auto core = startedCore(0, 0);
  core.step(10.0, {1, 0, 0});
  CHECK_NEAR(core.pose().position.z, 10.0 * FreecamCore::kMaxStepSeconds);

  auto bad = startedCore(0, 0);
  bad.step(-1.0, {1, 0, 0});
  bad.step(0.0, {1, 0, 0});
  bad.step(std::numeric_limits<double>::infinity(), {1, 0, 0});
  CHECK_NEAR(bad.pose().position.z, 0.0);
}

void inactiveCoreDoesNothing() {
  FreecamCore core;
  CHECK(!core.active());
  run(core, 1.0, {1, 0, 0});
  core.look(30, 30);
  CHECK_NEAR(core.pose().position.z, 0.0);
  CHECK_NEAR(core.pose().yawDeg, 0.0);

  core.begin(poseAt(1, 2, 3));
  CHECK(core.active());
  core.end();
  run(core, 1.0, {1, 0, 0});
  CHECK_NEAR(core.pose().position.z, 3.0);
}

void lookWrapsYawAndClampsPitch() {
  auto core = startedCore(170, 80);
  core.look(20, 30);
  CHECK_NEAR(core.pose().yawDeg, -170.0);
  CHECK_NEAR(core.pose().pitchDeg, 90.0);

  core.look(-40, -400);
  CHECK_NEAR(core.pose().yawDeg, 150.0);
  CHECK_NEAR(core.pose().pitchDeg, -90.0);

  core.look(std::numeric_limits<float>::quiet_NaN(), 0);
  CHECK_NEAR(core.pose().yawDeg, 150.0);
}

void beginNormalisesStartPose() {
  FreecamCore core;
  core.begin(poseAt(1, 2, 3, 540.0f, 200.0f));
  CHECK_NEAR(core.pose().yawDeg, -180.0);
  CHECK_NEAR(core.pose().pitchDeg, 90.0);
  CHECK_NEAR(core.pose().position.x, 1.0);

  const double nan = std::numeric_limits<double>::quiet_NaN();
  core.begin(poseAt(nan, 2, 3));
  CHECK_NEAR(core.pose().position.x, 0.0);
  CHECK_NEAR(core.pose().position.y, 2.0);
}

void speedIsClamped() {
  FreecamCore core;
  core.setMoveSpeed(1000.0);
  CHECK_NEAR(core.moveSpeed(), freecam::kMaxMoveSpeed);
  core.setMoveSpeed(0.0);
  CHECK_NEAR(core.moveSpeed(), freecam::kMinMoveSpeed);
  core.setMoveSpeed(std::numeric_limits<double>::quiet_NaN());
  CHECK_NEAR(core.moveSpeed(), freecam::kDefaultMoveSpeed);
  core.setMoveSpeed(7.5);
  CHECK_NEAR(core.moveSpeed(), 7.5);
}

// ---- Settings ---------------------------------------------------------------

void settingsNormalise() {
  Settings raw;
  raw.version = 99;
  raw.moveSpeed = -4.0;
  const auto fixed = freecam::normalized(raw);
  CHECK(fixed.version == Settings{}.version);
  CHECK_NEAR(fixed.moveSpeed, freecam::kMinMoveSpeed);

  raw.moveSpeed = std::numeric_limits<double>::infinity();
  CHECK_NEAR(freecam::normalized(raw).moveSpeed, freecam::kDefaultMoveSpeed);

  const Settings defaults;
  CHECK(defaults.showNameTags);
  CHECK_NEAR(defaults.moveSpeed, freecam::kDefaultMoveSpeed);
}

// ---- FreecamController ------------------------------------------------------

void controllerIdleByDefault() {
  FreecamController controller;
  CHECK(controller.armed());
  CHECK(!controller.requested());
  CHECK(!controller.suppressPlayerInput());
  CHECK(controller.shouldDrawNameTags());
  CHECK(!controller.advance(0.016, poseAt(1, 2, 3), {1, 0, 0}, 0, 0).has_value());
}

void controllerStartsFromPlayerEye() {
  FreecamController controller;
  controller.setActive(true);
  CHECK(controller.requested());
  CHECK(controller.suppressPlayerInput());

  // First frame seeds the camera at the player's eye, then moves it.
  const auto first =
      controller.advance(1.0 / 20.0, poseAt(100, 64, -20, 90, 10), {1, 0, 0}, 0, 0);
  CHECK(first.has_value());
  CHECK(first->position.x < 100.0); // yaw 90 = west
  CHECK(first->position.y > 63.0 && first->position.y < 64.0); // slight descent
  CHECK_NEAR(first->yawDeg, 90.0);

  // Later frames ignore the player pose; the camera keeps its own.
  const auto second =
      controller.advance(0.0, poseAt(-500, 0, 500), {0, 0, 0}, 0, 0);
  CHECK(second.has_value());
  CHECK_NEAR(second->position.x, first->position.x);
}

void controllerRestartsFromNewPlayerPose() {
  FreecamController controller;
  controller.setActive(true);
  (void)controller.advance(0.1, poseAt(0, 0, 0), {1, 0, 0}, 0, 0);

  controller.setActive(false);
  CHECK(!controller.advance(0.1, poseAt(0, 0, 0), {1, 0, 0}, 0, 0).has_value());
  CHECK(!controller.suppressPlayerInput());

  controller.setActive(true);
  const auto restarted = controller.advance(0.0, poseAt(7, 8, 9), {0, 0, 0}, 0, 0);
  CHECK(restarted.has_value());
  CHECK_NEAR(restarted->position.x, 7.0);
  CHECK_NEAR(restarted->position.y, 8.0);
  CHECK_NEAR(restarted->position.z, 9.0);
}

void controllerAppliesSettingsLive() {
  FreecamController controller;
  controller.setActive(true);
  (void)controller.advance(0.0, poseAt(0, 0, 0), {0, 0, 0}, 0, 0);

  Settings fast;
  fast.moveSpeed = 40.0;
  controller.applySettings(fast);
  const auto moved = controller.advance(0.1, poseAt(0, 0, 0), {1, 0, 0}, 0, 0);
  CHECK(moved.has_value());
  CHECK_NEAR(moved->position.z, 4.0);
  CHECK_NEAR(controller.settings().moveSpeed, 40.0);

  Settings crazy;
  crazy.moveSpeed = 9999.0;
  controller.applySettings(crazy);
  CHECK_NEAR(controller.settings().moveSpeed, freecam::kMaxMoveSpeed);
}

void controllerNameTagToggle() {
  FreecamController controller;
  Settings hidden;
  hidden.showNameTags = false;
  controller.applySettings(hidden);

  // Not flying: vanilla behaviour, name tags stay.
  CHECK(controller.shouldDrawNameTags());

  controller.setActive(true);
  CHECK(!controller.shouldDrawNameTags());

  Settings shown;
  shown.showNameTags = true;
  controller.applySettings(shown);
  CHECK(controller.shouldDrawNameTags());
}

void controllerDisarmEndsSession() {
  FreecamController controller;
  controller.setActive(true);
  (void)controller.advance(0.1, poseAt(0, 0, 0), {1, 0, 0}, 0, 0);

  controller.setArmed(false);
  CHECK(!controller.armed());
  CHECK(!controller.requested());
  CHECK(!controller.suppressPlayerInput());
  CHECK(!controller.advance(0.1, poseAt(0, 0, 0), {1, 0, 0}, 0, 0).has_value());

  // Cannot be turned on while disarmed...
  controller.setActive(true);
  CHECK(!controller.requested());

  // ...and re-arming does not resurrect the old request.
  controller.setArmed(true);
  CHECK(!controller.requested());
  controller.setActive(true);
  CHECK(controller.requested());
}

// ---- signatures.json parser -------------------------------------------------

void rulesParseShippedShape() {
  const auto set = freecam::parseSignatureRules(R"({
    "schemaVersion": 1,
    "module": "libminecraftpe.so",
    "note": "ignored",
    "rules": []
  })");
  CHECK(set.ok());
  CHECK(set.rules.empty());
  CHECK(set.module == "libminecraftpe.so");
}

void rulesParseValidAndSkipInvalid() {
  const auto set = freecam::parseSignatureRules(R"({
    "schemaVersion": 1,
    "module": "  libminecraftpe.so ",
    "rules": [
      {"label": "1.26.52", "cameraPose": "48 8B ?? ?? 89", "moveInput": "A?B?C?D?", "nameTag": "SomeSymbol"},
      {"cameraPose": "AA", "moveInput": "BB"},
      "nonsense",
      {"cameraPose": "11", "moveInput": " ", "nameTag": "33"},
      {"cameraPose": "11", "moveInput": "22", "nameTag": "33"}
    ]
  })");
  CHECK(set.ok());
  CHECK(set.module == "libminecraftpe.so");
  CHECK(set.rules.size() == 2);
  CHECK(set.warnings.size() == 3);
  CHECK(set.rules[0].label == "1.26.52");
  CHECK(set.rules[0].cameraPose == "48 8B ?? ?? 89");
  CHECK(set.rules[0].nameTag == "SomeSymbol");
  CHECK(set.rules[1].label == "rule #4");
}

void rulesRejectBrokenFiles() {
  CHECK(!freecam::parseSignatureRules("").ok());
  CHECK(!freecam::parseSignatureRules("not json").ok());
  CHECK(!freecam::parseSignatureRules("[]").ok());
  CHECK(!freecam::parseSignatureRules(R"({"rules": []})").ok());
  CHECK(!freecam::parseSignatureRules(R"({"schemaVersion": 2, "rules": []})").ok());
  CHECK(!freecam::parseSignatureRules(R"({"schemaVersion": 1})").ok());
  CHECK(!freecam::parseSignatureRules(R"({"schemaVersion": 1, "rules": {}})").ok());
}

struct TestCase {
  const char *name;
  std::function<void()> run;
};

} // namespace

int main() {
  const std::vector<TestCase> tests = {
      {"forwardFollowsYaw", forwardFollowsYaw},
      {"forwardFollowsPitch", forwardFollowsPitch},
      {"strafeAndVertical", strafeAndVertical},
      {"speedScalesLinearly", speedScalesLinearly},
      {"diagonalIsNotFaster", diagonalIsNotFaster},
      {"inputOutOfRangeIsClamped", inputOutOfRangeIsClamped},
      {"longFramesAreCapped", longFramesAreCapped},
      {"inactiveCoreDoesNothing", inactiveCoreDoesNothing},
      {"lookWrapsYawAndClampsPitch", lookWrapsYawAndClampsPitch},
      {"beginNormalisesStartPose", beginNormalisesStartPose},
      {"speedIsClamped", speedIsClamped},
      {"settingsNormalise", settingsNormalise},
      {"controllerIdleByDefault", controllerIdleByDefault},
      {"controllerStartsFromPlayerEye", controllerStartsFromPlayerEye},
      {"controllerRestartsFromNewPlayerPose", controllerRestartsFromNewPlayerPose},
      {"controllerAppliesSettingsLive", controllerAppliesSettingsLive},
      {"controllerNameTagToggle", controllerNameTagToggle},
      {"controllerDisarmEndsSession", controllerDisarmEndsSession},
      {"rulesParseShippedShape", rulesParseShippedShape},
      {"rulesParseValidAndSkipInvalid", rulesParseValidAndSkipInvalid},
      {"rulesRejectBrokenFiles", rulesRejectBrokenFiles},
  };

  for (const auto &test : tests) {
    const int before = gFailures;
    test.run();
    std::printf("%s %s\n", gFailures == before ? "ok  " : "FAIL", test.name);
  }
  std::printf("%d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
