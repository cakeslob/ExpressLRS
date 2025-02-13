#include "OTA_Legacy.h"
#include "OTA.h"
#include "common.h"
#include "logging.h"
#include <stdlib.h>

extern uint16_t OtaCrcInitializer;
#define OtaCrcInitializer_v3    (uint16_t)((OtaCrcInitializer ^ OTA_VERSION_ID) ^ (OTA_VERSION_ID - 1))
// this provides a version of OtaCrcInitializer as if it was a previous version

extern bool OtaIsFullRes;
extern Crc2Byte ota_crc;
//extern bool FHSSuseDualBand;

extern bool ICACHE_RAM_ATTR ProcessRFPacket(SX12xxDriverCommon::rx_status const status);
static uint8_t rateIdx2Enum(uint8_t x);

extern void ICACHE_RAM_ATTR GeneratePacketCrcFull(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR GeneratePacketCrcStd(OTA_Packet_s * const otaPktPtr);

bool ICACHE_RAM_ATTR ValidatePacketCrcFull_v3(OTA_Packet_v3_s * otaPktPtr)
{
    uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
    if (otaPktPtr->full.crc == calculatedCRC) {
        return true; 
    }
    return false;
}

bool ICACHE_RAM_ATTR ValidatePacketCrcStd_v3(OTA_Packet_v3_s * otaPktPtr)
{
    uint16_t const inCRC = ((uint16_t)otaPktPtr->std.crcHigh << 8) + otaPktPtr->std.crcLow;
    // For smHybrid the CRC only has the packet type in byte 0
    // For smWide the FHSS slot is added to the CRC in byte 0 on PACKET_TYPE_RCDATAs
#if defined(TARGET_RX)
    if (otaPktPtr->std.type == PACKET_TYPE_RCDATA && OtaSwitchModeCurrent == smWideOr8ch)
    {
        otaPktPtr->std.crcHigh = (OtaNonce % ExpressLRS_currAirRate_Modparams->FHSShopInterval) + 1;
    }
    else
#endif
    {
        otaPktPtr->std.crcHigh = 0;
    }
    uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);

    if (inCRC == calculatedCRC) {
        return true;
    }
    return false;
}

bool ICACHE_RAM_ATTR ProcessRFPacket_v3(SX12xxDriverCommon::rx_status const status)
{
    if (status != SX12xxDriverCommon::SX12XX_RX_OK)
    {
        return false;
    }

    OTA_Packet_v3_s * const otaPktPtr = (OTA_Packet_v3_s * const)Radio.RXdataBuffer;

    // these CRC validation functions will also
    if (OtaIsFullRes) {
        if (!ValidatePacketCrcFull_v3(otaPktPtr)) {
            return false;
        }
        GeneratePacketCrcFull((OTA_Packet_s*)otaPktPtr); // this will correct the CRC with the latest version salt
    }
    else {
        if (!ValidatePacketCrcStd_v3(otaPktPtr)) {
            return false;
        }
        GeneratePacketCrcStd((OTA_Packet_s*)otaPktPtr); // this will correct the CRC with the latest version salt
    }

    OTA_Sync_s otaSync;
    if (otaPktPtr->std.type == PACKET_TYPE_SYNC) {
        if (OtaIsFullRes) {
            memcpy(&otaSync, &otaPktPtr->full.sync.sync, sizeof(OTA_Sync_s));
        }
        else {
            memcpy(&otaSync, &otaPktPtr->std.sync, sizeof(OTA_Sync_s));
        }
        otaSync.rfRateEnum = rateIdx2Enum(OtaIsFullRes ? otaPktPtr->full.sync.sync.rateIndex : otaPktPtr->std.sync.rateIndex);
        #if 0
        if (isDualRadio()) {
            // unknown if packet is Gemini, so use Gemini if available
            otaSync.geminiMode = (config.GetAntennaMode() || FHSSuseDualBand) ? 1 : 0;
        }
        #else
        otaSync.geminiMode = 0; // TODO: I can't test this anyways
        #endif
        otaSync.otaProtocol = 0; // this bit forces change to MAVLink, but the user can also manually configure MAVLink
        if (OtaIsFullRes) {
            memcpy(&otaPktPtr->full.sync.sync, &otaSync, sizeof(OTA_Sync_s));
        }
        else {
            memcpy(&otaPktPtr->std.sync, &otaSync, sizeof(OTA_Sync_s));
        }
    }
    bool ret = ProcessRFPacket(status);
    return ret;
}

static uint8_t ICACHE_RAM_ATTR rateIdx2Enum_2G4(uint8_t x)
{
    switch(x)
    {
        case RATE_v3_LORA_25HZ     : return RATE_LORA_2G4_25HZ;
        case RATE_v3_LORA_50HZ     : return RATE_LORA_2G4_50HZ;
        case RATE_v3_LORA_100HZ    : return RATE_LORA_2G4_100HZ;
        case RATE_v3_LORA_100HZ_8CH: return RATE_LORA_2G4_100HZ_8CH;
        case RATE_v3_LORA_150HZ    : return RATE_LORA_2G4_150HZ;
        case RATE_v3_LORA_200HZ    : return RATE_LORA_2G4_200HZ;
        case RATE_v3_LORA_250HZ    : return RATE_LORA_2G4_250HZ;
        case RATE_v3_LORA_333HZ_8CH: return RATE_LORA_2G4_333HZ_8CH;
        case RATE_v3_LORA_500HZ    : return RATE_LORA_2G4_500HZ;
        case RATE_v3_DVDA_250HZ    : return RATE_FLRC_2G4_250HZ_DVDA;
        case RATE_v3_DVDA_500HZ    : return RATE_FLRC_2G4_500HZ_DVDA;
        case RATE_v3_FLRC_500HZ    : return RATE_FLRC_2G4_500HZ;
        case RATE_v3_FLRC_1000HZ   : return RATE_FLRC_2G4_1000HZ;
        case RATE_v3_LORA_200HZ_8CH: return RATE_LORA_2G4_200HZ_8CH;
        case RATE_v3_FSK_2G4_DVDA_500HZ: return RATE_FSK_2G4_500HZ_DVDA;
        case RATE_v3_FSK_2G4_1000HZ: return RATE_FSK_2G4_1000HZ;
        default: return 0xFF;
    }
}

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)
static uint8_t ICACHE_RAM_ATTR rateIdx2Enum_900(uint8_t x)
{
    switch(x)
    {
        case RATE_v3_LORA_25HZ     : return RATE_LORA_900_25HZ;
        case RATE_v3_LORA_50HZ     : return RATE_LORA_900_50HZ;
        case RATE_v3_LORA_100HZ    : return RATE_LORA_900_100HZ;
        case RATE_v3_LORA_100HZ_8CH: return RATE_LORA_900_100HZ_8CH;
        case RATE_v3_LORA_150HZ    : return RATE_LORA_900_150HZ;
        case RATE_v3_LORA_200HZ    : return RATE_LORA_900_200HZ;
        case RATE_v3_LORA_250HZ    : return RATE_LORA_900_250HZ;
        case RATE_v3_LORA_333HZ_8CH: return RATE_LORA_900_333HZ_8CH;
        case RATE_v3_LORA_500HZ    : return RATE_LORA_900_500HZ;
        case RATE_v3_DVDA_50HZ     : return RATE_LORA_900_50HZ_DVDA;
        case RATE_v3_LORA_200HZ_8CH: return RATE_LORA_900_200HZ_8CH;
        case RATE_v3_FSK_900_1000HZ_8CH: return RATE_FSK_900_1000HZ_8CH;
        default: return 0xFF;
    }
}
#endif

static uint8_t ICACHE_RAM_ATTR rateIdx2Enum(uint8_t x)
{
    #if defined(RADIO_SX127X)
    return rateIdx2Enum_900(x);
    #elif defined(RADIO_LR1121)
    uint8_t ret = rateIdx2Enum_2G4(x);
    if (ret == 0xFF) {
        ret = rateIdx2Enum_900(x);
    }
    return ret;
    #elif defined(RADIO_SX128X)
    return rateIdx2Enum_2G4(x);
    #endif
}
