#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// Motor pinleri
#define motorA1 37  
#define motorA2 38  
#define motorB1 39  
#define motorB2 40  

// LED ve buzzer pinleri
#define ledPin1 19  
#define ledPin2 20  
bool ledState = false;  
#define buzzerPin 14  

// Ultrasonik sensör pinleri
#define TRIG_PIN 4
#define ECHO_PIN 3

// Ana ESP'nin oluşturacağı WiFi hotspot bilgileri
const char* ap_ssid = "ESP32_Control";
const char* ap_password = "12345678";

// Bağlı ESP'nin (remote) HTTP sunucu adresi 
// (remote cihazın IP’si 192.168.4.2 olarak varsayılmıştır)
const char* remoteServerName = "http://192.168.4.2/motion";

// Web sunucusu
WebServer server(80);

// Sensör ayarları
#define THRESHOLD 3           // cm cinsinden eşik
#define SMOOTHING_FACTOR 5    // Ölçüm ortalaması için örnek sayısı
#define CHECK_COUNT 3         // Ani değişiklik kontrolü için örnek sayısı

float previousDistances[CHECK_COUNT] = {0};
float smoothDistances[SMOOTHING_FACTOR] = {0};
int distanceIndex = 0;
int smoothIndex = 0;

bool isMoving = false;
unsigned long lastMovementTime = 0;

// Canlı hareket durumunu takip etmek için global değişken
bool lastMotionState = false;

// Motor komutları için tanımlamalar
enum MotorCommand {
  CMD_NONE,
  CMD_FORWARD,
  CMD_BACKWARD,
  CMD_TURN_RIGHT,
  CMD_TURN_LEFT,
  CMD_FULL_FORWARD,
  CMD_FULL_BACKWARD,
  CMD_FULL_RIGHT,
  CMD_FULL_LEFT,
  CMD_STOP
};

MotorCommand currentCommand = CMD_NONE;
unsigned long commandEndTime = 0;
bool commandIsMomentary = false;

// Buzzer asenkron işlemleri
bool buzzerActive = false;
int buzzerStep = 0;
unsigned long buzzerTimer = 0;

// Sensör beep işlemi
bool sensorBeepActive = false;
unsigned long sensorBeepTimer = 0;

// Ölçüm fonksiyonu: Ultrasonik sensör ile mesafe ölçümü yapar.
float measureDistance() {
  long duration;
  float distance;
  
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = (duration * 0.0343) / 2;
  
  if (distance <= 0 || distance > 400) {
    return previousDistances[distanceIndex];
  }
  return distance;
}

// Ölçüm değerlerini filtrelemek için basit ortalama hesaplama
float getFilteredDistance(float newDistance) {
  smoothDistances[smoothIndex] = newDistance;
  smoothIndex = (smoothIndex + 1) % SMOOTHING_FACTOR;
  float total = 0;
  for (int i = 0; i < SMOOTHING_FACTOR; i++) {
    total += smoothDistances[i];
  }
  return total / SMOOTHING_FACTOR;
}

void stopMotors() {
  digitalWrite(motorA1, LOW);
  digitalWrite(motorA2, LOW);
  digitalWrite(motorB1, LOW);
  digitalWrite(motorB2, LOW);
}

//
// --- Web Arayüzü ve Komut İşleyicileri ---
//
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'/>";  html += "<title>Arac Kontrol</title>";
  html += "<style> button { font-size: 1.5em; margin: 10px; padding: 10px 20px; } body { text-align: center; margin-top: 50px; } </style>";
  html += "<script>";
  html += "function sendCommand(cmd){";
  html += "  fetch('/' + cmd).then(response => response.text()).then(data => { console.log(data); });";
  html += "}";
  html += "document.addEventListener('keydown', function(event){";
  html += "  if(event.defaultPrevented)return;";
  html += "  switch(event.key){";
  html += "    case 'ArrowUp': sendCommand('forward'); event.preventDefault(); break;";
  html += "    case 'ArrowDown': sendCommand('backward'); event.preventDefault(); break;";
  html += "    case 'ArrowLeft': sendCommand('left'); event.preventDefault(); break;";
  html += "    case 'ArrowRight': sendCommand('right'); event.preventDefault(); break;";
  html += "    case ' ': sendCommand('stop'); event.preventDefault(); break;";
  html += "    case 'b': case 'B': sendCommand('buzz'); event.preventDefault(); break;";
  html += "    case 'l': case 'L': sendCommand('toggleLED'); event.preventDefault(); break;";
  html += "    case 's': case 'S': sendCommand('selector'); event.preventDefault(); break;";
  html += "    case 'm': case 'M': sendCommand('motorSound'); event.preventDefault(); break;";
  html += "  }";
  html += "}, true);";
  html += "</script></head><body>";
  html += "<h1>Arac Kontrol</h1>";
  html += "<button onclick=\"sendCommand('forward')\">&#8593; İleri</button><br>";
  html += "<button onclick=\"sendCommand('left')\">&#8592; Sol</button>";
  html += "<button onclick=\"sendCommand('stop')\">Durdur</button>";
  html += "<button onclick=\"sendCommand('right')\">Sağ &#8594;</button><br>";
  html += "<button onclick=\"sendCommand('backward')\">&#8595; Geri</button><br><br>";
  html += "<button onclick=\"sendCommand('toggleLED')\">LED Aç/Kapa</button> ";
  html += "<button onclick=\"sendCommand('buzz')\">Buzzer</button> ";
  html += "<button onclick=\"sendCommand('selector')\">Selektör</button> ";
  html += "<button onclick=\"sendCommand('motorSound')\">Motor Sesi</button>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleForward() {
  digitalWrite(motorA1, HIGH);
  digitalWrite(motorB1, HIGH);
  digitalWrite(motorA2, LOW);
  digitalWrite(motorB2, LOW);
  currentCommand = CMD_FORWARD;
  commandIsMomentary = true;
  commandEndTime = millis() + 500;
  isMoving = true;
  lastMovementTime = millis();
  server.send(200, "text/plain", "İleri");
}

void handleBackward() {
  digitalWrite(motorA1, LOW);
  digitalWrite(motorB1, LOW);
  digitalWrite(motorA2, HIGH);
  digitalWrite(motorB2, HIGH);
  currentCommand = CMD_BACKWARD;
  commandIsMomentary = true;
  commandEndTime = millis() + 500;
  isMoving = true;
  lastMovementTime = millis();
  server.send(200, "text/plain", "Geri");
}

void handleLeft() {
  digitalWrite(motorA1, LOW);
  digitalWrite(motorA2, HIGH);
  digitalWrite(motorB1, HIGH);
  digitalWrite(motorB2, LOW);
  currentCommand = CMD_TURN_LEFT;
  commandIsMomentary = true;
  commandEndTime = millis() + 200;
  isMoving = true;
  lastMovementTime = millis();
  server.send(200, "text/plain", "Sol");
}

void handleRight() {
  digitalWrite(motorA1, HIGH);
  digitalWrite(motorA2, LOW);
  digitalWrite(motorB1, LOW);
  digitalWrite(motorB2, HIGH);
  currentCommand = CMD_TURN_RIGHT;
  commandIsMomentary = true;
  commandEndTime = millis() + 200;
  isMoving = true;
  lastMovementTime = millis();
  server.send(200, "text/plain", "Sağ");
}

void handleStop() {
  stopMotors();
  currentCommand = CMD_STOP;
  commandIsMomentary = true;
  commandEndTime = millis() + 2000;
  isMoving = false;
  server.send(200, "text/plain", "Durduruldu");
}

void handleToggleLED() {
  ledState = !ledState;
  digitalWrite(ledPin1, ledState ? HIGH : LOW);
  digitalWrite(ledPin2, ledState ? HIGH : LOW);
  server.send(200, "text/plain", ledState ? "LED Açık" : "LED Kapalı");
}

void handleBuzz() {
  buzzerActive = true;
  buzzerStep = 0;
  buzzerTimer = millis() + 200;
  tone(buzzerPin, 500);
  server.send(200, "text/plain", "Buzzer Çalıyor");
}

void handleSelector() {
  static bool selectorState = false;
  selectorState = !selectorState;
  String msg = selectorState ? "Selektör: Durum 1" : "Selektör: Durum 0";
  server.send(200, "text/plain", msg);
}

// --- Asenkron HTTP Güncelleme Görevi ---
void HTTPUpdateTask(void* pvParameters) {
  bool motionState = *((bool*) pvParameters);
  free(pvParameters);
  
  HTTPClient http;
  String url = remoteServerName;
  url += (motionState ? "?state=1" : "?state=0");
  Serial.print("Hareket durumu gönderiliyor: ");
  Serial.println(url);
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    Serial.print("Remote HTTP yanıt: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Remote HTTP hatası: ");
    Serial.println(httpResponseCode);
  }
  http.end();
  
  vTaskDelete(NULL);
}

void sendMotionUpdate(bool motionState) {
  bool* state = (bool*) malloc(sizeof(bool));
  if (state == NULL) {
    Serial.println("Memory allocation failed");
    return;
  }
  *state = motionState;
  xTaskCreate(HTTPUpdateTask, "HTTPUpdateTask", 4096, state, 1, NULL);
}

// Sensör okuma fonksiyonu: Ölçümü yapar, mevcut durumu belirler ve durum değişmişse remote ESP'ye iletir.
void sensorCheck() {

  
  float rawDistance = measureDistance();
  float currentDistance = getFilteredDistance(rawDistance);
  
  float previousAvg = 0;
  for (int i = 0; i < CHECK_COUNT; i++) {
    previousAvg += previousDistances[i];
  }
  previousAvg /= CHECK_COUNT;
  
  bool currentMotionState = false;
  if (previousAvg != 0 && abs(currentDistance - previousAvg) > THRESHOLD) {
    currentMotionState = true;
  }
  
  if (currentMotionState) {
    Serial.println("Hareket Var");
  } else {
    Serial.println("Hareket Yok");
  }
  
  if (currentMotionState != lastMotionState) {
    sendMotionUpdate(currentMotionState);
    lastMotionState = currentMotionState;
  }
  
  previousDistances[distanceIndex] = currentDistance;
  distanceIndex = (distanceIndex + 1) % CHECK_COUNT;
}

void updateMotorCommand() {
  if (commandIsMomentary && currentCommand != CMD_NONE && millis() >= commandEndTime) {
    stopMotors();
    currentCommand = CMD_NONE;
  }
}

void updateBuzzer() {
  if (buzzerActive && millis() >= buzzerTimer) {
    if (buzzerStep == 0) {
      noTone(buzzerPin);
      buzzerStep = 1;
      buzzerTimer = millis() + 300;
    } else if (buzzerStep == 1) {
      tone(buzzerPin, 500);
      buzzerStep = 2;
      buzzerTimer = millis() + 150;
    } else if (buzzerStep == 2) {
      noTone(buzzerPin);
      buzzerActive = false;
    }
  }
}

void updateSensorBeep() {
  if (sensorBeepActive && millis() >= sensorBeepTimer) {
    noTone(buzzerPin);
    sensorBeepActive = false;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Pin ayarları
  pinMode(motorA1, OUTPUT); pinMode(motorA2, OUTPUT);
  pinMode(motorB1, OUTPUT); pinMode(motorB2, OUTPUT);
  pinMode(ledPin1, OUTPUT); pinMode(ledPin2, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  
  // ESP'yi AP modunda hotspot olarak başlatıyoruz.
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP Adresi: ");
  Serial.println(IP);
  
  // Web sunucusu endpoint'leri
  server.on("/", handleRoot);
  server.on("/forward", handleForward);
  server.on("/backward", handleBackward);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/stop", handleStop);
  server.on("/toggleLED", handleToggleLED);
  server.on("/buzz", handleBuzz);
  server.on("/selector", handleSelector);
  
  server.begin();
  Serial.println("Web sunucu baslatildi.");
}

void loop() {
  server.handleClient();
  sensorCheck();
  updateSensorBeep();
  updateMotorCommand();
  updateBuzzer();
  
  if (isMoving && millis() - lastMovementTime >= 2000) {
    isMoving = false;
  }
  
  delay(10);
}
