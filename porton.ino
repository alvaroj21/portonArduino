//Librerias utilizadas
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

//Para el Login
const char* USUARIO = "admin";
const char* PASSWORD = "123";
const unsigned long TIMEOUT_SESION = 180000;

//Configuración de WiFi
const char* ssid = "magic5";         
const char* password = "12345678";      

// Definición de pines
#define TRIG_PIN D7 //Pin asigfnado al TRIGGER del sensor, manda el pulso
#define ECHO_PIN D6 //Pin asignado al ECHO del sensor, recibe el pulso de vuelta
#define SERVO_PIN D5 //Pin asignado al servomotor
#define BUZZER_PIN D4 //Pin asignado al buzzer

// definicion de objetos
Servo servoPorton; 
ESP8266WebServer server(80);

// Variables del sistema
int distanciaDeteccion = 10; //Distancia que se mide en cm para la deteccion de vehiculos
int velocidadAbrir = 35;  
int velocidadCerrar = 145;   
int velocidadDetener = 90;   
unsigned long tiempoRotacion = 2500; //tiempo en que el servo va a rotar para abrir/cerrar, se encuentra en milisegundos
unsigned long tiempoApertura = 0;  //variable para ser usada al momento de que el porton se abre, cuenta el tiempo que transcurre
unsigned long tiempoEsperaAutomatico = 5000; //tiempo en MS en que el porton cerrara automaticamente
bool portonAbierto = false;  //estado actual del porton (false = cerrado en este caso)
bool vehiculoDetectado = false; //estado actual de deteccion (false = no se ha detectado nada)
bool deteccionAutomaticaBloqueada = false;  // Estado del bloqueo automático para la deteccion
bool alarmaActiva = false; //variable de estado de la alarma (buzzer, se encuentra false = apagado)
unsigned long tiempoInicioAlarma = 0; //variable para el tiempo desde que empezo a sonar la alarma
const unsigned long duracionAlarma = 10000; //duracion en MS que dura la alarma (10 segundos)
unsigned long ultimoCambioTono = 0; //para el cambio de tono de la alarma
bool tonoAlto = true; //para alternar entre tono agudo (true) y tono grave (false)
long distanciaActual = 999; //distancia medida en cm, se encuentra en 999 por default y asi tambien se indica que la lectura no es valida

//Estrctura para la gestion de seguridad por medio de token, se almacena la informacion de sesion y se implementa un sistema de autenticacion por token
struct SesionSegura {
  String token;    //token que sera unico y de 32 caracteres en hexadecimal
  String ip;       //la direccion ip del usuario que se esta autenticando
  unsigned long tiempoCreacion;  //momento en el cual se creo la sesion
  bool activa;  //para indicar si la sesion es valida
};

SesionSegura sesionActual = {"", "", 0, false}; //para la instancia de la sesion actual

//Para la generacion de un token
String generarToken() {
  String token = "";
  for (int i = 0; i < 32; i++) {
    token += String(random(0, 16), HEX); //se genera un numero aleatorio de 0 a 15 en el cual HEX los convertira a hexadecimal
  }
  return token;
}

//Funcion para la validacion de credencailes, si las creenciales de inicio de sesion son validas se returna TRUE caso contrario FALSE
bool validarCredenciales(String usuario, String pass) {
  return (usuario == USUARIO && pass == PASSWORD);
}

//funcion para crear las sesion. Se genera un token, se guarda la ip, se gurda el momento de inicio de sesion, se marca la sesion como activa
bool crearSesion(String ip) {
  sesionActual.token = generarToken();
  sesionActual.ip = ip;
  sesionActual.tiempoCreacion = millis();
  sesionActual.activa = true;
  return true;
}

//funcion para validar la sesion y comprueba que esta no ha expirado,se toma en cuenta el token y la ip
//La sesion se encuentra activa, el token debera coincidira al igual que la IP, no debe haber pasado mas de 3 minutos (logout automatico)
bool validarSesion(String token, String ip) {
  if (!sesionActual.activa) return false;
  if (sesionActual.token != token) return false;
  if (sesionActual.ip != ip) return false;
  
  if (millis() - sesionActual.tiempoCreacion > TIMEOUT_SESION) {
    cerrarSesion();
    return false;
  }
  
  return true;
}
//para el cierre de sesion
//se invalida la sesion actual y limpia los datos como token e ip
void cerrarSesion() {
  sesionActual.activa = false;
  sesionActual.token = "";
  sesionActual.ip = "";
}

//Funciones para la alarma de cierre del porton

void sonarAlertaCierre() {
  Serial.println("Se esta emitiendo una alerta de cierre...");
  
  // Patrón de pitidos: se realizan 3 pitidos cortos antes de cerrar
  for (int i = 0; i < 3; i++) {
    analogWriteFreq(800);  // Frecuencia en Hz
    analogWrite(BUZZER_PIN, 200);  // Volumen
    delay(200);  // Pitido cada 200ms
    
    analogWrite(BUZZER_PIN, 0);  // Silencio
    delay(150);  // Pausa de 150ms
  }
  
  noTone(BUZZER_PIN);
  Serial.println("Se ha realizado la alerta de cierre");
}

//Es el setup el cual se ejecuta una sola vez ya sea al encender o resetear el microcontrolador
//se encarga de iniciar todos los componentes del sistema
void setup() {
  Serial.begin(115200); //inicia la comunicacion serie a 115200 baudios y permite ver los mensajes o prints
  randomSeed(analogRead(A0));
  Serial.println("Iniciando sistema");
  
  pinMode(TRIG_PIN, OUTPUT); //se configura pin TRIGGER como salida (envia)
  pinMode(ECHO_PIN, INPUT); //se configura pin ECHIO como entrada (recibe)
  pinMode(BUZZER_PIN, OUTPUT); //se configura pin BUZZER como salida 
  
  servoPorton.attach(SERVO_PIN); //se vincula el objeto creado al inicio con el pin del servo
  servoPorton.write(velocidadDetener); //se detiene el servo (punto muerto que es representado a 90 grados
  delay(1000);
  
//variables de estado
  portonAbierto = false; //el porton va a iniciar como cerrado
  alarmaActiva = false; //la alarma va a iniciar como apagada
  deteccionAutomaticaBloqueada = false; //la deteccion automatica inicia como activo

//inicio de conexion a red WiFi e imprime en el monitor serie la ip que podra ser usada para la conexion a la pagina
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP del servidor: ");
  Serial.println(WiFi.localIP());

//se inicia el servidor web
  setupServerRoutes(); //configuran las rutas del servidor
  server.begin(); //se inicia el servidor
  Serial.println("Servidor web iniciado");
  Serial.println("Sistema listo");
}

//funcion loop que funcionara como bucle para el sistema, se ejecuta constantemente
void loop() {
  server.handleClient(); //se manejan las peticiones http entrantes
  manejarAlarma(); //si la alarma esta activa se hace sonar el buzzer
  distanciaActual = medirDistancia(); //se lee la distancia actual en cm
  
  //para el bloqueo de la deteccion por medio del sensor
  if (!deteccionAutomaticaBloqueada) {  // Solo funciona si no está bloqueado
    if (distanciaActual <= distanciaDeteccion && distanciaActual > 0) {
      if (!vehiculoDetectado) {
        vehiculoDetectado = true;
        Serial.print("Vehículo detectado a ");
        Serial.print(distanciaActual);
        Serial.println(" cm");
      }
      //si el porton se encuentra cerrado se abrira automaticamente
      if (!portonAbierto) {
        Serial.println("Abriendo portón automáticamente");
        abrirPorton();
      }
      //tiempo de apertura, se reinicia el contador
      tiempoApertura = millis();
    } else {
      vehiculoDetectado = false; //si no hay nada en el rango de deteccion se marca como no detectado
    }
    //para el cierre automatico, si el porton se encuentra abierto y no hay vehiculo
    if (portonAbierto && !vehiculoDetectado) {
      if (millis() - tiempoApertura >= tiempoEsperaAutomatico) {
        Serial.println("Cerrando portón automáticamente"); //cuando pasen 5 segundos desde que se abrio, cerrar
        cerrarPorton();
      }
    }
    //En caso de que se active el bloqueo de deteccion y se habilite solo la apertura manual
  } else {
    // Cuando está bloqueado, solo actualiza la detección pero no abre
    if (distanciaActual <= distanciaDeteccion && distanciaActual > 0) {
      if (!vehiculoDetectado) {
        vehiculoDetectado = true;
        Serial.println("Vehiculo detectado - Apertura bloqueada");
      }
    } else {
      vehiculoDetectado = false;
    }
  }
  
  delay(50);
}

//Configuracion de las rutas del servidor

void setupServerRoutes() {
  server.on("/", paginaLogin); //ruta raiz
  server.on("/login", handleLogin); //procesa formulario
  server.on("/control", paginaControl); //pagina principal
  server.on("/datos", HTTP_GET, enviarDatos); //Para obtener los datos en JSON
  server.on("/logout", handleLogout); //se cierra la sesion del usuario
  server.onNotFound(handleNotFound); //maneja las url no existentes
}

//Pagina de login, se obtienen los token de url, se verifican si son validos y si los datos son correctos se redirige al panel de control
 
void paginaLogin() {
  String clientIP = server.client().remoteIP().toString();
  
  String token = server.arg("token");
  if (token != "" && validarSesion(token, clientIP)) {
    server.sendHeader("Location", "/control?token=" + token);
    server.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Acceso al Portón</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  
  html += "<style>";
  html += "body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0;}";
  html += ".login-container{background:white;padding:30px;max-width:300px;margin:0 auto;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;}";
  html += ".security-badge{background:#4CAF50;color:white;padding:5px 10px;border-radius:5px;font-size:12px;margin-bottom:15px;}";
  html += "input{width:100%;padding:10px;margin:5px 0;border:1px solid #ccc;box-sizing:border-box;border-radius:5px;}";
  html += "button{width:100%;padding:12px;margin:10px 0;background:#333;color:white;border:none;cursor:pointer;border-radius:5px;font-size:16px;}";
  html += "button:hover{background:#555;}";
  html += ".error{color:#cc0000;margin:10px 0;}";
  html += "</style></head><body>";
  //contenedor del formulario
  html += "<div class='login-container'>";
  html += "<div class='security-badge'>Acceso seguro</div>";
  html += "<h1>Control de Portón</h1>";
  
  String error = server.arg("error");
  if (error == "1") {
    html += "<div class='error'>❌ Usuario o contraseña incorrectos</div>";
  }
 //formulario de login
  html += "<form action='/login' method='POST'>";
  html += "<input type='text' name='user' placeholder='Usuario' required autocomplete='username'>";
  html += "<input type='password' name='pass' placeholder='Contraseña' required autocomplete='current-password'>";
  html += "<button type='submit'>Ingresar</button>";
  html += "</form>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}
//se maneja el formulario, se validan las credenciales y se crea una sesion
void handleLogin() {
  String clientIP = server.client().remoteIP().toString();
  String usuario = server.arg("user");
  String pass = server.arg("pass");
  
  if (validarCredenciales(usuario, pass)) {
    crearSesion(clientIP); //se crea una nueva sesion con un token unico
    Serial.println("Se ha iniciado sesion exitosamente desde: " + clientIP);
    
    server.sendHeader("Location", "/control?token=" + sesionActual.token); //se redirige al panel de control con el token unico
    server.send(303);
  } else {
    Serial.println("Login fallido desde: " + clientIP); //en caso de inici ode sesion incorrecto
    server.sendHeader("Location", "/?error=1");
    server.send(303);
  }
}
//cierra la sesion y se redirige a la pantalla de login
void handleLogout() {
  cerrarSesion();
  Serial.println("Sesión cerrada");
  server.sendHeader("Location", "/");
  server.send(303);
}

//Se encarga de generar la pgagina de control del sistena, se piden datos validos para acceder y procesa
//comandos de abrir, cerrar, alarma, registro de distancia, bloqueo de deteccion
void paginaControl() {
  String clientIP = server.client().remoteIP().toString();
  String token = server.arg("token");
  
  if (!validarSesion(token, clientIP)) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  
  String accion = server.arg("accion"); //se obtiene la accion soliciutada desde la pagina
  //acciones para abrir el porton
  if (accion == "abrir") {
    if (!portonAbierto) {
      Serial.println("Comando desde pagina web: abrir");
      abrirPorton();
    }
  } else if (accion == "cerrar") {
    if (distanciaActual <= distanciaDeteccion && distanciaActual > 0) {
      Serial.println("Comando desde pagina web: cerrar - el camino esta bloqueado");
    } else if (portonAbierto) {
      Serial.println("Comando desde pagina web: cerrar"); //cierra en caso de que no se detecta nada
      cerrarPorton();
    }
  } else if (accion == "activar_alarma") {
    if (!alarmaActiva) {
      Serial.println("Comando desde pagina web: activar alarma");
      activarAlarma();
    }
  } else if (accion == "desactivar_alarma") {
    if (alarmaActiva) {
      Serial.println("Comando desde pagina web: desactivar alarma");
      desactivarAlarma();
    }
  } else if (accion == "bloquear_auto") {
    deteccionAutomaticaBloqueada = true;
    Serial.println("Apertura automática apagado - Solo apertura manual");
  } else if (accion == "desbloquear_auto") {
    deteccionAutomaticaBloqueada = false;
    Serial.println("Apertura automática encendida - apertura automatica disponible");
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Control Portón</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  
  html += "<style>";
  html += "body{font-family:Arial;text-align:center;padding:20px;background:#f0f0f0;}";
  html += ".container{background:white;padding:30px;max-width:500px;margin:0 auto;border-radius:10px;}";
  html += ".security-header{background:#4CAF50;color:white;padding:10px;border-radius:5px;margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;}";
  html += ".logout-btn{background:#cc0000;color:white;padding:5px 15px;border:none;border-radius:5px;cursor:pointer;text-decoration:none;font-size:12px;}";
  html += "h1{color:#333;font-size:24px;}";
  html += "h2{color:#555;font-size:18px;margin:20px 0 15px;}";
  html += ".estado{font-size:20px;margin:15px 0;padding:15px;border-radius:5px;}";
  html += ".abierto{background:#90EE90;color:#333;}";
  html += ".cerrado{background:#FFB6C1;color:#333;}";
  html += ".alarma-activa{background:#FF6B6B;color:white;animation:parpadeo 1s infinite;}";
  html += ".alarma-inactiva{background:#DDD;color:#666;}";
  html += ".bloqueado{background:#FF8C00;color:white;font-weight:bold;animation:parpadeo 1.5s infinite;}";
  html += ".desbloqueado{background:#90EE90;color:#333;}";
  html += "@keyframes parpadeo{0%,100%{opacity:1;}50%{opacity:0.5;}}";
  html += ".boton{display:inline-block;padding:12px 25px;margin:8px;border:none;color:#333;text-decoration:none;font-weight:bold;border-radius:5px;cursor:pointer;}";
  html += ".btn-abrir{background:#90EE90;}";
  html += ".btn-cerrar{background:#FFB6C1;}";
  html += ".btn-alarma-on{background:#FF6B6B;color:white;}";
  html += ".btn-alarma-off{background:#DDD;}";
  html += ".btn-bloquear{background:#FF8C00;color:white;}";
  html += ".btn-desbloquear{background:#90EE90;}";
  html += ".seccion{margin:30px 0;padding:20px 0;border-top:2px solid #ccc;}";
  html += ".distancia-display{font-size:48px;font-weight:bold;margin:20px 0;padding:20px;border-radius:10px;}";
  html += ".libre{background:#90EE90;color:#2d5016;}";
  html += ".cerca{background:#FFD700;color:#8B6914;}";
  html += ".bloqueado-dist{background:#FF6B6B;color:#8B0000;}";
  html += ".info-dist{font-size:24px;color:#333;margin:10px 0;}";
  html += ".info-box{background:#FFF3CD;border-left:4px solid #FFC107;padding:12px;margin:15px 0;text-align:left;border-radius:5px;}";
  html += "</style>";
  
  html += "<script>";
  html += "const token='" + token + "';";
  html += "function actualizarDatos(){";
  html += "  fetch('/datos?token='+token).then(r=>r.json()).then(data=>{";
  html += "    if(data.error){window.location.href='/';return;}";
  html += "    document.getElementById('estadoPorton').textContent=data.porton?'PORTON ABIERTO':'PORTON CERRADO';";
  html += "    document.getElementById('estadoPorton').className='estado '+(data.porton?'abierto':'cerrado');";
  html += "    document.getElementById('estadoAlarma').textContent=data.alarma?'ALARMA ACTIVADA':'ALARMA DESACTIVADA';";
  html += "    document.getElementById('estadoAlarma').className='estado '+(data.alarma?'alarma-activa':'alarma-inactiva');";
  html += "    document.getElementById('estadoBloqueo').textContent=data.bloqueado?'DETECCIÓN BLOQUEADA':'DETECCIÓN ACTIVA';";
  html += "    document.getElementById('estadoBloqueo').className='estado '+(data.bloqueado?'bloqueado':'desbloqueado');";
  html += "    var dist=parseInt(data.distancia);";
  html += "    var displayDist=document.getElementById('displayDistancia');";
  html += "    document.getElementById('infoDist').textContent=dist+' cm';";
  html += "    if(dist<=20 && dist>0){displayDist.className='distancia-display bloqueado-dist';displayDist.textContent='BLOQUEADO';}";
  html += "    else if(dist<=50 && dist>20){displayDist.className='distancia-display cerca';displayDist.textContent='CERCA';}";
  html += "    else{displayDist.className='distancia-display libre';displayDist.textContent='LIBRE';}";
  html += "  }).catch(()=>window.location.href='/');";
  html += "}";
  html += "setInterval(actualizarDatos,1000);";
  html += "window.onload=actualizarDatos;";
  html += "</script></head><body>";
  
  html += "<div class='container'>";
  
  html += "<div class='security-header'>";
  html += "<span>Sesión Activa</span>";
  html += "<a href='/logout' class='logout-btn'>Cerrar Sesión</a>";
  html += "</div>";
  
  html += "<h1>Panel de Control</h1>";
  
  // Monitor de Sensor
  html += "<div class='seccion'>";
  html += "<h2>Sensor de Distancia</h2>";
  html += "<div id='displayDistancia' class='distancia-display libre'>LIBRE</div>";
  html += "<div id='infoDist' class='info-dist'>" + String(distanciaActual) + " cm</div>";
  html += "</div>";
  
  // Portón
  html += "<div class='seccion'>";
  html += "<h2>Portón</h2>";
  html += "<div id='estadoPorton' class='estado " + String(portonAbierto ? "abierto" : "cerrado") + "'>";
  html += portonAbierto ? "PORTON ABIERTO" : "PORTON CERRADO";
  html += "</div>";
  html += "<a href='/control?token=" + token + "&accion=abrir' class='boton btn-abrir'>ABRIR</a>";
  html += "<a href='/control?token=" + token + "&accion=cerrar' class='boton btn-cerrar'>CERRAR</a>";
  html += "</div>";
  
  //Modo automatico para el sistema (se enciende el sensor)
  html += "<div class='seccion'>";
  html += "<h2>Modo Automático</h2>";
  html += "<div id='estadoBloqueo' class='estado " + String(deteccionAutomaticaBloqueada ? "bloqueado" : "desbloqueado") + "'>";
  html += deteccionAutomaticaBloqueada ? "DETECCIÓN BLOQUEADA" : "DETECCIÓN ACTIVA";
  html += "</div>";
  html += "<div class='info-box'>";
  html += deteccionAutomaticaBloqueada ? 
    "⚠️ El portón NO se abrirá automáticamente. Debe abrirse manualmente desde la app." :
    "✓ El portón se abrirá automáticamente al detectar un vehículo.";
  html += "</div>";
  html += "<a href='/control?token=" + token + "&accion=bloquear_auto' class='boton btn-bloquear'>BLOQUEAR DETECCION</a>";
  html += "<a href='/control?token=" + token + "&accion=desbloquear_auto' class='boton btn-desbloquear'>DESBLOQUEAR DETECCION</a>";
  html += "</div>";
  
  // Alarma
  html += "<div class='seccion'>";
  html += "<h2>Alarma</h2>";
  html += "<div id='estadoAlarma' class='estado " + String(alarmaActiva ? "alarma-activa" : "alarma-inactiva") + "'>";
  html += alarmaActiva ? "ALARMA ACTIVADA" : "ALARMA DESACTIVADA";
  html += "</div>";
  html += "<a href='/control?token=" + token + "&accion=activar_alarma' class='boton btn-alarma-on'>ACTIVAR ALARMA</a>";
  html += "<a href='/control?token=" + token + "&accion=desactivar_alarma' class='boton btn-alarma-off'>APAGAR ALARMA</a>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

//para la validacion de la sesion actual y api que retornara el estado actual del sistenma
//esto se usa principalmente por medio de javascript para que la pagina se actualice en tiempo real
//se retornan la distabncia en cm, el porton si esta abierto o cerrado, la alarma si esta apagada o encendida y el bloqueo de deteccion
void enviarDatos() {
  String clientIP = server.client().remoteIP().toString();
  String token = server.arg("token");
  //si la sesion no es valida se retorna un error y que la sesion no es valida
  if (!validarSesion(token, clientIP)) {
    server.send(401, "application/json", "{\"error\":\"sesion_invalida\"}");
    return;
  }
  
  String json = "{";
  json += "\"distancia\":" + String(distanciaActual) + ",";
  json += "\"porton\":" + String(portonAbierto ? "true" : "false") + ",";
  json += "\"alarma\":" + String(alarmaActiva ? "true" : "false") + ",";
  json += "\"bloqueado\":" + String(deteccionAutomaticaBloqueada ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

//Medicion, para medir la distancia que se detecta por el sensor
//el sensor ultrasonico retorna la distancia en cm, 999 en caso de algun error
//se envia un pulso de 10 microsegundos, el sensor emitira varios pulsos ultrasonicos
//el ECHO se enciende hasta que regresa el eco, se mide la duracion del pulso y se calcula la distancia en tiempo x la velocidad del sonido dividido en 2

long medirDistancia() {
  digitalWrite(TRIG_PIN, LOW); //pin en LOW
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); //se activa el pulso
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW); //se finaliza el pulso
  
  long duracion = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracion == 0) return 999; //si no hay respuesta se retorna error 999
  //velocidad de sonido estara como 0.034 cm/us se divide esto en dos debido a que el sonido va y vuelve
  long distancia = duracion * 0.034 / 2;
  if (distancia < 2 || distancia > 400) return 999;
  //se valida que la distancia este en un rango razonable para el sensor, en este caso el sensor usado solo llegada
  //a un maximo de alrededor 140 cm
  
  return distancia;
}

void abrirPorton() {
  Serial.println("Abriendo - Servo rotando...");
  servoPorton.write(velocidadAbrir);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  portonAbierto = true;
  tiempoApertura = millis();
  Serial.println("Portón abierto");
}

void cerrarPorton() {
  //se enciende primero la alarma para indicar que el porton cerrara
  sonarAlertaCierre();
  delay(500);  // Pausa de medio segundo después de la alerta
  
  Serial.println("Cerrando - Servo rotando...");
  servoPorton.write(velocidadCerrar);
  delay(tiempoRotacion);
  servoPorton.write(velocidadDetener);
  portonAbierto = false;
  Serial.println("Portón cerrado");
}

void activarAlarma() {
  alarmaActiva = true;
  tiempoInicioAlarma = millis();
  Serial.println("Alarma activada - Se desactivará en 10 segundos");
}

void desactivarAlarma() {
  alarmaActiva = false;
  noTone(BUZZER_PIN);
  analogWrite(BUZZER_PIN, 0);
  Serial.println("Alarma desactivada");
}

void manejarAlarma() {
  if (alarmaActiva) {
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
}

void handleNotFound() {
  String message = "Ruta no encontrada\n";
  message += "URI: " + server.uri();
  server.send(404, "text/plain", message);
}
