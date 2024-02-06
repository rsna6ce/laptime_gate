# laptime_gate

![sample](img/sample.gif)

## Overview
* The Laptime gate measures the lap time of Plarail
* The Laptime gate detects the passing of a train using an IR sensor
* The LapTime Gate records 99 lap times

## Devices
* ESP-32 DOIT ESP32 DEVKIT V1 (Compatible)
* IR sensor (IR LED with LM393)
    * e.g. https://amazon.co.jp/dp/B08FL977TP/
* 20x4 LCD I2C
    * e.g. https://amazon.co.jp/dp/B08P5JJHHF/
* TM1637 7seg LED
    * https://amazon.co.jp/dp/B08NJD66HX/
* tactile switch (6*6*5mm) x4
    * e.g. https://amazon.co.jp/dp/B0199PC7LG/

## Wireing

| ESP32 pin	| device | pin |
| ---- | ---- | ---- |
| D5 | tactile switch for start | 1 |
| D18 | tactile switch for stop | 1 |
| D19 | tactile switch for down | 1 |
| D25 | tactile switch for up | 1 |
| D25 | IR sensor | DOTTL |
| D15 | TM1637 | CLK |
| D4 | TM1637 | DIO |
| D21 | 20x4 LCD I2C | SDA |
| D22 | 20x4 LCD I2C | SCL |

# Software development environment
* Arduino IDE 1.8.19
* Dependent library
    * LCD_I2C 2.3.0
    * TM1637 1.2.0

