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

#define BUTTON_DOWN LOW
#define BUTTON_UP   HIGH
#define IR_DETECTED HIGH
#define IR_NODETECT LOW

//TM1637 by Avishay Orpaz ver1.2.0
#define CLK 15
#define DIO 4
TM1637Display display7seg(CLK, DIO);
uint8_t data7seg[] = { 0xff, 0xff, 0xff, 0xff }; // all '1'

LCD_I2C lcd(0x27, 20, 4);

enum state {
    state_stop,
    state_ready,
    state_run,
    stete_finish};
enum state state_mode = state_stop;

typedef struct laptime_ {
  uint32_t count;
  uint32_t time_ms;
  uint32_t lap_ms;
} laptime;

#define LAPTIME_COUNT_MAX 99
laptime laptime_list[LAPTIME_COUNT_MAX] = {0};
uint32_t laptime_count = 0;

const uint32_t button_count_detect = 5;
uint32_t button_count_run = 0;
uint32_t button_count_stop = 0;
uint32_t button_count_up = 0;
uint32_t button_count_down = 0;
uint32_t cursor_lap_index = 0;

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

    display7seg.setBrightness(0x02);

    lcd.begin();
    lcd.clear();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print(ConvStr("LAPTIME GATE Ver0.9"));
    lcd.setCursor(0, 1);
    lcd.print(ConvStr("　"));
    lcd.setCursor(0, 2);
    lcd.print(ConvStr("ｽﾀｰﾄﾎﾞﾀﾝ ｦ ｵｼﾃｸﾀﾞｻｲ"));
    lcd.setCursor(0, 3);
    lcd.print(ConvStr("　"));

    state_mode = state_stop;
    display7seg.showNumberDecEx(0, 0x40, true);

    xTaskCreatePinnedToCore(loop2, "loop2", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(loop3, "loop3", 4096, NULL, 1, NULL, 0);
    Serial.println("setup finished.");
}

// 7segment manuary
//     a0
//  f5    b1
//     g6
//  e4    c2
//     d3
void display_7seg_stop(){
    data7seg[0] = (1<<0) | (0<<1) | (1<<2) | (1<<3) | (0<<4) | (1<<5) | (1<<6); // S
    data7seg[1] = (0<<0) | (0<<1) | (0<<2) | (1<<3) | (1<<4) | (1<<5) | (1<<6); // t
    data7seg[2] = (0<<0) | (0<<1) | (1<<2) | (1<<3) | (1<<4) | (0<<5) | (1<<6); // o
    data7seg[3] = (1<<0) | (1<<1) | (0<<2) | (0<<3) | (1<<4) | (1<<5) | (1<<6); // p
    display7seg.setSegments(data7seg);
}

static uint32_t count_for_display = 0;
static uint32_t laptime_count_latest = 0;

static const uint32_t gap_ignore_time = 300;
static const uint32_t laptime_hold_time = 700;

// loop for laptime
void loop2(void * params) {
    uint8_t ir_prev = digitalRead(PIN_IR);
    uint8_t ir_curr = ir_prev;
    uint32_t millis_curr = 0;
    uint32_t millis_run_started = 0;
    uint32_t millis_lap_started = 0;
    uint32_t millis_tail_started = 0;
    uint32_t millis_laptime_hold = 0;
    uint32_t latest_lap_ms = 0;
    while (true) {
        ir_prev = ir_curr;
        ir_curr = digitalRead(PIN_IR);
        millis_curr = (uint32_t)millis();
        if (state_mode == state_run) {
            if (ir_prev==IR_NODETECT && ir_curr==IR_DETECTED) {
                uint32_t elapsed_from_tail = millis_curr - millis_tail_started;
                if (gap_ignore_time < elapsed_from_tail) {
                    // append recorde
                    uint32_t laptime_index = laptime_count;
                    laptime_list[laptime_index].count = laptime_index + 1;
                    laptime_list[laptime_index].time_ms = (uint32_t)(millis_curr-millis_run_started);
                    laptime_list[laptime_index].lap_ms = (uint32_t)(millis_curr-millis_lap_started);
                    latest_lap_ms = (uint32_t)(millis_curr-millis_lap_started);
                    laptime_count++;
                    if (LAPTIME_COUNT_MAX <= laptime_count) {
                        // overflow finish
                        state_mode = state_stop;
                        cursor_lap_index = laptime_count-1;
                        display_lcd_lap_record(cursor_lap_index);
                    }
                    millis_lap_started = millis_curr;
                    millis_laptime_hold = millis_curr + laptime_hold_time;
                }
            } else if (ir_prev==IR_DETECTED && ir_curr==IR_NODETECT) {
                millis_tail_started = millis_curr;
            }
            if (millis_laptime_hold < millis_curr) {
                count_for_display = (uint32_t)(millis_curr-millis_lap_started)/10;
            } else {
                count_for_display = latest_lap_ms / 10;
            }
        } else if ( state_mode == state_ready) {
            if (ir_prev==IR_NODETECT && ir_curr == IR_DETECTED) {
                millis_run_started = millis_curr;
                millis_lap_started = millis_curr;
                millis_tail_started = millis_curr;
                laptime_count = 0;
                laptime_count_latest = 0;
                state_mode = state_run;
            }
        }
        delay(2);
    }
}

// loop for display7seg
void loop3(void * params) {
    uint32_t count_latest = 0;
    enum state state_mode_prev = state_stop;
    while (true) {
        if (state_mode == state_run) {
            display7seg.showNumberDecEx(count_for_display, 0x40, true);
        } else if (state_mode_prev != state_mode) {
            state_mode_prev = state_mode;
            if (state_mode == state_stop) {
                display_7seg_stop();
            } else if (state_mode == state_ready) {
                display7seg.showNumberDecEx(0, 0x40, true);
            }
        }
        delay(5);
    }
}

void display_lcd_lap_latest(int count) {
    lcd.clear();
    for (int i=0; i<4; i++) {
        // format '99 T:000.00 L:000.00'
        int index = count-1-i;
        if (index < 0) {
            //skip
        } else {
            char buf_all[24];
            char buf_time[8];
            char buf_laps[8];
            double time_10ms = (double)(laptime_list[index].time_ms/10) * 0.01;
            double lap_10ms = (double)(laptime_list[index].lap_ms/10) * 0.01;
            dtostrf(time_10ms, 6, 2, buf_time);
            dtostrf(lap_10ms, 6, 2, buf_laps);
            sprintf(buf_all,"%02d T:%s L:%s",(int)laptime_list[index].count, buf_time, buf_laps);
            lcd.setCursor(0, i);
            lcd.print(String(buf_all));
        }
    }
}

void display_lcd_lap_record(int cursor) {
    lcd.clear();
    for (int i=0; i<4; i++) {
        // format '99 T:000.00 L:000.00'
        int index = cursor-i;
        if (index < 0) {
            // skip
        } else {
            char buf_all[24];
            char buf_time[8];
            char buf_laps[8];
            double time_10ms = (double)(laptime_list[index].time_ms/10) * 0.01;
            double lap_10ms = (double)(laptime_list[index].lap_ms/10) * 0.01;
            dtostrf(time_10ms, 6, 2, buf_time);
            dtostrf(lap_10ms, 6, 2, buf_laps);
            sprintf(buf_all,"%02d T:%s L:%s",(int)laptime_list[index].count, buf_time, buf_laps);
            lcd.setCursor(0, i);
            lcd.print(String(buf_all));
        }
    }
}

//loop main
void loop() {
    // button events with chattering workaround
    // button run
    if (digitalRead(PIN_START)==BUTTON_DOWN) {
        if (++button_count_run == button_count_detect) {
            // stop -> start
            if (state_mode == state_stop) {
                state_mode = state_ready;
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print(ConvStr("ｼﾞｭﾝﾋﾞOK!!"));
                lcd.setCursor(0, 1);
                lcd.print(ConvStr(" "));
                lcd.setCursor(0, 2);
                lcd.print(ConvStr("ｹﾞｰﾄ ｦ ﾂｳｶ ｽﾙﾄ"));
                lcd.setCursor(0, 3);
                lcd.print(ConvStr("ｼﾞﾄﾞｳｹｲｿｸ ｶｲｼ ｼﾏｽ"));
            }
        }
    } else {
        button_count_run = 0;
    }
    // button stop
    if (digitalRead(PIN_STOP)==BUTTON_DOWN) {
        if (++button_count_stop == button_count_detect) {
            if (state_mode == state_run || state_mode == state_ready) {
                state_mode = state_stop;
                cursor_lap_index = laptime_count-1;
            }
        }
    } else {
        button_count_stop = 0;
    }
    // buton up
    if (digitalRead(PIN_UP)==BUTTON_DOWN) {
        if (++button_count_up == button_count_detect) {
            if (state_mode == state_stop) {
                Serial.print("cursor_lap_index"); Serial.println(cursor_lap_index);
                Serial.print("laptime_count"); Serial.println(laptime_count);
                if (cursor_lap_index+4 < laptime_count) {
                    cursor_lap_index += 4;
                }
                display_lcd_lap_record(cursor_lap_index);
            }
        }
    } else {
        button_count_up = 0;
    }
    // button down
    if (digitalRead(PIN_DOWN)==BUTTON_DOWN) {
        if (++button_count_down == button_count_detect) {
            if (state_mode == state_stop) {
                Serial.print("cursor_lap_index"); Serial.println(cursor_lap_index);
                Serial.print("laptime_count"); Serial.println(laptime_count);
                if (4 <= cursor_lap_index) {
                    cursor_lap_index -= 4;
                }
                display_lcd_lap_record(cursor_lap_index);
            }
        }
    } else {
        button_count_down = 0;
    }

    //display lcd
    if (state_mode == state_run) {
        if (laptime_count_latest < laptime_count) {
            laptime_count_latest = laptime_count;
            display_lcd_lap_latest(laptime_count_latest);
        } else if (laptime_count == 0) {
            lcd.clear();
        }
    }
    delay(10);
}