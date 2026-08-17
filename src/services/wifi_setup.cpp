#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdio>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/brew_settings.h"
#include "services/display_settings.h"
#include "services/ota_update.h"
#include "services/radar_location.h"
#include "services/weather_time.h"
#include "ui/brew_display.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

constexpr char kEmblemSvg[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-label="BrewSphere emblem">
<defs><clipPath id="beerClip"><path d="M219 207 C220 252 224 301 229 339 Q256 348 283 339 C288 301 292 252 293 207 Z"/></clipPath></defs>
<circle cx="256" cy="256" r="246" fill="#0B3552"/><circle cx="256" cy="256" r="222" fill="none" stroke="#06264A" stroke-width="48"/>
<circle cx="256" cy="256" r="222" fill="none" stroke="#FF9D00" stroke-width="48" stroke-dasharray="174.4 1220.5" transform="rotate(-45 256 256)"/>
<g stroke="#FFF1C9" stroke-width="10" stroke-linecap="butt"><path d="M394.6 117.4 L430 82"/><path d="M452 256 L502 256"/><path d="M394.6 394.6 L430 430"/><path d="M117.4 394.6 L82 430"/><path d="M60 256 L10 256"/><path d="M117.4 117.4 L82 82"/></g>
<circle cx="256" cy="256" r="246" fill="none" stroke="#FFF1C9" stroke-width="6"/><circle cx="256" cy="256" r="177" fill="none" stroke="#FFF1C9" stroke-width="11"/>
<g fill="none" stroke="#FFF1C9" stroke-width="6" stroke-linecap="round"><path d="M169 139 C111 194 111 318 169 373"/><path d="M343 139 C401 194 401 318 343 373"/></g>
<g clip-path="url(#beerClip)"><path d="M205 273 Q250 286 307 270 L307 365 L205 365 Z" fill="#FF9D00"/><path d="M205 329 Q256 347 307 326 L307 365 L205 365 Z" fill="#E87300"/></g>
<path d="M205 171 C205 151 222 142 239 148 C248 137 267 137 276 148 C294 142 311 151 311 171 Z" fill="#FFF1C9"/>
<path d="M207 188 C206 237 210 286 220 350 Q256 365 292 350 C302 286 306 237 305 188" fill="none" stroke="#FFF1C9" stroke-width="13" stroke-linecap="square" stroke-linejoin="round"/><path d="M226 340 Q256 350 286 340" fill="none" stroke="#FFF1C9" stroke-width="8"/>
<circle cx="273" cy="237" r="12" fill="#41DEDE" stroke="#FFF1C9" stroke-width="3"/><circle cx="249" cy="267" r="9" fill="#FF9D00" stroke="#FFF1C9" stroke-width="2"/><circle cx="267" cy="286" r="10" fill="#BFEFFF" stroke="#FFF1C9" stroke-width="2"/>
</svg>)SVG";

constexpr char kPortalGlobalStyle[] =
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:24px;background:#0d151e;color:#d7e0e9;"
    "font-family:Segoe UI,Arial,sans-serif;font-size:15px;line-height:1.45}"
    "body>div,body>form,form{max-width:760px;margin:0 auto}"
    ".wrap{display:block!important;width:100%!important;min-width:0!important;"
    "max-width:760px!important;text-align:left!important}"
    ".wrap form{display:block!important;width:100%!important}"
    ".wrap form>div{float:none!important;clear:both!important;width:100%!important;"
    "display:block!important}"
    "form{padding:24px;background:#141f2a;border:1px solid #2a3a49;"
    "border-radius:12px;box-shadow:0 12px 32px #0005}"
    ".c{float:none!important;clear:both!important;width:100%!important;"
    "box-sizing:border-box!important}"
    "h1,h2,h3{color:#edf3f8;font-weight:600;letter-spacing:.01em}"
    "h1{font-size:1.35rem;margin:0 0 1.2rem}"
    "h2{font-size:1.05rem;margin:1.5rem 0 .6rem}"
    "h3{font-size:1rem;margin:1.5rem 0 .7rem!important;padding:10px 12px;"
    "background:#1a2937;border-left:3px solid #62899d;border-radius:6px}"
    "label{color:#b8c6d3}"
    "input[type=text],input[type=password],input[type=number],select{"
    "width:100%;padding:9px 10px;background:#0f1923;color:#e4edf4;"
    "border:1px solid #35495b;border-radius:6px;outline:none}"
    "input[type=color]{width:52px;height:32px;padding:3px;background:#0f1923;"
    "border:1px solid #526577;border-radius:5px}"
    "input[type=range]{accent-color:#7098aa}input[type=checkbox]{accent-color:#7098aa}"
    "button,input[type=submit]{padding:8px 13px;background:#38596b;color:#eef5f8;"
    "border:1px solid #5e8191;border-radius:6px;font:inherit;cursor:pointer}"
    "button:hover,input[type=submit]:hover{background:#486f80;border-color:#83a8b7}"
    "a{color:#8eb5c5}small{color:#9aaabd}"
    ".home-link{display:inline-block;margin:0 0 16px;padding:8px 13px;"
    "background:#38596b;color:#eef5f8;border:1px solid #5e8191;"
    "border-radius:6px;text-decoration:none;font-weight:600}"
    ".home-link:hover{background:#486f80;border-color:#83a8b7}"
     ".portal-menu-link{display:block;width:100%;margin:16px 0;"
    "padding:12px;text-align:center;background:#38596b;color:#eef5f8;"
    "border:1px solid #5e8191;border-radius:6px;text-decoration:none;"
    "font-size:1.05rem;font-weight:600}"
     ".portal-menu-link:hover{background:#486f80;border-color:#83a8b7}"
     ".portal-action-form{padding:0!important;background:transparent!important;"
     "border:0!important;border-radius:0!important;box-shadow:none!important;"
     "margin:16px 0!important}"
     ".portal-action-form button,.portal-menu-link{display:block;width:100%;"
     "min-height:58px;padding:12px 16px;text-align:center;font-size:1.05rem;"
     "font-weight:600}"
     ".brand-banner{display:flex;align-items:center;justify-content:center;gap:16px;"
     "width:100%;margin:0 0 24px;padding:14px 18px;background:#fff1c9;"
     "border:1px solid #2a3a49;border-radius:12px;box-shadow:0 12px 32px #0005;"
     "color:#071A2A;font-size:1.45rem;font-weight:600;letter-spacing:.02em}"
     ".brand-banner img{width:64px;height:64px;display:block}"
     ".device-name{margin-bottom:6px;color:#edf3f8;font-size:1.15rem;"
     "font-weight:600}"
     ".portal-nav{width:100%;margin:0 0 24px}.portal-divider{"
     "border:0;border-top:1px solid #2a3a49;margin:24px 0 0}"
     "@media(max-width:520px){body{padding:12px}form{padding:16px}}"
    "</style>"
    "<script>document.addEventListener('DOMContentLoaded',function(){"
     "var w=document.querySelector('.wrap');if(!w)return;"
     "if(location.pathname==='/' ){"
     "var b=document.createElement('div');b.className='brand-banner';"
     "var i=document.createElement('img');i.src='/emblem.svg';i.alt='BrewSphere';"
     "var t=document.createElement('span');t.textContent='BrewSphere';"
     "b.appendChild(i);b.appendChild(t);w.prepend(b);"
     "var nav=document.createElement('div');nav.className='portal-nav';"
     "w.insertBefore(nav,w.children[1]);"
     "function l(h,t){var r=document.createElement('div');r.className='c portal-action';"
     "var a=document.createElement('a');a.href=h;a.textContent=t;"
     "a.className='portal-menu-link';r.appendChild(a);nav.appendChild(r);}"
     "l('/display','Display');l('/brew','Brewfather / Simulation');"
     "var divider=document.createElement('hr');divider.className='portal-divider';nav.appendChild(divider);"
     "w.querySelectorAll('form').forEach(function(f){"
     "if(!f.querySelector('input,select,textarea'))f.classList.add('portal-action-form');});"
     "var blocks=Array.from(w.children),status=blocks.find(function(e){"
     "return /Connected to|Not connected/i.test(e.textContent);}),devicePanel=blocks.find(function(e){"
     "return e!==status&&/esp32/i.test(e.textContent)&&/\\d{1,3}(?:\\.\\d{1,3}){3}/.test(e.textContent);});"
     "if(status){var title=w.querySelector('h1');"
     "if(title&&/^BrewSphere$/i.test(title.textContent.trim()))title.remove();"
     "if(devicePanel){var device=document.createElement('div');device.className='device-name';"
     "device.textContent=devicePanel.textContent.trim();status.prepend(device);devicePanel.remove();}"
     "w.appendChild(status);}return;}"
    "var a=document.createElement('a');a.href='/';a.textContent='Home';"
    "a.className='home-link';w.prepend(a);"
    "});</script>";

/** Separate WiFiManager namespace for the force-portal flag. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();
void attachSettingsRoutes();

void handleDisplayPage() {
  if (!s_wm.server) {
    return;
  }
  s_wm.server->send(200, "text/html",
                    "<!doctype html><html><head><meta name='viewport' "
                    "content='width=device-width,initial-scale=1'>"
                     "<title>BrewSphere Display</title><style>"
                    "body{margin:0;padding:20px;background:#0d151e;color:#d7e0e9;"
                    "font-family:Segoe UI,Arial,sans-serif;text-align:center}"
                    "main{max-width:520px;margin:auto;background:#141f2a;"
                    "padding:20px;border:1px solid #2a3a49;border-radius:12px}"
                    "img{width:min(100%,480px);height:auto;image-rendering:auto;"
                    "border:1px solid #526577;border-radius:8px}"
                    "a{display:inline-block;margin-top:16px;padding:8px 13px;"
                    "background:#38596b;color:#eef5f8;border:1px solid #5e8191;"
                     "border-radius:6px;text-decoration:none;margin:16px 6px 0}"
                     "</style></head><body>"
                     "<main><h2>BrewSphere Display</h2>"
                    "<img id='display' src='/display.bmp'>"
                    "<script>setInterval(function(){document.getElementById('display').src="
                    "'/display.bmp?t='+Date.now()},5000);</script>"
                     "<br><a href='/brew'>Datenquelle</a>"
                     "<a href='/'>Home</a></main></body></html>");
}

void handleDisplayBmp() {
  if (!s_wm.server) {
    return;
  }
  if (!ui::brewDisplayFrameAvailable()) {
    s_wm.server->send(503, "text/plain",
                      "Display preview unavailable: framebuffer allocation failed");
    return;
  }
  constexpr size_t kBmpSize = 54 + 240 * 240 * 3;
  s_wm.server->setContentLength(kBmpSize);
  s_wm.server->send(200, "image/bmp", "");
  WiFiClient client = s_wm.server->client();
  ui::brewDisplayWriteBmp(client);
}

void appendHtmlEscaped(String& html, const char* value) {
  if (value == nullptr) {
    return;
  }
  while (*value != '\0') {
    switch (*value++) {
      case '&':
        html += F("&amp;");
        break;
      case '<':
        html += F("&lt;");
        break;
      case '>':
        html += F("&gt;");
        break;
      case '\"':
        html += F("&quot;");
        break;
      case '\'':
        html += F("&#39;");
        break;
      default:
        html += value[-1];
        break;
    }
  }
}

void appendTextInput(String& html, const char* name, const char* label,
                     const char* value, int max_length) {
  html += F("<label for='");
  html += name;
  html += F("'>");
  html += label;
  html += F("</label><input id='");
  html += name;
  html += F("' name='");
  html += name;
  html += F("' type='text' maxlength='");
  html += max_length;
  html += F("' value='");
  appendHtmlEscaped(html, value);
  html += F("'>");
}

void appendNumberInput(String& html, const char* name, const char* label,
                       const String& value, const char* minimum,
                       const char* maximum, const char* step) {
  html += F("<label for='");
  html += name;
  html += F("'>");
  html += label;
  html += F("</label><input id='");
  html += name;
  html += F("' name='");
  html += name;
  html += F("' type='number' min='");
  html += minimum;
  html += F("' max='");
  html += maximum;
  html += F("' step='");
  html += step;
  html += F("' value='");
  html += value;
  html += F("'><input class='range' type='range' tabindex='-1' aria-label='");
  html += label;
  html += F(" Schieberegler' data-number='");
  html += name;
  html += F("' min='");
  html += minimum;
  html += F("' max='");
  html += maximum;
  html += F("' step='");
  html += step;
  html += F("' value='");
  html += value;
  html += F("'>");
}

bool settingsWriteAuthenticated() {
  if (!s_wm.server) {
    return false;
  }
  if (s_wm.server->authenticate(config::kOtaUsername,
                                services::settings::otaPassword())) {
    return true;
  }
  s_wm.server->requestAuthentication();
  return false;
}

char s_brew_csrf_token[17] = {};

void ensureBrewCsrfToken() {
  if (s_brew_csrf_token[0] == '\0') {
    snprintf(s_brew_csrf_token, sizeof(s_brew_csrf_token), "%08lx%08lx",
             static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
  }
}

bool brewCsrfValid(WebServer& web) {
  ensureBrewCsrfToken();
  return web.arg("csrf") == s_brew_csrf_token;
}

void handleBrewSettingsPage() {
  if (!s_wm.server || !settingsWriteAuthenticated()) {
    return;
  }
  const services::brew::SimulatedValues& simulated =
      services::brew::simulatedValues();
  const services::brew::BrewfatherCredentials& credentials =
      services::brew::brewfatherCredentials();
  const bool simulation = services::brew::sourceMode() ==
                          services::brew::SourceMode::kSimulated;
  ensureBrewCsrfToken();

  String html;
   html.reserve(8500);
  html += F(
      "<!doctype html><html lang='de'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>BrewSphere Datenquelle</title><style>"
      "*{box-sizing:border-box}body{margin:0;padding:20px;background:#0d151e;"
      "color:#d7e0e9;font:15px/1.45 Segoe UI,Arial,sans-serif}"
      "main{max-width:680px;margin:auto}form{padding:24px;background:#141f2a;"
      "border:1px solid #2a3a49;border-radius:12px;box-shadow:0 12px 32px #0005}"
      "h1{margin:0 0 6px;color:#edf3f8;font-size:1.4rem}"
      ".intro{margin:0 0 20px;color:#9aaabd}.grid{display:grid;"
      "grid-template-columns:1fr 1fr;gap:14px 18px}.full{grid-column:1/-1}"
      "label{display:block;margin:0 0 5px;color:#b8c6d3}"
      "input,select{width:100%;padding:9px 10px;background:#0f1923;"
      "color:#e4edf4;border:1px solid #35495b;border-radius:6px;font:inherit}"
      "input:focus,select:focus{outline:0;border-color:#7098aa;"
      "box-shadow:0 0 0 2px #7098aa33}.simulation{display:contents}"
      ".range{grid-column:1/-1;margin-top:-7px;padding:0;accent-color:#41dede}"
      ".check{grid-column:1/-1;"
      "display:flex;align-items:center;gap:9px;padding:10px 12px;background:#0f1923;"
      "border:1px solid #35495b;border-radius:6px}.check input{width:auto;margin:0}"
      ".actions{display:flex;gap:10px;margin-top:22px;flex-wrap:wrap}"
      ".live-status{align-self:center;color:#8eb5c5;min-width:9rem}"
      "button,a{padding:9px 14px;background:#38596b;color:#eef5f8;"
      "border:1px solid #5e8191;border-radius:6px;text-decoration:none;"
      "font:inherit;cursor:pointer}button:hover,a:hover{background:#486f80}"
      ".hint{grid-column:1/-1;padding:10px 12px;background:#1a2937;"
      "border-left:3px solid #62899d;border-radius:6px;color:#aebdca}"
      "@media(max-width:560px){body{padding:12px}form{padding:16px}"
      ".grid{grid-template-columns:1fr}.full,.hint{grid-column:1}}"
      "</style></head><body><main><form id='brewForm' method='post' action='/brew-save'>"
      "<h1>BrewSphere Datenquelle</h1>"
      "<p class='intro'>Zwischen echten Brewfather-Daten und frei einstellbaren "
      "Testwerten wechseln.</p><div class='grid'><div class='full'>"
      "<label for='brew_source'>Datenquelle</label>"
      "<select id='brew_source' name='brew_source'>"
      "<option value='brewfather'");
  if (!simulation) {
    html += F(" selected");
  }
  html += F(">Brewfather API</option><option value='simulated'");
  if (simulation) {
    html += F(" selected");
  }
  html += F(">Simulierte Werte</option></select>"
            "<input type='hidden' name='csrf' value='");
  html += s_brew_csrf_token;
   html += F("'></div><div class='full'><label for='brew_user_id'>Brewfather User-ID</label><input id='brew_user_id' name='brew_user_id' type='text' maxlength='95' value='");
   appendHtmlEscaped(html, credentials.user_id);
   html += F("' autocomplete='username'></div><div class='full'><label for='brew_api_key'>Brewfather API-Key</label><input id='brew_api_key' name='brew_api_key' type='password' maxlength='159' value='' placeholder='Leer lassen, um den gespeicherten Key zu behalten' autocomplete='current-password'></div><div class='hint'>Zugangsdaten werden nur auf dem Gerät gespeichert. Der API-Key wird aus Sicherheitsgründen nicht wieder angezeigt.</div><div id='simulation' class='simulation'>"
             "<p class='hint'>Änderungen werden live auf dem Display und in der "
            "Webvorschau verwendet. Speichern übernimmt sie dauerhaft.</p>");
  html += F("<label class='check'><input id='sim_demo' name='sim_demo' "
            "type='checkbox'");
  if (simulated.demo_mode) {
    html += F(" checked");
  }
  html += F(">Demo-Modus: Werte ändern sich langsam automatisch</label>");
  appendTextInput(html, "sim_batch_name", "Sudname", simulated.batch_name, 63);
  appendTextInput(html, "sim_recipe_name", "Rezeptname", simulated.recipe_name,
                  63);
  appendTextInput(html, "sim_status",
                  "Status (Fermenting, Brewing oder Conditioning)",
                  simulated.status, 19);
  appendNumberInput(html, "sim_batch_number", "Batchnummer",
                    String(simulated.batch_number), "0", "9999", "1");
  appendNumberInput(html, "sim_brew_day", "Brautag",
                    String(simulated.brew_day), "0", "9999", "1");
  appendNumberInput(html, "sim_plato", "Aktueller Wert (&deg;P)",
                    String(simulated.plato, 1), "0", "40", "0.1");
  appendNumberInput(html, "sim_target_plato", "Zielwert (&deg;P)",
                    String(simulated.target_plato, 1), "0", "40", "0.1");
  appendNumberInput(html, "sim_target_temp", "Solltemperatur (&deg;C)",
                    String(simulated.target_temperature_c, 1), "-20", "100",
                    "0.1");
  appendNumberInput(html, "sim_fridge_temp", "Isttemperatur (&deg;C)",
                    String(simulated.fridge_temperature_c, 1), "-20", "100",
                    "0.1");
  appendNumberInput(html, "sim_attenuation", "Verg&auml;rgrad (%)",
                    String(simulated.attenuation_percent, 0), "0", "100",
                    "1");
  appendNumberInput(html, "sim_end_attenuation",
                    "Endverg&auml;rgrad / Farbwechsel (%)",
                    String(simulated.end_attenuation_percent, 0), "0", "100",
                    "1");
  html += F(
      "</div></div><div class='actions'><button type='submit'>Speichern</button>"
      "<a href='/display'>Display ansehen</a><a href='/'>Home</a>"
      "<span id='liveStatus' class='live-status'>Live bereit</span></div></form>"
      "</main><script>(function(){var form=document.getElementById('brewForm'),"
      "source=document.getElementById('brew_source'),"
      "fields=document.getElementById('simulation'),"
      "status=document.getElementById('liveStatus'),timer;function update(){"
      "fields.style.display=source.value==='simulated'?'contents':'none';}"
      "function live(){clearTimeout(timer);status.textContent='Änderung...';"
      "timer=setTimeout(function(){fetch('/brew-live',{method:'POST',"
      "body:new FormData(form),credentials:'same-origin'}).then(function(response){"
      "if(!response.ok)throw new Error();status.textContent='Live aktualisiert';})"
      ".catch(function(){status.textContent='Eingabe prüfen';});},300);}"
      "document.querySelectorAll('.range').forEach(function(range){"
      "range.addEventListener('input',function(){document.getElementById("
      "range.dataset.number).value=range.value;});});"
      "form.querySelectorAll('input[type=number]').forEach(function(number){"
      "number.addEventListener('input',function(){var range=form.querySelector("
      "'.range[data-number=\"'+number.id+'\"]');if(range)range.value=number.value;});});"
      "form.addEventListener('input',live);source.addEventListener('change',"
      "function(){update();live();});update();})();</script>"
      "</body></html>");
  s_wm.server->send(200, "text/html; charset=utf-8", html);
}

bool applyBrewSettingsRequest(WebServer& web, bool persist_values) {
  const bool brewfather_requested = web.arg("brew_source") != "simulated";
  const bool credentials_valid = services::brew::saveCredentialsFromPortal(
      web.arg("brew_user_id").c_str(), web.arg("brew_api_key").c_str(),
      persist_values);
  if (brewfather_requested && !credentials_valid) {
    return false;
  }
  return services::brew::saveFromPortal(
      web.arg("brew_source").c_str(), web.arg("sim_batch_name").c_str(),
      web.arg("sim_recipe_name").c_str(), web.arg("sim_status").c_str(),
      web.arg("sim_demo").c_str(),
      web.arg("sim_batch_number").c_str(), web.arg("sim_brew_day").c_str(),
      web.arg("sim_plato").c_str(), web.arg("sim_target_plato").c_str(),
      web.arg("sim_target_temp").c_str(), web.arg("sim_fridge_temp").c_str(),
      web.arg("sim_attenuation").c_str(),
      web.arg("sim_end_attenuation").c_str(), persist_values);
}

void handleBrewSettingsLive() {
  if (!s_wm.server || !settingsWriteAuthenticated()) {
    return;
  }
  WebServer& web = *s_wm.server;
  if (!brewCsrfValid(web)) {
    web.send(403, "text/plain", "Invalid form token");
    return;
  }
  if (!applyBrewSettingsRequest(web, false)) {
    web.send(400, "text/plain", "Invalid simulation values");
    return;
  }
  services::weather::requestRefresh();
  web.send(204, "text/plain", "");
}

void handleBrewSettingsSaved() {
  if (!s_wm.server || !settingsWriteAuthenticated()) {
    return;
  }
  WebServer& web = *s_wm.server;
  if (!brewCsrfValid(web)) {
    web.send(403, "text/plain", "Invalid form token");
    return;
  }
  const bool saved = applyBrewSettingsRequest(web, true);
  if (!saved) {
    web.send(400, "text/html; charset=utf-8",
             "<!doctype html><html lang='de'><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<body style='font-family:Segoe UI,Arial,sans-serif;background:#0d151e;"
              "color:#d7e0e9;padding:2rem'><h2>Ungültige Brewfather- oder Simulationsdaten</h2>"
              "<p>Bitte Zugangsdaten und Eingabebereiche prüfen.</p><a style='color:#8eb5c5' "
             "href='/brew'>Zurück</a></body></html>");
    return;
  }
  services::weather::requestRefresh();
  web.sendHeader("Location", "/brew", true);
  web.send(303, "text/plain", "Saved");
}

constexpr int kOtaPasswordParamLen =
    static_cast<int>(services::settings::kOtaPasswordMaxLen);

constexpr char kOtaPasswordAttrs[] =
    "type=\"password\" autocomplete=\"new-password\" "
    "placeholder=\"leave blank to keep current\"";
WiFiManagerParameter s_param_ota_password(
    "ota_password", "OTA password (user: admin)", "", kOtaPasswordParamLen,
    kOtaPasswordAttrs);

void onPortalParamsSaved() {
  services::settings::saveOtaPasswordFromPortal(
      s_param_ota_password.getValue());
}

void attachSettingsRoutes() {
  if (!s_wm.server) {
    return;
  }
  // Browsers request this automatically when opening the portal. Returning
  // an empty response avoids a misleading "handler not found" log entry.
  s_wm.server->on("/favicon.ico", HTTP_GET, []() {
    s_wm.server->send(204, "text/plain", "");
  });
  s_wm.server->on("/emblem.svg", HTTP_GET, []() {
    s_wm.server->send_P(200, "image/svg+xml", kEmblemSvg);
  });
  s_wm.server->on("/display", HTTP_GET, handleDisplayPage);
  s_wm.server->on("/display.bmp", HTTP_GET, handleDisplayBmp);
  s_wm.server->on("/brew", HTTP_GET, handleBrewSettingsPage);
  s_wm.server->on("/brew-live", HTTP_POST, handleBrewSettingsLive);
  s_wm.server->on("/brew-save", HTTP_POST, handleBrewSettingsSaved);
}

void attachPortalParams(WiFiManager& wm) {
  s_param_ota_password.setValue("", kOtaPasswordParamLen);
  wm.addParameter(&s_param_ota_password);
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }
  return conf.sta.ssid[0] != '\0';
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  services::settings::clear();
  services::brew::clear();
  Serial.println("WiFi credentials, location, display, and brew settings cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setCustomHeadElement(kPortalGlobalStyle);
  s_wm.setTitle("BrewSphere");
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  services::ota::configure(s_wm, attachSettingsRoutes);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      return true;
    }
  }

  return false;
}

bool connectSavedNetwork(bool show_ui) {
  if (!storedWifiCredentials()) {
    return false;
  }

  ensureWifiManager();
  const String ssid = s_wm.getWiFiSSID();
  if (ssid.length() == 0) {
    return false;
  }
  const String pass = s_wm.getWiFiPass();
  return tryConnectWithUi(ssid, pass, show_ui);
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  if (force_portal) {
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — trying local fallback");
  } else {
    Serial.println("No saved WiFi — trying local fallback");
  }

  if (config::kWifiFallbackSSID[0] != '\0' &&
      tryConnectWithUi(config::kWifiFallbackSSID, config::kWifiFallbackPass,
                       true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected to fallback WiFi: %s  IP %s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
