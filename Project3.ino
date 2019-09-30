#include <SPI.h>
#include <WiFi101.h>
#include <ArduinoJson.h>
#include <Twitter.h>
#include "arduino_secrets.h"
#define TW_API_HOST "api.twitter.com" 
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key Index number (needed only for WEP)
//if you want to demo on own machine, get your own tokens :)
const static char apiKey[] = "";
const static char secretKey[] = "";
const static char accessToken[] = "";
const static char accessTokenS[] = "";
String bear = "";
String token = "";

//All Wifi or twitter related constants
int status = WL_IDLE_STATUS;
char server[] = "api.twitter.com";    // name address for twitter (using DNS)
WiFiSSLClient client;
const char* tweetid;
char buffer[1000];
uint16_t twitter_port = 443;
Twitter twitter(buffer, 1000);

//function declartions
void getTwitterData();
void retweet();
void unretweet();
void nextLine();

//global vars
String id[4];
String tweet[4];
int currentTweet = 0;
int currentChar = 0;
bool haveTweets = false;

//hardware set up and debounce
int retweetPin = 10;
int unretweetPin = 11;
typedef enum {RELEASED =0, PRESSED} key;
key rt = RELEASED;
key urt = RELEASED;
key rtPrev = RELEASED;
key urtPrev = RELEASED;

void setup() {
  //set up internal pullup resistors
  pinMode(retweetPin, INPUT_PULLUP);
  pinMode(unretweetPin, INPUT_PULLUP);
//  digitalWrite(retweetPin, HIGH); 
//  digitalWrite(unretweetPin, HIGH); 
  //Initialize serial and wait for port to open:
  WiFi.setPins(8,7,4,2); 
  Serial.begin(9600);
  
  //set up the twitter auth data
  twitter.set_client_id(apiKey, secretKey);
  twitter.set_account_id(accessToken,accessTokenS);

  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(2000);
  }
  Serial.println("Connected to wifi");
  //printWiFiStatus();

  getTwitterData();



  // Set up the generic clock (GCLK4) used to clock timers to fire off the interupt at T=5s
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(4) |          // Divide the 48MHz clock source by divisor 1: 48MHz/4=12MHz
                    GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);          // Select GCLK4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  // Feed GCLK4 to TC4 and TC5
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TC4_TC5;     // Feed the GCLK4 to TC4 and TC5
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
 
  REG_TC4_COUNT16_CC0 = 0xE4E1;                   // Set the TC4 CC0 register as the TOP value in match frequency mode
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization

  //NVIC_DisableIRQ(TC4_IRQn);
  //NVIC_ClearPendingIRQ(TC4_IRQn);
  NVIC_SetPriority(TC4_IRQn, 0);    // Set the Nested Vector Interrupt Controller (NVIC) priority for TC4 to 0 (highest)
  NVIC_EnableIRQ(TC4_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)

  REG_TC4_INTFLAG |= TC_INTFLAG_OVF;              // Clear the interrupt flags
  REG_TC4_INTENSET = TC_INTENSET_OVF;             // Enable TC4 interrupts
  // REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts
 
  REG_TC4_CTRLA |= TC_CTRLA_PRESCALER_DIV1024 |   // Set prescaler to 1024, 12MHz/1024 = not 46.875kHz lol
                   TC_CTRLA_WAVEGEN_MFRQ |        // Put the timer TC4 into match frequency (MFRQ) mode 
                   TC_CTRLA_ENABLE;               // Enable TC4
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization

}



uint8_t historyK0 = 0;
uint8_t historyK1 = 0;
void loop() {
  
 //do the debounce here only thing running other than the interrupt so can do in loop
 int rtIn = digitalRead(retweetPin);
 int unrtIn = digitalRead(unretweetPin);

//debounce
 historyK0 = historyK0 <<1;
    if(rtIn == 0) historyK0 = historyK0 | 0x1;
    if ((historyK0 & 0b111111) == 0b111111){
      Serial.println("Retweeted\n");
       rt = PRESSED;
    }
    else rt = RELEASED;

    historyK1 = historyK1 <<1;
    if(unrtIn == 0) historyK1 = historyK1 | 0x1;
    if ((historyK1 & 0b111111) == 0b111111){
      Serial.println("Un-retweeted\n");
       urt = PRESSED;
    }
    else urt = RELEASED;

//rt/un-rt logic
  if(rtPrev == RELEASED && rt == PRESSED){
      retweet();
  }
  rtPrev = rt; //remember the previous value of key0
  if(urtPrev == RELEASED && urt == PRESSED){
     unretweet();
  }
  urtPrev = urt;

    
}


void retweet(){  
  String idr = id[currentTweet];
  String url = "/1.1/statuses/retweet/";
  String json =".json";
  url += idr;
  url += json;
  twitter.set_twitter_endpoint(PSTR("api.twitter.com"), url.c_str(),twitter_port, false);
  twitter.post_rt();
}

void unretweet(){  
  String idr = id[currentTweet];
  String url = "/1.1/statuses/unretweet/";
  String json =".json";
  url += idr;
  url += json;
  twitter.set_twitter_endpoint(PSTR("api.twitter.com"),url.c_str(),twitter_port, false);
  twitter.post_rt();
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
void getTwitterData(){
  if (client.connect(TW_API_HOST, 443)) {
        unsigned long now;
        bool avail;
        Serial.println(".... connected to server");
        String command = "https://api.twitter.com/1.1/statuses/user_timeline.json?screen_name=nbcnews&count=4";
        client.println("GET " + command +  " HTTP/1.1");
        client.println("Host: " TW_API_HOST);
        client.println("User-Agent: arduino/1.0.0");
        client.println("Authorization: Bearer " + bear);
        client.println("");
        now=millis();
        avail=false;
        
        // Check HTTP status
        char status[32] = {0};
   //     while(client.connected()) Serial.print((char)client.read());
        client.readBytesUntil('\r', status, sizeof(status));
      if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
        Serial.print(F("Unexpected response: "));
        Serial.println(status);
        return;
      }
    
      // Skip HTTP headers
      char endOfHeaders[] = "\r\n\r\n";
      if (!client.find(endOfHeaders)) {
        Serial.println(F("Invalid response"));
        return;
      }
      //put everthing into user friendly Json
         const size_t bufferSize = 8*JSON_ARRAY_SIZE(0) + 2*JSON_ARRAY_SIZE(1) + 3*JSON_ARRAY_SIZE(2) + 4*JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(4) + 2*JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(23) + 2*JSON_OBJECT_SIZE(42) + 4600;
        DynamicJsonBuffer jsonBuffer(bufferSize);
         
        
         JsonArray& root = jsonBuffer.parseArray(client);
          if (!root.success()) {
        Serial.println("parseObject() failed");
        return;
      }
   
          tweet[0] = String((const char*)root[0]["text"]);    
          id[0] = String((const char*)root[0]["id_str"]);
          tweet[1] = String((const char*)root[1]["text"]);    
          id[1] = String((const char*)root[1]["id_str"]);
          tweet[2] = String((const char*)root[2]["text"]);    
          id[2] = String((const char*)root[2]["id_str"]);
          tweet[3] = String((const char*)root[3]["text"]);    
          id[3] = String((const char*)root[3]["id_str"]);
         // tweetid = String(id[0]);
//          Serial.println(tweet[0]);
//          Serial.println(id[0]);
//          Serial.println(tweet[1]);
//          Serial.println(id[1]);
          haveTweets = true;
        } 
}
void TC4_Handler()                              // Interrupt Service Routine (ISR) for timer TC4
{     
  //rotates the tweets at around 5s of read time for the user
  if (TC4->COUNT16.INTFLAG.bit.OVF && TC4->COUNT16.INTENSET.bit.OVF)             
  {
  //  Serial.println("hey bud");
    nextLine();
    REG_TC4_INTFLAG = TC_INTFLAG_OVF;         // Clear the OVF interrupt flag
  }
}
//logic for printing out, emulating a UART 16x2 lcd display on monitor
void nextLine(){
  int bufferSize = 0;
  String bufferWord = "";
  int lineSize = 0;
  int cc = currentChar; //need a constant value of current char while in the method
  for(int i = 0; i<32; i++){
   //end of tweet
     if(i+cc >= tweet[currentTweet].length()){
      if(!((bufferWord.substring(0,4)).equals("http")))Serial.print(bufferWord); //gets rid of the link
      if(currentTweet == 3) currentTweet = 0;
      else currentTweet += 1;
      currentChar = 0;
      return;
     }
     //put charater into the bufferWord
    if(tweet[currentTweet].charAt(i+cc) != ' '){
    //  Serial.print("char put: "); Serial.println(tweet[currentTweet].charAt(i+cc));
      bufferWord += tweet[currentTweet].charAt(i+cc);
      bufferSize +=1;
    }
    //end of word 
    else{
     // Serial.print("hi2");
      if(bufferSize>16){
        currentChar +=bufferSize; 
        bufferWord = "";
        bufferSize=0;
      }
      if(lineSize + bufferSize > 16){
        Serial.print('\n');
        return;
      }
      else Serial.print(bufferWord);
      if(i != 31 || i!=16)Serial.print(" ");
     // Serial.print("Line Size: "); Serial.println(lineSize);
      currentChar += bufferSize +1;
      lineSize += bufferSize+1;
      bufferWord = "";
      bufferSize=0;
    } 
    //new line on end
    if(i == 15||i ==31){
    Serial.print('\n');
    lineSize = 0;
    }
  }
  
}
