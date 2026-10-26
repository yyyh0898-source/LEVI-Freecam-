#pragma once

// Parser for data/signatures.json: the per-Minecraft-build lookup table the game
// backend uses to find the three functions Freecam has to hook. Pure JSON, no
// Android dependency, so it is unit-tested on the host.
//
// File shape (unknown keys are ignored):
//
//   {
//     "schemaVersion": 1,
//     "module": "libminecraftpe.so",
//     "rules": [
//       {
//         "label": "1.26.52",
//         "cameraPose": "<signature or symbol>",
//         "moveInput":  "<signature or symbol>",
//         "nameTag":    "<signature or symbol>"
//       }
//     ]
//   }
//
// A signature string is anything pl::memory::resolveSignature() accepts: a byte
// pattern ("48 8B ?? ?? 89") or an exported symbol name. The backend tries the
// rules in order and uses the first one whose three entries all resolve.

#include <string>
#include <string_view>
#include <vector>

namespace freecam {

struct SignatureRule {
  std::string label;
  std::string cameraPose;
  std::string moveInput;
  std::string nameTag;
};

struct SignatureRuleSet {
  std::string module = "libminecraftpe.so";
  std::vector<SignatureRule> rules;
  // One line per skipped rule.
  std::vector<std::string> warnings;
  // Non-empty when the file as a whole could not be used.
  std::string error;

  [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] SignatureRuleSet parseSignatureRules(std::string_view jsonText);

} // namespace freecam
