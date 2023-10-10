#include "esp_system.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <TM1637Display.h> //TM1637 by Avishay Orpaz ver1.2.0
#include <LCD_I2C.h>

#define INNER_LED 2
#define PIN_START 5
#define PIN_STOP 18
#define PIN_DOWN 19
#define PIN_UP   23
#define PIN_IR   25

#define BUTTON_DOWN HIGH
#define BUTTON_UP   LOW
#define IR_DETECTED HIGH
#define IR_NODETECT LOW

//TM1637 by Avishay Orpaz ver1.2.0
#define CLK 15
#define DIO 4
TM1637Display display7seg(CLK, DIO);
uint8_t data7seg[] = { 0xff, 0xff, 0xff, 0xff }; // all '1'

LCD_I2C lcd(0x27, 20, 4);

// ConvStr function from(https://synapse.kyoto/)
String ConvStr(String str)
{
  struct LocalFunc{ // for defining local function
    static uint8_t CodeUTF8(uint8_t ch)
    {
      static uint8_t OneNum=0; // Number of successive 1s at MSBs first byte (Number of remaining bytes)
      static uint16_t Utf16; // UTF-16 code for multi byte character
      static boolean InUtf16Area; // Flag that shows character can be expressed as UTF-16 code

      if(OneNum==0) { // First byte
        uint8_t c;

        // Get OneNum
        c=ch;
        while(c&0x80) {
          c<<=1;
          OneNum++;
        } // while

        if(OneNum==1 || OneNum>6) { // First byte is in undefined area
          OneNum=0;
          return ch;
        } else if(OneNum==0) { // 1-byte character
          return ch;
        } else { // Multi byte character
          InUtf16Area=true;
          Utf16=ch&((1<<(7-OneNum--))-1); // Get first byte
        } // if
      } else { // not first byte
        if((ch&0xc0)!=0x80) { // First byte appears illegally
          OneNum=0;
          return ch;
        } // if
        if(Utf16&0xfc00) InUtf16Area=false;
        Utf16=(Utf16<<6)+(ch&0x3f);
        if(--OneNum==0) { // Last byte
          return (InUtf16Area && Utf16>=0xff61 && Utf16<=0xff9f) ? Utf16-(0xff61-0xa1) // kana
                                                                 : ' ';                // other character
        } // if
      } // if

      return 0;
    }; // CodeUTF8
  }; // LocalFunc

  const char charA[]="ｱ";
  if(*charA=='\xb1') return str;
  String result="";
  for(int i=0; i<str.length(); i++) {
    uint8_t b=LocalFunc::CodeUTF8((uint8_t)str.c_str()[i]);
    if(b) {
      result+=(char)b;
    } // if
  } // for i
  return result;
} // ConvStr

void setup() {
    Serial.begin(115200);
    pinMode(PIN_START, INPUT_PULLUP);
    pinMode(PIN_STOP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_IR, INPUT);

    pinMode(INNER_LED, OUTPUT);
    digitalWrite(INNER_LED, LOW);

    display7seg.setBrightness(0x0f);

    lcd.begin();
    lcd.clear();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print(ConvStr("ｼﾞｭﾝﾋﾞﾁｭｳ...  "));
    lcd.setCursor(0, 1);
    lcd.print(ConvStr("ｼﾊﾞﾗｸｵﾏﾁｸﾀﾞｻｲ"));
    lcd.setCursor(0, 2);
    lcd.print(ConvStr("Hello"));
    lcd.setCursor(0, 3);
    lcd.print(ConvStr("world"));

    xTaskCreatePinnedToCore(loop2, "loop2", 4096, NULL, 1, NULL, 0);
    Serial.println("setup finished.");
}

void loop2(void * params) {
    while (true) {
        delay(1000);
        Serial.print("IR ");
        Serial.println(digitalRead(PIN_IR));
        Serial.print("START ");
        Serial.println(digitalRead(PIN_START));
        Serial.print("STOP ");
        Serial.println(digitalRead(PIN_STOP));
        Serial.print("DOWN ");
        Serial.println(digitalRead(PIN_DOWN));
        Serial.print("UP ");
        Serial.println(digitalRead(PIN_UP));
    }
}

static uint32_t count = 0;
void loop() {
    count++;
    
    if(count&1) {
        data7seg[0] = count >= 1000 ? display7seg.encodeDigit((count / 1000)) : 0;
        data7seg[1] = display7seg.encodeDigit((count % 1000) / 100);
        data7seg[2] = display7seg.encodeDigit((count %  100) /  10);
        data7seg[3] = display7seg.encodeDigit((count %   10));
        display7seg.setSegments(data7seg);
    } else {
        display7seg.showNumberDecEx(count, 0x40, true);
    }
    
    digitalWrite(INNER_LED, count&1);
    delay(10);
}