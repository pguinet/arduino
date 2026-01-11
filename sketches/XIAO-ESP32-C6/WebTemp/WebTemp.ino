#include <WiFi.h>
#include <WebServer.h>
#include "credentials.h"

#define LED_PIN 15  // LED interne XIAO ESP32-C6

WebServer server(80);
bool ledState = false;

float getTemperature() {
  return temperatureRead();  // Capteur interne ESP32
}

void handleRoot() {
  float temp = getTemperature();

  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="5">
  <title>ESP32-C6 Temperature</title>
  <style>
    body {
      font-family: -apple-system, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      margin: 0;
      background: linear-gradient(135deg, #1e3c72, #2a5298);
      color: white;
    }
    .card {
      background: rgba(255,255,255,0.1);
      padding: 3rem;
      border-radius: 1rem;
      text-align: center;
      backdrop-filter: blur(10px);
    }
    .temp {
      font-size: 5rem;
      font-weight: bold;
      margin: 1rem 0;
    }
    .label { opacity: 0.8; }
    .info { font-size: 0.9rem; opacity: 0.6; margin-top: 2rem; }
    .btn {
      margin-top: 1.5rem;
      padding: 1rem 2rem;
      font-size: 1.2rem;
      border: none;
      border-radius: 0.5rem;
      cursor: pointer;
      transition: all 0.3s;
    }
    .btn-on { background: #4CAF50; color: white; }
    .btn-off { background: #f44336; color: white; }
    .btn:hover { transform: scale(1.05); }
  </style>
</head>
<body>
  <div class="card">
    <div class="label">Temperature interne</div>
    <div class="temp">)" + String(temp, 1) + R"(&deg;C</div>
    <div class="label">XIAO ESP32-C6</div>
    <button class="btn )" + String(ledState ? "btn-on" : "btn-off") + R"(" onclick="location.href='/led'">
      LED: )" + String(ledState ? "ON" : "OFF") + R"(
    </button>
    <div class="info">Actualisation auto toutes les 5s</div>
  </div>
</body>
</html>
)";

  server.send(200, "text/html", html);
}

void handleApi() {
  String json = "{\"temperature\":" + String(getTemperature(), 1) + ",\"led\":" + String(ledState ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleLed() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.print("Connexion WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/api/temp", handleApi);
  server.on("/led", handleLed);
  server.begin();

  Serial.println("Serveur web demarre");
}

void loop() {
  server.handleClient();
}
