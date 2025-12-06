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

// ==================== VARIABLES DE CONECTIVIDAD ====================
bool wifiConectado = false;
bool firebaseConectado = false;
bool modoAutonomo = false;
unsigned long ultimaConexionExitosa = 0;
unsigned long tiempoSinConexion = 0;
const unsigned long tiempoMaxSinConexion = 10000; // 10 segundos sin conexión = modo autónomo
int intentosReconexionConsecutivos = 0;
const int maxIntentosReconexion = 3;

// ==================== SISTEMA DE COLA OFFLINE ====================
#define MAX_EVENTOS_COLA 50  // Máximo de eventos a guardar en memoria

struct EventoDeteccion {
  long distancia;
  unsigned long timestamp;  // Tiempo en millis()
  String tipo;  // "deteccion", "apertura", "cierre", "alerta"
};

EventoDeteccion colaEventos[MAX_EVENTOS_COLA];
int contadorEventos = 0;
bool sincronizacionPendiente = false;


// Variables para control de registro
unsigned long ultimoRegistroDeteccion = 0;
const unsigned long intervaloRegistro = 2000;  // Registrar cada 2 segundos como mínimo

// ==================== VARIABLES DEL SISTEMA ====================
// Control del portón
int distanciaDeteccion = 10;
int velocidadAbrir = 35;
int velocidadCerrar = 135;
int velocidadDetener = 90;
unsigned long tiempoRotacion = 3500;
unsigned long tiempoEsperaAutomatico = 5000;
bool portonAbierto = false;
bool vehiculoDetectado = false;
bool deteccionAutomaticaBloqueada = false;
unsigned long tiempoApertura = 0;

// Variables de modo
String modoOperacion = "Automatico"; // Por defecto automático para modo autónomo
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
unsigned long lastConexionCheck = 0;
unsigned long ultimaSincronizacion = 0;

const unsigned long modoInterval = 3000;
const unsigned long controlInterval = 1000;
const unsigned long configInterval = 20000;
const unsigned long sensorInterval = 2000;
const unsigned long distanceInterval = 300;
const unsigned long temporizadorInterval = 1000;
const unsigned long conexionCheckInterval = 5000; // Verificar conexión cada 5 segundos
const unsigned long sincronizacionInterval = 10000; // Sincronizar cada 10 segundos

// ==================== DECLARACIÓN DE FUNCIONES ====================
void registrarEvento(String tipo, long distancia);
void sincronizarEventosConFirebase();
void sonarSincronizacionExitosa();

// ==================== FUNCIONES DE COLA OFFLINE ====================
void registrarEvento(String tipo, long distancia) {
  if (contadorEventos >= MAX_EVENTOS_COLA) {
    Serial.println("⚠ Cola de eventos llena, descartando evento más antiguo");
    // Desplazar eventos (eliminar el más antiguo)
    for (int i = 0; i < MAX_EVENTOS_COLA - 1; i++) {
      colaEventos[i] = colaEventos[i + 1];
    }
    contadorEventos = MAX_EVENTOS_COLA - 1;
  }
  
  colaEventos[contadorEventos].distancia = distancia;
  colaEventos[contadorEventos].timestamp = millis();
  colaEventos[contadorEventos].tipo = tipo;
  contadorEventos++;
  sincronizacionPendiente = true;
  
  Serial.print("📝 Evento registrado [");
  Serial.print(tipo);
  Serial.print("] - Distancia: ");
  Serial.print(distancia);
  Serial.print(" cm - Total en cola: ");
  Serial.println(contadorEventos);
}

void sincronizarEventosConFirebase() {
  if (!wifiConectado || !firebaseConectado || modoAutonomo) {
    return;
  }
  
  if (!sincronizacionPendiente || contadorEventos == 0) {
    return;
  }
  
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║   SINCRONIZANDO EVENTOS OFFLINE    ║");
  Serial.print("║   Eventos pendientes: ");
  Serial.print(contadorEventos);
  Serial.println("            ║");
  Serial.println("╚════════════════════════════════════╝");
  
  String url_eventos = "https://" + String(firebase_host) + "/eventosOffline.json";
  
  int eventosEnviados = 0;
  int eventosFallidos = 0;
  
  for (int i = 0; i < contadorEventos; i++) {
    // Crear JSON del evento
    StaticJsonDocument<256> doc;
    doc["tipo"] = colaEventos[i].tipo;
    doc["distancia"] = colaEventos[i].distancia;
    doc["timestampOffline"] = colaEventos[i].timestamp;
    doc["timestampSync"] = millis();
    doc["modoAutonomo"] = true;
    
    // Calcular tiempo offline aproximado (en segundos)
    unsigned long tiempoOffline = (millis() - colaEventos[i].timestamp) / 1000;
    doc["segundosOffline"] = tiempoOffline;
    
    String jsonEvento;
    serializeJson(doc, jsonEvento);
    
    // Enviar a Firebase usando POST (añade nuevo registro)
    if (sendHttpsRequest("POST", url_eventos, jsonEvento)) {
      eventosEnviados++;
      Serial.print("✅ Evento ");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(contadorEventos);
      Serial.println(" sincronizado");
    } else {
      eventosFallidos++;
      Serial.print("❌ Error en evento ");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.println(contadorEventos);
    }
    
    delay(200);  // Pequeño delay entre envíos para no saturar
  }
  
  // Limpiar cola si todos fueron enviados exitosamente
  if (eventosFallidos == 0) {
    contadorEventos = 0;
    sincronizacionPendiente = false;
    Serial.println("\n✅ Sincronización completada exitosamente");
    Serial.print("   Total enviados: ");
    Serial.println(eventosEnviados);
    sonarSincronizacionExitosa();
  } else {
    Serial.println("\n⚠ Sincronización parcial");
    Serial.print("   Enviados: ");
    Serial.print(eventosEnviados);
    Serial.print(" | Fallidos: ");
    Serial.println(eventosFallidos);
  }
}

void sonarSincronizacionExitosa() {
  // Sonido de sincronización exitosa (3 tonos ascendentes)
  for (int i = 0; i < 3; i++) {
    analogWriteFreq(1000 + (i * 300));
    analogWrite(BUZZER_PIN, 120);
    delay(150);
    analogWrite(BUZZER_PIN, 0);
    delay(100);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  SISTEMA PORTÓN AUTOMÁTICO IoT");
  Serial.println("  Alvaro Pinto y Allan Alquinta");
  Serial.println("  MODO AUTÓNOMO HABILITADO");
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
  
  // Inicializar variables de cola offline
  contadorEventos = 0;
  sincronizacionPendiente = false;
  
  conectarWiFi();
  
  client.setInsecure();
  client.setBufferSizes(1024, 512);
  
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  int intentosNTP = 0;
  while (time(nullptr) < 100000 && intentosNTP < 20) {
    delay(500);
    intentosNTP++;
  }
  
  if (wifiConectado) {
    setupFirebaseStructure();
  } else {
    Serial.println("⚠ Iniciando en MODO AUTÓNOMO");
    modoAutonomo = true;
    modoOperacion = "Automatico";
  }
  
  Serial.println("  SISTEMA LISTO");
  Serial.println("========================================\n");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  unsigned long currentTime = millis();
  
  // Verificar estado de conexión periódicamente
  if (currentTime - lastConexionCheck >= conexionCheckInterval) {
    verificarConectividad();
    lastConexionCheck = currentTime;
  }
  
  // Intentar sincronizar eventos offline periódicamente
  if (!modoAutonomo && wifiConectado && firebaseConectado && sincronizacionPendiente) {
    if (currentTime - ultimaSincronizacion >= sincronizacionInterval) {
      sincronizarEventosConFirebase();
      ultimaSincronizacion = currentTime;
    }
  }
  
  // Si hay conexión, operar normalmente con Firebase
  if (!modoAutonomo && wifiConectado && firebaseConectado) {
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
    
    if (currentTime - lastSensorSend >= sensorInterval) {
      enviarSensoresFirebase();
      lastSensorSend = currentTime;
    }
  }
  
  // Medición de distancia (siempre activa)
  if (currentTime - lastDistanceMeasure >= distanceInterval) {
    distanciaActual = medirDistancia();
    lastDistanceMeasure = currentTime;
  }
  
  // Funciones que siempre deben ejecutarse (con o sin conexión)
  manejarAlarma();
  manejarLED();
  verificarHorariosPorton();
  ejecutarLogicaPorton();
  
  delay(20);
}

// ==================== FUNCIONES DE CONECTIVIDAD ====================
void verificarConectividad() {
  // Verificar WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConectado) {
      Serial.println("❌ WiFi desconectado");
      wifiConectado = false;
      firebaseConectado = false;
    }
    
    // Intentar reconectar solo si no hemos superado el límite
    if (intentosReconexionConsecutivos < maxIntentosReconexion) {
      Serial.println("🔄 Intentando reconectar WiFi...");
      conectarWiFi();
      intentosReconexionConsecutivos++;
    } else {
      // Activar modo autónomo
      if (!modoAutonomo) {
        activarModoAutonomo();
      }
    }
  } else {
    // WiFi conectado
    if (!wifiConectado) {
      Serial.println("✅ WiFi reconectado");
      wifiConectado = true;
      intentosReconexionConsecutivos = 0;
    }
    
    // Verificar conectividad con Firebase
    verificarConexionFirebase();
  }
  
  // Calcular tiempo sin conexión
  if (!wifiConectado || !firebaseConectado) {
    tiempoSinConexion = millis() - ultimaConexionExitosa;
    
    // Si pasa mucho tiempo sin conexión, activar modo autónomo
    if (tiempoSinConexion >= tiempoMaxSinConexion && !modoAutonomo) {
      activarModoAutonomo();
    }
  } else {
    ultimaConexionExitosa = millis();
    tiempoSinConexion = 0;
    
    // Si estábamos en modo autónomo y recuperamos la conexión
    if (modoAutonomo) {
      desactivarModoAutonomo();
    }
  }
}

void verificarConexionFirebase() {
  // Hacer una petición simple para verificar conectividad
  String response = "";
  bool exito = getHttpsRequest(url_modo, response);
  
  if (exito) {
    if (!firebaseConectado) {
      Serial.println("✅ Firebase conectado");
      firebaseConectado = true;
    }
  } else {
    if (firebaseConectado) {
      Serial.println("❌ Firebase desconectado");
      firebaseConectado = false;
    }
  }
}

void activarModoAutonomo() {
  modoAutonomo = true;
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║   MODO AUTÓNOMO ACTIVADO           ║");
  Serial.println("║   Operando con últimos valores     ║");
  Serial.println("╚════════════════════════════════════╝");
  
  // Forzar modo automático para que siga funcionando
  modoOperacion = "Automatico";
  
  // Mostrar configuración actual
  Serial.println("\n📋 Configuración autónoma:");
  Serial.print("   • Distancia detección: ");
  Serial.print(distanciaDeteccion);
  Serial.println(" cm");
  Serial.print("   • Tiempo cierre: ");
  Serial.print(tiempoCierre);
  Serial.println(" seg");
  Serial.print("   • Horario LED: ");
  Serial.print(horaEncendidoLED);
  Serial.print(" - ");
  Serial.println(horaApagadoLED);
  Serial.print("   • Horario portón: ");
  Serial.print(horaAperturaPorton);
  Serial.print(" - ");
  Serial.println(horaCierrePorton);
  Serial.println();
  
  // Sonido de alerta de modo autónomo
  sonarModoAutonomo();
}

void desactivarModoAutonomo() {
  modoAutonomo = false;
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║   CONEXIÓN RESTAURADA              ║");
  Serial.println("║   Volviendo a modo Firebase        ║");
  Serial.println("╚════════════════════════════════════╝\n");
  
  // Sincronizar eventos registrados offline
  if (contadorEventos > 0) {
    Serial.print("📤 Sincronizando ");
    Serial.print(contadorEventos);
    Serial.println(" eventos registrados offline...");
    sincronizarEventosConFirebase();
  }
  
  // Sincronizar con Firebase
  setupFirebaseStructure();
  leerConfiguraciones();
  leerHorariosLED();
  leerHorariosPorton();
  
  // Sonido de reconexión exitosa
  sonarReconexion();
}

// ==================== FUNCIONES WIFI ====================
void conectarWiFi() {
  Serial.print("🔌 Conectando a WiFi");
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✅");
    Serial.print("   IP: ");
    Serial.println(WiFi.localIP());
    wifiConectado = true;
    ultimaConexionExitosa = millis();
  } else {
    Serial.println(" ❌");
    wifiConectado = false;
  }
}

// ==================== FUNCIONES FIREBASE ====================
void setupFirebaseStructure() {
  if (!wifiConectado) return;
  
  sendHttpsRequest("PUT", url_porton_abierto, "false");
  delay(100);
  sendHttpsRequest("PUT", url_estado_porton, "\"Cerrado\"");
  delay(100);
  sendHttpsRequest("PUT", url_modo, "\"Automatico\"");
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
  if (!wifiConectado || modoAutonomo) return;
  
  if (abs(distanciaActual - lastDistanciaEnviada) > 3) {
    sendHttpsRequest("PUT", url_distancia, String(distanciaActual));
    lastDistanciaEnviada = distanciaActual;
  }
  
  if (abs(valorLuzActual - lastLuzEnviada) > 30) {
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
    
    if (!modoAutonomo && wifiConectado) {
      sendHttpsRequest("PUT", url_temporizador, String(temporizador));
    }
  }
}

bool sendHttpsRequest(String method, String url, String payload) {
  if (!wifiConectado || modoAutonomo) return false;
  
  HTTPClient https;
  
  if (strlen(firebase_auth) > 0) {
    url += "?auth=" + String(firebase_auth);
  }
  
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(5000);
  
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
  if (!wifiConectado) return false;
  
  HTTPClient https;
  
  if (strlen(firebase_auth) > 0) {
    url += "?auth=" + String(firebase_auth);
  }
  
  https.begin(client, url);
  https.setTimeout(5000);
  
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
      if (!modoAutonomo) {
        sendHttpsRequest("PUT", url_modo, "\"Automatico\"");
      }
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
      
      if (!modoAutonomo) {
        sendHttpsRequest("PUT", url_modo, "\"Manual\"");
      }
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
  // En modo autónomo, siempre usar modo automático
  if (modoAutonomo) {
    modoOperacion = "Automatico";
  }
  
  if (modoOperacion == "Desactivado" || modoOperacion == "Manual") {
    // En modo manual sin conexión, no hacer nada automático
    if (modoAutonomo && modoOperacion == "Manual") {
      return;
    }
    return;
  }
  
  if (modoOperacion == "Automatico") {
    if (distanciaActual <= distanciaDeteccion && distanciaActual > 0) {
      if (!vehiculoDetectado) {
        vehiculoDetectado = true;
        Serial.println("→ Vehículo detectado");
        sonarDeteccion();
        
        // Registrar detección en modo offline
        if (modoAutonomo && millis() - ultimoRegistroDeteccion >= intervaloRegistro) {
          registrarEvento("deteccion", distanciaActual);
          ultimoRegistroDeteccion = millis();
        }
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
  if (!modoAutonomo) {
    sendHttpsRequest("PUT", url_estado_porton, "\"Abriendo\"");
  }
  
  Serial.println("🚪 Abriendo portón...");
  
  servoPorton.write(velocidadAbrir);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  
  portonAbierto = true;
  tiempoApertura = millis();
  
  // Registrar apertura en modo offline
  if (modoAutonomo) {
    registrarEvento("apertura", distanciaActual);
  }
  
  if (!modoAutonomo) {
    sendHttpsRequest("PUT", url_estado_porton, "\"Abierto\"");
    sendHttpsRequest("PUT", url_porton_abierto, "true");
  }
  
  Serial.println("✅ Portón abierto");
}

void cerrarPorton() {
  sonarAlertaCierre();
  delay(500);
  
  if (!modoAutonomo) {
    sendHttpsRequest("PUT", url_estado_porton, "\"Cerrando\"");
  }
  
  Serial.println("🚪 Cerrando portón...");
  
  servoPorton.write(velocidadCerrar);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  
  portonAbierto = false;
  
  // Registrar cierre en modo offline
  if (modoAutonomo) {
    registrarEvento("cierre", distanciaActual);
  }
  
  if (!modoAutonomo) {
    sendHttpsRequest("PUT", url_estado_porton, "\"Cerrado\"");
    sendHttpsRequest("PUT", url_porton_abierto, "false");
  }
  
  Serial.println("✅ Portón cerrado");
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
void sonarDeteccion() {
  analogWriteFreq(1200);
  analogWrite(BUZZER_PIN, 150);
  delay(100);
  analogWrite(BUZZER_PIN, 0);
  delay(50);
  
  analogWriteFreq(1500);
  analogWrite(BUZZER_PIN, 150);
  delay(100);
  analogWrite(BUZZER_PIN, 0);
}

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

void sonarModoAutonomo() {
  // Sonido característico para modo autónomo
  for (int i = 0; i < 2; i++) {
    analogWriteFreq(600);
    analogWrite(BUZZER_PIN, 180);
    delay(300);
    
    analogWriteFreq(900);
    analogWrite(BUZZER_PIN, 180);
    delay(300);
    
    analogWrite(BUZZER_PIN, 0);
    delay(200);
  }
}

void sonarReconexion() {
  // Sonido ascendente para indicar reconexión
  for (int freq = 800; freq <= 1600; freq += 200) {
    analogWriteFreq(freq);
    analogWrite(BUZZER_PIN, 150);
    delay(100);
  }
  analogWrite(BUZZER_PIN, 0);
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
    if (!modoAutonomo) {
      sendHttpsRequest("PUT", url_led_estado, "\"ON\"");
    }
  } else if (!debeEstarEncendido && ledEncendido) {
    digitalWrite(LED_PIN, LOW);
    ledEncendido = false;
    if (!modoAutonomo) {
      sendHttpsRequest("PUT", url_led_estado, "\"OFF\"");
    }
  }
}

bool verificarHorarioLED() {
  time_t ahora = time(nullptr);
  struct tm* tiempoInfo = localtime(&ahora);
  
  if (tiempoInfo->tm_year < 100) {
    // Si no hay tiempo válido, usar el comando manual si hay conexión
    if (!modoAutonomo) {
      return (ledEstadoComando == "ON");
    }
    // En modo autónomo sin hora válida, usar fotoresistor
    return (valorLuzActual > umbralOscuridad);
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