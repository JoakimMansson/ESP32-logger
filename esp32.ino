#include <Arduino.h>
#include <time.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Firebase_ESP_Client.h>
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"
#include <vector>
#include <string>


//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Firebase
#define DATABASE_URL ""
#define API_KEY ""

#define USER_EMAIL ""
#define USER_PASSWORD ""

// WiFi
#define SSID ""
#define WIFI_PASSWORD ""

//Pins
#define RED_LED 25
#define GREEN_LED 27
#define WAKEUP_LOG 12
#define WAKEUP_UPLOAD 15
#define BUTTON_PIN_BITMASK 0x0000005000


//Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

// Wifi object
WiFiUDP ntpUDP;
// To fetch current time from NTPClient->router
NTPClient timeClient(ntpUDP);


// Variable to save USER UID
String uid;

// Where to store in database
String path;


RTC_DATA_ATTR unsigned int tired_counter = 0;
RTC_DATA_ATTR unsigned long last_sync_time = 0;


void wake_up_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  // If wakeup was by blue or red button
  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : full_upload_chain(); break;
    case ESP_SLEEP_WAKEUP_EXT1 : increment_tired_counter(); break;
    default : flash_led(true, 1000); break;
  }
}


int initWifi()
{
  //WiFi.mode(WIFI_STA); //Optional
  WiFi.begin(SSID, WIFI_PASSWORD);
  Serial.println("\nConnecting");

  unsigned long start_time = millis();
  while(WiFi.status() != WL_CONNECTED)
  {
    if(millis() - start_time > 10000)
    {
      return -1;
    }
    Serial.print(".");
    delay(100);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  return 1;
}

int initFirebase()
{
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  
  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  
  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  unsigned long start_time = millis();
  while ((auth.token.uid) == "") 
  {
    if(millis() - start_time > 10000)
    {
      return -1;
    }    
    Serial.print('.');
    delay(1000);
  }

  // Print user UID
  uid = auth.token.uid.c_str();
  path = "/UsersData/" + uid;
  Serial.print("User UID: ");
  Serial.println(uid);
  
  Serial.println("DataBase initialized");
  return 1;
}


int send_FB_data(String path, String time_data)
{
  if(Firebase.RTDB.pushString(&fbdo, path.c_str(), time_data))
  {
    Serial.print("Writing value: ");
    Serial.print (time_data);
    Serial.print(" on the following path: ");
    Serial.println(path);
    Serial.println("PASSED");
    Serial.println("PATH: " + fbdo.dataPath());
    Serial.println("TYPE: " + fbdo.dataType());
    return 1;
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
    return -1;
  }
}


void full_upload_chain()
{
  int WiFi_status = initWifi();

  // WiFi failed
  if(WiFi_status != 1)
  {
    digitalWrite(RED_LED, HIGH);
    delay(500);
    digitalWrite(RED_LED, LOW);
    return;
  }


  int Firebase_status = initFirebase();
  
  // Initialization of firebase failed
  if(Firebase_status != 1)
  {
    digitalWrite(RED_LED, HIGH);
    delay(500);
    digitalWrite(RED_LED, LOW);
    return;    
  }
  
  String current_date = get_current_date();

  // Current date was not fetched from NTP and failed
  if(current_date == "")
  {
    digitalWrite(RED_LED, HIGH);
    delay(500);
    digitalWrite(RED_LED, LOW);   
    return;
  }

  String data_to_upload = current_date + ", tired_count: " + String(tired_counter);   // Format the String to upload
  int upload_status = send_FB_data(path, data_to_upload);                       // Uploading to firebase


  // Upload was not succesful
  if(upload_status != 1)
  {
    digitalWrite(RED_LED, HIGH);
    delay(500);
    digitalWrite(RED_LED, LOW);      
    return;
  }
    

  Serial.println("ALL DONE AND UPLOADED");
  tired_counter = 0;
  digitalWrite(GREEN_LED, HIGH);
  delay(500);
  digitalWrite(GREEN_LED, LOW);  
}

// Requires WiFi
String get_current_date()
{
  struct tm timeinfo;
  bool is_updated = timeClient.update();

  if(is_updated == false)
  {
    digitalWrite(RED_LED, HIGH);
    delay(500);
    digitalWrite(RED_LED, LOW); 
    return "";
  }

  time_t now = timeClient.getEpochTime(); 
  localtime_r(&now, &timeinfo);

  String day = String(timeinfo.tm_mday);
  String month = String(timeinfo.tm_mon + 1);
  String year = String(timeinfo.tm_year + 1900);
  String full_date = day+"/"+month+"/"+year;

  return full_date;
}

void increment_tired_counter()
{
  tired_counter++;
  Serial.println("New tired_counter: " + String(tired_counter));
  digitalWrite(GREEN_LED, HIGH);
  delay(500);
  digitalWrite(GREEN_LED, LOW); 
}


void flash_led(bool flash_red, int time)
{
  int led_pin = GREEN_LED;
  if(flash_red)
  {
    led_pin = RED_LED;
  }

  digitalWrite(led_pin, HIGH);
  delay(time);
  digitalWrite(led_pin, LOW); 
}


void setup() {
  Serial.begin(115200);
  //RTCMemory.begin();

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  // RED BUTTON EXT1
  rtc_gpio_pullup_en(GPIO_NUM_12);
  rtc_gpio_pulldown_dis(GPIO_NUM_12);
  esp_sleep_enable_ext1_wakeup(GPIO_SEL_12, ESP_EXT1_WAKEUP_ALL_LOW);

  // BLUE BUTTON EXT0
  rtc_gpio_pullup_en(GPIO_NUM_15);
  rtc_gpio_pulldown_dis(GPIO_NUM_15);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15,LOW);
  
  wake_up_reason();

  esp_deep_sleep_start();
}

void loop() 
{}
