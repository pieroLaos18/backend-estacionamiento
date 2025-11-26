/**
 * @file ESP32.ino
 * @brief Firmware para el controlador ESP32 del sistema de estacionamiento inteligente.
 * 
 * Este código maneja:
 * - Conexión WiFi y MQTT (HiveMQ).
 * - Lectura de sensores ultrasónicos (HC-SR04) para detección de ocupación de plazas.
 * - Lectura de sensores IR para detección de paso de vehículos.
 * - Control de servomotores para barreras de entrada/salida.
 * - Lógica de estabilidad para evitar falsos positivos en la detección.
 * 
 * @author Rodrigo Quiroz
 */

#include <NewPing.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
  
  puertaEntrada.attach(SERVO_ENTRADA_PIN);
  puertaEntrada.write(SERVO_ANGULO_CERRADO);
  
#if HARDWARE_COMPLETO
  puertaSalida.attach(SERVO_SALIDA_PIN);
  puertaSalida.write(SERVO_ANGULO_CERRADO);
#endif

  pinMode(IR_ENTRADA_PIN, INPUT);
#if HARDWARE_COMPLETO
  pinMode(IR_SALIDA_PIN, INPUT);
#endif
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);

  setupWiFi();
  conectarMQTT();
  
  Serial.println("Sistema listo");
}

// --- Loop ---
void loop() {
  if (!client.connected()) {
    conectarMQTT();
  }
  client.loop();

  // Detección de vehículo en IR con delay de 2 segundos
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
      Serial.println("Vehiculo en entrada");
      
      StaticJsonDocument<100> doc;
      doc["evento"] = "vehiculo_detectado";
      doc["timestamp"] = millis();
      
      char jsonBuffer[100];
      serializeJson(doc, jsonBuffer);
      client.publish("estacionamiento/eventos/entrada", jsonBuffer);
      
      eventoIRPublicado = true;
    }
  }

  if (!vehiculoEnIR) {
    eventoIRPublicado = false;
  }

  vehiculoEnIRAnterior = vehiculoEnIR;

  // Detección de vehículo estacionado
  unsigned int distancia1 = sonar1.ping_cm();
  if (distancia1 == 0) distancia1 = MAX_DISTANCE;
  bool ocupado1 = distancia1 <= 15;

  // Lógica de estabilidad para OCUPADO (Entrada)
  if (ocupado1) {
    if (abs((int)distancia1 - (int)distanciaAnterior) <= UMBRAL_VARIACION) {
      if (tiempoEstable == 0) {
        tiempoEstable = millis();
      } else if (millis() - tiempoEstable >= TIEMPO_ESTABILIDAD_ENTRADA) {
        if (!vehiculoEstacionadoConfirmado) {
          Serial.println("Vehiculo estacionado confirmado (5 segundos estable)");
          
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
        Serial.println("Detectando posible salida... (esperando 3 segundos)");
      } else if (millis() - tiempoDeteccionSalida >= TIEMPO_ESTABILIDAD_SALIDA) {
        // Confirmar salida después de 3 segundos de estar libre
        Serial.println("Plaza liberada confirmada (3 segundos libre)");
        
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

  // Publicación de estado
#if HARDWARE_COMPLETO
  unsigned int distancia2 = sonar2.ping_cm();
  unsigned int distancia3 = sonar3.ping_cm();
  if (distancia2 == 0) distancia2 = MAX_DISTANCE;
  if (distancia3 == 0) distancia3 = MAX_DISTANCE;
  bool ocupado2 = distancia2 <= 15;
  bool ocupado3 = distancia3 <= 15;
#else
  unsigned int distancia2 = MAX_DISTANCE;
  unsigned int distancia3 = MAX_DISTANCE;
  bool ocupado2 = false;
  bool ocupado3 = false;
#endif

  char msgPlaza1[128];
  // Usar vehiculoEstacionadoConfirmado en lugar de ocupado1 para reflejar el delay
  snprintf(msgPlaza1, sizeof(msgPlaza1), "{\"ocupado\":%s,\"distancia\":%u,\"entradaAbierta\":%s}",
           vehiculoEstacionadoConfirmado ? "true" : "false", distancia1, entradaAbierta ? "true" : "false");
  client.publish("estacionamiento/plaza1/estado", msgPlaza1);

  char msgPlaza2[128];
  snprintf(msgPlaza2, sizeof(msgPlaza2), "{\"ocupado\":false,\"distancia\":200}");
  client.publish("estacionamiento/plaza2/estado", msgPlaza2);

  char msgPlaza3[128];
  snprintf(msgPlaza3, sizeof(msgPlaza3), "{\"ocupado\":false,\"distancia\":200}");
  client.publish("estacionamiento/plaza3/estado", msgPlaza3);

  Serial.printf("P1: %u cm (%s) | Puerta: %s | IR: %s\n",
                distancia1, ocupado1 ? "Ocupado" : "Libre",
                entradaAbierta ? "Abierta" : "Cerrada",
                vehiculoEnIR ? "Detectado" : "Libre");

  delay(500);
}