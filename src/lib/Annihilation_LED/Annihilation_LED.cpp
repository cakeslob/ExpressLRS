#include "Annihilation_LED.h"
//#include "ShrewDevHook.h"
#include "common.h"
#include "esp32rgb.h"
extern void WS281BsetLED(int index, uint32_t color);
extern void WS281BsetLED(uint32_t color);
extern void WS281BshowLEDs(void);
extern uint32_t HsvToRgb(const blinkyColor_t &blinkyColor);

#define IDLE_ANIMATION_STYLE 0
#define SECONDARY_ANIMATION_STYLE 0

static blinkyColor_t blinkyColor;

void chasing_rainbow(uint8_t color_difference) {
    static int i = 0;
    blinkyColor.s = 255;
    i = i + 8;
    if (i > 255) {
        i = 0;
    }
    for (int j = 0; j < LED_STRIP_LENGTH; j++) {
        //Serial.print("LED index:  ");
        //Serial.print(j);
        blinkyColor.h = i + (j * color_difference);
        //Serial.print("  LED Hue:   ");
        //Serial.println(blinkyColor.h);
        WS281BsetLED(j, HsvToRgb(blinkyColor));
    }
    WS281BshowLEDs();
}

void fire_animation(int color) {
    for (uint8_t i = 0; i < LED_STRIP_LENGTH; i++) {
        blinkyColor.h = color;
        blinkyColor.s = 255;
        blinkyColor.v = (rand() % 200) + 50;
        WS281BsetLED(i, HsvToRgb(blinkyColor));
    }
    WS281BshowLEDs();
}

uint32_t last_time_led_animation = 0;
int led_index_number = 0;

void christmas_animation() {
    uint32_t now = millis();
    if ((now - last_time_led_animation) < 700) {
        return;
    }
    led_index_number ++;
    if (led_index_number == 254) {led_index_number = 0;}
    last_time_led_animation = now;
    for (uint8_t i = 0; i < LED_STRIP_LENGTH; i++) {
        int switch_case_number = led_index_number + i;
        switch_case_number = switch_case_number % 3;
        switch (switch_case_number) {
            case 0:
                blinkyColor.h = 0;
                break;
            case 1:
                blinkyColor.h = 100;
                break;
            case 2:
                blinkyColor.h = 170;
                break;
        }
        blinkyColor.s = 255;
        blinkyColor.v = 150;
        WS281BsetLED(i, HsvToRgb(blinkyColor));
    }
    WS281BshowLEDs();
}

void christmas_animation_no_blue() {
    uint32_t now_led_animation = millis();
    if ((now_led_animation - last_time_led_animation) < 700) {
        return;
    }
    led_index_number ++;
    if (led_index_number == 254) {led_index_number = 0;}
    last_time_led_animation = now_led_animation;
    for (uint8_t i = 0; i < LED_STRIP_LENGTH; i++) {
        int switch_case_number = led_index_number + i;
        switch_case_number = switch_case_number % 2;
        switch (switch_case_number) {
            case 0:
                blinkyColor.h = 0;
                break;
            case 1:
                blinkyColor.h = 120;
                break;
        }
        blinkyColor.s = 255;
        blinkyColor.v = 150;
        WS281BsetLED(i, HsvToRgb(blinkyColor));
    }
    WS281BshowLEDs();
}

// brightness algorithm 255 - (j * (255 / firework_size))

uint16_t firework_generator = 0;
uint16_t firework_randomness = 75;
uint16_t fade_speed = 15;
uint16_t firework_color = 0;
uint16_t led_brightness[LED_STRIP_LENGTH] = {0,0,0,0,0,0,0,0};
uint16_t led_color[LED_STRIP_LENGTH] = {0,0,0,0,0,0,0,0};
void firework_animation() {
    //fade all leds properly
    for(uint16_t i = 0; i < LED_STRIP_LENGTH; i++) {
        blinkyColor.h = led_color[i];
        if(led_brightness[i] - fade_speed < 0) {
            led_brightness[i] = 0;
            blinkyColor.v = 0;
        }
        else {
            blinkyColor.v = led_brightness[i] - fade_speed;
            led_brightness[i] -= fade_speed;
        }

        WS281BsetLED(i, HsvToRgb(blinkyColor));
    }

    //randomly generate explosion
    for(uint16_t i = 0; i < LED_STRIP_LENGTH; i++) {
        
        firework_generator = random() % firework_randomness;
        firework_color = random() % 255;
        if(firework_generator == 0) {
            Serial.print("Generated firework at ");
            Serial.println(i);
            blinkyColor.h = firework_color;
            blinkyColor.s = 255;
            blinkyColor.v = 255;
            led_color[i] = blinkyColor.h;
            led_brightness[i] = blinkyColor.v;
            WS281BsetLED(i, HsvToRgb(blinkyColor));
        }
    }

    WS281BshowLEDs();
}

void annihilation_led_tick()
{
    // Begin Channel inputted led animations
    if (ChannelData[2] > 340) {
        if(ChannelData[2] > 1300) {
            fire_animation(170);
        }
        else if(ChannelData[2] > 900) {
            fire_animation(200);
        }
        else {
            fire_animation(0);
        }

    }
    else if (ChannelData[5] > 1300) {
        switch (SECONDARY_ANIMATION_STYLE) {
            case 0:
                firework_animation();
                break;
            case 1:
                christmas_animation_no_blue();
                break;
        }
    }
    else {
        switch (IDLE_ANIMATION_STYLE) {
            case 0:
                chasing_rainbow(8);
                break;
            case 1:
                christmas_animation_no_blue();
                break;
        }
    }
}
