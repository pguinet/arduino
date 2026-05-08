/*
 * Matrix_Paint - Arduino UNO R4 WiFi
 *
 * Serveur web qui expose une grille 12x8 cliquable depuis le navigateur :
 * chaque clic toggle un pixel sur la matrice LED integree.
 * Plusieurs clients peuvent peindre simultanement (sync via polling).
 * Boutons Clear / Fill / Coeur en preset.
 *
 * Board: Arduino UNO R4 WiFi
 * FQBN: arduino:renesas_uno:unor4wifi
 *
 * @dependencies (aucune)
 */

#include <WiFiS3.h>
#include "Arduino_LED_Matrix.h"
#include "credentials.h"

ArduinoLEDMatrix matrix;
WiFiServer server(80);

uint32_t frame[3] = {0, 0, 0};

const uint32_t HEART_FRAME[3] = {0x3184a448, 0x42081100, 0xa0040000};

void setBit(int idx, bool on) {
  if (idx < 0 || idx >= 96) return;
  int w = idx / 32;
  int b = 31 - (idx % 32);
  if (on) frame[w] |= (1UL << b);
  else    frame[w] &= ~(1UL << b);
}

bool getBit(int idx) {
  if (idx < 0 || idx >= 96) return false;
  int w = idx / 32;
  int b = 31 - (idx % 32);
  return (frame[w] >> b) & 1UL;
}

void toggleBit(int idx) {
  setBit(idx, !getBit(idx));
}

void render() {
  matrix.loadFrame(frame);
}

void sendHTML(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: text/html; charset=utf-8");
  c.println("Connection: close");
  c.println();
  c.println(F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>UNO R4 - Matrix Paint</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1e1e2e;color:#cdd6f4;text-align:center;padding:10px;margin:0;}"
    "h1{color:#89b4fa;margin:10px 0;}"
    ".grid{display:grid;grid-template-columns:repeat(12,1fr);gap:4px;max-width:480px;margin:20px auto;}"
    ".cell{aspect-ratio:1;background:#45475a;border-radius:4px;cursor:pointer;transition:.1s;}"
    ".cell.on{background:#f38ba8;box-shadow:0 0 8px #f38ba8;}"
    ".btn{background:#89b4fa;color:#1e1e2e;padding:10px 20px;border:none;border-radius:6px;margin:5px;font-weight:bold;cursor:pointer;font-size:16px;}"
    ".btn:hover{background:#74c7ec;}"
    "</style></head><body>"
    "<h1>Club Domontois - Matrix Paint</h1>"
    "<p>Clique sur un pixel pour le toggler. Plusieurs telephones peuvent jouer ensemble.</p>"
    "<div class='grid' id='g'></div>"
    "<button class='btn' onclick='cmd(\"/clear\")'>Clear</button>"
    "<button class='btn' onclick='cmd(\"/fill\")'>Fill</button>"
    "<button class='btn' onclick='cmd(\"/heart\")'>Coeur</button>"
    "<script>"
    "const g=document.getElementById('g');"
    "let s=new Array(96).fill(0);"
    "function render(){g.innerHTML='';for(let i=0;i<96;i++){const c=document.createElement('div');"
    "c.className='cell'+(s[i]?' on':'');c.onclick=()=>tog(i);g.appendChild(c);}}"
    "async function tog(i){s[i]=s[i]?0:1;render();await fetch('/toggle?i='+i);}"
    "async function cmd(u){await fetch(u);await refresh();}"
    "async function refresh(){try{const r=await fetch('/state');const j=await r.json();s=j;render();}catch(e){}}"
    "setInterval(refresh,800);render();refresh();"
    "</script></body></html>"
  ));
}

void sendJSON(WiFiClient& c, const String& body) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: application/json");
  c.println("Connection: close");
  c.println();
  c.println(body);
}

void sendOK(WiFiClient& c) {
  c.println("HTTP/1.1 204 No Content");
  c.println("Connection: close");
  c.println();
}

String stateJSON() {
  String s = "[";
  for (int i = 0; i < 96; i++) {
    if (i) s += ',';
    s += getBit(i) ? '1' : '0';
  }
  s += ']';
  return s;
}

void handleRequest(WiFiClient& c, const String& req) {
  if (req.startsWith("GET / ") || req.startsWith("GET /index")) {
    sendHTML(c);
  } else if (req.startsWith("GET /state")) {
    sendJSON(c, stateJSON());
  } else if (req.startsWith("GET /toggle?i=")) {
    int idx = req.substring(14).toInt();
    toggleBit(idx);
    render();
    sendOK(c);
  } else if (req.startsWith("GET /clear")) {
    frame[0] = frame[1] = frame[2] = 0;
    render();
    sendOK(c);
  } else if (req.startsWith("GET /fill")) {
    frame[0] = frame[1] = frame[2] = 0xFFFFFFFF;
    render();
    sendOK(c);
  } else if (req.startsWith("GET /heart")) {
    memcpy(frame, HEART_FRAME, sizeof(frame));
    render();
    sendOK(c);
  } else {
    c.println("HTTP/1.1 404 Not Found");
    c.println("Connection: close");
    c.println();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Matrix Paint - UNO R4 WiFi ===");

  matrix.begin();
  render();

  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  // Attente de l'attribution d'IP par DHCP
  IPAddress ip = WiFi.localIP();
  while (ip == IPAddress(0, 0, 0, 0)) {
    delay(200);
    Serial.print("?");
    ip = WiFi.localIP();
  }
  Serial.println();
  Serial.print("Connecte. Ouvre : http://");
  Serial.println(ip);

  // Affiche un coeur a l'init pour montrer que tout marche
  memcpy(frame, HEART_FRAME, sizeof(frame));
  render();

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  String req;
  unsigned long start = millis();
  while (client.connected() && millis() - start < 2000) {
    if (client.available()) {
      char ch = client.read();
      req += ch;
      if (req.endsWith("\r\n\r\n")) break;
    }
  }

  if (req.length() > 0) {
    handleRequest(client, req);
  }
  delay(2);
  client.stop();
}
