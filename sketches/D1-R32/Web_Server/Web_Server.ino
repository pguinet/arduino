/*
 * Web_Server - WEMOS D1 R32 (ESP32)
 *
 * Petit serveur web : page d'accueil avec uptime, RSSI WiFi, free heap,
 * et boutons pour controler la LED integree (pin 2).
 * L'URL est affichee sur le moniteur serie au demarrage.
 *
 * Board: WEMOS D1 R32
 * FQBN: esp32:esp32:d1_uno32
 *
 * @dependencies (aucune)
 */

#include <WiFi.h>
#include <WebServer.h>
#include "credentials.h"

#define LED_PIN 2

WebServer server(80);
bool ledState = false;

String htmlPage() {
  String s = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>D1 R32 Demo</title>";
  s += "<style>";
  s += "body{font-family:sans-serif;background:#1e1e2e;color:#cdd6f4;margin:0;padding:20px;}";
  s += "h1{color:#89b4fa;}";
  s += ".card{background:#313244;padding:20px;border-radius:10px;margin:15px 0;}";
  s += ".led{width:80px;height:80px;border-radius:50%;margin:20px auto;";
  s += "background:" + String(ledState ? "#fab387;box-shadow:0 0 40px #fab387;" : "#45475a;") + "}";
  s += "a.btn{display:inline-block;background:#89b4fa;color:#1e1e2e;padding:12px 24px;";
  s += "text-decoration:none;border-radius:6px;font-weight:bold;margin:5px;}";
  s += "a.btn:hover{background:#74c7ec;}";
  s += ".stat{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #45475a;}";
  s += "</style></head><body>";

  s += "<h1>ESP32 - Club Domontois</h1>";

  s += "<div class='card' style='text-align:center'>";
  s += "<h2>LED integree</h2>";
  s += "<div class='led'></div>";
  s += "<p>Etat : <b>" + String(ledState ? "ALLUMEE" : "ETEINTE") + "</b></p>";
  s += "<a class='btn' href='/on'>Allumer</a>";
  s += "<a class='btn' href='/off'>Eteindre</a>";
  s += "<a class='btn' href='/toggle'>Toggle</a>";
  s += "</div>";

  s += "<div class='card'>";
  s += "<h2>Infos ESP32</h2>";
  s += "<div class='stat'><span>Uptime</span><span>" + String(millis() / 1000) + " s</span></div>";
  s += "<div class='stat'><span>WiFi RSSI</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
  s += "<div class='stat'><span>SSID</span><span>" + WiFi.SSID() + "</span></div>";
  s += "<div class='stat'><span>IP</span><span>" + WiFi.localIP().toString() + "</span></div>";
  s += "<div class='stat'><span>MAC</span><span>" + WiFi.macAddress() + "</span></div>";
  s += "<div class='stat'><span>Free heap</span><span>" + String(ESP.getFreeHeap()) + " bytes</span></div>";
  s += "<div class='stat'><span>CPU freq</span><span>" + String(ESP.getCpuFreqMHz()) + " MHz</span></div>";
  s += "</div>";

  s += "</body></html>";
  return s;
}

void handleRoot()    { server.send(200, "text/html; charset=utf-8", htmlPage()); }
void handleOn()      { ledState = true;  digitalWrite(LED_PIN, HIGH); server.sendHeader("Location", "/"); server.send(303); }
void handleOff()     { ledState = false; digitalWrite(LED_PIN, LOW);  server.sendHeader("Location", "/"); server.send(303); }
void handleToggle()  { ledState = !ledState; digitalWrite(LED_PIN, ledState); server.sendHeader("Location", "/"); server.send(303); }

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connecte ! Adresse : http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/toggle", handleToggle);
  server.begin();
  Serial.println("Serveur HTTP demarre");
}

void loop() {
  server.handleClient();
}
