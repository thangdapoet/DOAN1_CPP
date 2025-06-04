#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <MQ135.h>

const char *ssid = "THANGDAPOET";
const char *password = "15112004";
const char *serverUrl = "http://192.168.16.223:8000/api/sensor-data";
const char *serverSessionUrl = "http://192.168.16.223:8000/session/";
int sessionId = -1;

#define DHTPIN 0
#define DHTTYPE DHT22
#define PIN_MQ135 34
#define BUTTON_PIN 5

enum GasType
{
  CO2,      // Carbon dioxide
  CO,       // Carbon monoxide
  NH3,      // Ammonia
  Toluen,      // Methane
  C6H6, 
  GAS_COUNT // bien dem
};

const char *gasNames[] = {
    "CO2",
    "CO",
    "NH3",
    "Toluen",
    "C6H6"};
// he so ab
const struct
{
  float a;
  float b;
} 
// gasCoefficients[] = {
//     {110.47, -2.862}, // CO2
//     {605.18, -3.937  },           // CO
//     {102.2, -2.473},            // NH3
//     {44.947, -3.445}, //toluen
//     {27.63596952, -3.874510413} ,          // c6h6
   
// };

gasCoefficients[] = {
  {103.286332, -2.961377594}, // CO2
  {371.4088184, -3.395023807 },           // CO
  {117.297584, -2.391300149},            // NH3
  {36.24540474, -3.827143444}, //toluen
  {27.63596952 , -3.874510413 },           // c6h6

};
float R0 = 194.27;   
float Rload = 36;
DHT dht(DHTPIN, DHTTYPE);
//MQ135 mq135_sensor(PIN_MQ135, R0, Rload );
MQ135 mq135_sensor(PIN_MQ135,R0, Rload);
LiquidCrystal_I2C lcd(0x27, 20, 4);
GasType currentGas = CO2; // mac dinh do co2
unsigned long lastGasChange = 0;
bool buttonPressed = false;

void showInitialDisplay()
{
  lcd.setCursor(0, 0);
  lcd.print("Nhiet do: ");
  lcd.setCursor(4, 1);
  lcd.print("Do am: ");
  lcd.setCursor(0, 2);
  lcd.print(gasNames[currentGas]);
  lcd.print(": ");
}

void updateLCD(float temp, float humi, float ppm)
{
  lcd.setCursor(9, 0);
  lcd.print(temp);
  lcd.print(" *C ");

  lcd.setCursor(10, 1);
  lcd.print(humi);
  lcd.print(" %  ");

  lcd.setCursor(7, 2);
  lcd.print(ppm);
  lcd.print(" ppm  ");

  // warning
  // lcd.setCursor(0, 3);
  // if ((currentGas == CO2 && ppm > 1100) ||
  //     (currentGas == CO && ppm > 32) ||
  //     (currentGas == NH3 && ppm > 35) ||
  //     (currentGas == Toluen && ppm > 200) ||
  //     (currentGas == C6H6 && ppm > 1))
  // {
  //   lcd.print("Dangerous! ");
  // }
  // else
  // {
  //   lcd.print("            ");
  // }
}

void connectToWiFi()
{
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 3);
  lcd.print("Connecting WiFi...");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected!");
  lcd.setCursor(0, 1);
  lcd.print("IP: ");
  lcd.print(WiFi.localIP());
  delay(2000);
  lcd.clear();
  showInitialDisplay();
}

float readGasPPM(float temp, float humi, GasType gasType)
{
  float RS = mq135_sensor.getResistance();
  
  float ratio = RS / R0;

  return gasCoefficients[gasType].a * pow(ratio, gasCoefficients[gasType].b);
}

void handleButtonPress()
{
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    buttonPressed = true;
    currentGas = (GasType)((currentGas + 1) % GAS_COUNT);

    lcd.setCursor(0, 2);
    lcd.print(gasNames[currentGas]);
    lcd.print(":       ");

    Serial.print("Changed gas type to: ");
    Serial.println(gasNames[currentGas]);
  }

  else if (digitalRead(BUTTON_PIN) == HIGH)
  {
    buttonPressed = false;
  }
}

void sendDataToServer(float temp, float humi, GasType displayGasType, int sessionId)
{
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  // tinh toan ppm gui len server
  float allPPM[GAS_COUNT];
  for (int i = 0; i < GAS_COUNT; i++)
  {
    allPPM[i] = readGasPPM(temp, humi, (GasType)i);
  }

  JsonDocument doc;
  doc["nhietdo"] = temp;
  doc["doam"] = humi;

  doc["ppm_co2"] = allPPM[CO2];
  doc["ppm_co"] = allPPM[CO];
  doc["ppm_nh3"] = allPPM[NH3];
  doc["ppm_toluen"] = allPPM[Toluen];
  doc["ppm_c6h6"] = allPPM[C6H6];
  doc["session_id"] = sessionId;

  String payload;
  serializeJson(doc, payload);

  Serial.println("Sending: " + payload);

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode == HTTP_CODE_OK)
  {
    String response = http.getString();
    Serial.println("Response: " + response);

    JsonDocument resDoc;
    deserializeJson(resDoc, response);

    if (strcmp(resDoc["status"], "success") == 0)
    {
      lcd.setCursor(0, 3);
      lcd.print("Sent OK        ");
    }
    else
    {
      lcd.setCursor(0, 3);
      lcd.print("Server error   ");
    }
  }
  else
  {
    Serial.printf("Error code: %d\n", httpResponseCode);
    lcd.setCursor(0, 3);
    lcd.print("Error: ");
    lcd.print(httpResponseCode);
  }

  http.end();
}

void createSession()
{
  HTTPClient http;
  http.begin(serverSessionUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  JsonDocument doc;
  doc["temp"] = 1;

  String payload;
  serializeJson(doc, payload);

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode == HTTP_CODE_OK)
  {
    String response = http.getString();
    Serial.println("Response: " + response);

    JsonDocument resDoc;
    deserializeJson(resDoc, response);

    if (strcmp(resDoc["status"], "success") == 0)
    {
      lcd.setCursor(0, 3);
      lcd.print("Sent OK        ");
      sessionId = resDoc["id"];
    }
    else
    {
      lcd.setCursor(0, 3);
      lcd.print("Server error   ");
    }
  }
  else
  {
    Serial.printf("Error code: %d\n", httpResponseCode);
    lcd.setCursor(0, 3);
    lcd.print("Error session: ");
    lcd.print(httpResponseCode);
  }
  http.end();
}


void setup()
{
  Serial.begin(9600);
  analogReadResolution(10); // thay thanh 10 bits
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dht.begin();
  lcd.init();
  lcd.backlight();
  showInitialDisplay();

  connectToWiFi();
}

void loop()
{
  handleButtonPress();

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature))
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DHT22 Error!");
    delay(2000);
    return;
  }

  float ppm = readGasPPM(temperature, humidity, currentGas);
  updateLCD(temperature, humidity, ppm);

  if(sessionId == -1){
    createSession();
  }

  if (WiFi.status() == WL_CONNECTED && sessionId != -1)
  {
    sendDataToServer(temperature, humidity, currentGas, sessionId);
  }
  else if (sessionId != -1)
  {
    lcd.setCursor(0, 3);
    lcd.print("WiFi disconnected!");
    connectToWiFi();
  }

  delay(5000);
}




// #include <Arduino.h>
// #include <MQ135.h>

// #define MQ135_PIN 34  // 
// float Rload = 36;    // 

// MQ135 mq135_sensor(MQ135_PIN, 1.0, Rload); // 

// void setup() {
//   Serial.begin(9600);
//   analogReadResolution(10);
//   delay(2000); // 

//   Serial.println("Calibrating R0...");
//   float rs = mq135_sensor.getResistance();

//   // Theo datasheet MQ135, ti le rs/ro trong kk sach
//   float R0 = rs / 0.6179480415;

//   Serial.print("RS = ");
//   Serial.println(rs);
//   Serial.print("R0 (calibrated) = ");
//   Serial.println(R0);

// }

// void loop() {
// }

 
