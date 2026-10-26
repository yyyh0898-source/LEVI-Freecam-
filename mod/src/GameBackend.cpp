#include "GameBackend.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include <pl/Mod.hpp>
#include <pl/memory/Signature.hpp>

#include "SignatureRules.hpp"

namespace freecam {
namespace {

struct ResolvedTargets {
  std::uintptr_t cameraPose = 0;
  std::uintptr_t moveInput = 0;
  std::uintptr_t nameTag = 0;

  [[nodiscard]] bool complete() const {
    return cameraPose != 0 && moveInput != 0 && nameTag != 0;
  }
};

bool readFile(const std::filesystem::path &path, std::string &out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(stream),
             std::istreambuf_iterator<char>());
  return true;
}

ResolvedTargets resolve(const SignatureRule &rule, const std::string &module) {
  ResolvedTargets targets;
  targets.cameraPose = pl::memory::resolveSignature(rule.cameraPose, module);
  targets.moveInput = pl::memory::resolveSignature(rule.moveInput, module);
  targets.nameTag = pl::memory::resolveSignature(rule.nameTag, module);
  return targets;
}

} // namespace

BackendStatus GameBackend::attach(const std::filesystem::path &signaturesFile) {
  detach();

  auto *self = ll::mod::NativeMod::current();
  const auto warn = [self](const std::string &message) {
    if (self != nullptr) {
      self->getLogger().warn("{}", message);
    }
  };

  std::string text;
  if (!readFile(signaturesFile, text)) {
    mStatus.summary = "data/signatures.json is missing, so the camera cannot "
                      "be detached.";
    return mStatus;
  }

  const SignatureRuleSet rules = parseSignatureRules(text);
  for (const auto &warning : rules.warnings) {
    warn("signatures.json: " + warning);
  }
  if (!rules.ok()) {
    mStatus.summary = rules.error + ".";
    return mStatus;
  }
  if (rules.rules.empty()) {
    mStatus.summary = "No game signatures are bundled for this Minecraft "
                      "build, so the camera cannot be detached yet.";
    return mStatus;
  }

  const SignatureRule *matched = nullptr;
  ResolvedTargets targets;
  for (const auto &rule : rules.rules) {
    const ResolvedTargets candidate = resolve(rule, rules.module);
    if (candidate.complete()) {
      matched = &rule;
      targets = candidate;
      break;
    }
  }
  if (matched == nullptr) {
    mStatus.summary = "None of the signatures in signatures.json match this "
                      "Minecraft build.";
    return mStatus;
  }

  // ---------------------------------------------------------------------------
  // EXTENSION POINT. `targets` now holds the addresses of the camera, input and
  // name-tag functions for this build. Install the detours here with
  // pl::memory::HookHandle (keep the handles as members and reset them in
  // detach()), following the contract documented in GameBackend.hpp. The
  // detours have to match the real function prototypes exactly.
  // ---------------------------------------------------------------------------
  (void)targets;
  (void)mController;

  mStatus.summary = "Signatures matched (" + matched->label +
                    "), but this build has no hook detours yet.";
  return mStatus;
}

void GameBackend::detach() {
  // Nothing is installed yet. When detours are added, reset their HookHandles
  // here so unloading the mod always restores the vanilla code.
  mStatus.hooksInstalled = false;
}

} // namespace freecam
