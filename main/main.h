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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h" 
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

#include "utilities.h"
#include "config.h"

#define SLEEPTIME 30
#define BUTTON_PIN GPIO_NUM_13
#define RELAY0 GPIO_NUM_26
#define RELAY1 GPIO_NUM_27
#define RELAY2 GPIO_NUM_9
#define RELAY3 GPIO_NUM_10
#define S_TO_uS(s) (s * 1000000)
#define uS_TO_S(s) (s / 1000000)

static void log_error_if_nonzero(const char *message, int error_code);
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_connection(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);
void app_main(void);

#endif // __MAIN_H__