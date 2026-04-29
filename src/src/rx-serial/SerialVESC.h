#pragma once
#include "SerialIO.h"
#include "VESC.h"
#include "CRSFRouter.h"

class SerialVESC final : public SerialIO, public CRSFConnector {
public:
    explicit SerialVESC(Stream &out, Stream &in)
        : SerialIO(&out, &in)
    {
        crsfRouter.addConnector(this);
    }
    ~SerialVESC() override;

    void begin(uint8_t idx, int8_t pin = -1, int8_t pin_rx = -1);

    uint32_t sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData) override;
    void forwardMessage(const crsf_header_t *message) override;

    bool sendImmediateRC() override { return true; }

private:
    void processBytes(uint8_t *bytes, uint16_t size) override;
    bool processPacketPayload(const uint8_t *message, uint8_t payloadLength, uint8_t cachedLength);
    void resetReceiveState();
    bool handleTcpBridge(uint8_t *bytes, uint16_t size);

    enum receive_state_e : uint8_t {
        RECEIVE_WAIT_START,
        RECEIVE_LENGTH,
        RECEIVE_PAYLOAD,
        RECEIVE_CRC_HIGH,
        RECEIVE_CRC_LOW,
        RECEIVE_STOP
    };

    // COMM_GET_VALUES currently consumes 59 payload bytes total:
    // 1 byte packet id + 58 bytes of decoded/skipped fields. Keep a little
    // headroom so minor payload growth does not immediately require resizing.
    static constexpr uint8_t PAYLOAD_CACHE_SIZE = 64;

    bool configed = false; // indicate if begin() has been called
    uint8_t idx;           // either 0 for main Serial, or 1 for Serial1
    int pin;               // physical GPIO number
    int pin_rx;            // physical GPIO number
    vesc_cfg_t cfg[3];     // loaded configuration cached here
    int32_t range[3];      // cached decoded range value

    bool is_main_vesc = false; // the main VESC is the one with the RX pin, it can use telemetry and TCP bridge

    receive_state_e receiveState = RECEIVE_WAIT_START;
    uint8_t payloadLength = 0;
    uint8_t payloadBytesReceived = 0;
    uint8_t payloadBytesCached = 0;
    uint8_t payloadCache[PAYLOAD_CACHE_SIZE] = {};
    uint16_t calculatedCrc = 0;
    uint16_t receivedCrc = 0;
    uint32_t last_byte_time = 0;
    uint32_t last_packet_time = 0;
    uint32_t last_req_time = 0;

    vesc_telem_t telem_data;
};
