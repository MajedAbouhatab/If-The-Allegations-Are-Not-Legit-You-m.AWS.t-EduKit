#include <M5Core2.h>
#include <HTTPClient.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFiConnect.h"
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "secrets.h"

WiFiClientSecure net;
MQTTClient mqtt = MQTTClient(512);
WiFiClientSecure client;
HTTPClient http;
AudioGeneratorMP3 *mp3;
AudioOutputI2S *out;
WiFiConnect wc;
StaticJsonDocument<1000> doc;

char jsonBuffer[512], *Tables[] = {"engines", "     The Engines of\n     Our Ingenuity",
                                   "mmwr", " Morbidity and Mortality\n     Weekly Report",
                                   "cidrap", "  Center for Infectious\n  Disease Research and\n  Policy"};
int TableNumber = -1;
bool EnableSpeaker = true, ShowQRCode;

// Write text on LCD
void LCDText(const char *txt, int C, int X, int Y, int S)
{
  M5.Lcd.setTextColor(C);
  M5.Lcd.setCursor(X, Y);
  M5.Lcd.setTextSize(S);
  M5.Lcd.printf(txt);
}

// Main running screen
void ShowMainScreen(void)
{
  M5.Lcd.fillScreen(BLACK);
  LCDText(Tables[TableNumber + 1], RED, 0, 5, 2);
  LCDText(("\n          " + doc["EpisodeNumber"].as<String>()).c_str(), BLUE, M5.Lcd.getCursorX(), M5.Lcd.getCursorY(), 2);
  LCDText(("\n" + doc["Title"].as<String>()).c_str(), BLUE, M5.Lcd.getCursorX(), M5.Lcd.getCursorY(), 2);
  LCDText((String(EnableSpeaker ? "Mute  " : "Unmute") + (String("            QR Code             Skip"))).c_str(), WHITE, 30, 215, 1);
}

// Getting the last mp3 source used
void MessageReceived(String &topic, String &payload)
{
  deserializeJson(doc, payload);
  TableNumber = doc["state"]["TableNumber"].as<int>();
}

// Make HTTPS request then save the response
void GetJSON(String S)
{
  http.begin(client, S);
  http.GET();
  deserializeJson(doc, http.getString());
  http.end();
}

// Send the outcome to AWS
void TrackingMQTT(String payload)
{
  String TempString = doc["EpisodeNumber"].as<String>();
  GetJSON("https://worldclockapi.com/api/json/utc/now");
  TempString = doc["currentDateTime"].as<String>() + "_ESP_" + String(ESP_getChipId()) + "_" + Tables[TableNumber] + "_" + TempString;
  doc.clear();
  doc["Outcome"] = payload;
  doc["Client"] = TempString;
  serializeJson(doc, jsonBuffer);
  // Send outcome to Tracking table
  mqtt.publish("Tracking", jsonBuffer);
  delay(1000);
  // This is the end; time to reboot
  ESP.restart();
}

// Handle Mute button
void MuteUnmute(Event &e)
{
  if (!ShowQRCode)
  {
    EnableSpeaker = !EnableSpeaker;
    M5.Axp.SetSpkEnable(EnableSpeaker);
    // Write over old value first
    LCDText(!EnableSpeaker ? "Mute" : "Unmute", BLACK, 30, 215, 1);
    LCDText(EnableSpeaker ? "Mute" : "Unmute", WHITE, 30, 215, 1);
  }
}

// Handle QR Code button
void QRC(Event &e)
{
  ShowQRCode = !ShowQRCode;
  if (ShowQRCode)
  {
    M5.Lcd.fillScreen(BLACK);
    m5.Lcd.qrcode(doc["QRCode"].as<char *>());
  }
  else
    ShowMainScreen();
}

// Handle Skip button
void SkipThis(Event &e)
{
  if (!ShowQRCode)
  {
    M5.Axp.SetSpkEnable(false);
    TrackingMQTT("Skipped");
  }
}

void setup()
{
  // LCDEnable, SDEnable, SerialEnable, and I2CEnable
  M5.begin(true, true, true, true);
  client.setInsecure();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextFont(2);
  LCDText("Trying to Connect\n to Wi-Fi ...", WHITE, 10, 50, 2);
  //wc.resetSettings();//wc.setDebug(true);
  wc.setRetryAttempts(1);
  // Try to connect to last Wi-Fi or wait until connected
  if (!wc.autoConnect())
  {
    M5.Lcd.fillScreen(BLACK);
    wc.setAPName("");
    LCDText("  Connect me to\n   your Wi-Fi", RED, 10, 10, 2);
    LCDText(("\n\n   SSID:\n   " + String(wc.getAPName())).c_str(), GREEN, M5.Lcd.getCursorX(), M5.Lcd.getCursorY(), 2);
    LCDText("\n\n   http://192.168.4.1", BLUE, M5.Lcd.getCursorX(), M5.Lcd.getCursorY(), 2);
    wc.startConfigurationPortal(AP_WAIT);
    ESP.restart();
  }
  M5.Lcd.fillScreen(BLACK);
  LCDText("UH", RED, 10, 10, 4);
  LCDText("CDC", GREEN, 100, 90, 4);
  LCDText("UMN", BLUE, 200, 170, 4);
  // Wait for user input
  for (int i = 0; i < 3000; i++)
  {
    TouchPoint_t coordinate = M5.Touch.getPressPoint();
    if (coordinate.y > -1 && coordinate.x > -1 && coordinate.y < 241)
    {
      TableNumber = int(coordinate.y / (M5.Lcd.height() / 3)) * 2;
      break;
    }
    delay(1);
  }
  M5.Lcd.fillScreen(BLACK);
  LCDText("Loading ...", WHITE, 30, 90, 4);
  M5.BtnA.addHandler(MuteUnmute, E_TOUCH);
  M5.BtnB.addHandler(QRC, E_TOUCH);
  M5.BtnC.addHandler(SkipThis, E_TOUCH);
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  mqtt.begin(AWS_IOT_ENDPOINT, 8883, net);
  mqtt.setKeepAlive(36000);
  while (!mqtt.connect(THINGNAME))
    yield();
  // if no selecion is made check device shadow
  if (TableNumber == -1)
  {
    mqtt.onMessage(MessageReceived);
    mqtt.subscribe(UpdateShadow + "/delta");
    mqtt.publish(UpdateShadow, "{\"state\": {\"reported\": {\"TableNumber\": " + String(TableNumber) + "}}}");
    delay(1000);
    mqtt.loop();
  }
  else
    // If selection is made update the device shadow
    mqtt.publish(UpdateShadow, "{\"state\": {\"desired\": {\"TableNumber\": " + String(TableNumber) + "}}}");
  // First record is the number of episodes
  GetJSON(StageURL + Tables[TableNumber] + "/0");
  // Get a random episode
  GetJSON(StageURL + Tables[TableNumber] + "/" + String(random(1, doc["Title"].as<int>() + 1)));
  ShowMainScreen();
  out = new AudioOutputI2S(0, 0);
  // Set I2S pins bclk, wclk, and dout
  out->SetPinout(12, 0, 2);
  mp3 = new AudioGeneratorMP3();
  // Start playing audio
  mp3->begin(new AudioFileSourceBuffer(new AudioFileSourceHTTPStream(doc["Audio"].as<char *>()), 4096), out);
  // Now enable speaker to hear the audio
  M5.Axp.SetSpkEnable(EnableSpeaker);
  while (mp3->loop())
    // Check if any button is pressed
    M5.update();
  // Finished listening
  TrackingMQTT("Enlightened");
}

// Nothing to do here
void loop() {}