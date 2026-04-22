var Joy1;
var Joy2;
var Slider1;
var Slider2;

var webjoy_task_timer;
var webjoy_task_timer_backup;
var wakeLock = null;
var webjoy_paused = false;

const CRSF_CHANNEL_VALUE_MIN  = 172;
const CRSF_CHANNEL_VALUE_1000 = 191;
const CRSF_CHANNEL_VALUE_MID  = 992;
const CRSF_CHANNEL_VALUE_2000 = 1792;
const CRSF_CHANNEL_VALUE_MAX  = 1811;

globalThis.channel = new Array(16).fill(CRSF_CHANNEL_VALUE_MID);
globalThis.channel16 = new Uint16Array(16);

function webjoy_onLoad() {
    var joystickScale = Number(joystick_size_scale);
    if (joystickScale > 0) {
        var joystickDivs = [document.getElementById("joy1Div"), document.getElementById("joy2Div")];
        for (let i = 0; i < joystickDivs.length; i++) {
            var joystickDiv = joystickDivs[i];
            joystickDiv.style.width = (joystickDiv.clientWidth * joystickScale / 100) + "px";
            joystickDiv.style.height = (joystickDiv.clientHeight * joystickScale / 100) + "px";
        }
    }
    Joy1 = new JoyStick('joy1Div');
    Joy2 = new JoyStick('joy2Div');
    Slider1 = document.getElementById("slider_1");
    Slider2 = document.getElementById("slider_2");
    var sliderScale = Number(slider_width_scale);
    if (sliderScale > 0) {
        var sliderWrappers = document.getElementsByClassName("slider-with-ticks");
        for (let i = 0; i < sliderWrappers.length; i++) {
            sliderWrappers[i].style.transform = "scaleX(" + (sliderScale / 100) + ")";
            sliderWrappers[i].style.transformOrigin = "center center";
        }
    }
    Slider1.onchange = webjoy_onSliderChange;
    Slider2.onchange = webjoy_onSliderChange;
    if (slider_bidirectional) {
        setSliderVal(Slider1, 50);
        setSliderVal(Slider2, 50);
    }
    else {
        setSliderVal(Slider1, 0);
        setSliderVal(Slider2, 0);
    }
    document.getElementById('joystick_area').classList.add("hidden");
    try {
        initWakeLock();
    }
    catch (e) {
        console.log("err initWakeLock");
        console.log(e);
    }
    setupGamepadEvents();
    setupVisibilitySafety();
    websock_init();
    startWebjoyTask();
}

function startWebjoyTask() {
    if (webjoy_paused || webjoy_task_timer != null || webjoy_task_timer_backup != null) {
        return;
    }
    webjoy_task_timer = requestAnimationFrame(webjoy_task);
}

function stopWebjoyTask() {
    if (webjoy_task_timer != null) {
        cancelAnimationFrame(webjoy_task_timer);
        webjoy_task_timer = null;
    }
    if (webjoy_task_timer_backup != null) {
        clearTimeout(webjoy_task_timer_backup);
        webjoy_task_timer_backup = null;
    }
}

function setWebjoyPaused(paused) {
    if (webjoy_paused === paused) {
        return;
    }
    webjoy_paused = paused;
    if (paused) {
        stopWebjoyTask();
        clearTimeout(ws_reconnect_timer);
        ws_reconnect_timer = null;
        if (ws != null) {
            ws.close();
        }
    }
    else {
        websock_init();
        startWebjoyTask();
    }
}

function setupVisibilitySafety() {
    document.addEventListener('visibilitychange', () => {
        setWebjoyPaused(document.visibilityState !== 'visible');
    });
    window.addEventListener('pagehide', () => {
        setWebjoyPaused(true);
    });
    window.addEventListener('pageshow', () => {
        setWebjoyPaused(document.visibilityState !== 'visible');
    });
}

function webjoy_task()
{
    if (webjoy_paused) {
        return;
    }
    clearTimeout(webjoy_task_timer_backup);
    webjoy_task_timer = null;
    webjoy_task_timer_backup = null;
    var currentTime = Date.now();

    updateMixerDisplay();

    if (MyGamepad == null) {
        updateGamepadState();
    } else {
        pollGamepad();
    }

    var tosend = runMixer();

    if (ws_checkConnection() || ws.readyState === WebSocket.OPEN)
    {
        document.getElementById("msg_wifidisconnected").classList.add("hidden");
        if (ws.bufferedAmount === 0)
        {
            var timedout = ((currentTime - ws_timestamp) >= 100);
            if (timedout) {
                tosend = true;
            }
            if (typeof tosend === 'boolean' && tosend === true) {
                for (let i = 0; i < channel.length; i++) {
                    var x = clamp(channel[i], 0, 2048);
                    channel16[i] = Math.round(x);
                }
                var buffer = new ArrayBuffer((channel.length * 2) + 2);
                var dataview = new DataView(buffer);
                var headstr = '>'; //gamepadIsDisconnected() ? "crsf" : "CRSF";
                for (let i = 0; i < headstr.length; i++) {
                    dataview.setUint8(i, headstr.charCodeAt(i));
                }
                channel16.forEach(function(x, idx) {
                    dataview.setUint16((idx * 2) + 1, x, true); // true for little-endian
                });
                dataview.setUint8((channel.length * 2) + 1, '#'.charCodeAt(0));
                ws.send(buffer);
            }
        }
    }
    else {
        document.getElementById("msg_wifidisconnected").classList.remove("hidden");
    }
    if (gamepadIsDisconnected()) {
        document.getElementById("msg_gamepaddisconnected").classList.remove("hidden");
    }
    else {
        document.getElementById("msg_gamepaddisconnected").classList.add("hidden");
    }
    if (!webjoy_paused) {
        webjoy_task_timer = requestAnimationFrame(webjoy_task);
        webjoy_task_timer_backup = setTimeout(webjoy_task, 100);
    }
}

function updateMixerDisplay() {
    var secondColumn = document.getElementById('second_column');
    var touchArea = document.getElementById('joystick_area');
    var gamepadArea = document.getElementById('gamepad_area');

    var nothing_shown = true;

    if (MyGamepad == null) {
        gamepadArea.classList.add('hidden');
    }
    else {
        gamepadArea.classList.remove('hidden');
        nothing_shown = false;
    }

    touchArea.classList.remove('hidden');
    secondColumn.classList.add('hidden'); // always hide second stick now
    nothing_shown = false;

    if (nothing_shown)
    {
        if (MyGamepad != null) {
            gamepadArea.classList.remove('hidden');
        }
        else {
            touchArea.classList.remove('hidden');
        }
    }
}

var ws;
var ws_reconnect_timer = null;
var ws_timestamp = Date.now();
function websock_init() {
    if (webjoy_paused || document.visibilityState !== 'visible') {
        return;
    }
    ws_reconnect_timer = null;
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${protocol}://${window.location.host}/ws`;
    console.log("Connecting WebSocket " + url);
    ws = new WebSocket(url);
    ws_timestamp = Date.now();
    var conn_timeout = setTimeout(function() {
        if (ws.readyState !== WebSocket.OPEN) {
            console.log('Connection timeout. Closing WebSocket.');
            ws.close();
            ws_primeReconnect();
        }
    }, 3000);
    ws.onopen = function() {
        clearTimeout(conn_timeout);
        ws_timestamp = Date.now();
        console.log('WebSocket connection established');
    };
    ws.onmessage = function(event) {
        ws_timestamp = Date.now();
        console.log('WebSocket data received:', event.data);
        if (typeof event.data === 'string' && event.data.startsWith("T:")) {
            let telem = event.data.substring(3).split(',');
            let telemStr = `RSSI:&nbsp;${telem[0]}&nbsp;;&nbsp;LQ:&nbsp;${telem[1]}&nbsp;;&nbsp;SNR:&nbsp;${telem[2]}`;
            document.getElementById("msg_telemetry").innerHTML = telemStr;
            document.getElementById("msg_telemetry").classList.remove("hidden");
        }
        else if (typeof event.data === 'string' && event.data == "!") {
            document.getElementById("msg_telemetry").classList.add("hidden");
        }
        else if (typeof event.data === 'string' && event.data == "X") {
            document.getElementById("msg_nocontrol").classList.remove("hidden");
        }
    };
    ws.onclose = function(event) {
        console.log('WebSocket closed, event reason:', event.reason);
        ws_checkConnection();
        document.getElementById("msg_wifidisconnected").classList.remove("hidden");
    };
    ws.onerror = function(error) {
        console.error('WebSocket error: ' + error.code);
        ws_checkConnection();
    };
}

function ws_primeReconnect() {
    if (webjoy_paused || document.visibilityState !== 'visible') {
        return;
    }
    if (ws_reconnect_timer != null) {
        return;
    }
    clearTimeout(ws_reconnect_timer);
    ws_reconnect_timer = setTimeout(function() {
        ws_reconnect_timer = null;
        websock_init();
    }, 1000);
}

var timeout_cnt = 0;
function ws_checkConnection() {
    if (ws == null) {
        return false;
    }
    var currentTime = Date.now();
    var timedout = ((currentTime - ws_timestamp) >= 1000);
    if (ws.readyState !== WebSocket.OPEN) {
        return false;
    }
    else {
        if (timedout) {
            console.log('WebSocket no response, closing');
            ws.close();
            ws_primeReconnect();
            return false;
        }
    }
    return ws.readyState === WebSocket.OPEN;
}

var activeGamepadIndex = null;
var activeGamepadId = null;
var MyGamepad = null;
var PrevButton = [];
var OnButtonPress = null;

function updateGamepadState() {
    const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (let i = 0; i < gamepads.length; i++) {
        const gamepad = gamepads[i];
        if (gamepad && activeGamepadIndex === null) {
            if (gamepad.buttons.some(button => button.pressed) || gamepad.axes.some(axis => Math.abs(axis) > 0.1)) {
                activeGamepadIndex = gamepad.index;
                console.log(`Active gamepad set to index ${activeGamepadIndex}`);
                MyGamepad = navigator.getGamepads()[activeGamepadIndex];
                if (activeGamepadId != MyGamepad.id) {
                    activeGamepadId = MyGamepad.id;
                    buildGamepadView();
                }
                PrevButton = new Array(MyGamepad.buttons.length).fill(false);
            }
        }
    }
}

function buildGamepadView() {
    document.getElementById('gamepad_id').innerHTML = "Gamepad: " + MyGamepad.id;
    var container = document.getElementById('gamepad_buttons');
    container.innerHTML = "";
    for (let i = 0; i < MyGamepad.buttons.length; i++) {
        const square = document.createElement('span');
        square.classList.add('square');
        square.id = "sqr_btn_" + i;
        square.innerHTML = i.toString();
        container.appendChild(square);
    }
    container = document.getElementById('gamepad_axis');
    container.innerHTML = "";
    for (let i = 0; i < MyGamepad.axes.length; i++) {
        const square = document.createElement('span');
        square.classList.add('square');
        square.id = "sqr_axes_" + i;
        square.innerHTML = i.toString();
        container.appendChild(square);
    }
    pollGamepad();
}

function pollGamepad() {
    const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
    MyGamepad = gamepads[activeGamepadIndex];
    for (let i = 0; i < MyGamepad.buttons.length; i++) {
        var ele = document.getElementById("sqr_btn_" + i);
        if (MyGamepad.buttons[i].pressed) {
            var r = 127; var g = 127; var b = 127; var x = MyGamepad.buttons[i].value;
            r = mapRange(x, 0, 1, 127, 255, true);
            g = mapRange(x, 0, 1, 127, 0, true);
            b = g;
            ele.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
            ele.style.borderColor     = 'green';
        }
        else {
            ele.style.backgroundColor = 'gray';
            ele.style.borderColor     = 'black';
        }
        if (PrevButton.length > i ) {
            if (MyGamepad.buttons[i].pressed && PrevButton[i] != true && typeof OnButtonPress === 'function') {
                OnButtonPress(i);
            }
            PrevButton[i] = MyGamepad.buttons[i].pressed;
        }
    }
    for (let i = 0; i < MyGamepad.axes.length; i++) {
        var ele = document.getElementById("sqr_axes_" + i);
        var x = MyGamepad.axes[i];
        var r = 0; var g = 0; var b = 0;
        if (x < 0) {
            b = mapRange(x, -1, 0, 255, 0, true);
            ele.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
            //ele.style.borderColor     = 'rgb(32, 32, 32)';
        }
        else {
            r = mapRange(x, 0, 1, 0, 255, true);
            ele.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
            //ele.style.borderColor     = 'rgb(223, 223, 223)';
        }
        ele.style.borderColor = 'rgb(32, 32, 32)';
    }
}

function setupGamepadEvents() {
    window.addEventListener('gamepadconnected', (event) => {
        console.log(`Gamepad connected at index ${event.gamepad.index}: ${event.gamepad.id}.`);
        updateGamepadState();
    });
    
    window.addEventListener('gamepaddisconnected', (event) => {
        console.log(`Gamepad disconnected from index ${event.gamepad.index}: ${event.gamepad.id}`);
        if (activeGamepadIndex === event.gamepad.index) {
            activeGamepadIndex = null;
            MyGamepad = null;
            console.log('Active gamepad has been disconnected.');
            if (slider_bidirectional) {
                setSliderVal(Slider1, 50);
            }
            else {
                setSliderVal(Slider1, 0);
            }
        }
        updateGamepadState();
    });
}

function gamepadIsDisconnected() {
    if (MyGamepad == null && activeGamepadId != null) {
        return true;
    }
    return false;
}

function initWakeLock() {
    const requestWakeLock = async () => {
        try {
            wakeLock = await navigator.wakeLock.request('screen');
            console.log('Screen wake lock is active');
            wakeLock.addEventListener('release', () => {
                console.log('Screen wake lock was released');
            });
        } catch (err) {
            console.error(`${err.name}, ${err.message}`);
        }
        try {
            //const anyNav: any = navigator;
            if ('wakeLock' in navigator) {
                navigator["wakeLock"].request("screen")
            }
        } catch (err) {
            console.error(`${err.name}, ${err.message}`);
        }
    };
    document.addEventListener('visibilitychange', async () => {
        if (wakeLock !== null && document.visibilityState === 'visible') {
            await requestWakeLock();
        }
    });
    requestWakeLock();
}

function webjoy_onSliderChange(event)
{
    if (!slider_snap) {
        return;
    }

    var slider = event.target;
    var value = Number(slider.value);
    if (slider_bidirectional)
    {
        if (value >= 40 && value <= 60) {
            slider.value = 50;
        }
    }
    else
    {
        if (value < 20) {
            slider.value = 0;
        }
    }
}

function changeSliderVal(slder, amount_in_percent)
{
    var min = Number(slder.min);
    var max = Number(slder.max);
    var range = max - min;
    slder.value = clamp(Number(slder.value) + (range * amount_in_percent / 100), min, max);
}

function setSliderVal(slder, val)
{
    slder.value = val;
}

function clamp(value, limit1, limit2) {
    var min = Math.min(limit1, limit2);
    var max = Math.max(limit1, limit2);
    return Math.min(Math.max(value, min), max);
}

function mapRange(value, inputLow, inputHigh, outputLow, outputHigh, limit) {
    var x = outputLow + (outputHigh - outputLow) * (value - inputLow) / (inputHigh - inputLow);
    if (limit) {
        x = clamp(x, outputLow, outputHigh);
    }
    return x;
}

function scaleToCRSF(x, s) {
    return mapRange(x * s, -1, 1, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, true);
}

function sliderToCRSF(x, s) {
    return mapRange(x * s, 0, 100, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, true);
}

function findFarthest(data) {
    let farthest = 0;
    let farthestAbs = -1;
    for (let i = 0, len = data.length; i < len; i++) {
        const value = data[i];
        const valueAbs = value < 0 ? -value : value;
        if (valueAbs > farthestAbs) {
            farthestAbs = valueAbs;
            farthest = value;
        }
    }
    return farthest;
}

function runMixer()
{
    if (drive_mix_mode == MIXMODE_MIXED) // assume arcade tank drive mixing is internal to ELRS
    {
        var x = [];
        x.push(Joy1.GetX());
        x.push(Joy2.GetX());
        if (MyGamepad != null) {
            if (slider_assignment != 1) {
                x.push(MyGamepad.axes[PAD_IDX_STICK_LEFT_X]);
            }
            if (slider_assignment != 2) {
                x.push(MyGamepad.axes[PAD_IDX_STICK_RIGHT_X]);
            }
        }
        var y = [];
        y.push(Joy1.GetY());
        y.push(Joy2.GetY());
        if (MyGamepad != null) {
            if (slider_assignment != 1) {
                y.push(MyGamepad.axes[PAD_IDX_STICK_LEFT_Y]);
            }
            if (slider_assignment != 2) {
                y.push(MyGamepad.axes[PAD_IDX_STICK_RIGHT_Y]);
            }
            if (slider_assignment != 3) {
                y.push(MyGamepad.buttons[PAD_IDX_TRIGGER_LEFT].value - MyGamepad.buttons[PAD_IDX_TRIGGER_RIGHT].value);
            }
        }
        x = findFarthest(x);
        y = findFarthest(y);

        if (throttle_chan >= 0) {
            channel[throttle_chan] = scaleToCRSF(y, 1);
        }
        if (steering_chan >= 0) {
            channel[steering_chan] = scaleToCRSF(x, 1);
        }
        if (typeof simpleTankMix === 'function' && mix_cfg != null) {
            simpleTankMix(y, x, mix_cfg);
        }

        if (weapon_chan >= 0)
        {
            channel[weapon_chan] = scaleToCRSF(-1, 1);
            if (MyGamepad == null || slider_assignment == 0) {
                channel[weapon_chan] = scaleToCRSF(mapRange(Slider1.value, 0, 100, -1, 1, true), 1);
            }
            else if (MyGamepad != null)
            {
                if (slider_assignment == 1) {
                    channel[weapon_chan] = scaleToCRSF(MyGamepad.axes[PAD_IDX_STICK_LEFT_Y], 1);
                }
                else if (slider_assignment == 2) {
                    channel[weapon_chan] = scaleToCRSF(MyGamepad.axes[PAD_IDX_STICK_RIGHT_Y], 1);
                }
                else if (slider_assignment == SLIDERMODE_TRIGGERS) {
                    var t = MyGamepad.buttons[PAD_IDX_TRIGGER_LEFT].value - MyGamepad.buttons[PAD_IDX_TRIGGER_RIGHT].value;
                    if (slider_bidirectional == false) {
                        t = mapRange(t, 0, 1, -1, 1, true);
                    }
                    setSliderVal(Slider1, mapRange(t, -1, 1, 0, 100, true));
                    channel[weapon_chan] = scaleToCRSF(t, 1);
                }
            }
        }
    }
    else if (drive_mix_mode == MIXMODE_LEFT_RIGHT)
    {
        if (MyGamepad == null) {
            return false;
        }

        if (throttle_chan >= 0) {
            channel[throttle_chan] = scaleToCRSF(MyGamepad.axes[PAD_IDX_STICK_LEFT_Y], 1);
        }
        if (steering_chan >= 0) {
            channel[steering_chan] = scaleToCRSF(MyGamepad.axes[PAD_IDX_STICK_RIGHT_Y], 1);
        }
        if (slider_assignment == SLIDERMODE_TRIGGERS) {
            var t = MyGamepad.buttons[PAD_IDX_TRIGGER_LEFT].value - MyGamepad.buttons[PAD_IDX_TRIGGER_RIGHT].value;
            if (slider_bidirectional == false) {
                t = mapRange(t, 0, 1, -1, 1, true);
            }
            setSliderVal(Slider1, mapRange(t, -1, 1, 0, 100, true));
            if (weapon_chan >= 0) {
                channel[weapon_chan] = scaleToCRSF(t, 1);
            }
        }
    }

    if (slider_assignment == SLIDERMODE_BUTTON_MOMENTARY)
    {
        var v = 0;
        // if no button is pressed or if the gamepad is missing, send the stop signal by default
        if (slider_bidirectional) {
            v = 0;
        }
        else {
            v = -1;
        }

        if (MyGamepad != null)
        {
            if (btnidx_weapon_faster >= 0 && MyGamepad.buttons[btnidx_weapon_faster].value > 0) // button assigned and is being pressed
            {
                // we'll use the slider value as the active weapon value
                if (slider_bidirectional) {
                    v = mapRange(Slider1.value, 0, 100, -1, 1);
                }
                else {
                    v = mapRange(Slider1.value, 0, 100, 0, 1);
                }
            }
        }

        if (weapon_chan >= 0) {
            channel[weapon_chan] = scaleToCRSF(v, 1);
        }
    }
    else if (slider_assignment == SLIDERMODE_BUTTON_LATCH)
    {
        if (MyGamepad != null)
        {
            if (btnidx_weapon_faster >= 0 && btnidx_weapon_stop >= 0) // these must be defined, for safety, we can't let you run a weapon and have no way of stopping it
            {
                if (btnidx_weapon_faster >= 0 && MyGamepad.buttons[btnidx_weapon_faster].value > 0)
                {
                    changeSliderVal(Slider1, 10);
                }
                if (btnidx_weapon_slower >= 0 && MyGamepad.buttons[btnidx_weapon_slower].value > 0)
                {
                    changeSliderVal(Slider1, -10);
                }
                if (btnidx_weapon_stop >= 0 && MyGamepad.buttons[btnidx_weapon_stop].value > 0)
                {
                    if (slider_bidirectional) {
                        setSliderVal(Slider1, 50);
                    }
                    else {
                        setSliderVal(Slider1, 0);
                    }
                }
            }
        }
        let v = Slider1.value;
        v = mapRange(v, 0, 100, -1, 1);
        if (weapon_chan >= 0) {
            channel[weapon_chan] = scaleToCRSF(v, 1);
        }
    }

    return true;
}
