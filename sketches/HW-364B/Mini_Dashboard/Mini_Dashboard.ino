/*
 * Mini Dashboard - HW-364B (ESP8266 + OLED integre)
 *
 * Dashboard web avec controles (sliders, boutons, toggles).
 * L'ecran OLED affiche l'etat en temps reel.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "credentials.h"

// Configuration OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

#define YELLOW_ZONE_HEIGHT 16

// Serveur web
ESP8266WebServer server(80);

// Etat du dashboard
struct DashboardState {
  uint8_t brightness;    // 0-100
  uint8_t temperature;   // 16-30
  uint8_t mode;          // 0=Eco, 1=Normal, 2=Turbo
  bool ledOn;
  bool soundOn;
  uint16_t counter;
  char message[32];
} state = {50, 22, 1, false, true, 0, "Bienvenue!"};

unsigned long lastDisplayUpdate = 0;
bool displayNeedsUpdate = true;

// === Page Web (stockee en PROGMEM) ===
const char HTML_PART1[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Mini Dashboard</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, sans-serif;
      margin: 0; padding: 1rem;
      background: linear-gradient(135deg, #1a1a2e, #16213e);
      color: white; min-height: 100vh;
    }
    h1 { text-align: center; margin-bottom: 1.5rem; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 1rem;
      max-width: 800px;
      margin: 0 auto;
    }
    .card {
      background: rgba(255,255,255,0.1);
      border-radius: 1rem;
      padding: 1.2rem;
    }
    .card h3 { margin: 0 0 1rem 0; opacity: 0.8; font-size: 0.9rem; }
    .slider-container { display: flex; align-items: center; gap: 1rem; }
    .slider {
      flex: 1; height: 8px; -webkit-appearance: none;
      background: rgba(255,255,255,0.2); border-radius: 4px; outline: none;
    }
    .slider::-webkit-slider-thumb {
      -webkit-appearance: none; width: 24px; height: 24px;
      background: #4CAF50; border-radius: 50%; cursor: pointer;
    }
    .value { min-width: 50px; text-align: right; font-size: 1.2rem; font-weight: bold; }
    .btn-group { display: flex; gap: 0.5rem; }
    .btn {
      flex: 1; padding: 0.8rem; border: none; border-radius: 0.5rem;
      font-size: 1rem; cursor: pointer; transition: all 0.2s;
      background: rgba(255,255,255,0.1); color: white;
    }
    .btn:hover { background: rgba(255,255,255,0.2); }
    .btn.active { background: #4CAF50; }
    .toggle {
      width: 60px; height: 32px; border-radius: 16px;
      background: #555; position: relative; cursor: pointer; transition: 0.3s;
    }
    .toggle.on { background: #4CAF50; }
    .toggle::after {
      content: ""; position: absolute; width: 26px; height: 26px;
      background: white; border-radius: 50%; top: 3px; left: 3px; transition: 0.3s;
    }
    .toggle.on::after { left: 31px; }
    .toggle-row { display: flex; justify-content: space-between; align-items: center; margin: 0.8rem 0; }
    .toggle-label { font-size: 1.1rem; }
    .counter-btn {
      width: 100%; padding: 1.5rem; font-size: 2rem; font-weight: bold;
      background: linear-gradient(135deg, #667eea, #764ba2);
      border: none; border-radius: 0.5rem; color: white; cursor: pointer;
    }
    .counter-btn:active { transform: scale(0.98); }
    input[type="text"] {
      width: 100%; padding: 0.8rem; border: none; border-radius: 0.5rem;
      font-size: 1rem; margin-bottom: 0.5rem;
    }
    .send-btn {
      width: 100%; padding: 0.8rem; background: #e91e63;
      border: none; border-radius: 0.5rem; color: white; font-size: 1rem; cursor: pointer;
    }
    .info { text-align: center; margin-top: 1.5rem; opacity: 0.5; font-size: 0.8rem; }
  </style>
</head>
<body>
  <h1>Mini Dashboard</h1>
  <div class="grid">
    <div class="card">
      <h3>LUMINOSITE</h3>
      <div class="slider-container">
        <input type="range" class="slider" id="brightness" min="0" max="100" value=")=====";

const char HTML_PART2[] PROGMEM = R"=====(">
        <span class="value" id="brightnessVal">)=====";

const char HTML_PART3[] PROGMEM = R"=====(%</span>
      </div>
    </div>
    <div class="card">
      <h3>TEMPERATURE</h3>
      <div class="slider-container">
        <input type="range" class="slider" id="temp" min="16" max="30" value=")=====";

const char HTML_PART4[] PROGMEM = R"=====(">
        <span class="value" id="tempVal">)=====";

const char HTML_PART5[] PROGMEM = R"=====(&deg;C</span>
      </div>
    </div>
    <div class="card">
      <h3>MODE</h3>
      <div class="btn-group" id="modeGrp">
        <button class="btn" data-mode="0">Eco</button>
        <button class="btn" data-mode="1">Normal</button>
        <button class="btn" data-mode="2">Turbo</button>
      </div>
    </div>
    <div class="card">
      <h3>CONTROLES</h3>
      <div class="toggle-row">
        <span class="toggle-label">LED</span>
        <div class="toggle" id="led"></div>
      </div>
      <div class="toggle-row">
        <span class="toggle-label">Son</span>
        <div class="toggle" id="sound"></div>
      </div>
    </div>
    <div class="card">
      <h3>COMPTEUR</h3>
      <button class="counter-btn" id="counter">0</button>
    </div>
    <div class="card">
      <h3>MESSAGE</h3>
      <input type="text" id="msg" placeholder="Texte a afficher..." maxlength="30">
      <button class="send-btn" id="sendBtn">Envoyer</button>
    </div>
  </div>
  <div class="info" id="info">Connexion...</div>
<script>
var brightness=)=====";

const char HTML_PART6[] PROGMEM = R"=====(,temperature=)=====";
const char HTML_PART7[] PROGMEM = R"=====(,mode=)=====";
const char HTML_PART8[] PROGMEM = R"=====(,ledOn=)=====";
const char HTML_PART9[] PROGMEM = R"=====(,soundOn=)=====";
const char HTML_PART10[] PROGMEM = R"=====(,counter=)=====";

const char HTML_SCRIPT[] PROGMEM = R"=====(;
function $(id){return document.getElementById(id);}
function upd(p,v){
  var x=new XMLHttpRequest();
  x.open("GET","/api?"+p+"="+v,true);
  x.onload=function(){if(x.status==200){var d=JSON.parse(x.responseText);sync(d);}};
  x.send();
}
function sync(d){
  $("brightness").value=d.brightness;
  $("brightnessVal").textContent=d.brightness+"%";
  $("temp").value=d.temperature;
  $("tempVal").textContent=d.temperature+"\u00B0C";
  var btns=$("modeGrp").getElementsByClassName("btn");
  for(var i=0;i<btns.length;i++){
    btns[i].className=btns[i].getAttribute("data-mode")==d.mode?"btn active":"btn";
  }
  $("led").className=d.ledOn?"toggle on":"toggle";
  $("sound").className=d.soundOn?"toggle on":"toggle";
  $("counter").textContent=d.counter;
  $("info").textContent="IP: "+d.ip+" | RSSI: "+d.rssi+" dBm";
}
$("brightness").oninput=function(){$("brightnessVal").textContent=this.value+"%";};
$("brightness").onchange=function(){upd("brightness",this.value);};
$("temp").oninput=function(){$("tempVal").textContent=this.value+"\u00B0C";};
$("temp").onchange=function(){upd("temperature",this.value);};
var btns=$("modeGrp").getElementsByClassName("btn");
for(var i=0;i<btns.length;i++){
  btns[i].onclick=function(){upd("mode",this.getAttribute("data-mode"));};
}
$("led").onclick=function(){upd("led","toggle");};
$("sound").onclick=function(){upd("sound","toggle");};
$("counter").onclick=function(){upd("counter","inc");};
$("sendBtn").onclick=function(){var m=$("msg").value;if(m)upd("message",encodeURIComponent(m));};
$("msg").onkeypress=function(e){if(e.key=="Enter"){$("sendBtn").click();}};
sync({brightness:brightness,temperature:temperature,mode:mode,ledOn:ledOn,soundOn:soundOn,counter:counter,ip:"...",rssi:0});
setInterval(function(){
  var x=new XMLHttpRequest();
  x.open("GET","/api",true);
  x.onload=function(){if(x.status==200){sync(JSON.parse(x.responseText));}};
  x.send();
},2000);
</script>
</body>
</html>
)=====";

void handleRoot() {
  String html;
  html.reserve(4000);

  html += FPSTR(HTML_PART1);
  html += String(state.brightness);
  html += FPSTR(HTML_PART2);
  html += String(state.brightness);
  html += FPSTR(HTML_PART3);
  html += String(state.temperature);
  html += FPSTR(HTML_PART4);
  html += String(state.temperature);
  html += FPSTR(HTML_PART5);
  html += String(state.brightness);
  html += FPSTR(HTML_PART6);
  html += String(state.temperature);
  html += FPSTR(HTML_PART7);
  html += String(state.mode);
  html += FPSTR(HTML_PART8);
  html += String(state.ledOn ? "true" : "false");
  html += FPSTR(HTML_PART9);
  html += String(state.soundOn ? "true" : "false");
  html += FPSTR(HTML_PART10);
  html += String(state.counter);
  html += FPSTR(HTML_SCRIPT);

  server.send(200, "text/html", html);
}

void handleApi() {
  // Traitement des parametres
  if (server.hasArg("brightness")) {
    state.brightness = constrain(server.arg("brightness").toInt(), 0, 100);
    displayNeedsUpdate = true;
  }
  if (server.hasArg("temperature")) {
    state.temperature = constrain(server.arg("temperature").toInt(), 16, 30);
    displayNeedsUpdate = true;
  }
  if (server.hasArg("mode")) {
    state.mode = constrain(server.arg("mode").toInt(), 0, 2);
    displayNeedsUpdate = true;
  }
  if (server.hasArg("led")) {
    state.ledOn = !state.ledOn;
    displayNeedsUpdate = true;
  }
  if (server.hasArg("sound")) {
    state.soundOn = !state.soundOn;
    displayNeedsUpdate = true;
  }
  if (server.hasArg("counter")) {
    state.counter++;
    displayNeedsUpdate = true;
  }
  if (server.hasArg("message")) {
    String msg = server.arg("message");
    msg.toCharArray(state.message, sizeof(state.message));
    displayNeedsUpdate = true;
  }

  // Reponse JSON
  String json = "{";
  json += "\"brightness\":" + String(state.brightness) + ",";
  json += "\"temperature\":" + String(state.temperature) + ",";
  json += "\"mode\":" + String(state.mode) + ",";
  json += "\"ledOn\":" + String(state.ledOn ? "true" : "false") + ",";
  json += "\"soundOn\":" + String(state.soundOn ? "true" : "false") + ",";
  json += "\"counter\":" + String(state.counter) + ",";
  json += "\"message\":\"" + String(state.message) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI());
  json += "}";

  server.send(200, "application/json", json);
}

void setupWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "Dashboard";
  int x = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(x, 13, title);
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(5, 35, "Connexion WiFi...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  u8g2.drawStr(5, 50, WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
  delay(1500);
}

void drawProgressBar(int x, int y, int w, int h, int percent) {
  u8g2.drawFrame(x, y, w, h);
  int fillW = (w - 2) * percent / 100;
  if (fillW > 0) {
    u8g2.drawBox(x + 1, y + 1, fillW, h - 2);
  }
}

void updateDisplay() {
  u8g2.clearBuffer();

  // === ZONE JAUNE ===
  u8g2.setFont(u8g2_font_6x10_tr);

  // Message centre
  int msgWidth = u8g2.getStrWidth(state.message);
  int msgX = (128 - msgWidth) / 2;
  if (msgX < 0) msgX = 0;
  u8g2.drawStr(msgX, 12, state.message);

  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE ===

  // Ligne 1: Luminosite
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 28, "Lum");
  drawProgressBar(25, 20, 60, 10, state.brightness);
  char buf[10];
  sprintf(buf, "%3d%%", state.brightness);
  u8g2.drawStr(90, 28, buf);

  // Ligne 2: Temperature
  u8g2.drawStr(0, 40, "Tmp");
  int tempPercent = (state.temperature - 16) * 100 / 14;
  drawProgressBar(25, 32, 60, 10, tempPercent);
  sprintf(buf, "%2dC", state.temperature);
  u8g2.drawStr(93, 40, buf);
  u8g2.drawCircle(104, 33, 1);

  // Ligne 3: Mode + Toggles
  const char* modes[] = {"ECO ", "NORM", "TURB"};
  u8g2.drawStr(0, 52, modes[state.mode]);

  // LED indicator
  u8g2.drawStr(40, 52, "LED");
  if (state.ledOn) {
    u8g2.drawDisc(62, 48, 4);
  } else {
    u8g2.drawCircle(62, 48, 4);
  }

  // Sound indicator
  u8g2.drawStr(75, 52, "SND");
  if (state.soundOn) {
    u8g2.drawDisc(97, 48, 4);
  } else {
    u8g2.drawCircle(97, 48, 4);
  }

  // Compteur a droite
  sprintf(buf, "#%d", state.counter);
  int cntX = 128 - u8g2.getStrWidth(buf);
  u8g2.drawStr(cntX, 52, buf);

  // Ligne 4: IP
  u8g2.setFont(u8g2_font_5x7_tr);
  String ip = WiFi.localIP().toString();
  int ipX = (128 - u8g2.getStrWidth(ip.c_str())) / 2;
  u8g2.drawStr(ipX, 63, ip.c_str());

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nMini Dashboard - HW-364B");

  u8g2.begin();
  setupWiFi();

  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.begin();

  Serial.print("Dashboard: http://");
  Serial.println(WiFi.localIP());

  displayNeedsUpdate = true;
}

void loop() {
  server.handleClient();

  // Mise a jour affichage si necessaire ou toutes les secondes
  if (displayNeedsUpdate || millis() - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = millis();
    displayNeedsUpdate = false;
    updateDisplay();
  }
}
