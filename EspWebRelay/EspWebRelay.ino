#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// ---- Pin / relay ----
#define RELAY_PIN 0
#define RELAY_ACTIVE_LOW true

// ---- EEPROM layout ----
#define EEPROM_SIZE 128
#define EEPROM_MAGIC 0xA5
#define SSID_MAX 32
#define PASS_MAX 64

struct WifiCreds {
  uint8_t magic;
  char ssid[SSID_MAX + 1];
  char pass[PASS_MAX + 1];
};

// ---- AP defaults ----
#define AP_PASSWORD "relay1234"
#define STA_TIMEOUT_MS 15000UL
#define STA_RETRY_MS 45000UL

ESP8266WebServer server(80);

bool relayOn = false;
bool apMode = false;
char savedSsid[SSID_MAX + 1] = "";
char savedPass[PASS_MAX + 1] = "";
unsigned long lastStaAttemptMs = 0;

// ---- Relay ----
void setRelay(bool on) {
  relayOn = on;
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

// ---- EEPROM ----
bool loadWifi() {
  WifiCreds creds;
  EEPROM.get(0, creds);
  if (creds.magic != EEPROM_MAGIC) {
    savedSsid[0] = '\0';
    savedPass[0] = '\0';
    return false;
  }
  creds.ssid[SSID_MAX] = '\0';
  creds.pass[PASS_MAX] = '\0';
  strncpy(savedSsid, creds.ssid, SSID_MAX);
  strncpy(savedPass, creds.pass, PASS_MAX);
  savedSsid[SSID_MAX] = '\0';
  savedPass[PASS_MAX] = '\0';
  return savedSsid[0] != '\0';
}

void saveWifi(const String &ssid, const String &pass) {
  WifiCreds creds;
  memset(&creds, 0, sizeof(creds));
  creds.magic = EEPROM_MAGIC;
  strncpy(creds.ssid, ssid.c_str(), SSID_MAX);
  strncpy(creds.pass, pass.c_str(), PASS_MAX);
  EEPROM.put(0, creds);
  EEPROM.commit();
  strncpy(savedSsid, creds.ssid, SSID_MAX);
  strncpy(savedPass, creds.pass, PASS_MAX);
}

void clearWifi() {
  WifiCreds creds;
  memset(&creds, 0, sizeof(creds));
  EEPROM.put(0, creds);
  EEPROM.commit();
  savedSsid[0] = '\0';
  savedPass[0] = '\0';
}

// ---- WiFi modes ----
String deviceName() {
  String name = "ESP-Relay-" + String(ESP.getChipId() & 0xFFFF, HEX);
  name.toUpperCase();
  return name;
}

void applyHostname() {
  String name = deviceName();
  WiFi.hostname(name);
  Serial.print(F("Hostname: "));
  Serial.println(name);
}

void promoteToSta() {
  apMode = false;
  WiFi.setAutoReconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  Serial.print("STA connected. IP: ");
  Serial.println(WiFi.localIP());
}

bool connectSTA() {
  if (savedSsid[0] == '\0') {
    return false;
  }

  Serial.printf("Connecting to WiFi: %s\n", savedSsid);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  applyHostname();
  WiFi.setAutoReconnect(true);
  WiFi.begin(savedSsid, savedPass);
  lastStaAttemptMs = millis();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < STA_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    promoteToSta();
    return true;
  }

  Serial.println("STA connect failed.");
  WiFi.disconnect(/*wifioff=*/false);
  return false;
}

void startAP() {
  String apSsid = deviceName();

  // Keep STA available so saved credentials can reconnect in the background
  // (common after power outages when the router boots slower than the ESP).
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  applyHostname();
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  apMode = true;
  lastStaAttemptMs = millis();

  Serial.print("AP started: ");
  Serial.print(apSsid);
  Serial.print("  password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  if (savedSsid[0] != '\0') {
    Serial.println(F("Will keep retrying saved WiFi in background."));
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSsid, savedPass);
  }
}

void maintainWifi() {
  if (savedSsid[0] == '\0') {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (apMode) {
      promoteToSta();
    }
    return;
  }

  unsigned long now = millis();
  if (now - lastStaAttemptMs < STA_RETRY_MS) {
    return;
  }

  lastStaAttemptMs = now;
  Serial.printf("Retrying WiFi: %s\n", savedSsid);

  if (apMode) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  applyHostname();
  WiFi.setAutoReconnect(true);
  WiFi.begin(savedSsid, savedPass);
}

// ---- HTML ----
String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else if (c == '\t') {
      out += F("\\t");
    } else if ((uint8_t)c < 0x20) {
      char buf[7];
      snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

String buildPage(bool showWifiForm) {
  String html;
  html.reserve(5200);
  html += F(
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
    "<title>ESP Relay</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;"
    "background:#0f1419;color:#e7ecf1;min-height:100vh;padding:1.25rem}"
    "main{max-width:420px;margin:0 auto}"
    "h1{font-size:1.5rem;margin:0 0 .5rem;font-weight:700}"
    ".meta{color:#9aa7b5;font-size:.95rem;margin:0 0 1.25rem;line-height:1.4}"
    ".badge{display:inline-block;padding:.35rem .7rem;border-radius:999px;font-size:.85rem;font-weight:600}"
    ".on{background:#1a7f4b;color:#fff}"
    ".off{background:#5c6570;color:#fff}"
    ".row{display:flex;flex-direction:column;gap:.75rem;margin:1.25rem 0}"
    "a.btn,button{display:block;width:100%;text-align:center;text-decoration:none;"
    "border:0;border-radius:12px;padding:1rem 1.1rem;font-size:1.15rem;font-weight:700;"
    "cursor:pointer;-webkit-tap-highlight-color:transparent}"
    "a.onbtn{background:#22c55e;color:#052e16}"
    "a.offbtn{background:#ef4444;color:#fff}"
    "a.ghost{background:#1c2430;color:#e7ecf1}"
    "section{margin-top:1.75rem;padding-top:1.25rem;border-top:1px solid #243041}"
    "h2{font-size:1.1rem;margin:0 0 .75rem}"
    "label{display:block;font-size:.9rem;color:#9aa7b5;margin:0 0 .35rem}"
    "input{width:100%;padding:.9rem 1rem;border-radius:10px;border:1px solid #334155;"
    "background:#151b24;color:#e7ecf1;font-size:1.05rem;margin-bottom:.85rem}"
    "button.save{background:#3b82f6;color:#fff}"
    "button.scan{background:#1c2430;color:#e7ecf1;margin-bottom:.75rem}"
    "button.scan:disabled{opacity:.6}"
    "button.danger{background:#7f1d1d;color:#fecaca;margin-top:.5rem}"
    "p.hint{color:#9aa7b5;font-size:.85rem;line-height:1.4;margin:.5rem 0 0}"
    "#netStatus{color:#9aa7b5;font-size:.85rem;margin:0 0 .5rem;min-height:1.2em}"
    "#netList{list-style:none;margin:0 0 1rem;padding:0;max-height:220px;overflow:auto;"
    "border:1px solid #334155;border-radius:10px;background:#151b24}"
    "#netList:empty{display:none}"
    "#netList li{display:flex;justify-content:space-between;align-items:center;gap:.75rem;"
    "padding:.85rem 1rem;border-bottom:1px solid #243041;cursor:pointer}"
    "#netList li:last-child{border-bottom:0}"
    "#netList li:active,#netList li.sel{background:#1c2430}"
    "#netList .n{font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    "#netList .m{color:#9aa7b5;font-size:.8rem;flex-shrink:0}"
    "</style></head><body><main>"
  );

  html += F("<h1>ESP Relay</h1>");
  html += F("<p class=\"meta\">Mode: <strong>");
  html += apMode ? F("AP") : F("STA");
  html += F("</strong><br>IP: ");
  html += apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  html += F("</p>");

  html += F("<p><span class=\"badge ");
  html += relayOn ? F("on") : F("off");
  html += F("\">Relay ");
  html += relayOn ? F("ON") : F("OFF");
  html += F("</span></p>");

  html += F("<div class=\"row\">");
  html += F("<a class=\"btn onbtn\" href=\"/on\">Turn ON</a>");
  html += F("<a class=\"btn offbtn\" href=\"/off\">Turn OFF</a>");
  html += F("<a class=\"btn ghost\" href=\"/toggle\">Toggle</a>");
  html += F("</div>");

  if (showWifiForm) {
    html += F(
      "<section>"
      "<h2>WiFi settings</h2>"
      "<button class=\"scan\" type=\"button\" id=\"scanBtn\">Scan networks</button>"
      "<p id=\"netStatus\">Scanning…</p>"
      "<ul id=\"netList\"></ul>"
      "<form method=\"POST\" action=\"/wifi\">"
      "<label for=\"ssid\">SSID</label>"
      "<input id=\"ssid\" name=\"ssid\" type=\"text\" maxlength=\"32\" required "
      "autocapitalize=\"none\" autocomplete=\"username\" value=\""
    );
    html += htmlEscape(String(savedSsid));
    html += F(
      "\">"
      "<label for=\"pass\">Password</label>"
      "<input id=\"pass\" name=\"pass\" type=\"password\" maxlength=\"64\" "
      "autocomplete=\"current-password\">"
      "<button class=\"save\" type=\"submit\">Save &amp; Connect</button>"
      "</form>"
      "<form method=\"POST\" action=\"/reset-wifi\" "
      "onsubmit=\"return confirm('Clear saved WiFi and restart into AP mode?');\">"
      "<button class=\"danger\" type=\"submit\">Forget WiFi</button>"
      "</form>"
      "<p class=\"hint\">Tap a network to fill SSID, enter the password, then Save &amp; Connect. "
      "The device reboots and tries to join. If that fails, it returns to AP mode.</p>"
      "<script>"
      "var btn=document.getElementById('scanBtn'),"
      "st=document.getElementById('netStatus'),"
      "list=document.getElementById('netList'),"
      "ssid=document.getElementById('ssid'),"
      "pass=document.getElementById('pass');"
      "function pick(s,el){"
      "ssid.value=s;"
      "var els=list.querySelectorAll('li');"
      "for(var i=0;i<els.length;i++)els[i].classList.remove('sel');"
      "if(el)el.classList.add('sel');"
      "pass.focus();"
      "}"
      "function scan(){"
      "btn.disabled=true;"
      "st.textContent='Scanning…';"
      "list.innerHTML='';"
      "fetch('/scan').then(function(r){return r.json()}).then(function(nets){"
      "if(!nets.length){st.textContent='No networks found.';return;}"
      "st.textContent=nets.length+' network'+(nets.length===1?'':'s')+' — tap to select';"
      "nets.sort(function(a,b){return b.rssi-a.rssi});"
      "nets.forEach(function(n){"
      "if(!n.ssid)return;"
      "var li=document.createElement('li');"
      "var left=document.createElement('span');left.className='n';"
      "left.textContent=n.ssid+(n.secure?' (locked)':'');"
      "var right=document.createElement('span');right.className='m';"
      "right.textContent=n.rssi+' dBm';"
      "li.appendChild(left);li.appendChild(right);"
      "li.onclick=function(){pick(n.ssid,li)};"
      "list.appendChild(li);"
      "});"
      "}).catch(function(){st.textContent='Scan failed. Try again.';})"
      ".then(function(){btn.disabled=false;});"
      "}"
      "btn.onclick=scan;"
      "scan();"
      "</script>"
      "</section>"
    );
  } else {
    html += F("<p class=\"meta\"><a class=\"btn ghost\" href=\"/wifi\" style=\"margin-top:1rem\">WiFi settings</a></p>");
  }

  html += F("</main></body></html>");
  return html;
}

// ---- Handlers ----
void handleRoot() {
  server.send(200, "text/html", buildPage(apMode));
}

void handleWifiPage() {
  server.send(200, "text/html", buildPage(true));
}

void handleOn() {
  setRelay(true);
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleOff() {
  setRelay(false);
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleToggle() {
  setRelay(!relayOn);
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleStatus() {
  String json = F("{\"relay\":");
  json += relayOn ? F("true") : F("false");
  json += F(",\"mode\":\"");
  json += apMode ? F("AP") : F("STA");
  json += F("\"}");
  server.send(200, "application/json", json);
}

void handleScan() {
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  if (n < 0) {
    n = 0;
  }
  String json;
  json.reserve(n > 0 ? n * 64 : 16);
  json += '[';
  for (int i = 0; i < n; i++) {
    if (i > 0) {
      json += ',';
    }
    json += F("{\"ssid\":\"");
    json += jsonEscape(WiFi.SSID(i));
    json += F("\",\"rssi\":");
    json += WiFi.RSSI(i);
    json += F(",\"secure\":");
    json += (WiFi.encryptionType(i) != ENC_TYPE_NONE) ? F("true") : F("false");
    json += '}';
  }
  json += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleWifiSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > SSID_MAX) {
    server.send(400, "text/plain", "Invalid ssid");
    return;
  }
  if (pass.length() > PASS_MAX) {
    server.send(400, "text/plain", "Password too long");
    return;
  }

  saveWifi(ssid, pass);
  server.send(200, "text/html",
              F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<meta http-equiv=\"refresh\" content=\"3;url=/\">"
                "<title>Saved</title></head><body style=\"font-family:sans-serif;padding:1.5rem\">"
                "<p>WiFi saved. Rebooting…</p></body></html>"));
  delay(500);
  ESP.restart();
}

void handleResetWifi() {
  clearWifi();
  server.send(200, "text/html",
              F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>Reset</title></head><body style=\"font-family:sans-serif;padding:1.5rem\">"
                "<p>WiFi cleared. Rebooting into AP…</p></body></html>"));
  delay(500);
  ESP.restart();
}

void setupWebRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/reset-wifi", HTTP_POST, handleResetWifi);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("ESP Web Relay"));

  EEPROM.begin(EEPROM_SIZE);
  bool hasCreds = loadWifi();

  if (hasCreds && connectSTA()) {
    // STA mode
  } else {
    startAP();
  }

  setupWebRoutes();
  server.begin();
  Serial.println(F("HTTP server started"));
}

void loop() {
  server.handleClient();
  maintainWifi();
}
