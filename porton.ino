#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Servo.h>
#include <time.h>

// ==================== CONFIGURACIÓN WIFI ====================
const char* ssid = "magic5";
const char* password = "12345678";

// ==================== CONFIGURACIÓN FIREBASE ====================
const char* firebase_host = "fir-eva2-dbf63-default-rtdb.firebaseio.com";
const char* firebase_auth = "MPrKTbNRr5cxYX9O7xAZoVBNEJAgPmeBVtC3ccJb";

// URLs Firebase REST API
String url_modo = "https://" + String(firebase_host) + "/modoOperacion.json";
String url_porton_abierto = "https://" + String(firebase_host) + "/portonAbierto.json";
String url_estado_porton = "https://" + String(firebase_host) + "/estadoPorton.json";
String url_tiempos = "https://" + String(firebase_host) + "/tiemposPorton.json";
String url_distancia = "https://" + String(firebase_host) + "/distanciaActual.json";
String url_deteccion = "https://" + String(firebase_host) + "/distanciaDeteccion.json";
String url_luz = "https://" + String(firebase_host) + "/luz.json";
String url_temporizador = "https://" + String(firebase_host) + "/temporizador.json";
String url_led_estado = "https://" + String(firebase_host) + "/ledEstado.json";
String url_horarios_luces = "https://" + String(firebase_host) + "/lucesHorarios.json";
String url_horarios_porton = "https://" + String(firebase_host) + "/horarios.json";

// ==================== DEFINICIÓN DE PINES ====================
#define TRIG_PIN D7
#define ECHO_PIN D6
#define SERVO_PIN D5
#define BUZZER_PIN D4
#define LDR_PIN A0
#define LED_PIN D1

// ==================== OBJETOS ====================
Servo servoPorton;
WiFiClientSecure client;

// ==================== VARIABLES DEL SISTEMA ====================
// Control del portón
int distanciaDeteccion = 10;
int velocidadAbrir = 40;
int velocidadCerrar = 132;
int velocidadDetener = 90;
unsigned long tiempoRotacion = 3500;
unsigned long tiempoEsperaAutomatico = 5000;
bool portonAbierto = false;
bool vehiculoDetectado = false;
bool deteccionAutomaticaBloqueada = false;
unsigned long tiempoApertura = 0;

// Variables de modo
String modoOperacion = "Manual";
String estadoPorton = "Cerrado";
int tiempoCierre = 5;
int temporizador = 0;

// Sensor de distancia
long distanciaActual = 999;
long lastDistanciaEnviada = 999;

// Fotoresistor y LED
int valorLuzActual = 0;
int lastLuzEnviada = 0;
int umbralOscuridad = 700;
bool ledEncendido = false;
bool modoAutomaticoLED = false; 
String ledEstadoComando = "OFF";
String horaEncendidoLED = "18:00";
String horaApagadoLED = "06:00";

// Horarios del portón
String horaAperturaPorton = "08:00";
String horaCierrePorton = "22:00";
bool horarioAperturaEjecutado = false;
bool horarioCierreEjecutado = false;

// Sistema de alarmas
bool alarmaActiva = false;
unsigned long tiempoInicioAlarma = 0;
const unsigned long duracionAlarma = 10000;
unsigned long ultimoCambioTono = 0;
bool tonoAlto = true;

// Temporización OPTIMIZADA
unsigned long lastModoRead = 0;
unsigned long lastControlRead = 0;
unsigned long lastConfigRead = 0;
unsigned long lastSensorSend = 0;
unsigned long lastDistanceMeasure = 0;
unsigned long lastTemporizador = 0;

const unsigned long modoInterval = 3000;         // Reducido de 5s a 3s
const unsigned long controlInterval = 1000;      // Reducido de 2s a 1s
const unsigned long configInterval = 20000;      // Aumentado de 15s a 20s
const unsigned long sensorInterval = 2000;       // Reducido de 3s a 2s
const unsigned long distanceInterval = 300;      // Reducido de 500ms a 300ms
const unsigned long temporizadorInterval = 1000;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  SISTEMA PORTÓN AUTOMÁTICO IoT");
  Serial.println("  Alvaro Pinto y Allan Alquinta");
  Serial.println("========================================");
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  servoPorton.attach(SERVO_PIN);
  servoPorton.write(velocidadDetener);
  delay(1000);
  
  portonAbierto = false;
  alarmaActiva = false;
  deteccionAutomaticaBloqueada = false;
  ledEncendido = false;
  
  conectarWiFi();
  
  client.setInsecure();
  client.setBufferSizes(1024, 512);
  
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  int intentosNTP = 0;
  while (time(nullptr) < 100000 && intentosNTP < 20) {
    delay(500);
    intentosNTP++;
  }
  
  setupFirebaseStructure();
  
  Serial.println("  SISTEMA LISTO");
  Serial.println("========================================\n");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  unsigned long currentTime = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  
  if (currentTime - lastModoRead >= modoInterval) {
    leerModoOperacion();
    lastModoRead = currentTime;
  }
  
  if (currentTime - lastControlRead >= controlInterval) {
    leerControlesManual();
    lastControlRead = currentTime;
  }
  
  if (currentTime - lastConfigRead >= configInterval) {
    leerConfiguraciones();
    leerHorariosLED();
    leerHorariosPorton();
    lastConfigRead = currentTime;
  }
  
  if (currentTime - lastControlRead >= controlInterval) {
    leerEstadoLED();
  }
  
  if (currentTime - lastDistanceMeasure >= distanceInterval) {
    distanciaActual = medirDistancia();
    lastDistanceMeasure = currentTime;
  }
  
  if (currentTime - lastSensorSend >= sensorInterval) {
    enviarSensoresFirebase();
    lastSensorSend = currentTime;
  }
  
  manejarAlarma();
  manejarLED();
  verificarHorariosPorton();
  ejecutarLogicaPorton();
  
  delay(20);  // Reducido de 50ms a 20ms
}

// ==================== FUNCIONES WIFI ====================
void conectarWiFi() {
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500);
    intentos++;
  }
}

// ==================== FUNCIONES FIREBASE ====================
void setupFirebaseStructure() {
  sendHttpsRequest("PUT", url_porton_abierto, "false");
  delay(100);
  sendHttpsRequest("PUT", url_estado_porton, "\"Cerrado\"");
  delay(100);
  sendHttpsRequest("PUT", url_modo, "\"Manual\"");
  delay(100);
  sendHttpsRequest("PUT", url_distancia, String(distanciaActual));
  delay(100);
  sendHttpsRequest("PUT", url_deteccion, String(distanciaDeteccion));
  delay(100);
  sendHttpsRequest("PUT", url_luz, String(valorLuzActual));
  delay(100);
  
  StaticJsonDocument<100> tiemposDoc;
  tiemposDoc["tiempoCierre"] = tiempoCierre;
  String tiemposJson;
  serializeJson(tiemposDoc, tiemposJson);
  sendHttpsRequest("PUT", url_tiempos, tiemposJson);
  delay(100);
  
  sendHttpsRequest("PUT", url_temporizador, "0");
  delay(100);
  sendHttpsRequest("PUT", url_led_estado, "\"OFF\"");
}

void leerModoOperacion() {
  String response = "";
  if (getHttpsRequest(url_modo, response)) {
    response.replace("\"", "");
    if (response.length() > 0 && response != "null") {
      modoOperacion = response;
      deteccionAutomaticaBloqueada = (modoOperacion == "Desactivado");
    }
  }
}

void leerControlesManual() {
  if (modoOperacion == "Desactivado") {
    return;
  }
  
  String response = "";
  
  if (getHttpsRequest(url_porton_abierto, response)) {
    response.replace("\"", "");
    bool comandoAbierto = (response == "true");
    
    if (comandoAbierto && !portonAbierto) {
      abrirPorton();
    } 
    else if (!comandoAbierto && portonAbierto) {
      cerrarPorton();
    }
  }
}

void leerEstadoLED() {
  String response = "";
  if (getHttpsRequest(url_led_estado, response)) {
    response.replace("\"", "");
    if (response.length() > 0 && response != "null") {
      ledEstadoComando = response;
    }
  }
}

void leerHorariosLED() {
  String response = "";
  if (getHttpsRequest(url_horarios_luces, response)) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      String horaEnc = doc["encendido"] | "18:00";
      String horaApa = doc["apagado"] | "06:00";
      
      if (horaEnc.length() > 0) {
        horaEncendidoLED = horaEnc;
      }
      
      if (horaApa.length() > 0) {
        horaApagadoLED = horaApa;
      }
    }
  }
}

void leerHorariosPorton() {
  String response = "";
  if (getHttpsRequest(url_horarios_porton, response)) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      String horaAper = doc["apertura"] | "08:00";
      String horaCier = doc["cierre"] | "22:00";
      
      if (horaAper.length() > 0) {
        horaAperturaPorton = horaAper;
      }
      
      if (horaCier.length() > 0) {
        horaCierrePorton = horaCier;
      }
    }
  }
}

void leerConfiguraciones() {
  String responseDeteccion = "";
  if (getHttpsRequest(url_deteccion, responseDeteccion)) {
    int nuevaDistancia = responseDeteccion.toInt();
    if (nuevaDistancia > 0 && nuevaDistancia <= 100) {
      distanciaDeteccion = nuevaDistancia;
    }
  }
  
  String responseTiempos = "";
  if (getHttpsRequest(url_tiempos, responseTiempos)) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, responseTiempos) == DeserializationError::Ok) {
      int nuevoTiempoCierre = doc["tiempoCierre"] | 5;
      tiempoCierre = nuevoTiempoCierre;
      tiempoEsperaAutomatico = nuevoTiempoCierre * 1000;
    }
  }
}

void enviarSensoresFirebase() {
  if (abs(distanciaActual - lastDistanciaEnviada) > 3) {  // Más sensible: 3cm en vez de 5cm
    sendHttpsRequest("PUT", url_distancia, String(distanciaActual));
    lastDistanciaEnviada = distanciaActual;
  }
  
  if (abs(valorLuzActual - lastLuzEnviada) > 30) {  // Más sensible: 30 en vez de 50
    sendHttpsRequest("PUT", url_luz, String(valorLuzActual));
    lastLuzEnviada = valorLuzActual;
  }
}

void actualizarTemporizador() {
  if (portonAbierto && !vehiculoDetectado && modoOperacion == "Automatico") {
    unsigned long tiempoTranscurrido = (millis() - tiempoApertura) / 1000;
    temporizador = tiempoCierre - tiempoTranscurrido;
    
    if (temporizador < 0) {
      temporizador = 0;
    }
    
    sendHttpsRequest("PUT", url_temporizador, String(temporizador));
  }
}

bool sendHttpsRequest(String method, String url, String payload) {
  HTTPClient https;
  
  if (strlen(firebase_auth) > 0) {
    url += "?auth=" + String(firebase_auth);
  }
  
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(5000);  // Reducido de 8000 a 5000
  
  int httpCode;
  if (method == "PUT") {
    httpCode = https.PUT(payload);
  } else if (method == "POST") {
    httpCode = https.POST(payload);
  } else {
    httpCode = https.GET();
  }
  
  bool success = (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED);
  https.end();
  return success;
}

bool getHttpsRequest(String url, String &response) {
  HTTPClient https;
  
  if (strlen(firebase_auth) > 0) {
    url += "?auth=" + String(firebase_auth);
  }
  
  https.begin(client, url);
  https.setTimeout(5000);  // Reducido de 8000 a 5000
  
  int httpCode = https.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    response = https.getString();
    https.end();
    return true;
  }
  
  https.end();
  return false;
}

// ==================== HORARIOS PORTÓN ====================
void verificarHorariosPorton() {
  time_t ahora = time(nullptr);
  struct tm* tiempoInfo = localtime(&ahora);
  
  if (tiempoInfo->tm_year < 100) {
    return;
  }
  
  int horaActual = tiempoInfo->tm_hour;
  int minutoActual = tiempoInfo->tm_min;
  
  int horaAper = horaAperturaPorton.substring(0, 2).toInt();
  int minAper = horaAperturaPorton.substring(3, 5).toInt();
  
  int horaCier = horaCierrePorton.substring(0, 2).toInt();
  int minCier = horaCierrePorton.substring(3, 5).toInt();
  
  if (horaActual == horaAper && minutoActual == minAper) {
    if (!horarioAperturaEjecutado) {
      sendHttpsRequest("PUT", url_modo, "\"Automatico\"");
      modoOperacion = "Automatico";
      
      horarioAperturaEjecutado = true;
      horarioCierreEjecutado = false;
    }
  } else {
    if (horarioAperturaEjecutado && minutoActual != minAper) {
      horarioAperturaEjecutado = false;
    }
  }
  
  if (horaActual == horaCier && minutoActual == minCier) {
    if (!horarioCierreEjecutado) {
      if (portonAbierto) {
        cerrarPorton();
        delay(500);
      }
      
      sendHttpsRequest("PUT", url_modo, "\"Manual\"");
      modoOperacion = "Manual";
      
      horarioCierreEjecutado = true;
      horarioAperturaEjecutado = false;
    }
  } else {
    if (horarioCierreEjecutado && minutoActual != minCier) {
      horarioCierreEjecutado = false;
    }
  }
}

// ==================== LÓGICA DEL PORTÓN ====================
void ejecutarLogicaPorton() {
  if (modoOperacion == "Desactivado" || modoOperacion == "Manual") {
    return;
  }
  
  if (modoOperacion == "Automatico") {
    if (distanciaActual <= distanciaDeteccion && distanciaActual > 0) {
      if (!vehiculoDetectado) {
        vehiculoDetectado = true;
      }
      
      if (!portonAbierto) {
        abrirPorton();
      }
      
      tiempoApertura = millis();
    } else {
      vehiculoDetectado = false;
    }
    
    if (portonAbierto && !vehiculoDetectado) {
      if (millis() - tiempoApertura >= tiempoEsperaAutomatico) {
        cerrarPorton();
      }
    }
  }
}

void abrirPorton() {
  sendHttpsRequest("PUT", url_estado_porton, "\"Abriendo\"");
  
  servoPorton.write(velocidadAbrir);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  
  portonAbierto = true;
  tiempoApertura = millis();
  
  sendHttpsRequest("PUT", url_estado_porton, "\"Abierto\"");
  sendHttpsRequest("PUT", url_porton_abierto, "true");
}

void cerrarPorton() {
  sonarAlertaCierre();
  delay(500);
  
  sendHttpsRequest("PUT", url_estado_porton, "\"Cerrando\"");
  
  servoPorton.write(velocidadCerrar);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  
  portonAbierto = false;
  
  sendHttpsRequest("PUT", url_estado_porton, "\"Cerrado\"");
  sendHttpsRequest("PUT", url_porton_abierto, "false");
}

// ==================== SENSOR DE DISTANCIA ====================
long medirDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duracion = pulseIn(ECHO_PIN, HIGH, 15000);
  
  if (duracion == 0) return 999;
  
  long distancia = duracion * 0.034 / 2;
  if (distancia < 2 || distancia > 400) return 999;
  
  return distancia;
}

// ==================== SISTEMA DE ALERTAS ====================
void sonarAlertaCierre() {
  for (int i = 0; i < 3; i++) {
    analogWriteFreq(800);
    analogWrite(BUZZER_PIN, 200);
    delay(200);
    
    analogWrite(BUZZER_PIN, 0);
    delay(150);
  }
  
  noTone(BUZZER_PIN);
}

void activarAlarma() {
  alarmaActiva = true;
  tiempoInicioAlarma = millis();
}

void desactivarAlarma() {
  alarmaActiva = false;
  noTone(BUZZER_PIN);
  analogWrite(BUZZER_PIN, 0);
}

void manejarAlarma() {
  if (!alarmaActiva) return;
  
  if (millis() - tiempoInicioAlarma >= duracionAlarma) {
    desactivarAlarma();
    return;
  }
  
  if (millis() - ultimoCambioTono >= 300) {
    ultimoCambioTono = millis();
    tonoAlto = !tonoAlto;
    noTone(BUZZER_PIN);
    
    if (tonoAlto) {
      analogWriteFreq(1000);
      analogWrite(BUZZER_PIN, 180);
    } else {
      analogWriteFreq(500);
      analogWrite(BUZZER_PIN, 180);
    }
  }
}

// ==================== CONTROL LED ====================
void manejarLED() {
  valorLuzActual = analogRead(LDR_PIN);
  
  bool debeEstarEncendido = verificarHorarioLED();
  
  if (debeEstarEncendido && !ledEncendido) {
    digitalWrite(LED_PIN, HIGH);
    ledEncendido = true;
    sendHttpsRequest("PUT", url_led_estado, "\"ON\"");
  } else if (!debeEstarEncendido && ledEncendido) {
    digitalWrite(LED_PIN, LOW);
    ledEncendido = false;
    sendHttpsRequest("PUT", url_led_estado, "\"OFF\"");
  }
}

bool verificarHorarioLED() {
  time_t ahora = time(nullptr);
  struct tm* tiempoInfo = localtime(&ahora);
  
  if (tiempoInfo->tm_year < 100) {
    return (ledEstadoComando == "ON");
  }
  
  int horaActual = tiempoInfo->tm_hour;
  int minutoActual = tiempoInfo->tm_min;
  int minutosActuales = horaActual * 60 + minutoActual;
  
  int horaEnc = horaEncendidoLED.substring(0, 2).toInt();
  int minEnc = horaEncendidoLED.substring(3, 5).toInt();
  int minutosEncendido = horaEnc * 60 + minEnc;
  
  int horaApa = horaApagadoLED.substring(0, 2).toInt();
  int minApa = horaApagadoLED.substring(3, 5).toInt();
  int minutosApagado = horaApa * 60 + minApa;
  
  bool dentroDeRango;
  if (minutosEncendido < minutosApagado) {
    dentroDeRango = (minutosActuales >= minutosEncendido && minutosActuales < minutosApagado);
  } else {
    dentroDeRango = (minutosActuales >= minutosEncendido || minutosActuales < minutosApagado);
  }
  
  return dentroDeRango;
}