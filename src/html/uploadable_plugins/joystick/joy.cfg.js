// Useful enumeration definitions first, then followed by user edited configuration

const PAD_IDX_STICK_LEFT_X  = 0;
const PAD_IDX_STICK_LEFT_Y  = 1;
const PAD_IDX_STICK_RIGHT_X = 2;
const PAD_IDX_STICK_RIGHT_Y = 3;

// Standard Gamepad API button indices for Sony DualShock / DualSense controllers.
// Xbox equivalents are included for the same standard mapping.
const PAD_IDX_BTN_CROSS         = 0;  // Xbox A
const PAD_IDX_BTN_CIRCLE        = 1;  // Xbox B
const PAD_IDX_BTN_SQUARE        = 2;  // Xbox X
const PAD_IDX_BTN_TRIANGLE      = 3;  // Xbox Y
const PAD_IDX_BTN_L1            = 4;  // Xbox LB
const PAD_IDX_BTN_R1            = 5;  // Xbox RB
const PAD_IDX_BTN_L2            = 6;  // Xbox LT
const PAD_IDX_BTN_R2            = 7;  // Xbox RT
const PAD_IDX_BTN_SHARE         = 8;  // Xbox View
const PAD_IDX_BTN_CREATE        = 8;  // Xbox View
const PAD_IDX_BTN_OPTIONS       = 9;  // Xbox Menu
const PAD_IDX_BTN_L3            = 10; // Xbox left stick press
const PAD_IDX_BTN_R3            = 11; // Xbox right stick press
const PAD_IDX_BTN_DPAD_UP       = 12; // Xbox D-pad up
const PAD_IDX_BTN_DPAD_DOWN     = 13; // Xbox D-pad down
const PAD_IDX_BTN_DPAD_LEFT     = 14; // Xbox D-pad left
const PAD_IDX_BTN_DPAD_RIGHT    = 15; // Xbox D-pad right
const PAD_IDX_BTN_PS            = 16; // Xbox Guide
const PAD_IDX_BTN_TOUCHPAD      = 17; // Xbox Share

const PAD_IDX_TRIGGER_LEFT      = PAD_IDX_BTN_L2;
const PAD_IDX_TRIGGER_RIGHT     = PAD_IDX_BTN_R2;

const MIXMODE_MIXED = 0;
const MIXMODE_LEFT_RIGHT = 1; // left game-pad stick maps to left motor channel, same goes for right

const SLIDERMODE_NONE = 0;
const SLIDERMODE_LEFT_STICK = 1;
const SLIDERMODE_RIGHT_STICK = 2;
const SLIDERMODE_TRIGGERS = 3;
const SLIDERMODE_BUTTON_MOMENTARY = 4;
const SLIDERMODE_BUTTON_LATCH = 5;

// ### BEGIN USER EDITED CONFIGURATION ###

const drive_mix_mode = MIXMODE_MIXED;
// assign channels, if using mixing, left+right channels may be configured with mix_cfg
const throttle_chan = -1;
const steering_chan = -1;
const weapon_chan   = -1;

const slider_assignment = SLIDERMODE_NONE; // if using a game-pad, this selects which part of the game-pad maps to the slider/weapon
const slider_bidirectional = false;
const slider_snap = true; // please use this if the slider is controlling a spinning weapon

// assign game-pad buttons if SLIDERMODE_BUTTON_ is used
const btnidx_weapon_stop   = PAD_IDX_BTN_CIRCLE;
const btnidx_weapon_faster = PAD_IDX_BTN_CROSS;
const btnidx_weapon_slower = PAD_IDX_BTN_TRIANGLE;

// adjusts the sizes of the UI elements, in percentage units from default, 100 is default
const joystick_size_scale = 100;
const slider_width_scale  = 100;

// if this is null, then ELRS internal custom mixer is used
// const mix_cfg = null;
// if this cfg has data, it will use the plugin's custom mixer
const mix_cfg = {fn: 0, thr_scale: 1, str_scale: 1, thr_dz: 0.05, str_dz: 0.05, thr_exp: 0, str_exp: 0, thr_trim: 0, str_trim: 0, left_scale: 1, right_scale: 1, left_dz: 0, right_dz: 0, left_exp: 0, right_exp: 0, left_trim: 0, right_trim: 0, left_chan: 0, right_chan: 1};
// if this is used, ideally, set throttle_chan and steering_chan to -1 unless you still want them

// ### END USER EDITED CONFIGURATION ###
