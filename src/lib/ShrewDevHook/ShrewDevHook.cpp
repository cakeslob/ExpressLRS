#include "ShrewDevHook.h"
#if defined(PLATFORM_ESP32)

// here are a bunch of functions that are called from various places throughout the ExpressLRS firmware
// it is much easier for you to put content into these functions, rather than modifying existing ExpressLRS files
// this way, when ExpressLRS firmware gets an update, it will never conflict with your code
// you can also make your own files, and then use the functions in this file to call into your own files

// please do not modify "ShrewDevHook.h", if you need additional header files, use your own include statements
// please do not modify the function signatures

// this is called from the top of rx_main setup()
// ONLY use this to initialize your own variables
// no hardware/pins has been setup/initialized at this point
void shrewdevhook_preSetup()
{

}

// this is called from the end of rx_main setup()
// some hardware has been initialized, and can be further initialized by you
void shrewdevhook_postSetup()
{

}

// overrides the number of LED strip pixels right before initializing the strip
// return 0 or negative means "use the count from the hardware JSON"
// returning anything else will set the maximum number of pixels on the strip
// return 0 if you are not using this functionality
int16_t shrewdevhook_getPixelCount(void)
{
    return 0;
}

// called every X-ms from devRGB.cpp, return false to continue default animation, return true to write your own animation (not run the default animation)
// this is where you want to put LED strip animations
// return false if you are not using this functionality
bool shrewdevhook_onLedTick(void)
{
    return false;
}

// sets the repetition period, in milliseconds, that shrewdevhook_onLedTick is repeatedly called with
// must be greater or equal to 10
// this is only read once, it cannot be dynamically changing
uint32_t shrewdevhook_getLedTickPeriod(void)
{
    return 0;
}

// called from shrew_bootStatus inside devRGB.cpp
// return 0 means continue default boot status animation
// return 1 means stop boot status animation
// return 2 means executing custom boot animation, do not run default boot status animation
// return 0 if you are not using this functionality
uint8_t shrewdevhook_onLedBootStatusTick(void)
{
    return 0;
}

// called from event "servoNewChannelsAvailable", which happens when a new packet arrives from the transmitter
void shrewdevhook_onNewData(void)
{

}

// called from the end of "servoFailsafe"
void shrewdevhook_postServoFailsafe(void)
{

}

// called on every tick of the servo device update function
// this is where you can service a sensor's state machine
void shrewdevhook_tick(uint32_t now, bool new_data_avail)
{

}

// called when servos are about to be updated, but after mixing has already occurred
// this is where you want to change ChannelData if you want to manipulate outputs
void shrewdevhook_preServoUpdate(uint32_t now)
{

}

// called after servos have been updated
void shrewdevhook_postServoUpdate(void)
{

}

// called after servos have been initialized
void shrewdevhook_postServoStart(void)
{

}

#endif // PLATFORM_ESP32
