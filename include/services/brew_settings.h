#pragma once

#include <cstdint>

namespace services::brew {

enum class SourceMode : uint8_t {
  kBrewfatherApi,
  kSimulated,
};

struct SimulatedValues {
  bool demo_mode = false;
  char batch_name[64] = {};
  char recipe_name[64] = {};
  char status[20] = {};
  int batch_number = 0;
  int brew_day = 0;
  float plato = 0.0f;
  float target_plato = 0.0f;
  float target_temperature_c = 0.0f;
  float fridge_temperature_c = 0.0f;
  float attenuation_percent = 0.0f;
  float end_attenuation_percent = 0.0f;
};

struct BrewfatherCredentials {
  char user_id[96] = {};
  char api_key[160] = {};
};

/** Load the Brewfather/simulation source and simulated values from NVS. */
void init();

SourceMode sourceMode();
const SimulatedValues& simulatedValues();
const BrewfatherCredentials& brewfatherCredentials();

/** Update Brewfather credentials in RAM and optionally persist them in NVS. */
bool saveCredentialsFromPortal(const char* user_id, const char* api_key,
                               bool persist_values = true);

/** Validate values received from the web form and optionally persist them. */
bool saveFromPortal(const char* source, const char* batch_name,
                    const char* recipe_name, const char* status,
                    const char* demo_mode,
                    const char* batch_number, const char* brew_day,
                    const char* plato, const char* target_plato,
                    const char* target_temperature_c,
                    const char* fridge_temperature_c,
                    const char* attenuation_percent,
                    const char* end_attenuation_percent,
                    bool persist_values = true);

/** Restore defaults during a full BOOT-button reset. */
void clear();

}  // namespace services::brew
