#include "SignatureRules.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

namespace freecam {
namespace {

constexpr int kSupportedSchemaVersion = 1;

std::string trimmed(std::string value) {
  const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
  while (!value.empty() && !notSpace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && !notSpace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

// Reads a required non-empty string member; returns false when absent/blank.
bool readRequired(const nlohmann::json &object, const char *key,
                  std::string &out) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return false;
  }
  out = trimmed(it->get<std::string>());
  return !out.empty();
}

} // namespace

SignatureRuleSet parseSignatureRules(std::string_view jsonText) {
  SignatureRuleSet result;

  const nlohmann::json root =
      nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    result.error = "signatures.json is not a valid JSON object";
    return result;
  }

  const auto schema = root.find("schemaVersion");
  if (schema == root.end() || !schema->is_number_integer() ||
      schema->get<int>() != kSupportedSchemaVersion) {
    result.error = "signatures.json has an unsupported schemaVersion (expected 1)";
    return result;
  }

  if (const auto module = root.find("module");
      module != root.end() && module->is_string()) {
    auto name = trimmed(module->get<std::string>());
    if (!name.empty()) {
      result.module = std::move(name);
    }
  }

  const auto rules = root.find("rules");
  if (rules == root.end() || !rules->is_array()) {
    result.error = "signatures.json is missing the \"rules\" array";
    return result;
  }

  std::size_t index = 0;
  for (const auto &rule : *rules) {
    const std::string where = "rule #" + std::to_string(index++);
    if (!rule.is_object()) {
      result.warnings.push_back(where + " is not an object, skipped");
      continue;
    }

    SignatureRule parsed;
    if (!readRequired(rule, "cameraPose", parsed.cameraPose) ||
        !readRequired(rule, "moveInput", parsed.moveInput) ||
        !readRequired(rule, "nameTag", parsed.nameTag)) {
      result.warnings.push_back(
          where + " needs non-empty cameraPose, moveInput and nameTag, skipped");
      continue;
    }

    std::string label;
    if (readRequired(rule, "label", label)) {
      parsed.label = std::move(label);
    } else {
      parsed.label = where;
    }
    result.rules.push_back(std::move(parsed));
  }

  return result;
}

} // namespace freecam
