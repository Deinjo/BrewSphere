#include "services/brew_settings.h"

#include <Preferences.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace services::brew {
namespace {

constexpr char kPrefsNamespace[] = "brewSim";
constexpr char kKeySource[] = "source";
constexpr char kKeyBatchName[] = "batchName";
constexpr char kKeyRecipeName[] = "recipeName";
constexpr char kKeyStatus[] = "status";
constexpr char kKeyBatchNumber[] = "batchNo";
constexpr char kKeyBrewDay[] = "brewDay";
constexpr char kKeyPlato[] = "plato";
constexpr char kKeyTargetPlato[] = "targetP";
constexpr char kKeyTargetTemperature[] = "targetTemp";
constexpr char kKeyFridgeTemperature[] = "fridgeTemp";
constexpr char kKeyAttenuation[] = "attenuation";
constexpr char kKeyEndAttenuation[] = "endAtten";

SourceMode s_source_mode = SourceMode::kBrewfatherApi;
SimulatedValues s_simulated;

void copyDisplayText(const char* value, char* output, size_t output_len,
                     const char* fallback) {
  if (output_len == 0) {
    return;
  }
  const char* source = value != nullptr && value[0] != '\0' ? value : fallback;
  size_t written = 0;
  bool previous_space = true;
  for (size_t index = 0; source[index] != '\0' && written + 1 < output_len;
       ++index) {
    const uint8_t byte = static_cast<uint8_t>(source[index]);
    if (byte < 0x20 || byte == 0x7F) {
      if (!previous_space) {
        output[written++] = ' ';
        previous_space = true;
      }
      continue;
    }
    output[written++] = source[index];
    previous_space = byte == ' ';
  }
  while (written > 0 && output[written - 1] == ' ') {
    --written;
  }
  output[written] = '\0';
}

bool parseIntInRange(const char* value, int minimum, int maximum, int& result) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  result = static_cast<int>(parsed);
  return true;
}

bool parseFloatInRange(const char* value, float minimum, float maximum,
                       float& result) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  if (errno != 0 || end == value || *end != '\0' || !std::isfinite(parsed) ||
      parsed < minimum || parsed > maximum) {
    return false;
  }
  result = parsed;
  return true;
}

void loadDefaults() {
  s_source_mode = SourceMode::kBrewfatherApi;
  copyDisplayText("Sud", s_simulated.batch_name,
                  sizeof(s_simulated.batch_name), "Sud");
  copyDisplayText("Erdbeer-Woelkchen", s_simulated.recipe_name,
                  sizeof(s_simulated.recipe_name), "BrewSphere");
  copyDisplayText("Fermenting", s_simulated.status,
                  sizeof(s_simulated.status), "Fermenting");
  s_simulated.batch_number = 39;
  s_simulated.brew_day = 13;
  s_simulated.plato = 24.2f;
  s_simulated.target_plato = 2.6f;
  s_simulated.target_temperature_c = 3.0f;
  s_simulated.fridge_temperature_c = 3.4f;
  s_simulated.attenuation_percent = 81.0f;
  s_simulated.end_attenuation_percent = 84.0f;
}

float validatedFloat(float value, float minimum, float maximum,
                     float fallback) {
  return std::isfinite(value) && value >= minimum && value <= maximum ? value
                                                                      : fallback;
}

void validateLoadedValues() {
  if (s_simulated.batch_number < 0 || s_simulated.batch_number > 9999) {
    s_simulated.batch_number = 39;
  }
  if (s_simulated.brew_day < 0 || s_simulated.brew_day > 9999) {
    s_simulated.brew_day = 13;
  }
  s_simulated.plato =
      validatedFloat(s_simulated.plato, 0.0f, 40.0f, 24.2f);
  s_simulated.target_plato =
      validatedFloat(s_simulated.target_plato, 0.0f, 40.0f, 2.6f);
  s_simulated.target_temperature_c = validatedFloat(
      s_simulated.target_temperature_c, -20.0f, 100.0f, 3.0f);
  s_simulated.fridge_temperature_c = validatedFloat(
      s_simulated.fridge_temperature_c, -20.0f, 100.0f, 3.4f);
  s_simulated.attenuation_percent = validatedFloat(
      s_simulated.attenuation_percent, 0.0f, 100.0f, 81.0f);
  s_simulated.end_attenuation_percent = validatedFloat(
      s_simulated.end_attenuation_percent, 0.0f, 100.0f, 84.0f);
}

void persist() {
  Preferences preferences;
  if (!preferences.begin(kPrefsNamespace, false)) {
    return;
  }
  preferences.putUChar(kKeySource, static_cast<uint8_t>(s_source_mode));
  preferences.putString(kKeyBatchName, s_simulated.batch_name);
  preferences.putString(kKeyRecipeName, s_simulated.recipe_name);
  preferences.putString(kKeyStatus, s_simulated.status);
  preferences.putInt(kKeyBatchNumber, s_simulated.batch_number);
  preferences.putInt(kKeyBrewDay, s_simulated.brew_day);
  preferences.putFloat(kKeyPlato, s_simulated.plato);
  preferences.putFloat(kKeyTargetPlato, s_simulated.target_plato);
  preferences.putFloat(kKeyTargetTemperature,
                       s_simulated.target_temperature_c);
  preferences.putFloat(kKeyFridgeTemperature,
                       s_simulated.fridge_temperature_c);
  preferences.putFloat(kKeyAttenuation, s_simulated.attenuation_percent);
  preferences.putFloat(kKeyEndAttenuation,
                       s_simulated.end_attenuation_percent);
  preferences.end();
}

}  // namespace

void init() {
  loadDefaults();
  Preferences preferences;
  if (!preferences.begin(kPrefsNamespace, true)) {
    return;
  }

  const uint8_t source = preferences.getUChar(
      kKeySource, static_cast<uint8_t>(SourceMode::kBrewfatherApi));
  s_source_mode = source == static_cast<uint8_t>(SourceMode::kSimulated)
                      ? SourceMode::kSimulated
                      : SourceMode::kBrewfatherApi;

  String value = preferences.getString(kKeyBatchName, s_simulated.batch_name);
  copyDisplayText(value.c_str(), s_simulated.batch_name,
                  sizeof(s_simulated.batch_name), "Sud");
  value = preferences.getString(kKeyRecipeName, s_simulated.recipe_name);
  copyDisplayText(value.c_str(), s_simulated.recipe_name,
                  sizeof(s_simulated.recipe_name), "BrewSphere");
  value = preferences.getString(kKeyStatus, s_simulated.status);
  copyDisplayText(value.c_str(), s_simulated.status,
                  sizeof(s_simulated.status), "Fermenting");
  s_simulated.batch_number =
      preferences.getInt(kKeyBatchNumber, s_simulated.batch_number);
  s_simulated.brew_day =
      preferences.getInt(kKeyBrewDay, s_simulated.brew_day);
  s_simulated.plato = preferences.getFloat(kKeyPlato, s_simulated.plato);
  s_simulated.target_plato =
      preferences.getFloat(kKeyTargetPlato, s_simulated.target_plato);
  s_simulated.target_temperature_c = preferences.getFloat(
      kKeyTargetTemperature, s_simulated.target_temperature_c);
  s_simulated.fridge_temperature_c = preferences.getFloat(
      kKeyFridgeTemperature, s_simulated.fridge_temperature_c);
  s_simulated.attenuation_percent =
      preferences.getFloat(kKeyAttenuation, s_simulated.attenuation_percent);
  s_simulated.end_attenuation_percent = preferences.getFloat(
      kKeyEndAttenuation, s_simulated.end_attenuation_percent);
  preferences.end();
  validateLoadedValues();
}

SourceMode sourceMode() { return s_source_mode; }

const SimulatedValues& simulatedValues() { return s_simulated; }

bool saveFromPortal(const char* source, const char* batch_name,
                    const char* recipe_name, const char* status,
                    const char* batch_number, const char* brew_day,
                    const char* plato, const char* target_plato,
                    const char* target_temperature_c,
                    const char* fridge_temperature_c,
                    const char* attenuation_percent,
                    const char* end_attenuation_percent) {
  const SourceMode requested_mode =
      source != nullptr && strcmp(source, "simulated") == 0
          ? SourceMode::kSimulated
          : SourceMode::kBrewfatherApi;
  if (requested_mode == SourceMode::kBrewfatherApi) {
    s_source_mode = requested_mode;
    persist();
    return true;
  }

  int parsed_batch_number = s_simulated.batch_number;
  int parsed_brew_day = s_simulated.brew_day;
  float parsed_plato = s_simulated.plato;
  float parsed_target_plato = s_simulated.target_plato;
  float parsed_target_temperature = s_simulated.target_temperature_c;
  float parsed_fridge_temperature = s_simulated.fridge_temperature_c;
  float parsed_attenuation = s_simulated.attenuation_percent;
  float parsed_end_attenuation = s_simulated.end_attenuation_percent;
  if (!parseIntInRange(batch_number, 0, 9999, parsed_batch_number) ||
      !parseIntInRange(brew_day, 0, 9999, parsed_brew_day) ||
      !parseFloatInRange(plato, 0.0f, 40.0f, parsed_plato) ||
      !parseFloatInRange(target_plato, 0.0f, 40.0f,
                         parsed_target_plato) ||
      !parseFloatInRange(target_temperature_c, -20.0f, 100.0f,
                         parsed_target_temperature) ||
      !parseFloatInRange(fridge_temperature_c, -20.0f, 100.0f,
                         parsed_fridge_temperature) ||
      !parseFloatInRange(attenuation_percent, 0.0f, 100.0f,
                         parsed_attenuation) ||
      !parseFloatInRange(end_attenuation_percent, 0.0f, 100.0f,
                         parsed_end_attenuation)) {
    return false;
  }

  s_source_mode = requested_mode;
  copyDisplayText(batch_name, s_simulated.batch_name,
                  sizeof(s_simulated.batch_name), "Sud");
  copyDisplayText(recipe_name, s_simulated.recipe_name,
                  sizeof(s_simulated.recipe_name), "BrewSphere");
  copyDisplayText(status, s_simulated.status, sizeof(s_simulated.status),
                  "Fermenting");

  s_simulated.batch_number = parsed_batch_number;
  s_simulated.brew_day = parsed_brew_day;
  s_simulated.plato = parsed_plato;
  s_simulated.target_plato = parsed_target_plato;
  s_simulated.target_temperature_c = parsed_target_temperature;
  s_simulated.fridge_temperature_c = parsed_fridge_temperature;
  s_simulated.attenuation_percent = parsed_attenuation;
  s_simulated.end_attenuation_percent = parsed_end_attenuation;
  persist();
  return true;
}

void clear() {
  Preferences preferences;
  if (preferences.begin(kPrefsNamespace, false)) {
    preferences.clear();
    preferences.end();
  }
  loadDefaults();
}

}  // namespace services::brew
