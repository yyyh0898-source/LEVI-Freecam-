// Freecam for LeviLauncher: lifecycle, Mod Menu module and floating button.
//
//   Mod Menu module "Freecam"
//     - module switch      arms Freecam and shows the floating camera button
//     - Show Name Tags     toggle, applied while the camera is detached
//     - Movement Speed     slider, blocks per second
//   Floating camera button (toggle) detaches / re-attaches the camera.
//
// Settings are persisted in config/config.json (typed config + schema, same
// mechanism as the launcher's full-cpp-mod example).

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include "ConfigSchema.hpp"
#include "Controller.hpp"
#include "GameBackend.hpp"
#include "IconData.hpp"
#include "Settings.hpp"

namespace {

using freecam::Settings;

constexpr const char *kModuleId = "freecam.module";
constexpr const char *kToggleButtonId = "freecam.toggle_button";
constexpr const char *kShowNameTagsKey = "showNameTags";
constexpr const char *kMoveSpeedKey = "moveSpeed";

bool parseBool(std::string_view value, bool fallback) {
  if (value == "true" || value == "1" || value == "on" || value == "enabled") {
    return true;
  }
  if (value == "false" || value == "0" || value == "off" ||
      value == "disabled") {
    return false;
  }
  return fallback;
}

double parseDouble(std::string_view value, double fallback) {
  const std::string text(value);
  char *end = nullptr;
  errno = 0;
  const double parsed = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
    return fallback;
  }
  return parsed;
}

std::string boolToMenuValue(bool value) { return value ? "true" : "false"; }

std::string doubleToMenuValue(double value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

class FreecamMod {
public:
  static FreecamMod &instance() {
    static FreecamMod mod;
    return mod;
  }

  FreecamMod() : mSelf(*ll::mod::NativeMod::current()), mBackend(mController) {}

  [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

  // Shared with the game backend; also lets the host tests observe state.
  [[nodiscard]] freecam::FreecamController &controller() {
    return mController;
  }

  bool load() {
    auto &self = getSelf();

    Settings settings;
    {
      std::lock_guard lock(mConfigMutex);
      mConfigFile.emplace();
      if (!mConfigFile->load()) {
        self.getLogger().error("Failed to load config");
        mConfigFile.reset();
        return false;
      }

      mConfigFile->value() = freecam::normalized(mConfigFile->value());
      if (!mConfigFile->save()) {
        self.getLogger().error("Failed to persist normalized config");
        mConfigFile.reset();
        return false;
      }
      settings = mConfigFile->value();
      self.getLogger().info("Loaded config from {}",
                            mConfigFile->configPath().string());
    }
    mController.applySettings(settings);

    // A backend failure is not fatal: the menu and settings stay usable and
    // the reason is shown in the module description.
    const auto status = mBackend.attach(self.getDataDir() / "signatures.json");
    if (status.hooksInstalled) {
      self.getLogger().info("Camera hooks installed: {}", status.summary);
    } else {
      self.getLogger().warn("Camera hooks not installed: {}", status.summary);
    }
    return true;
  }

  bool enable() {
    auto &self = getSelf();
    const Settings settings = mController.settings();

    std::string description =
        "Detach the camera from the player and fly around. Use the camera "
        "button to start and stop.";
    if (!mBackend.status().hooksInstalled) {
      description += " Note: " + mBackend.status().summary;
    }

    const bool moduleRegistered =
        pl::modmenu::ModuleBuilder(kModuleId, "Freecam")
            .modId(self.getId())
            .description(description)
            .defaultEnabled(true)
            .onToggle(onModuleToggle)
            .config(kShowNameTagsKey, "Show Name Tags",
                    pl::modmenu::ConfigType::Toggle,
                    boolToMenuValue(settings.showNameTags))
            .config(kMoveSpeedKey, "Movement Speed",
                    pl::modmenu::ConfigType::SliderFloat,
                    doubleToMenuValue(settings.moveSpeed),
                    doubleToMenuValue(freecam::kMinMoveSpeed),
                    doubleToMenuValue(freecam::kMaxMoveSpeed))
            .onConfigChanged(onConfigChanged)
            .registerModule();

    const bool buttonRegistered =
        moduleRegistered &&
        pl::modmenu::ButtonBuilder(kToggleButtonId, "Freecam")
            .modId(self.getId())
            .moduleId(kModuleId)
            .label("Cam")
            .behavior(pl::modmenu::ButtonBehavior::Toggle)
            .stylePreset(pl::modmenu::ButtonStylePreset::Accent)
            .pngIcon(freecam::assets::kIconPng)
            .onEvent(onButtonEvent)
            .registerButton();

    if (!(moduleRegistered && buttonRegistered)) {
      self.getLogger().error(
          "Failed to register Mod Menu entries: module={} button={}",
          moduleRegistered, buttonRegistered);
      unregisterMenu();
      return false;
    }

    mController.setArmed(true);
    self.getLogger().info("Registered Mod Menu module {}", kModuleId);
    return true;
  }

  bool disable() {
    unregisterMenu();
    getSelf().getLogger().info("Disabled");
    return true;
  }

  bool unload() {
    unregisterMenu();
    mBackend.detach();
    {
      std::lock_guard lock(mConfigMutex);
      mConfigFile.reset();
    }
    getSelf().getLogger().info("Unloaded");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
  freecam::FreecamController mController;
  freecam::GameBackend mBackend;
  std::mutex mConfigMutex;
  std::optional<pl::config::ConfigFile<Settings>> mConfigFile;

  static void onModuleToggle(std::string_view moduleId, bool enabled) {
    instance().handleModuleToggle(moduleId, enabled);
  }

  static void onConfigChanged(std::string_view moduleId, std::string_view key,
                              std::string_view value) {
    instance().handleConfigChanged(moduleId, key, value);
  }

  static void onButtonEvent(std::string_view buttonId,
                            pl::modmenu::ButtonEvent event, float value) {
    instance().handleButtonEvent(buttonId, event, value);
  }

  void handleModuleToggle(std::string_view moduleId, bool enabled) {
    if (moduleId != kModuleId) {
      return;
    }
    mController.setArmed(enabled);
    getSelf().getLogger().info("Module {}", enabled ? "enabled" : "disabled");
  }

  void handleButtonEvent(std::string_view buttonId,
                         pl::modmenu::ButtonEvent event, float value) {
    if (buttonId != kToggleButtonId ||
        event != pl::modmenu::ButtonEvent::StateChanged) {
      return;
    }
    const bool active = value > 0.5f;
    mController.setActive(active);
    getSelf().getLogger().info("Freecam {}", active ? "on" : "off");
  }

  void handleConfigChanged(std::string_view moduleId, std::string_view key,
                           std::string_view value) {
    if (moduleId != kModuleId || key.empty()) {
      return;
    }

    Settings updated;
    {
      std::lock_guard lock(mConfigMutex);
      if (!mConfigFile) {
        return;
      }

      auto &config = mConfigFile->value();
      if (key == kShowNameTagsKey) {
        config.showNameTags = parseBool(value, config.showNameTags);
      } else if (key == kMoveSpeedKey) {
        config.moveSpeed =
            freecam::clampMoveSpeed(parseDouble(value, config.moveSpeed));
      } else {
        return;
      }

      config = freecam::normalized(config);
      updated = config;
      if (!mConfigFile->save()) {
        getSelf().getLogger().warn("Failed to persist config after {}", key);
      }
    }
    mController.applySettings(updated);
  }

  void unregisterMenu() {
    mController.setArmed(false);
    pl::modmenu::unregisterButton(kToggleButtonId);
    pl::modmenu::unregisterModule(kModuleId);
  }
};

} // namespace

PL_REGISTER_MOD(FreecamMod, FreecamMod::instance())
