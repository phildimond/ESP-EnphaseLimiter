/* Power Manager for solar limiter
   
   Contains routines to manage power measurements and calculations for
   the Enphase solar limiter project.

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

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "commonvalues.h"
#include "powerManager.h"

// Relay power % table
const float relayPower[16] = {1.0, 0.94, 0.88, 0.82, 0.76, 0.64, 0.58, 0.52, 0.46, 0.40, 0.34, 0.28, 0.22, 0.16, 0.10};
const float maxSolarPowerkW = 8.2;
const float maxBatteryChargekW = 5.0;

// -----------------------------------------------
// Initialise a power manager instance
// -----------------------------------------------
void PowerManager_Initialise(powerManager_T* instance)
{
    instance->importPrice = 0.0;
    instance->exportPrice = 0.0;
    instance->gridPowerkW = 0.0;
    instance->housePowerkW = 0.0;
    instance->solarPowerkW = 0.0;
    instance->batteryPowerkW = 0.0;
}

// ---------------------------------------------------
// Decode a JSON string to a new powerManager instance
// 
// Decodes a powerManager struct from a JSON string
//
// Params - instance - the struct to populate
//        - s - JSON string to decode
// Returns- 0 on success, non-zero otherwise
// ---------------------------------------------------
int PowerManager_Decode(powerManager_T* instance, const char* s)
{
    int status = 0;
    
    cJSON* monitor_json = cJSON_Parse(s);
    
    if (monitor_json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error decoding JSON power data. Received: %s\r\nError before: %s\r\n", s, error_ptr);
        }
        return 1;
    }

    const cJSON* p = NULL;
    p = cJSON_GetObjectItem(monitor_json, "importPrice");
    if (cJSON_IsNumber(p)) { instance->importPrice = p->valuedouble;}
    p = cJSON_GetObjectItem(monitor_json, "exportPrice");
    if (cJSON_IsNumber(p)) { instance->exportPrice = p->valuedouble;}
    p = cJSON_GetObjectItem(monitor_json, "batteryLevel");
    if (cJSON_IsNumber(p)) { instance->batteryLevel = p->valuedouble;}
    p = cJSON_GetObjectItem(monitor_json, "powerValues");
    if (cJSON_IsArray(p)) { 
        const cJSON* ai = NULL;
        int len = cJSON_GetArraySize(p);
        char name[80];
        char units[80];
        float val = 0.0;
        for (int i = 0; i < len; i++) {
            ai = cJSON_GetArrayItem(p, i);
            const cJSON* n = cJSON_GetObjectItem(ai, "name");
            const cJSON* u = cJSON_GetObjectItem(ai, "units");
            const cJSON* v = cJSON_GetObjectItem(ai, "value");
            if (n == NULL || u == NULL || v == NULL) {
                status = 1;
                ESP_LOGE(TAG, "Error decoding JSON power data. Error in values array item %d", i + 1);
                break;
            } else {
                if (cJSON_IsString(n)) { strcpy(name, cJSON_GetStringValue(n)); } else { status = 1; }
                if (cJSON_IsString(u)) { strcpy(units, cJSON_GetStringValue(u)); } else { status = 1; }
                if (cJSON_IsNumber(v)) { val = v->valuedouble; }
                if (status == 0) {
                    if (strcmp(units, "kW") != 0) {val /= 1000.0; } // convert to kW is needed
                    if (strcmp(name, "House") == 0) { instance->housePowerkW = val; }
                    else if (strcmp(name, "Solar") == 0) { instance->solarPowerkW = val; }
                    else if (strcmp(name, "Battery") == 0) { instance->batteryPowerkW = val; }
                    else if (strcmp(name, "Grid") == 0) { instance->gridPowerkW = val; }
                    else {
                        ESP_LOGE(TAG, "Error decoding JSON power data. Element %d had unknown type %s.", i, name);
                        status = 1;
                    }
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "Error decoding JSON power data. Couldn't find the powervalues array tag.");
        status = 1;
    }

    if (status == 0) {
        ESP_LOGI(TAG, "Power data: Import = $%0.2f, Export = $%0.2f, BatteryLevel=%0.1f%%, House = %0.3fkW, Grid = %0.3fkW, Solar = %0.3fkW, Battery = %0.3fkW",
            instance->importPrice, instance->exportPrice, instance->batteryLevel, instance->housePowerkW, instance->gridPowerkW, instance->solarPowerkW, instance->batteryPowerkW);
    }

    
    //if (monitor_json != NULL) { cJSON_Delete(monitor_json); }
    if (monitor_json != NULL) { cJSON_Delete(monitor_json); }

    return status;
}

// -----------------------------------------------------------------------------
// Calculate the relay settings needed to zero the solar system's export
// 
// Decodes a powerManager struct from a JSON string
//
// Params - instance - struct toi base calculations on
//        - currentRelayValue - the current relay setting
// Returns- an 8 bit value with the required relay setting
// -----------------------------------------------------------------------------
uint8_t CalculateRelaySettings(powerManager_T* instance, uint8_t currentRelayValue)
{
    float loadkW = 0;

    // We want to avoid battery drain (ie solar first). Calculation will depend on the battery state.
    // If it's less than fully charged we want the solar to feed the house load and provide maximum
    // battery charging current (if it can). If the battery is charged, then only the house load
    // matters, as even if the battery has started to drain then it should be going to the house if
    // we are limiting export. If we are limiting export but the battery is exporting, there is something
    // wrong, and we should ignore it by just covering the house load. The battery manufacturer or 
    // owner may be doing something we don't know about and it's not our place to stop it.
    if (instance->batteryLevel < 100.0) {
        // Battery is charging (or wants to be) - load is house plus max battery can charge at
        loadkW = instance->housePowerkW + maxBatteryChargekW;
    } else {
        // Battery is full but *may* be draining - load is house plus +ve battery
        if (instance->batteryPowerkW <= 0) {
            loadkW = instance->housePowerkW;
        } else {
            loadkW = instance->housePowerkW + instance->batteryPowerkW;
        }
    }

    // Possible maximum solar right now
    float solarMaxPossibleNow = instance->solarPowerkW / relayPower[currentRelayValue];

    // Desired production percentage. Avoid exactly zero max possible solar div by zero error
    if (solarMaxPossibleNow == 0) { solarMaxPossibleNow = 0.100; }
    float desiredSolarProductionPc = loadkW / solarMaxPossibleNow;

    // Find the appropriate load setting by going through the values and finding the one that is closest
    // to and higher than the desired percentage. We will assume the index of zero so we will have the
    // maximum production if we don't find one.
    uint8_t desiredIndex = 0;
    for (int i = 0; i < 16; i++) {
        ESP_LOGI(TAG, "i = %u, desiredIndex = %u, solamMaxPossibleNow = %0.3fkW, prod this index = %0.3fkW", 
            i, desiredIndex, solarMaxPossibleNow, solarMaxPossibleNow * relayPower[i]);
        if (i > desiredIndex && relayPower[i] > desiredSolarProductionPc) {
            desiredIndex = i;
        }
    }

    ESP_LOGI(TAG, "Results of calculation... Maximum possible solar generation now = %0.3fkW", solarMaxPossibleNow);
    ESP_LOGI(TAG, "                          Desired production to cover house & battery charge is %0.3fkW", loadkW);
    ESP_LOGI(TAG, "                          Selected relay = %u which is %0.0f%% power, which is %0.3fkw.", 
        desiredIndex, relayPower[desiredIndex] * 100.0, solarMaxPossibleNow * relayPower[desiredIndex]);

    return desiredIndex;
}
