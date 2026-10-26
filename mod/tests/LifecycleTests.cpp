// Host integration test for FreecamMod.cpp and GameBackend.cpp.
//
// The real preloader only exists inside the game process, so tests/FakePreloader.cpp
// stands in for it: it records the Mod Menu module/button the mod registers and
// lets this test fire the callbacks the launcher would (module toggle, config
// change, button event). FreecamMod.cpp is #included so its anonymous-namespace
// class can be reached; everything else is linked as in the real build.

#include "../src/FreecamMod.cpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "FakePreloader.hpp"
#include "TestHarness.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

json readJson(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return json::parse(in, nullptr, false);
}

void writeFile(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

bool wasCalled(const std::string &entry) {
  for (const auto &call : fake::recorder().calls) {
    if (call == entry) {
      return true;
    }
  }
  return false;
}

const char *kModuleName = "freecam.module";
const char *kButtonName = "freecam.toggle_button";

// ---- signature backend ------------------------------------------------------

void backendReportsWhyItCannotAttach(const fs::path &dir) {
  auto &rec = fake::recorder();
  freecam::FreecamController controller;
  freecam::GameBackend backend(controller);

  // The file shipped in the package: no rules yet.
  auto status = backend.attach(FREECAM_SHIPPED_SIGNATURES);
  CHECK(!status.hooksInstalled);
  CHECK(contains(status.summary, "No game signatures"));

  status = backend.attach(dir / "does-not-exist.json");
  CHECK(!status.hooksInstalled);
  CHECK(contains(status.summary, "missing"));

  writeFile(dir / "broken.json", "{ nope");
  status = backend.attach(dir / "broken.json");
  CHECK(contains(status.summary, "not a valid JSON"));

  // Rules whose signatures are not found in this build.
  writeFile(dir / "unmatched.json", R"({
    "schemaVersion": 1,
    "rules": [{"label": "other build", "cameraPose": "AA", "moveInput": "BB", "nameTag": "CC"}]
  })");
  rec.signatures.clear();
  status = backend.attach(dir / "unmatched.json");
  CHECK(contains(status.summary, "None of the signatures"));

  // Only two of three resolve: still not a match.
  rec.signatures = {{"AA", 0x1000}, {"BB", 0x2000}};
  status = backend.attach(dir / "unmatched.json");
  CHECK(contains(status.summary, "None of the signatures"));

  // The second rule matches, the first does not; the module name is passed on.
  writeFile(dir / "second.json", R"({
    "schemaVersion": 1,
    "module": "libminecraftpe.so",
    "rules": [
      {"label": "first", "cameraPose": "X1", "moveInput": "X2", "nameTag": "X3"},
      {"label": "second", "cameraPose": "AA", "moveInput": "BB", "nameTag": "CC"}
    ]
  })");
  rec.signatures = {{"AA", 0x1000}, {"BB", 0x2000}, {"CC", 0x3000}};
  status = backend.attach(dir / "second.json");
  CHECK(contains(status.summary, "Signatures matched (second)"));
  CHECK(contains(status.summary, "no hook detours"));
  CHECK(!status.hooksInstalled); // honest: nothing is hooked yet
  CHECK(rec.lastResolveModule == "libminecraftpe.so");

  backend.detach();
  backend.detach(); // idempotent
  rec.signatures.clear();
}

// ---- mod lifecycle ----------------------------------------------------------

void moduleAndButtonAreRegistered(FreecamMod &mod) {
  auto &rec = fake::recorder();
  CHECK(wasCalled(std::string("registerModule:") + kModuleName));
  CHECK(wasCalled(std::string("registerButton:") + kButtonName));
  CHECK(rec.module.has_value());
  CHECK(rec.button.has_value());
  if (!rec.module || !rec.button) {
    return;
  }

  const auto &module = *rec.module;
  CHECK(module.moduleId == kModuleName);
  CHECK(module.displayName == "Freecam");
  CHECK(module.modId == "Freecam");
  CHECK(module.defaultEnabled);
  CHECK(static_cast<bool>(module.onToggle));
  CHECK(static_cast<bool>(module.onConfigChanged));
  // No signatures are shipped, so the menu must say that Freecam is inert.
  CHECK(contains(module.description, "No game signatures"));

  CHECK(module.configs.size() == 2);
  if (module.configs.size() == 2) {
    const auto &tags = module.configs[0];
    CHECK(tags.key == "showNameTags");
    CHECK(tags.displayName == "Show Name Tags");
    CHECK(tags.type == pl::modmenu::ConfigType::Toggle);
    CHECK(tags.defaultValue == "true");

    const auto &speed = module.configs[1];
    CHECK(speed.key == "moveSpeed");
    CHECK(speed.displayName == "Movement Speed");
    CHECK(speed.type == pl::modmenu::ConfigType::SliderFloat);
    CHECK(speed.defaultValue == "10");
    CHECK(speed.minValue == "0.5");
    CHECK(speed.maxValue == "50");
  }

  const auto &button = *rec.button;
  CHECK(button.buttonId == kButtonName);
  CHECK(button.moduleId == kModuleName);
  CHECK(button.modId == "Freecam");
  CHECK(button.behavior == pl::modmenu::ButtonBehavior::Toggle);
  CHECK(button.iconFormat == pl::modmenu::ButtonIconFormat::Png);
  CHECK(button.iconData.size() > 8);
  if (button.iconData.size() > 8) {
    // PNG signature: the mod's own icon is what the button shows.
    CHECK(button.iconData[0] == 0x89 && button.iconData[1] == 'P' &&
          button.iconData[2] == 'N' && button.iconData[3] == 'G');
  }
  CHECK(static_cast<bool>(button.onEvent));
  CHECK(mod.controller().armed());
}

void settingsArePersistedFromTheMenu(FreecamMod &mod, const fs::path &configPath) {
  auto &module = *fake::recorder().module;

  module.onConfigChanged(kModuleName, "moveSpeed", "25");
  json saved = readJson(configPath);
  CHECK(saved.is_object() && saved["moveSpeed"] == 25.0);
  CHECK_NEAR(mod.controller().settings().moveSpeed, 25.0);

  module.onConfigChanged(kModuleName, "showNameTags", "false");
  saved = readJson(configPath);
  CHECK(saved["showNameTags"] == false);
  CHECK(!mod.controller().settings().showNameTags);

  // Slider values outside the range are clamped, garbage is ignored.
  module.onConfigChanged(kModuleName, "moveSpeed", "9999");
  CHECK_NEAR(mod.controller().settings().moveSpeed, 50.0);
  CHECK(readJson(configPath)["moveSpeed"] == 50.0);

  module.onConfigChanged(kModuleName, "moveSpeed", "abc");
  CHECK_NEAR(mod.controller().settings().moveSpeed, 50.0);

  module.onConfigChanged(kModuleName, "moveSpeed", "0.1");
  CHECK_NEAR(mod.controller().settings().moveSpeed, freecam::kMinMoveSpeed);

  // Other modules' callbacks and unknown keys must not touch our config.
  module.onConfigChanged("someone.else", "moveSpeed", "33");
  module.onConfigChanged(kModuleName, "unknownKey", "1");
  module.onConfigChanged(kModuleName, "", "1");
  CHECK_NEAR(mod.controller().settings().moveSpeed, freecam::kMinMoveSpeed);

  // Bool spellings the launcher may send.
  module.onConfigChanged(kModuleName, "showNameTags", "1");
  CHECK(mod.controller().settings().showNameTags);
  module.onConfigChanged(kModuleName, "showNameTags", "off");
  CHECK(!mod.controller().settings().showNameTags);
  module.onConfigChanged(kModuleName, "showNameTags", "maybe");
  CHECK(!mod.controller().settings().showNameTags); // unparseable: unchanged
}

void buttonAndModuleDriveTheController(FreecamMod &mod) {
  auto &rec = fake::recorder();
  auto &button = *rec.button;
  auto &module = *rec.module;
  auto &controller = mod.controller();

  CHECK(!controller.requested());
  button.onEvent(kButtonName, pl::modmenu::ButtonEvent::StateChanged, 1.0f);
  CHECK(controller.requested());
  CHECK(controller.suppressPlayerInput());

  // Name-tag toggle (currently off) applies while the camera is detached.
  CHECK(!controller.shouldDrawNameTags());
  module.onConfigChanged(kModuleName, "showNameTags", "true");
  CHECK(controller.shouldDrawNameTags());

  // Events that are not a state change, or belong to another button, are ignored.
  button.onEvent(kButtonName, pl::modmenu::ButtonEvent::Click, 0.0f);
  button.onEvent("other.button", pl::modmenu::ButtonEvent::StateChanged, 0.0f);
  CHECK(controller.requested());

  button.onEvent(kButtonName, pl::modmenu::ButtonEvent::StateChanged, 0.0f);
  CHECK(!controller.requested());

  // Turning the module off in the Mod Menu leaves freecam and blocks the button.
  button.onEvent(kButtonName, pl::modmenu::ButtonEvent::StateChanged, 1.0f);
  CHECK(controller.requested());
  module.onToggle(kModuleName, false);
  CHECK(!controller.armed());
  CHECK(!controller.requested());
  module.onToggle("someone.else", true); // not ours
  CHECK(!controller.armed());
  module.onToggle(kModuleName, true);
  CHECK(controller.armed());
  CHECK(!controller.requested());
}

void persistedSettingsAreLoadedAndNormalised(
    const pl::mod::ModRegistration *reg, pl::mod::ModContext &context,
    FreecamMod &mod, const fs::path &configPath) {
  writeFile(configPath,
            R"({"version": 1, "showNameTags": false, "moveSpeed": 999.0})");
  CHECK(reg->load(reg->instance, context));
  CHECK(!mod.controller().settings().showNameTags);
  CHECK_NEAR(mod.controller().settings().moveSpeed, freecam::kMaxMoveSpeed);
  // The normalised value is written back, not just used in memory.
  CHECK(readJson(configPath)["moveSpeed"] == 50.0);

  // A corrupt file falls back to defaults instead of failing the load.
  writeFile(configPath, "{ this is not json");
  CHECK(reg->load(reg->instance, context));
  CHECK(mod.controller().settings().showNameTags);
  CHECK_NEAR(mod.controller().settings().moveSpeed, freecam::kDefaultMoveSpeed);
  CHECK(readJson(configPath).is_object());
}

} // namespace

int main() {
  const fs::path root =
      fs::temp_directory_path() / ("freecam-lifecycle-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root / "data");
  fs::copy_file(FREECAM_SHIPPED_SIGNATURES, root / "data" / "signatures.json");

  pl::mod::ModInfo info;
  info.id = "Freecam";
  info.displayName = "Freecam";
  info.author = "test";
  info.version = "1.0.0";
  info.entryFileName = "libfreecam.so";
  info.modRootPath = root;
  pl::mod::NativeMod nativeMod(nullptr, info);
  fake::setCurrent(&nativeMod);
  pl::mod::ModContext context(nullptr, info);

  backendReportsWhyItCannotAttach(root / "backend");
  fake::recorder().calls.clear();

  const pl::mod::ModRegistration *reg = PLGetModRegistration();
  CHECK(reg != nullptr);
  if (reg == nullptr || reg->instance == nullptr || !reg->load || !reg->enable ||
      !reg->disable || !reg->unload) {
    std::fprintf(stderr, "registration is incomplete\n");
    return 1;
  }
  FreecamMod &mod = FreecamMod::instance();
  const fs::path configPath = root / "config" / "config.json";

  // load(): defaults are created on first run, together with the schema.
  CHECK(reg->load(reg->instance, context));
  CHECK(fs::exists(configPath));
  CHECK(fs::exists(root / "config" / "config.schema.json"));
  CHECK(mod.controller().settings().showNameTags);
  CHECK_NEAR(mod.controller().settings().moveSpeed, freecam::kDefaultMoveSpeed);
  CHECK(readJson(configPath)["version"] == 1);
  // Without signatures the backend failure is logged, not fatal.
  bool loggedBackendNote = false;
  for (const auto &line : fake::recorder().logLines) {
    loggedBackendNote = loggedBackendNote || contains(line, "Camera hooks not installed");
  }
  CHECK(loggedBackendNote);

  // enable(): module + button appear in the Mod Menu.
  CHECK(reg->enable(reg->instance, context));
  moduleAndButtonAreRegistered(mod);
  settingsArePersistedFromTheMenu(mod, configPath);
  buttonAndModuleDriveTheController(mod);

  // disable(): everything it registered is removed again.
  const pl::modmenu::ModuleInfo moduleCopy = *fake::recorder().module;
  CHECK(reg->disable(reg->instance, context));
  CHECK(wasCalled(std::string("unregisterButton:") + kButtonName));
  CHECK(wasCalled(std::string("unregisterModule:") + kModuleName));
  CHECK(!fake::recorder().module.has_value());
  CHECK(!fake::recorder().button.has_value());
  CHECK(!mod.controller().armed());

  // ...and it can be enabled again with the saved settings.
  CHECK(reg->enable(reg->instance, context));
  CHECK(fake::recorder().module.has_value());
  CHECK(mod.controller().armed());
  if (fake::recorder().module && fake::recorder().module->configs.size() == 2) {
    CHECK(fake::recorder().module->configs[1].defaultValue ==
          doubleToMenuValue(mod.controller().settings().moveSpeed));
  }

  // unload(): late callbacks from the launcher must be harmless.
  CHECK(reg->unload(reg->instance, context));
  const json beforeLate = readJson(configPath);
  moduleCopy.onConfigChanged(kModuleName, "moveSpeed", "12");
  CHECK(readJson(configPath) == beforeLate);

  persistedSettingsAreLoadedAndNormalised(reg, context, mod, configPath);

  fs::remove_all(root);
  std::printf("%d checks, %d failed\n", testing_lite::gChecks,
              testing_lite::gFailures);
  return testing_lite::gFailures == 0 ? 0 : 1;
}
