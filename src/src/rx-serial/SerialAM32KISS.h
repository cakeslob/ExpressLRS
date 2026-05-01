#pragma once
#include "SerialIO.h"
#include "CRSFRouter.h"

typedef struct __attribute__((packed))
{
    uint8_t temperature; // temperature in Celcius
    uint8_t voltage_h; // voltage in centivolts
    uint8_t voltage_l;
    uint8_t current_h; // current in centiamps
    uint8_t current_l;
    uint8_t consumption_h; // accumulated current consumption in mAH
    uint8_t consumption_l;
    uint8_t erpm_h; // eRPM * 100, so 1 in the packet means 100 eRPM
    uint8_t erpm_l;
    uint8_t crc;
}
kiss_telem_pkt_t; // sizeof(kiss_telem_pkt_t) = 10

class SerialAM32KISS final : public SerialIO, public CRSFConnector {
public:
    explicit SerialAM32KISS(Stream &out, Stream &in)
        : SerialIO(&out, &in)
    {
        crsfRouter.addConnector(this);
    }
    ~SerialAM32KISS() override;

    void begin(uint8_t idx, int8_t pin_rx = -1);

    uint32_t sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData) override;
    void forwardMessage(const crsf_header_t *message) override;

    bool sendImmediateRC() override { return true; }

    const kiss_telem_pkt_t *getTelemetry() const { return &data; }
    void resetReceiveState();

private:
    void processBytes(uint8_t *bytes, uint16_t size) override;
    bool processPacketPayload(const uint8_t *message, uint8_t payloadLength, uint16_t cachedLength);

    static constexpr uint16_t PAYLOAD_CACHE_SIZE = sizeof(kiss_telem_pkt_t);
    static constexpr uint32_t RECEIVE_RESET_GAP_MS = 10;

    bool configed = false; // indicate if begin() has been called
    uint8_t idx;           // either 0 for main Serial, or 1 for Serial1
    int pin_rx;            // physical GPIO number

    uint16_t payloadBytesReceived = 0;
    uint8_t payloadCache[PAYLOAD_CACHE_SIZE] = {};
    uint32_t last_byte_time = 0;
    uint32_t last_packet_time = 0;

    // this stores the telemetry data
    kiss_telem_pkt_t data = {};
};
