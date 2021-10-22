#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <WiFiUdp.h>

//Macros
#define LED 2
#define LED_DELAY 1000
#define BUZZER 4
#define BOTON 0
#define LE 16
#define CLK 14
#define SDI 12
#define OE 13
#define TYPE 1 //Tipo de dispositivo

//FRECUENCIA DE CADA UNA DE LAS NOTAS DESDE C5 HASTA C6
#define C5 523
#define C5S 554
#define D5 587
#define D5S 622
#define E5 659
#define F5 698
#define F5S 739
#define G5 783
#define G5S 830
#define A5 880
#define A5S 932
#define B5 987
#define C6 1046

//VALOR DE TIEMPOS DE LAS NOTAS
#define REDONDA 2000
#define BLANCA REDONDA / 2
#define NEGRA BLANCA / 2
#define CORCHEA NEGRA / 2
#define SEMICORCHEA CORCHEA / 2
#define SILENCIO 10

//Constantes
const String DEVICE = "RelojcinWifi";

//Variables
bool reboot = false;
bool onWiFiDisconnectedFlg = false;
bool onWiFiGotIpFlg = false;
int pwmValue = 1024;
int songStatus = 0;
int zeroHour = 0;
String deviceName;
String pass;
String ssid;
unsigned long currentTime;

//Estructura
struct MemoryData
{
  String ssid;
  String pass;
  bool alarm;
  int hzone;
  int alarmHour;
  int alarmMinute;
} memoryData;

//Objetos
WiFiEventHandler cbGotIp;
WiFiEventHandler cbDisconnected;
WiFiClient client;
WiFiServer tcpServer(2000);
WiFiUDP udp;

//Declaración de funciones
bool setData(String data);
String handleRequest(String request);
String getData();
void connectToWifi();
void getMemoryData();
void handleLed();
void handleSong();
void handleTcpServer();
void handleTime();
void handleUdp();
void handleWiFi();
void onWiFiConnected();
void saveData();
void serialHour(int hour, int minute);
void soundBuzzer(int frequ, int time);
void syncHour(int hour);
void setBrightness(int pwmVal);

//Callbacks WiFi
void onStationGotIp(const WiFiEventStationModeGotIP &evt)
{
  onWiFiGotIpFlg = true;
}

void onStationDisconnected(const WiFiEventStationModeDisconnected &evt)
{
  onWiFiDisconnectedFlg = true;
}

void setup()
{
  //Inicializar pines
  pinMode(LED, OUTPUT);
  pinMode(LE, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(SDI, OUTPUT);
  pinMode(OE, OUTPUT);
  pinMode(BOTON, INPUT);
  digitalWrite(LED, LOW);
  setBrightness(1024);

//Definir nombre
#if TYPE == 0
  deviceName = DEVICE + "_C";
#else
  deviceName = DEVICE + "_M";
#endif

  //Mostrar hora
  serialHour(12, 48);

  //Inicializar puerto serial
  Serial.begin(115200);

  //Inicializar datos de memoria
  getMemoryData();

  //Inicializar callBack's de WiFi
  cbGotIp = WiFi.onStationModeGotIP(&onStationGotIp);
  cbDisconnected = WiFi.onStationModeDisconnected(&onStationDisconnected);

  //Conectarse a WiFi
  connectToWifi();
}

void loop()
{
  //Manejar led de actividad
  handleLed();

  //Manejar WiFi
  handleWiFi();

  //Manejar tiempo
  handleTime();

  //Manejar cancion
  handleSong();

  //Manejar UDP
  handleUdp();

  //Manejar comunicacion TCP
  handleTcpServer();

  //Manejar reinicio
  if (reboot)
  {
    ESP.reset();
  }

  //Estabilizacion
  yield();
}

//Definición de funciones
void connectToWifi()
{
  //Reiniciar WiFi
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP_STA);

  //Desactivar reconexión
  WiFi.setAutoReconnect(false);

  //Iniciar AP
  WiFi.softAP(deviceName.c_str(), "12345678");

  //Iniciar servidor TCP
  tcpServer.begin();

  //Conectarse al WiFi
  if (memoryData.ssid != "")
  {
    WiFi.begin(memoryData.ssid, memoryData.pass);
    Serial.println("Conectandose a WiFi");
    Serial.println("SSID -> " + memoryData.ssid);
  }
  else
  {
    Serial.println("No hay red para conectarse");
  }

  //Iniciar servidor UDP para identificación
  udp.begin(2400);
}

void getMemoryData()
{
  //Inicializar memoria EEPROM
  EEPROM.begin(512);

  String data = "";
  for (int addr = 0; addr < 512; addr++)
  {
    //Obtener caracter
    char mChar = (char)EEPROM.read(addr);

    //Evaluar si es nulo
    if (mChar == 0)
    {
      //Terminar ciclo
      break;
    }
    else
    {
      //Agregar a cadena
      data += mChar;
    }
  }

  Serial.print("\nDatos de memoria -> ");
  Serial.println(data);

  //Intentar convertir a JSON
  if (!setData(data))
  {
    //Definir valores default si falló...
    memoryData.ssid = "";
    memoryData.pass = "";
    memoryData.hzone = 0;
    memoryData.alarm = false;
    memoryData.alarmHour = 0;
    memoryData.alarmMinute = 0;
  }
}

void saveData()
{
  //Convertir a JSON
  String jsonString = getData();

  //Variable que guarda la direccion de memoria actual
  int addr = 0;

  //Almacenar en memoria
  for (addr = 0; addr < jsonString.length(); addr++)
  {
    //Almacenar caracter
    EEPROM.write(addr, (char)jsonString.charAt(addr));
  }

  //Almacenar nulo
  EEPROM.write(addr, 0);

  //Guardar cambios
  EEPROM.commit();
}

void handleLed()
{
  static unsigned long timeRef = 0;

  if (millis() - timeRef > LED_DELAY)
  {
    digitalWrite(LED, !digitalRead(LED));
    timeRef = millis();
  }
}

String handleRequest(String request)
{
  //Preparar respuesta
  String response, message, status;

  //Crear un buffer dinámico
  DynamicJsonBuffer requestBuffer;
  DynamicJsonBuffer responseBuffer;

  //Hacer conversión a objeto JSON
  JsonObject &root = requestBuffer.parseObject(request);

  //Validar conversión
  if (!root.success())
  {
    message = "invalid request!";
    status = "error";
  }
  else
  {
    String key = root["key"];

    if (key == "version")
    {
      message = "3.1.0";
      status = "ok";
    }
    else if (key == "play_song")
    {
      //Reproducir cancion
      songStatus = 1;
      message = "playing song";
      status = "ok";
    }
    else if (key == "stop_song")
    {
      //Detener cancion
      songStatus = 0;
      message = "stopping song";
      status = "ok";
    }
    else if (key == "get_data")
    {
      //Retornar
      message = getData();
      status = "ok";
    }
    else if (key == "set_data")
    {
      if (setData(root["data"]))
      {
        //Guardar en memoria
        saveData();
        message = "Guardado correctamente";
        status = "ok";
      }
      else
      {
        message = "Error al guardar";
        status = "error";
      }
    }
    else if (key == "reboot")
    {
      //Retornar
      reboot = true;
      message = "Reiniciando...";
      status = "ok";
    }
    else if (key == "sync_hour")
    {
      //Obtener hora
      if (root.containsKey("hour"))
      {
        syncHour(root["hour"]);
        message = "Hora sincronizada correctamente";
        status = "ok";
      }
      else
      {
        message = "Error al sincronizar la hora";
        status = "error";
      }
    }
    else if (key == "set_brightness") 
    {
      if (root.containsKey("value")) 
      {
        setBrightness(root["value"]);
        saveData();
        message = "Brillo cambiado correctamente";
        status = "ok";
      } 
      else 
      {
        message = "Error al aplicar nuevo nivel de brillo";
        status = "error";
      }
    }
    else
    {
      message = "invalid key";
      status = "error";
    }
  }

  //Preparar objeto JSON
  JsonObject &responseRoot = responseBuffer.createObject();
  responseRoot["message"] = message;
  responseRoot["status"] = status;

  //Pasar a "response"
  responseRoot.prettyPrintTo(response);

  //Devolver respuesta
  return response;
}

String getData()
{
  String data;

  //Preparar objeto JSON
  DynamicJsonBuffer rootBuffer;
  JsonObject &root = rootBuffer.createObject();

  //Armar diccionario
  root["ssid"] = memoryData.ssid;
  root["pass"] = memoryData.pass;
  root["hzone"] = memoryData.hzone;
  root["alarm"] = memoryData.alarm;
  root["alarm_hour"] = memoryData.alarmHour;
  root["alarm_minute"] = memoryData.alarmMinute;
  root["pwm_value"] = pwmValue;

  //Pasar a String
  root.prettyPrintTo(data);

  //Retornar datos
  return data;
}

bool setData(String data)
{
  //Preparar objeto JSON
  DynamicJsonBuffer rootBuffer;
  JsonObject &root = rootBuffer.parseObject(data);

  if (!root.success())
  {
    return false;
  }
  else
  {
    //Obtener datos de objeto JSON
    String theSsid = root["ssid"];
    String thePass = root["pass"];
    memoryData.ssid = theSsid;
    memoryData.pass = thePass;
    memoryData.hzone = root["hzone"];
    memoryData.alarm = root["alarm"];
    memoryData.alarmHour = root["alarm_hour"];
    memoryData.alarmMinute = root["alarm_minute"];

    if (root.containsKey("pwm_value")) {
      int pwmValue = root["pwm_value"];
      setBrightness(pwmValue);
    }
  }

  return true;
}

void handleTcpServer()
{
  //Evaluar si hay algo que llega por WiFi
  client = tcpServer.available();

  //Si hay algo, leer
  if (client)
  {
    unsigned long timeout = millis();
    Serial.println("Nueva conexion");

    //Miestras este conectado...
    while (client.connected() && millis() - timeout < 5000)
    {
      //Obtener datos del buffer
      int bufferSize = client.available();
      if (bufferSize)
      {
        String values = "";
        Serial.print("Request -> ");

        //Obtener request byte por byte
        while (bufferSize--)
        {
          char currentChar = (char)client.read();
          values += currentChar;
        }

        //Mostrar request
        Serial.println(values);

        //Preparar variable para respuesta
        String response = handleRequest(values);

        //Responder
        client.print(response);

        //Respondido!
        soundBuzzer(2300, 250);

        break;
      }
    }

    //Terminar conexión
    client.stop();
  }
}

void handleTime()
{
  static unsigned long timeRef = 0;

  if (millis() - timeRef >= 1000)
  {
    //Actualizar base de tiempo
    timeRef = millis();

    //Obtener tiempo
    time_t now;

    //Asignarlo
    time(&now);

    //Mostrar EPOCH
    Serial.print("epoch -> ");
    Serial.println(now);

    //Convertir a hora leíble
    int horas = ((now % 86400L) / 3600);
    int minutos = (now % 3600) / 60;
    int segundos = now % 60;
    zeroHour = horas; // Asignar como hora sin ajuste de zona horaria

    //Ajustar hora
    if (memoryData.hzone != 0)
    {
      for (int i = 0; i < abs(memoryData.hzone); i++)
      {
        memoryData.hzone < 0 ? horas-- : horas++;
        if (horas < 0)
          horas = 23;
        if (horas > 23)
          horas = 0;
      }
    }

    //Mostrar hora leíble
    Serial.print("time -> ");
    Serial.print(horas);
    Serial.print(":");
    Serial.print(minutos);
    Serial.print(":");
    Serial.print(segundos);
    Serial.println();

    //Mostrar hora en leds
    serialHour(horas, minutos);

    //Evaluar si debe sonar la alarma
    if (horas == memoryData.alarmHour && memoryData.alarmMinute == minutos && segundos == 0 && memoryData.alarm)
    {
      songStatus = 1;
    }
  }
}

void handleUdp()
{
  //Esperar un paquete UDP
  int packetSize = udp.parsePacket();

  //Validar si existe alguno
  if (packetSize)
  {
    //Definir respuesta
    const String replyBuffer = "{\"device\":\"" + DEVICE + "\", \"device\":\"" + deviceName + "\"}";

    //Buffer para recibir paquete
    char packetBuffer[255];

    //Leer el paquete
    int len = udp.read(packetBuffer, 255);
    if (len > 0)
    {
      packetBuffer[len] = 0;
    }

    //Enviar una respuesta
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(replyBuffer.c_str(), replyBuffer.length());
    udp.endPacket();
  }
}

void handleWiFi()
{
  if (onWiFiGotIpFlg)
  {
    onWiFiConnected();
    onWiFiGotIpFlg = false;
  }

  if (onWiFiDisconnectedFlg)
  {
    Serial.println("Desconectado del WiFi");
    onWiFiDisconnectedFlg = false;
  }
}

void onWiFiConnected()
{
  //Sonar buzzer
  soundBuzzer(2300, 2000);

  //Mostrar IP
  Serial.println("IP asignada!");
  Serial.print("IP -> ");
  Serial.println(WiFi.localIP());

  //Definir reconexión
  WiFi.setAutoReconnect(true);

  //Configurar reloj
  configTime(0, 0, "pool.ntp.org");
}

void handleSong()
{
  static unsigned int noteidx = 0;
  static unsigned long timeRef = 0;

  int notas[53] = {B5, A5, G5, 0, B5, 0, B5, 0, B5, A5, G5, A5, G5, 0, G5, A5, G5, F5S, 0, B5, 0, B5, 0, B5, A5, G5, A5, G5, 0, G5, D5, 0, D5, F5S, G5,
                   B5, 0, B5, A5, G5, A5, G5, 0, G5, C6, B5, G5, A5, G5, 0, G5, A5, G5};

  int tiempos[53] = {BLANCA, BLANCA, NEGRA, CORCHEA, SEMICORCHEA, SILENCIO, SEMICORCHEA, SILENCIO, CORCHEA, SEMICORCHEA, CORCHEA, SEMICORCHEA, CORCHEA,
                     SILENCIO, BLANCA, BLANCA, CORCHEA, CORCHEA, CORCHEA, SEMICORCHEA, SILENCIO, SEMICORCHEA, SILENCIO, CORCHEA, SEMICORCHEA, CORCHEA, SEMICORCHEA, CORCHEA,
                     SILENCIO, CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, SILENCIO, CORCHEA, CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, CORCHEA, SILENCIO, CORCHEA + SEMICORCHEA,
                     CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, SILENCIO, CORCHEA, CORCHEA + SEMICORCHEA, CORCHEA + SEMICORCHEA, CORCHEA,
                     CORCHEA + SEMICORCHEA, CORCHEA, CORCHEA + SEMICORCHEA, BLANCA, BLANCA, REDONDA};

  //Reproducir
  switch (songStatus)
  {
  //STOP
  case 0:
    noTone(BUZZER);
    noteidx = 0;
    break;

  //PLAY
  case 1:
    noteidx = 0;
    songStatus = 2;
    break;

  //PLAYING
  case 2:
    tone(BUZZER, notas[noteidx]);
    timeRef = millis();
    songStatus = 3;
    break;

  //SUSTAIN
  case 3:
    if (millis() - timeRef > tiempos[noteidx])
    {
      noteidx++;
      if (noteidx == 53)
      {
        songStatus = 0;
      }
      else
      {
        songStatus = 2;
      }
    }
    break;
  }
}

//Mostrar la hora en ledDrivers
void serialHour(int hour, int minute)
{
#if TYPE == 0
  byte hourH = hour / 10;
  byte hourL = hour % 10;
  byte minuteH = minute / 10;
  byte minuteL = minute % 10;

  short limites[4] = {4, 3, 4, 2};
  byte clockData[4] = {minuteL, minuteH, hourL, hourH};

  digitalWrite(SDI, LOW);
  digitalWrite(CLK, LOW);
  digitalWrite(LE, LOW);

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < limites[i]; j++)
    {
      digitalWrite(SDI, clockData[i] & 0x01);
      clockData[i] >>= 1;

      digitalWrite(CLK, HIGH);
      delay(1);
      digitalWrite(CLK, LOW);
    }
  }

  digitalWrite(LE, HIGH);
  delay(1);
  digitalWrite(LE, LOW);
#else
  byte hourH = hour / 10;
  byte hourL = hour % 10;
  byte minuteH = minute / 10;
  byte minuteL = minute % 10;

  digitalWrite(SDI, LOW);
  digitalWrite(CLK, LOW);
  digitalWrite(LE, LOW);

  byte clockData[4] = {hourH, hourL, minuteH, minuteL};

  int numPosition = 1;

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      digitalWrite(SDI, clockData[j] & numPosition);

      digitalWrite(CLK, HIGH);
      delay(1);
      digitalWrite(CLK, LOW);
    }
    numPosition *= 2;
  }

  digitalWrite(LE, HIGH);
  delay(1);
  digitalWrite(LE, LOW);
#endif
}

void soundBuzzer(int frequ, int time)
{
  tone(BUZZER, frequ);
  delay(time);
  noTone(BUZZER);
}

void syncHour(int syncedHour)
{
  // Calcular nueva zona horaria
  int newHourzone = syncedHour - zeroHour;
  memoryData.hzone = newHourzone;

  // Guardar cambios
  saveData();
}

void setBrightness(int pwmVal)
{
  analogWrite(OE, 1024 - pwmVal);
  pwmValue = pwmVal;
}

//{"key":"version"}

//{"key":"play_song"}

//{"key":"stop_song"}

/*
  {"key":"set_data","data":{"ssid":"PG-WiFi","pass":"F@m1l14P@rr4_2409","hzone":-5,"alarm":true,"alarm_hour":10,"alarm_minute":00}}
*/

//{"key":"get_data"}

//{"key":"reboot"}

//{"key":"sync_hour", "hour": 10}  **La hora debe estar en formato 24h**

//{"key":"set_brightness", "value":50}