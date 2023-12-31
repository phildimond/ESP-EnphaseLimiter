#ifndef __POWERMANAGER_C__
#define __POWERMANAGER_C__

typedef struct {
    float importPrice;
    float exportPrice;
    float batteryLevel;
    float gridPowerkW;
    float housePowerkW;
    float solarPowerkW;
    float batteryPowerkW;
} powerManager_T;

void PowerManager_Initialise(powerManager_T* instance);
int PowerManager_Decode(powerManager_T*  instance, const char* s);
uint8_t CalculateRelaySettings(powerManager_T* instance, uint8_t currentRelayValue);

#endif // __POWERMANAGER_C__
