#include "shrew_testbench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <HardwareSerial.h>
#include <common.h>
#include <logging.h>
#include <SX1280Driver.h>
#include <FHSS.h>
#include <POWERMGNT.h>
#include <device.h>
#include <OTA.h>

typedef void (*func_ptr_t)(void);

#define BUF_SIZE (512)

char inp_line[BUF_SIZE];
uint32_t inp_idx = 0;
char inp_cmd[128];
long inp_ints[8];
int32_t inp_paramCnt = 0;

void execute_cmd(char*);
void phy_calib_and_save();
bool split_and_parse(const char *input, char *first_part, long *integers, int *num_integers);
bool match_and_execute(const char* cmd, char* inp, int param_cnt, func_ptr_t func);
void wait_for_send();
uint32_t calc_hop_delay(uint32_t x);

bool ICACHE_RAM_ATTR testbench_RXdoneISR(SX12xxDriverCommon::rx_status const status);
void ICACHE_RAM_ATTR testbench_TXdoneISR();

extern void SetRFLinkRate(uint8_t index, bool bindMode);
extern const fhss_config_t *FHSSconfig;
extern uint32_t freq_spread;

static volatile bool tx_is_done = true;
static int tx_continuous_mode = 0;
static int last_cfg_idx = -1;
static uint32_t cur_freq;
static uint32_t freq_space;
static uint32_t freq_lim_max;
static uint32_t freq_lim_min;
static uint32_t freq_twin;
static uint32_t hop_delay;
static uint32_t hop_last_time = 0;

static uint32_t tx_cnt = 0;

extern void shrew_blinky_init(int gpio_pin, int num_leds);
extern void shrew_blinky_once(uint8_t r, uint8_t g, uint8_t b, uint32_t delay);

void shrew_testbench()
{
    setCpuFrequencyMhz(80);
    Serial.begin(115200);

    shrew_blinky_init(GPIO_PIN_LED_WS2812, 1);
    while (1) {
        shrew_blinky_once(0, 128, 128, 200);
        shrew_blinky_once(0, 0, 0, 800);
    }

    esp_sleep_enable_uart_wakeup(0);
    devicesRegister(NULL, 0);
    Serial.print("\r\nhello world shrew testbench");
    Serial.printf("\r\n>\r\n");
    while (true)
    {
        char ch;
        if (Serial.available() > 0)
        {
            ch = Serial.read();
            if (ch == 0 || ch == '\r' || ch == '\n') {
                inp_line[inp_idx] = 0;
                int slen = strlen(inp_line);
                if (slen > 0) {
                    Serial.printf("CMD: %s\r\n", inp_line);
                    execute_cmd(inp_line);
                }
                inp_idx = 0;
            }
            else {
                inp_line[inp_idx] = ch;
                if (inp_idx < (BUF_SIZE - 3)) {
                    inp_idx++;
                }
            }
        }

        if (tx_continuous_mode != 0) {
            bool time_passed = false;
            uint32_t now;
            if (hop_delay >= 50000) {
                if (((now = millis()) - hop_last_time) >= (hop_delay / 1000) && tx_is_done) {
                    hop_last_time = now;
                    time_passed = true;
                }
            }
            else {
                if (((now = micros()) - hop_last_time) >= hop_delay && tx_is_done) {
                    hop_last_time = now;
                    time_passed = true;
                }
            }
            if (tx_is_done && time_passed) {
                tx_is_done = false;
                if (tx_continuous_mode == 1) {
                    int f;
                    Radio.SetFrequencyReg(f = FHSSgetNextFreq());
                    //Serial.printf("h %d\r\n", f);
                }
                else if (tx_continuous_mode == 2) {
                    Radio.SetFrequencyHz(cur_freq * 1000, SX12XX_Radio_1);
                    //Serial.printf("s %d KHz\r\n", cur_freq);
                    cur_freq += freq_space;
                    if (cur_freq > freq_lim_max) {
                        cur_freq = freq_lim_min;
                    }
                }
                else if (tx_continuous_mode == 3) {
                    if (cur_freq == freq_twin) {
                        cur_freq = freq_twin + freq_space;
                    }
                    else {
                        cur_freq = freq_twin;
                    }
                    Radio.SetFrequencyHz(cur_freq * 1000, SX12XX_Radio_1);
                    //Serial.printf("s %d KHz\r\n", cur_freq);
                }
                Radio.TXnb((uint8_t*)inp_line, ELRS8_TELEMETRY_BYTES_PER_CALL, SX12XX_Radio_1);
                tx_cnt++;
                if ((tx_cnt % 100) == 0) {
                    //Serial.printf("tx: %u / %u\r\n", tx_cnt, millis());
                }
            }
        }

        yield();
        //esp_light_sleep_start();
    }
}

void func_echo()
{
    Serial.println("echo");
}

void func_radioconfig()
{
    int rate_idx = inp_ints[0];
    SetRFLinkRate(rate_idx, false);
    Serial.printf("Radio configured to config index %d\r\n", rate_idx);
    last_cfg_idx = rate_idx;
}

void func_radiobegin()
{
    FHSSrandomiseFHSSsequence(uidMacSeedGet());
    Radio.currFreq = FHSSgetInitialFreq();
    bool init_success = Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq());
    POWERMGNT::init();
    DBGLN("attaching callbacks");
    Radio.RXdoneCallback = &testbench_RXdoneISR;
    Radio.TXdoneCallback = &testbench_TXdoneISR;
    if (init_success) {
        Serial.printf("Radio initialized\r\n");
        freq_lim_min = FHSSgetMinimumFreq();
        freq_lim_min = (unsigned long)lround(((double)freq_lim_min) * FREQ_STEP / 1000.0);
        freq_lim_max = FHSSgetMaximumFreq();
        freq_lim_max = (unsigned long)lround(((double)freq_lim_max) * FREQ_STEP / 1000.0);
        freq_space = freq_spread;
        freq_space = (unsigned long)lround(((double)freq_space) * FREQ_STEP / 1000.0 / (double)FREQ_SPREAD_SCALE);
        Serial.printf("f_min = %u KHz\r\n", freq_lim_min);
        Serial.printf("f_max = %u KHz\r\n", freq_lim_max);
        Serial.printf("channels = %u\r\n", FHSSconfig->freq_count);
        Serial.printf("f_spacing = %u KHz (%u)\r\n", freq_space, freq_spread);
        /*
        f_min = 2400400 KHz
        f_max = 2479400 KHz
        channels = 80
        f_spacing = 1000 KHz (1290554)
        */
    }
    else {
        Serial.printf("ERROR: Radio FAILED!!!\r\n");
    }
}

void func_radioend()
{
    wait_for_send();
    tx_continuous_mode = 0;
    Radio.End();
    Serial.printf("Radio stopped and deinitialized\r\n");
}

void func_radiopower()
{
    int pwr = inp_ints[0];
    Radio.SetOutputPower(pwr);
    Serial.printf("Radio SetOutputPower %d (range %d to %d)\r\n", pwr, SX1280_POWER_MIN, SX1280_POWER_MAX);
}

void func_radiofreq()
{
    wait_for_send();
    unsigned long freq = (unsigned long)inp_ints[0] * 100;
    if (inp_ints[0] == 0) {
        Radio.SetFrequencyReg(freq = FHSSgetInitialFreq(), SX12XX_Radio_1);
        DBGLN("init freq %d", freq);
        freq = (unsigned long)lround(((double)freq) * FREQ_STEP / 1000.0);
    }
    else if (inp_ints[0] == -1) {
        Radio.SetFrequencyReg(freq = FHSSgetMinimumFreq(), SX12XX_Radio_1);
        DBGLN("min freq %d", freq);
        freq = (unsigned long)lround(((double)freq) * FREQ_STEP / 1000.0);
    }
    else if (inp_ints[0] == -2) {
        Radio.SetFrequencyReg(freq = FHSSgetMaximumFreq(), SX12XX_Radio_1);
        DBGLN("max freq %d", freq);
        freq = (unsigned long)lround((((double)freq)) * FREQ_STEP / 1000.0);
    }
    else {
        Radio.SetFrequencyHz(freq, SX12XX_Radio_1);
    }
    Serial.printf("Radio set frequency to %u KHz\r\n", freq);
}

void func_radiostarthop()
{
    tx_continuous_mode = 1;
    Serial.printf("Radio starting to freq hop and continuously transmitting packets\r\n");
}

void func_radiostartsweep()
{
    tx_continuous_mode = 2;
    cur_freq = freq_lim_min;
    Serial.printf("Radio starting to freq sweep and continuously transmitting packets\r\n");
}

void func_radiostarttwin()
{
    tx_continuous_mode = 3;
    freq_twin = inp_ints[1] * 100;
    cur_freq = freq_twin;
    Serial.printf("Radio starting to freq twin hopping and continuously transmitting packets\r\n");
}

void func_radiostartsingle()
{
    if (inp_paramCnt == 1) {
        wait_for_send();
        func_radiofreq();
    }
    tx_continuous_mode = 4;
    Serial.printf("Radio starting to continuously transmit packets\r\n");
}

void func_radiotone()
{
    wait_for_send();
    unsigned long freq = (unsigned long)inp_ints[0] * 100;
    if (inp_ints[0] == 0) {
        freq = FHSSgetInitialFreq();
        DBGLN("init freq %d", freq);
        freq = (long)lround(((double)freq) * FREQ_STEP / 1000.0);
    }
    else if (inp_ints[0] == -1) {
        freq = FHSSgetMinimumFreq();
        DBGLN("min freq %d", freq);
        freq = (long)lround(((double)freq) * FREQ_STEP / 1000.0);
    }
    else if (inp_ints[0] == -2) {
        freq = FHSSgetMaximumFreq();
        DBGLN("max freq %d", freq);
        freq = (long)lround(((double)freq) * FREQ_STEP / 1000.0);
    }
    Radio.startCWTest(freq * 1000, SX12XX_Radio_1);
    Serial.printf("Radio starting tone test at %d KHz\r\n", freq);
}

void func_teststart()
{
    func_radiobegin();
    func_radioconfig();
    inp_ints[0] = inp_ints[1];
    func_radiopower();
    if (inp_ints[2] == 0)
    {
        inp_ints[0] = inp_ints[3];
        func_radiotone();
    }
    else
    {
        inp_ints[0] = inp_ints[4];
        if (inp_ints[0] > 0) {
            hop_delay = calc_hop_delay(inp_ints[0]);
            Serial.printf("hop delay %u\r\n", hop_delay);
        }
        else {
            hop_delay = 0;
        }
        switch (inp_ints[2])
        {
            case 1:
                func_radiostarthop();
                break;
            case 2:
                func_radiostartsweep();
                break;
            case 3:
                inp_ints[1] = inp_ints[3];
                func_radiostarttwin();
                break;
        }
    }
    Serial.printf("\r\nteststart command success\r\n");
}

void func_radiostop()
{
    wait_for_send();
    tx_continuous_mode = 0;
    SetRFLinkRate(last_cfg_idx, false);
    Serial.printf("Radio stopping\r\n");
}

void func_reboot()
{
    func_radioend();
    Serial.printf("\r\nESP32 rebooting...\r\n");
    delay(500);
    ESP.restart();
}

uint32_t calc_hop_delay(uint32_t x)
{
    double xd = x;
    double p = 1000000 / xd;
    return (uint32_t)lround(p);
}

bool split_and_parse(const char *input, char *first_part, long *integers, int *num_integers)
{
    // Initialize the number of integers parsed
    *num_integers = 0;

    // Create a copy of the input string to tokenize
    static char input_copy[BUF_SIZE];
    strcpy(input_copy, input);

    // Tokenize the first part (alphanumeric)
    char *token = strtok(input_copy, " ");
    if (token != NULL) {
        strcpy(first_part, token);
    }

    // Tokenize the subsequent parts (integers)
    while ((token = strtok(NULL, " ")) != NULL) {
        char *endptr;
        long value;
        if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            value = strtol(&token[2], &endptr, 16);
        }
        else {
            value = strtol(token, &endptr, 10);
        }
        if (*endptr == '\0') {
            integers[*num_integers] = value;
            (*num_integers)++;
        } else {
            printf("Error: Failed to parse integer from '%s'\n", token);
            return false;
        }
    }
    return true;
}

void trim_and_normalize(char *str)
{
    // Trim leading whitespace
    char *start = str;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    // Convert tabs to spaces and eliminate repeating spaces
    char *src = start;
    char *dst = str;
    int space_found = 0;

    while (*src != '\0') {
        if (*src == '\t') {
            *src = ' ';
        }

        if (*src == ' ') {
            if (!space_found) {
                *dst++ = ' ';
                space_found = 1;
            }
        } else {
            *dst++ = *src;
            space_found = 0;
        }
        src++;
    }
    *dst = '\0';

    // Copy the normalized string back to the original buffer
    memmove(str, start, dst - start + 1);
}

void execute_cmd(char* s)
{
    trim_and_normalize(s);
    bool suc = split_and_parse((const char*)s, inp_cmd, inp_ints, &inp_paramCnt);
    if (suc == false) {
        return;
    }

    //for (int i = 0; i < inp_paramCnt; i++) {
    //    DBGLN("PARAM %d %d", i, inp_ints[i]);
    //}

    suc = false;

    suc |= match_and_execute("echo", inp_cmd, 0, func_echo);
    suc |= match_and_execute("reboot", inp_cmd, 0, func_reboot);
    suc |= match_and_execute("radiobegin", inp_cmd, 0, func_radiobegin);
    suc |= match_and_execute("radioconfig", inp_cmd, 1, func_radioconfig);
    suc |= match_and_execute("radioend", inp_cmd, 0, func_radioend);
    suc |= match_and_execute("radiostop", inp_cmd, 0, func_radiostop);
    suc |= match_and_execute("radiofreq", inp_cmd, 1, func_radiofreq);
    suc |= match_and_execute("radiopower", inp_cmd, 1, func_radiopower);
    suc |= match_and_execute("radiostarthop", inp_cmd, 0, func_radiostarthop);
    suc |= match_and_execute("radiostartsweep", inp_cmd, 0, func_radiostartsweep);
    suc |= match_and_execute("radiostartsingle", inp_cmd, 0, func_radiostartsingle);
    suc |= match_and_execute("radiostarttwin", inp_cmd, 0, func_radiostarttwin);
    suc |= match_and_execute("radiotone", inp_cmd, 1, func_radiotone);
    suc |= match_and_execute("teststart", inp_cmd, 5, func_teststart);

    if (suc == false) {
        Serial.printf("ERROR: command '%s' is unknown\r\n", inp_cmd);
    }

    Serial.printf("\r\n>\r\n");
}

bool match_and_execute(const char* cmd, char* inp, int param_cnt, func_ptr_t func)
{
    if (strcmp(cmd, inp) != 0) {
        return false;
    }
    if (inp_paramCnt != param_cnt) {
        Serial.printf("ERROR: command '%s' expects %d parameters but got %d parameters\r\n", cmd, inp_paramCnt, param_cnt);
        return true;
    }
    if (func) {
        func();
    }
    return true;
}

bool ICACHE_RAM_ATTR testbench_RXdoneISR(SX12xxDriverCommon::rx_status const status)
{
    return false;
}

void ICACHE_RAM_ATTR testbench_TXdoneISR()
{
    tx_is_done = true;
}

void wait_for_send()
{
    if (tx_is_done) {
        return;
    }
    uint32_t t = millis();
    while (tx_is_done == false && (millis() - t) <= 100) {
        yield();
    }
    tx_is_done = false;
}