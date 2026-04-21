#include "WebBackend.h"

#include "common.h"
#if defined(TARGET_RX)
#include "CustomMixer.h"
#include "devServoOutput.h"
#include "../../src/rx-serial/devSerialIO.h"
#include "AM32.h"
#endif
#if defined(TARGET_TX)
#include "handset.h"
#endif

static bool webbe_installed = false;

#ifdef BUILD_WEB_BACKEND_WEBSOCKET
static AsyncWebSocket* ws;
static bool ws_started = false;

static constexpr uint8_t WS_CHANNEL_PACKET_HEADER = '>';
static constexpr uint8_t WS_CHANNEL_PACKET_FOOTER = '#';
static constexpr uint8_t WS_ACK = '!';
static constexpr size_t WS_CHANNEL_PACKET_LEN = CRSF_NUM_CHANNELS * 2U;
static uint8_t wsChannelPacket[WS_CHANNEL_PACKET_LEN];
static size_t wsChannelPacketLen = 0;
static bool wsChannelPacketActive = false;
static uint32_t wsLastPacketTime = 0;

static inline void resetWsChannelPacket()
{
    wsChannelPacketLen = 0;
    wsChannelPacketActive = false;
}

static bool parseWsChannelPacket()
{
    if (wsChannelPacketLen != WS_CHANNEL_PACKET_LEN)
    {
        return false;
    }

    const uint16_t *packet = (const uint16_t *)wsChannelPacket;
    for (uint8_t ch = 0; ch < CRSF_NUM_CHANNELS; ++ch)
    {
        ChannelData[ch] = (uint16_t)packet[ch * 2] | ((uint16_t)packet[(ch * 2) + 1] << 8);
    }

    return true;
}

void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len)
{
    // assume single client!

    if (type == WS_EVT_CONNECT)
    {
        resetWsChannelPacket();
        ws_started = true;
    }
    else if (type == WS_EVT_DATA)
    {
        uint32_t now = millis();

        // if we stop mid-stream for a long time, reset the state machine
        if ((now - wsLastPacketTime) >= 500 && wsLastPacketTime != 0) {
            resetWsChannelPacket();
        }

        for (size_t i = 0; i < len; ++i) // for every byte in this current payload
        {
            const uint8_t ch = data[i];

            // state machine is waiting for header
            if (!wsChannelPacketActive && ch == WS_CHANNEL_PACKET_HEADER)
            {
                wsChannelPacketLen = 0;
                wsChannelPacketActive = true;
                // go to next state, check next byte
                continue;
            }

            if (!wsChannelPacketActive)
            {
                // do not populate temporary buffer
                continue;
            }

            // end of packet expected
            if (wsChannelPacketLen == WS_CHANNEL_PACKET_LEN)
            {
                // is end of packet, validated (and copied)
                if (ch == WS_CHANNEL_PACKET_FOOTER && parseWsChannelPacket())
                {
                    wsLastPacketTime = now;
                    #if defined(TARGET_RX)
                    custommixer_mix();
                    // signal to other services
                    servoNewChannelsAvailable();
                    crsfRCFrameAvailable();
                    #endif
                    #if defined(TARGET_TX)
                    handset->FakeDataReceived();
                    #endif
                    // reply back to front-end with an ack
                    client->text(&WS_ACK, 1U);
                }
                resetWsChannelPacket(); // restart state machine
                continue;
            }

            if (wsChannelPacketLen >= WS_CHANNEL_PACKET_LEN) // error, stream did not have a terminator
            {
                resetWsChannelPacket(); // restart state machine
                continue;
            }

            wsChannelPacket[wsChannelPacketLen++] = ch;
        }
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        (void)server;
        (void)client;
        resetWsChannelPacket();
        #if defined(TARGET_RX)
        servosFailsafe();
        #endif
    }
    else if (type == WS_EVT_ERROR)
    {
        // WebSocket error event.
    }
    else if (type == WS_EVT_PONG)
    {
        // WebSocket pong frame received from the client.
    }
}
#endif

void webbe_tick()
{
    // this function only runs if wifiStarted is true
    // it will repeat as fast as the device schedule engine allows

    uint32_t now = millis();
    (void)now;

    #if defined(TARGET_RX) && defined(PLATFORM_ESP32)
    am32_tick();
    #endif

    #if defined(TARGET_RX) && defined(BUILD_WEB_BACKEND_WEBSOCKET)
    if (ws_started) {
        servosUpdate(now); // transitioning to Wi-Fi mode would have disabled the servos device scheduler, so we call the update function here
        // no need to call handleSerialIO() as it is called in the main application loop
        // the serial IO device scheduler is still running, and will handle the new data if crsfRCFrameAvailable is called
    }
    
    if ((now - wsLastPacketTime) >= 500 && wsLastPacketTime != 0) {
        // I understand this might be redundant as servosUpdate itself has an internal timeout
        servosFailsafe();
    }
    #endif
}

void webbe_install(AsyncWebServer* srv)
{
    if (webbe_installed) {
        // do not repeat
        return;
    }

    #if defined(TARGET_RX) && defined(PLATFORM_ESP32)
    am32_setupServer(srv);
    #endif

    #ifdef BUILD_WEB_BACKEND_WEBSOCKET
    ws = new AsyncWebSocket("/ws");
    ws->onEvent(onWsEvent);
    srv->addHandler(ws);
    #endif

    webbe_installed = true; // do not repeat
}
