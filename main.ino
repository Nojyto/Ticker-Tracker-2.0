#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "SSD1306.h"

//exmpl url => https://finnhub.io/api/v1/quote?symbol=TSLA&token=c5kv562ad3ibikglgeq0
#define host       "finnhub.io"
#define apiUrl     "/api/v1/quote?symbol="
#define apiKey     "&token=c5kv562ad3ibikglgeq0"
//site fingerprint (changes every few months)
//get it from https://www.grc.com/fingerprints.htm
#define fingPr     "95 E6 C6 0D D3 9E C0 B2 40 7C 47 66 B5 89 8F 51 72 81 27 A6"
#define upInterval 300000 //5 min
#define upPin      D5
#define downPin    D7
#define confirmPin D6

/*
  --Pinout--
  Addr => 0x3C (oled specific)
  D1   => SDA  (serial Data)
  D2   => SCL  (serial Clock)
*/
SSD1306            dp(0x3C, D1, D2);
WiFiClientSecure   client;

String ssid   = "";
String pass   = "";
String ticker = "TSLA";

void setup() {
    /*pinMode(0, OUTPUT);
    digitalWrite(0, LOW);
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    pinMode(8, OUTPUT);
    digitalWrite(8, LOW);*/

    pinMode(upPin,      INPUT_PULLUP);
    pinMode(downPin,    INPUT_PULLUP);
    pinMode(confirmPin, INPUT_PULLUP);

    dp.init();
    dp.flipScreenVertically();
    //dp.setFont(customFont);
    dp.setFont(ArialMT_Plain_16);
    dp.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);

    EEPROM.begin(4096);
    if(EEPROM.read(100) == 69){
        int i = 101;
        ticker = "";
        while(EEPROM.read(i) != '\0')
            ticker += char(EEPROM.read(i++));
    }
    if(EEPROM.read(0) == 69){
        int i = 1;
        while(EEPROM.read(i) != '\0')
            ssid += char(EEPROM.read(i++));
        i++;
        while(EEPROM.read(i) != '\0')
            pass += char(EEPROM.read(i++));

        updateDisplay(ssid + "\n" + pass);

        if(connectToWifi()) return;
    }
    EEPROM.end();

    chooseNetwork();
}

void loop() {
    static unsigned long prvTime = upInterval;
    if(millis() - prvTime >= upInterval) {
        prvTime = millis();
        makeHTTPRequest();
    }

    if(digitalRead(confirmPin) == LOW){
        String newTicker = selectWord("Change ticker");
        if(newTicker != ""){
            updateDisplay("Changed ticker to:\n" + newTicker);
            while(true){
                if(digitalRead(upPin) == LOW || digitalRead(downPin) == LOW){
                    updateDisplay("Action\naborted");
                    break;
                }else if(digitalRead(confirmPin) == LOW){
                    ticker = newTicker;

                    EEPROM.begin(4096);

                    EEPROM.write(100, 69);
                    int i = 101;
                    for(int j = 0; j < ticker.length(); j++)
                        EEPROM.write(i++, ticker[j]);
                    EEPROM.write(i++, '\0');
                    
                    EEPROM.end();

                    updateDisplay("Ticker was\nupdated");
                    break;
                }
                delay(25);
            }
        }
        prvTime = upInterval;
        delay(2000);
    }
    delay(50);
}

void updateDisplay(const String msg){
    static const int cW  = dp.getWidth()  / 2;
    static const int cH  = dp.getHeight() / 2;
    dp.clear();
    dp.drawString(cW, cH, msg);
    dp.display();
}

String truncate(const String str){
    String trunc = "";

    for(int i = 0; i < 8; i++)
        trunc += str[i];
    trunc += "..";

    return trunc;
}

void makeHTTPRequest(){
    updateDisplay("Fetching data...");

    if(!client.connect(host, 443)){
        updateDisplay("Connection\nfailed.");
        return;
    }
    
    yield();

    client.println("GET " + String(apiUrl) + String(ticker) + String(apiKey) + " HTTP/1.0");
    client.println("Host: " + String(host));
    client.println("Cache-Control: no-cache");

    if(client.println() == 0){
        updateDisplay("Failed to send\nrequest.");
        return;
    }

    if(!client.find("\r\n\r\n")){
        updateDisplay("Invalid\nresponse.");
        return;
    }

    DynamicJsonDocument doc(512);
    deserializeJson(doc, client.readString());
    JsonObject obj = doc.as<JsonObject>();

    String stockValue  = String(float(obj["c"]), 2) + "$";
    String stockChange = String(float(obj["dp"]), 2) + "%";;
    if(stockChange[0] != '-')
        stockChange = "+" + stockChange;

    updateDisplay(ticker + "\n" + stockValue + "\n" + stockChange);
}

String selectWord(const String header){
    String word = "";
    int i = 0;
    static const String abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789~!@#$%^&*";
    static const int n = 70 + 2;

    delay(300);

    while(true){
        if(i == n - 1){
            updateDisplay(header + "\n" + word + "\n" + "Confirm");
        }else if(i == n){
            updateDisplay(header + "\n" + "Cancel?");
        }else{
            updateDisplay(header + "\n" + word + abc[i]);
        }

        if(digitalRead(confirmPin) == LOW){
            if(i == n - 1) return word;
            if(i == n)     return "";
            word += abc[i];
            delay(300);
        }else if(digitalRead(upPin) == LOW){
            if(--i < 0) i = n;
            delay(300);
        }else if(digitalRead(downPin) == LOW){
            if(++i > n) i = 0;
            delay(300);
        }
        delay(25);
    }
    return "";
}

void chooseNetwork(){
    updateDisplay("Scanning\n for wifi...");
    int n = WiFi.scanNetworks();

    if(n == 0){
        updateDisplay("No networks\nfound");
        delay(5000);
        chooseNetwork();
    }
    
    delay(2000);
    updateDisplay(String(n) + " networks\nfound");
    delay(2000);

    int i = 0;
    while(true){
        updateDisplay("Select a network\n--" + String(i + 1) + "/" + String(n) + "--" + "\n" + truncate(WiFi.SSID(i)));
        if(digitalRead(confirmPin) == LOW){
            ssid = WiFi.SSID(i);
            if(ssid == "") ESP.restart();
            pass = selectWord(ssid);
            break;
        }else if(digitalRead(upPin) == LOW){
            if(--i < 0) i = n - 1;
            delay(300);
        }else if(digitalRead(downPin) == LOW){
            if(++i >= n) i = 0;
            delay(300);
        }
        delay(25);
    }

    if(!connectToWifi())
        ESP.restart();
}

bool connectToWifi(){
    updateDisplay("Connecting to:\n" + truncate(ssid));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    int tries = 20;
    while(WiFi.status() != WL_CONNECTED && --tries > 0) delay(450);
    if(tries <= 0){
        updateDisplay("Failed to connect");
        delay(5000);
        return false;
    } 

    client.setFingerprint(fingPr);

    updateDisplay("Successfully\nconnected.");

    EEPROM.begin(4096);
    
    EEPROM.write(0, 69);
    int i = 1;
    for(int j = 0; j < ssid.length(); j++)
        EEPROM.write(i++, ssid[j]);
    EEPROM.write(i++, '\0');
    for(int j = 0; j < pass.length(); j++)
        EEPROM.write(i++, pass[j]);
    EEPROM.write(i++, '\0');
        
    EEPROM.end();

    return true;
}