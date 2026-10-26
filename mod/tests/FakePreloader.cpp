#include "FakePreloader.hpp"

#include <cstdarg>
#include <cstdio>

#include <pl/memory/Signature.hpp>

namespace {
pl::mod::NativeMod *gCurrent = nullptr;
}

namespace fake {

Recorder &recorder() {
  static Recorder instance;
  return instance;
}

void setCurrent(pl::mod::NativeMod *mod) { gCurrent = mod; }

} // namespace fake

pl::mod::NativeMod *pl::mod::NativeMod::current() noexcept { return gCurrent; }

namespace pl::modmenu {

bool registerModule(const ModuleInfo &info) {
  fake::recorder().module = info;
  fake::recorder().calls.push_back("registerModule:" + info.moduleId);
  return true;
}

void unregisterModule(std::string_view moduleId) {
  fake::recorder().module.reset();
  fake::recorder().calls.push_back("unregisterModule:" + std::string(moduleId));
}

bool registerButton(const ButtonInfo &info) {
  fake::recorder().button = info;
  fake::recorder().calls.push_back("registerButton:" + info.buttonId);
  return true;
}

void unregisterButton(std::string_view buttonId) {
  fake::recorder().button.reset();
  fake::recorder().calls.push_back("unregisterButton:" + std::string(buttonId));
}

} // namespace pl::modmenu

namespace pl::memory {

uintptr_t resolveSignature(std::string_view signature,
                           std::string_view moduleName) {
  auto &rec = fake::recorder();
  rec.lastResolveModule = std::string(moduleName);
  const auto it = rec.signatures.find(std::string(signature));
  return it == rec.signatures.end() ? 0 : it->second;
}

} // namespace pl::memory

extern "C" int __android_log_print(int prio, const char *tag, const char *fmt,
                                   ...) {
  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  fake::recorder().logLines.push_back(std::string(tag) + ": " + buffer);
  (void)prio;
  return written;
}
