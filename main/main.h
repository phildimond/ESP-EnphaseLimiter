/* MQTT Sensor Sender for Home Assistant
   
   Reads a temperature & humidity probe (SHT20, SDA on IO21 and SCL on IO22)
   and sends the data to Home Assistant (or anything else) via MQTT. Hold a 
   button low on IO27 at boot to set the configuration variables. Also sends
   the current battery voltage read from IO34 via a divider network of 
   2 x 1M resistors. Config allows you to calibrate the battery voltage by
   measuring it and entering the real value. Uses a DF Robot Firebeetle 
   ESP32-E board which has the button and battery divider on board, but
   will operate with any ESP32 if properly compiled.

   Copyright 2023 Phillip C Dimond

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#ifndef __MAIN_H__
#define __MAIN_H__

#define SLEEPTIME 30
#define BUTTON_PIN GPIO_NUM_13
#define RELAY0 GPIO_NUM_26
#define RELAY1 GPIO_NUM_27
#define RELAY2 GPIO_NUM_9
#define RELAY3 GPIO_NUM_10
#define S_TO_uS(s) (s * 1000000)
#define uS_TO_S(s) (s / 1000000)

extern const char* TAG;

#endif // __MAIN_H__