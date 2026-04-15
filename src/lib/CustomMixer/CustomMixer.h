#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "common.h"
#include "CustomMixer_types.h"
#include <ArduinoJson.h>

extern custom_mixer_t* custom_mixer;
extern uint32_t ChannelDataMixed[CRSF_NUM_CHANNELS];

void custommixer_init(const custom_mixer_t* cfg_ptr);
void custom_mixer_to_json(const custom_mixer_t* mixer, JsonObject obj);
void json_to_custom_mixer(JsonObjectConst obj, custom_mixer_t* mixer);

void custommixer_mix();
bool custommixer_isArmed();
