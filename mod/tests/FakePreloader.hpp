#pragma once

// Host-only stand-in for the preloader runtime (libpreloader.so). It records what
// the mod registers and lets tests drive the callbacks the launcher would fire.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace fake {

struct Recorder {
  std::optional<pl::modmenu::ModuleInfo> module;
  std::optional<pl::modmenu::ButtonInfo> button;
  std::vector<std::string> calls;

  // signature/symbol -> address returned by the fake resolveSignature().
  std::unordered_map<std::string, std::uintptr_t> signatures;
  std::string lastResolveModule;
  std::vector<std::string> logLines;
};

Recorder &recorder();

// What NativeMod::current() returns.
void setCurrent(pl::mod::NativeMod *mod);

} // namespace fake
