#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>

// WiFi AP bilgileri (Ana ESP'nin oluşturduğu hotspot)
const char* ssid = "ESP32_Control";
const char* password = "12345678";

// Web sunucusu (80 port)
WebServer server(80);

// LED pini (örneğin, dahili LED ya da harici LED için)
#define LED_PIN 2

// LCD ekran: LiquidCrystal_I2C kütüphanesi ile 16x2 LCD (genellikle I2C adresi 0x27)
// ESP32'de I2C: SDA = 21, SCL = 22
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Hareket bildirimi için sayaç ve zamanlayıcı değişkenleri
int motionCount = 0;
unsigned long lastMotionTimestamp = 0;
bool yavaslaDisplayed = false;

// Gelen HTTP isteğini alan endpoint fonksiyonu
void handleMotion() {
  Serial.println("Hareket bildirimi alındı!");
  
  // LED kısa süre yanıp söner (1ms)
  digitalWrite(LED_PIN, HIGH);
  delay(1);
  digitalWrite(LED_PIN, LOW);
  
  // Her hareket bildiriminde sayaç artırılır ve zaman güncellenir
  motionCount++;
  lastMotionTimestamp = millis();
  
  server.send(200, "text/plain", "Motion received");
}

void setup() {
  Serial.begin(115200);
  
  // LED pini çıkış olarak ayarlanır
  pinMode(LED_PIN, OUTPUT);
  
  // LCD ekranı başlat
  lcd.init();
  lcd.backlight();
  lcd.clear();
  // Başlangıçta LCD'ye "Iyi yolculuklar" yazalım
  lcd.setCursor(0, 0);
  lcd.print("Iyi yolculuklar");
  
  // WiFi'ye bağlan (Ana ESP'nin oluşturduğu hotspot)
  WiFi.begin(ssid, password);
  Serial.print("WiFi'ye baglaniliyor");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Baglandi, IP: ");
  Serial.println(WiFi.localIP());
  
  // "/motion" endpoint'ini ayarla
  server.on("/motion", handleMotion);
  server.begin();
  Serial.println("HTTP sunucu baslatildi.");
}

void loop() {
  server.handleClient();
  
  // Eğer 3 veya daha fazla hareket bildirimi geldiyse ve "Yavasla, yaya var" henüz gösterilmediyse LCD'ye yaz
  if (motionCount >= 3 && !yavaslaDisplayed) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Yavasla yaya var");
    yavaslaDisplayed = true;
  }
  
  // Eğer "Yavasla, yaya var" gösteriliyorsa ve son bildirimin üzerinden 4 saniye geçtiyse,
  // LCD'ye "Iyi yolculuklar" yaz, sayaçları sıfırla
  if (yavaslaDisplayed && (millis() - lastMotionTimestamp >= 4000)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Iyi yolculuklar");
    yavaslaDisplayed = false;
    motionCount = 0;
  }
  
  delay(10);
}
