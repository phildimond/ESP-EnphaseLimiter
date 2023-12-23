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
#include "esp_log.h"
#include "cJSON.h"
#include "commonvalues.h"
#include "powerManager.h"

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
    
    cJSON *monitor_json = cJSON_Parse(s);
    
    if (monitor_json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        status = 0;
        return 1;
    }

    const cJSON* p = NULL;
    p = cJSON_GetObjectItem(monitor_json, "importPrice");
    if (cJSON_IsNumber(p))
    {
        ESP_LOGV(TAG, "Import price is %f", p->valuedouble);
    }

    
    //if (monitor_json != NULL) { cJSON_Delete(monitor_json); }
    if (monitor_json != NULL) { cJSON_Delete(monitor_json); }

    return 0;
}
