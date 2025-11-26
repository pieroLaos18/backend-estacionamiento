/**
 * @file now_esp32.ino
 * @brief Firmware para el controlador ESP32 del sistema de estacionamiento inteligente.
 * 
 * Este código maneja:
 * - Conexión WiFi con WiFiManager (portal cautivo)
 * - Conexión MQTT a HiveMQ Cloud
 * - Lectura de sensores ultrasónicos (HC-SR04) para detección de ocupación
 * - Lectura de sensores IR para detección de entrada/salida
 * - Control de servomotores para barreras
 * - Lógica de estabilidad para evitar falsos positivos
 * - Publicación de eventos y estados a MQTT
 * 
 * @author Rodrigo Quiroz
 * @date 2025-11-26
 */

#include <NewPing.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <WiFiManager.h>

// ============================================
// CONFIGURACIÓN DE HARDWARE
// ============================================
#define HARDWARE_COMPLETO 0  // 0 = Solo 1 plaza, 1 = 3 plazas completas
#define DETECCION_SALIDA_AUTO 1  // 1 = Detectar salida automáticamente, 0 = Solo por IR

// ============================================
// PINES
// ============================================
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

#define RESET_WIFI_PIN 0  // Botón físico para resetear WiFi

// ============================================
// CONSTANTES
// ============================================
#define SERVO_ANGULO_ABIERTO 90
#define SERVO_ANGULO_CERRADO 0
#define UMBRAL_DISTANCIA 15  // cm para considerar ocupado
#define UMBRAL_VARIACION 3   // cm de variación permitida
#define TIEMPO_ESTABILIDAD_ENTRADA 5000  // 5 segundos para confirmar entrada
#define TIEMPO_ESTABILIDAD_SALIDA 3000   // 3 segundos para confirmar salida
#define DELAY_IR 2000  // 2 segundos de delay para IR

// ============================================
// MQTT HIVEMQ - CREDENCIALES DE RODRIGO
// ============================================
const char* mqttServer = "bb61f8b7c8bf4b23939d0b7efc3effc2.s1.eu.hivemq.cloud";
const int mqttPort = 8883;
const char* mqttUser = "hivemq.webclient.1763931936305";
const char* mqttPassword = "L;@15loW>:F9VT4hfHrv";

// ============================================
// OBJETOS GLOBALES
// ============================================
Servo puertaEntrada;
#if HARDWARE_COMPLETO
Servo puertaSalida;
#endif

NewPing sonar1(TRIG_PIN_1, ECHO_PIN_1, MAX_DISTANCE);
#if HARDWARE_COMPLETO
NewPing sonar2(TRIG_PIN_2, ECHO_PIN_2, MAX_DISTANCE);
NewPing sonar3(TRIG_PIN_3, ECHO_PIN_3, MAX_DISTANCE);
#endif

WiFiClientSecure espClient;
PubSubClient client(espClient);
Preferences preferences;

// ============================================
// VARIABLES GLOBALES
// ============================================
bool entradaAbierta = false;
bool salidaAbierta = false;
bool modoManual = false;

// Variables para detección estable
unsigned long tiempoEstable = 0;
unsigned long tiempoDeteccionSalida = 0;
unsigned int distanciaAnterior = MAX_DISTANCE;
bool vehiculoEstacionadoConfirmado = false;

// Variables para IR con delay
bool vehiculoEnIR = false;
bool vehiculoEnIRAnterior = false;
unsigned long tiempoDeteccionIR = 0;
bool eventoIRPublicado = false;

// ============================================
// FUNCIONES DE CONTROL DE PUERTAS
// ============================================
void abrirEntrada() {
  Serial.println("🚪 Abriendo entrada...");
  puertaEntrada.write(SERVO_ANGULO_ABIERTO);
  entradaAbierta = true;
  Serial.println("✅ Entrada abierta");
}

void cerrarEntrada() {
  Serial.println("🚪 Cerrando entrada...");
  puertaEntrada.write(SERVO_ANGULO_CERRADO);
  entradaAbierta = false;
  Serial.println("✅ Entrada cerrada");
}

#if HARDWARE_COMPLETO
void abrirSalida() {
  Serial.println("🚪 Abriendo salida...");
  puertaSalida.write(SERVO_ANGULO_ABIERTO);
  salidaAbierta = true;
  Serial.println("✅ Salida abierta");
}

void cerrarSalida() {
  Serial.println("🚪 Cerrando salida...");
  puertaSalida.write(SERVO_ANGULO_CERRADO);
  salidaAbierta = false;
  Serial.println("✅ Salida cerrada");
}
#endif

// ============================================
// CONFIGURACIÓN WIFI
// ============================================
void setupWiFi() {
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);
  
  // Verificar si se presionó el botón de reset
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
    Serial.println("🔁 Conectando a red guardada: " + ssid);
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
  Serial.println("📡 IP: " + WiFi.localIP().toString());
}

// ============================================
// MQTT CALLBACK
// ============================================
void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("📨 Mensaje recibido en: ");
  Serial.println(topic);
  
  String payload;
  for (unsigned int i = 0; i < length; i++) {
    payload += (char)message[i];
  }
  Serial.println("📄 Payload: " + payload);

  // Configuración WiFi remota
  if (String(topic) == "estacionamiento/wifi/config") {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      String newSSID = doc["ssid"];
      String newPASS = doc["pass"];
      Serial.println("🔧 Nueva configuración WiFi:");
      Serial.println("   SSID: " + newSSID);
      preferences.begin("wifi", false);
      preferences.putString("ssid", newSSID);
      preferences.putString("pass", newPASS);
      preferences.end();
      Serial.println("💾 Configuración guardada. Reiniciando...");
      delay(1500);
      ESP.restart();
    } else {
      Serial.println("❌ Error al parsear JSON");
    }
  }

  // Control de puertas
  if (String(topic) == "estacionamiento/puerta/control") {
    if (payload == "abrirEntrada") {
      abrirEntrada();
      modoManual = true;
    } else if (payload == "cerrarEntrada") {
      cerrarEntrada();
      modoManual = true;
    }
#if HARDWARE_COMPLETO
    else if (payload == "abrirSalida") {
      abrirSalida();
      modoManual = true;
    } else if (payload == "cerrarSalida") {
      cerrarSalida();
      modoManual = true;
    }
#endif
    else if (payload == "auto") {
      modoManual = false;
      Serial.println("🤖 Modo automático activado");
    } else if (payload == "manual") {
      modoManual = true;
      Serial.println("👤 Modo manual activado");
    }
  }
}

// ============================================
// CONEXIÓN MQTT
// ============================================
void conectarMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  espClient.setInsecure();
  
  Serial.print("🔌 Conectando a MQTT...");
  while (!client.connected()) {
    if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
      Serial.println(" ✅ Conectado a HiveMQ");
      client.subscribe("estacionamiento/puerta/control");
      client.subscribe("estacionamiento/wifi/config");
      Serial.println("📡 Suscrito a tópicos");
    } else {
      Serial.print("❌ Falló, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n🚗 ========================================");
  Serial.println("   Sistema de Estacionamiento Inteligente");
  Serial.println("   ========================================\n");
  
  // Configurar servos
  puertaEntrada.attach(SERVO_ENTRADA_PIN);
  puertaEntrada.write(SERVO_ANGULO_CERRADO);
  
#if HARDWARE_COMPLETO
  puertaSalida.attach(SERVO_SALIDA_PIN);
  puertaSalida.write(SERVO_ANGULO_CERRADO);
#endif

  // Configurar pines IR
  pinMode(IR_ENTRADA_PIN, INPUT);
#if HARDWARE_COMPLETO
  pinMode(IR_SALIDA_PIN, INPUT);
#endif
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);

  // Conectar WiFi y MQTT
  setupWiFi();
  conectarMQTT();
  
  Serial.println("✅ Sistema listo\n");
}

// ============================================
// LOOP
// ============================================
void loop() {
  // Mantener conexión MQTT
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();

  // ==========================================
  // DETECCIÓN DE VEHÍCULO EN IR (con delay)
  // ==========================================
  int irEntrada = digitalRead(IR_ENTRADA_PIN);
  vehiculoEnIR = (irEntrada == LOW);

  if (vehiculoEnIR && !vehiculoEnIRAnterior) {
    // Vehículo detectado, iniciar timer
    tiempoDeteccionIR = millis();
    eventoIRPublicado = false;
  }

  if (vehiculoEnIR && !eventoIRPublicado) {
    // Verificar si han pasado 2 segundos
    if (millis() - tiempoDeteccionIR >= DELAY_IR) {
      Serial.println("🚗 Vehículo en entrada (IR confirmado)");
      
      StaticJsonDocument<100> doc;
      doc["evento"] = "vehiculo_detectado";
      doc["timestamp"] = millis();
      
      char jsonBuffer[100];
      serializeJson(doc, jsonBuffer);
      client.publish("estacionamiento/eventos/entrada", jsonBuffer);
      
      eventoIRPublicado = true;
      
      // Abrir puerta automáticamente si no está en modo manual
      if (!modoManual && !entradaAbierta) {
        abrirEntrada();
      }
    }
  }

  if (!vehiculoEnIR) {
    eventoIRPublicado = false;
    
    // Cerrar puerta automáticamente si está abierta y no hay vehículo
    if (!modoManual && entradaAbierta) {
      cerrarEntrada();
    }
  }

  vehiculoEnIRAnterior = vehiculoEnIR;

  // ==========================================
  // DETECCIÓN DE VEHÍCULO ESTACIONADO
  // ==========================================
  unsigned int distancia1 = sonar1.ping_cm();
  if (distancia1 == 0) distancia1 = MAX_DISTANCE;
  bool ocupado1 = distancia1 <= UMBRAL_DISTANCIA;

  // Lógica de estabilidad para OCUPADO (Entrada)
  if (ocupado1) {
    if (abs((int)distancia1 - (int)distanciaAnterior) <= UMBRAL_VARIACION) {
      if (tiempoEstable == 0) {
        tiempoEstable = millis();
      } else if (millis() - tiempoEstable >= TIEMPO_ESTABILIDAD_ENTRADA) {
        if (!vehiculoEstacionadoConfirmado) {
          Serial.println("🅿️ Vehículo estacionado confirmado (5s estable)");
          
          StaticJsonDocument<100> doc;
          doc["evento"] = "vehiculo_estacionado";
          doc["plaza"] = 1;
          doc["distancia"] = distancia1;
          
          char jsonBuffer[100];
          serializeJson(doc, jsonBuffer);
          client.publish("estacionamiento/eventos/estacionado", jsonBuffer);
          
          vehiculoEstacionadoConfirmado = true;
        }
      }
    } else {
      tiempoEstable = 0;
    }
    // Resetear timer de salida si está ocupado
    tiempoDeteccionSalida = 0;
  } 
  // Lógica de estabilidad para LIBRE (Salida)
  else {
    tiempoEstable = 0; // Resetear timer de entrada
    
#if DETECCION_SALIDA_AUTO
    if (vehiculoEstacionadoConfirmado) {
      if (tiempoDeteccionSalida == 0) {
        tiempoDeteccionSalida = millis();
        Serial.println("🔍 Detectando posible salida... (esperando 3s)");
      } else if (millis() - tiempoDeteccionSalida >= TIEMPO_ESTABILIDAD_SALIDA) {
        // Confirmar salida después de 3 segundos de estar libre
        Serial.println("✅ Plaza liberada confirmada (3s libre)");
        
        StaticJsonDocument<100> doc;
        doc["evento"] = "plaza_liberada";
        doc["plaza"] = 1;
        
        char jsonBuffer[100];
        serializeJson(doc, jsonBuffer);
        client.publish("estacionamiento/eventos/salida", jsonBuffer);
        
        vehiculoEstacionadoConfirmado = false;
        tiempoDeteccionSalida = 0;
      }
    } else {
      tiempoDeteccionSalida = 0;
    }
#else
    // Detección automática de salida DESACTIVADA
    tiempoDeteccionSalida = 0;
#endif
  }

  distanciaAnterior = distancia1;

  // ==========================================
  // PUBLICACIÓN DE ESTADO
  // ==========================================
#if HARDWARE_COMPLETO
  unsigned int distancia2 = sonar2.ping_cm();
  unsigned int distancia3 = sonar3.ping_cm();
  if (distancia2 == 0) distancia2 = MAX_DISTANCE;
  if (distancia3 == 0) distancia3 = MAX_DISTANCE;
  bool ocupado2 = distancia2 <= UMBRAL_DISTANCIA;
  bool ocupado3 = distancia3 <= UMBRAL_DISTANCIA;
#else
  unsigned int distancia2 = MAX_DISTANCE;
  unsigned int distancia3 = MAX_DISTANCE;
  bool ocupado2 = false;
  bool ocupado3 = false;
#endif

  // Plaza 1 (usar vehiculoEstacionadoConfirmado para reflejar el delay)
  char msgPlaza1[128];
  snprintf(msgPlaza1, sizeof(msgPlaza1), "{\"ocupado\":%s,\"distancia\":%u,\"entradaAbierta\":%s}",
           vehiculoEstacionadoConfirmado ? "true" : "false", distancia1, entradaAbierta ? "true" : "false");
  client.publish("estacionamiento/plaza1/estado", msgPlaza1);

  // Plaza 2
  char msgPlaza2[128];
  snprintf(msgPlaza2, sizeof(msgPlaza2), "{\"ocupado\":false,\"distancia\":200}");
  client.publish("estacionamiento/plaza2/estado", msgPlaza2);

  // Plaza 3
  char msgPlaza3[128];
  snprintf(msgPlaza3, sizeof(msgPlaza3), "{\"ocupado\":false,\"distancia\":200}");
  client.publish("estacionamiento/plaza3/estado", msgPlaza3);

  // Log en Serial
  Serial.printf("P1: %u cm (%s) | Puerta: %s | IR: %s\n",
                distancia1, ocupado1 ? "Ocupado" : "Libre",
                entradaAbierta ? "Abierta" : "Cerrada",
                vehiculoEnIR ? "Detectado" : "Libre");

  delay(500);
}
