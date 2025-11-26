#include <NewPing.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <WiFiManager.h>

// --- Pines ---
#define TRIG_PIN_1 4
#define ECHO_PIN_1 5
#define TRIG_PIN_2 16
#define ECHO_PIN_2 17
#define TRIG_PIN_3 18
#define ECHO_PIN_3 19
#define MAX_DISTANCE 200

#define IR_ENTRADA_PIN 15
#define IR_SALIDA_PIN 14

#define SERVO_ENTRADA_PIN 13
#define SERVO_SALIDA_PIN 12

#define RESET_WIFI_PIN 0 // Botón físico para resetear WiFi (opcional)

// --- Variables ---
Servo puertaEntrada;
Servo puertaSalida;

NewPing sonar1(TRIG_PIN_1, ECHO_PIN_1, MAX_DISTANCE);
NewPing sonar2(TRIG_PIN_2, ECHO_PIN_2, MAX_DISTANCE);
NewPing sonar3(TRIG_PIN_3, ECHO_PIN_3, MAX_DISTANCE);

bool entradaAbierta = false;
bool salidaAbierta = false;

bool modoManual = false;

// --- MQTT HiveMQ ---
const char* mqttServer = "33602f86bce34f23b85e3669cc41f0a6.s1.eu.hivemq.cloud";
const int mqttPort = 8883;
const char* mqttUser = "esp32_user";
const char* mqttPassword = "18122002Pi";

// --- Objetos globales ---
WiFiClientSecure espClient;
PubSubClient client(espClient);
Preferences preferences;

// --- Funciones para compuerta entrada ---
void abrirEntrada() {
  for (int angulo = 0; angulo <= 90; angulo += 5) {
    puertaEntrada.write(angulo);
    delay(20);
  }
  entradaAbierta = true;
}

void cerrarEntrada() {
  for (int angulo = 90; angulo >= 0; angulo -= 5) {
    puertaEntrada.write(angulo);
    delay(20);
  }
  entradaAbierta = false;
}

// --- Funciones para compuerta salida ---
void abrirSalida() {
  for (int angulo = 0; angulo <= 90; angulo += 5) {
    puertaSalida.write(angulo);
    delay(20);
  }
  salidaAbierta = true;
}

void cerrarSalida() {
  for (int angulo = 90; angulo >= 0; angulo -= 5) {
    puertaSalida.write(angulo);
    delay(20);
  }
  salidaAbierta = false;
}

// --- Reset WiFi ---
void resetearWiFi() {
  Serial.println("🧹 Borrando configuración WiFi guardada...");
  WiFi.disconnect(true, true);
  delay(1000);
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
  Serial.println("✅ Configuración WiFi borrada. Reiniciando...");
  delay(1500);
  ESP.restart();
}

// --- Setup WiFi ---
void setupWiFi() {
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_WIFI_PIN) == LOW) {
    Serial.println("🔄 Reiniciando configuración WiFi...");
    preferences.begin("wifi", false);
    preferences.clear();
    preferences.end();
    delay(1000);
  }

  preferences.begin("wifi", false);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid == "") {
    Serial.println("⚙ No hay red guardada, iniciando WiFiManager...");
    WiFiManager wm;
    bool res = wm.autoConnect("Configurar-ESP32");
    if (!res) {
      Serial.println("❌ No se pudo conectar, reiniciando...");
      ESP.restart();
    } else {
      Serial.println("✅ Conectado por WiFiManager");
      preferences.begin("wifi", false);
      preferences.putString("ssid", WiFi.SSID());
      preferences.putString("pass", WiFi.psk());
      preferences.end();
    }
  } else {
    Serial.println("🔁 Conectando a red guardada...");
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\n⚠ No se pudo conectar, abriendo portal...");
      WiFiManager wm;
      bool res = wm.autoConnect("Configurar-ESP32");
      if (res) {
        preferences.begin("wifi", false);
        preferences.putString("ssid", WiFi.SSID());
        preferences.putString("pass", WiFi.psk());
        preferences.end();
      }
    }
  }
  Serial.println("\n✅ WiFi conectado: " + WiFi.SSID());
  Serial.println("IP: " + WiFi.localIP().toString());
}

// --- MQTT callback ---
void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Mensaje recibido en el tópico: ");
  Serial.println(topic);
  String payload;
  for (unsigned int i = 0; i < length; i++) {
    payload += (char)message[i];
  }

  if (String(topic) == "estacionamiento/wifi/config") {
    Serial.println("📡 Configuración WiFi recibida: " + payload);
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      String newSSID = doc["ssid"];
      String newPASS = doc["pass"];
      Serial.println("🔧 Nueva red WiFi:");
      Serial.println("SSID: " + newSSID);
      Serial.println("PASS: " + newPASS);
      preferences.begin("wifi", false);
      preferences.putString("ssid", newSSID);
      preferences.putString("pass", newPASS);
      preferences.end();
      Serial.println("💾 Configuración guardada. Reiniciando ESP...");
      delay(1500);
      ESP.restart();
    } else {
      Serial.println("❌ Error al parsear JSON");
    }
  }

  if (String(topic) == "estacionamiento/puerta/control") {
    if (payload == "abrirEntrada") {
      abrirEntrada();
      entradaAbierta = true;
      modoManual = true;
    } else if (payload == "cerrarEntrada") {
      cerrarEntrada();
      entradaAbierta = false;
      modoManual = true;
    } else if (payload == "abrirSalida") {
      abrirSalida();
      salidaAbierta = true;
      modoManual = true;
    } else if (payload == "cerrarSalida") {
      cerrarSalida();
      salidaAbierta = false;
      modoManual = true;
    } else if (payload == "auto") {
      modoManual = false;
    } else if (payload == "manual") {
      modoManual = true;
    }
  }
}

// --- Conexión MQTT ---
void conectarMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  espClient.setInsecure();
  Serial.print("Conectando a MQTT...");
  while (!client.connected()) {
    if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
      Serial.println("✅ Conectado a MQTT");
      client.subscribe("estacionamiento/puerta/control");
      client.subscribe("estacionamiento/wifi/config");
    } else {
      Serial.print("Falló, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  puertaEntrada.attach(SERVO_ENTRADA_PIN);
  puertaEntrada.write(0);
  puertaSalida.attach(SERVO_SALIDA_PIN);
  puertaSalida.write(0);

  pinMode(IR_ENTRADA_PIN, INPUT);
  pinMode(IR_SALIDA_PIN, INPUT);
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);

  setupWiFi();
  conectarMQTT();

  Serial.println("🚗 Sistema de estacionamiento con 3 plazas listo");
}

// --- Loop ---
void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();

  int irEntrada = digitalRead(IR_ENTRADA_PIN);
  int irSalida = digitalRead(IR_SALIDA_PIN);

  if (!modoManual) {
    if (irEntrada == LOW && !entradaAbierta) {
      abrirEntrada();
      entradaAbierta = true;
    } else if (irEntrada == HIGH && entradaAbierta) {
      cerrarEntrada();
      entradaAbierta = false;
    }

    if (irSalida == LOW && !salidaAbierta) {
      abrirSalida();
      salidaAbierta = true;
    } else if (irSalida == HIGH && salidaAbierta) {
      cerrarSalida();
      salidaAbierta = false;
    }
  }

  unsigned int distancia1 = sonar1.ping_cm();
  unsigned int distancia2 = sonar2.ping_cm();
  unsigned int distancia3 = sonar3.ping_cm();

  if (distancia1 == 0) distancia1 = MAX_DISTANCE;
  if (distancia2 == 0) distancia2 = MAX_DISTANCE;
  if (distancia3 == 0) distancia3 = MAX_DISTANCE;

  bool ocupado1 = distancia1 <= 15;
  bool ocupado2 = distancia2 <= 15;
  bool ocupado3 = distancia3 <= 15;

  // Publicación MQTT del estado de plazas y compuertas

  char msgPlaza1[128];
  snprintf(msgPlaza1, sizeof(msgPlaza1), "{\"ocupado\":%s,\"distancia\":%u,\"entradaAbierta\":%s,\"salidaAbierta\":%s}",
           ocupado1 ? "true" : "false", distancia1, entradaAbierta ? "true" : "false", salidaAbierta ? "true" : "false");
  client.publish("estacionamiento/plaza1/estado", msgPlaza1);

  char msgPlaza2[128];
  snprintf(msgPlaza2, sizeof(msgPlaza2), "{\"ocupado\":%s,\"distancia\":%u,\"entradaAbierta\":%s,\"salidaAbierta\":%s}",
           ocupado2 ? "true" : "false", distancia2, entradaAbierta ? "true" : "false", salidaAbierta ? "true" : "false");
  client.publish("estacionamiento/plaza2/estado", msgPlaza2);

  char msgPlaza3[128];
  snprintf(msgPlaza3, sizeof(msgPlaza3), "{\"ocupado\":%s,\"distancia\":%u,\"entradaAbierta\":%s,\"salidaAbierta\":%s}",
           ocupado3 ? "true" : "false", distancia3, entradaAbierta ? "true" : "false", salidaAbierta ? "true" : "false");
  client.publish("estacionamiento/plaza3/estado", msgPlaza3);

  Serial.printf("P1: %u cm (%s) | P2: %u cm (%s) | P3: %u cm (%s) | Entrada: %s | Salida: %s\n",
                distancia1, ocupado1 ? "Ocupado" : "Libre",
                distancia2, ocupado2 ? "Ocupado" : "Libre",
                distancia3, ocupado3 ? "Ocupado" : "Libre",
                entradaAbierta ? "Abierta" : "Cerrada",
                salidaAbierta ? "Abierta" : "Cerrada");

  delay(2000);
}