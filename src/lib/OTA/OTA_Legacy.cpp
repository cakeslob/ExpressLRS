#include "OTA_Legacy.h"
#include "OTA.h"
#include "common.h"
#include "logging.h"
#include "FHSS.h"
#include <stdlib.h>
#include "esp_task_wdt.h"

bool ota_isLegacy = false;
extern uint8_t ExpressLRS_nextAirRateIndex; // used to call SetRFLinkRate
extern uint32_t RFmodeLastCycled; // reset the scan timer

static uint32_t consecutive_old_pkt_cnt = 0;
static uint32_t consecutive_new_pkt_cnt = 0;

extern bool OtaIsFullRes;
extern Crc2Byte ota_crc;

extern bool ICACHE_RAM_ATTR ProcessRFPacket(SX12xxDriverCommon::rx_status const status, bool skip_crc);
extern void SetRFLinkRate(uint8_t index, bool bindMode);
extern void FHSSrandomiseFHSSsequence(const uint32_t seed);
extern void FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, uint32_t freqCount, uint_fast8_t syncChannel, uint8_t *inSequence);
extern void ICACHE_RAM_ATTR GeneratePacketCrcFull(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR GeneratePacketCrcStd(OTA_Packet_s * const otaPktPtr);

static uint8_t rateIdxXform(uint8_t x);
static void FHSSrandomiseFHSSsequence_v3(const uint32_t seed);

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
    uint8_t preserveCrcHigh = otaPktPtr->std.crcHigh;
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
    otaPktPtr->std.crcHigh = preserveCrcHigh;
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
    }
    else {
        if (!ValidatePacketCrcStd_v3(otaPktPtr)) {
            return false;
        }
    }

    esp_task_wdt_reset();

    consecutive_old_pkt_cnt++;
    consecutive_new_pkt_cnt = 0;
    if (consecutive_old_pkt_cnt >= 5) { // got enough packets passing CRC to think we might be hearing a transmitter running older firmware
        if (ota_isLegacy == false) {
            // time to switch over
            DBGLN("many legacy packets detected");
            ota_isLegacy = true;
            // reset the search scan timer
            RFmodeLastCycled = millis();
            // regenerate the hop table so we know what the actual sync channel is
            FHSSrandomiseFHSSsequence_v3(uidMacSeedGet_v3());
            // reinitialize the radio with the new config
            SetRFLinkRate(ExpressLRS_nextAirRateIndex, false);
            Radio.RXnb();
            // now we should be listening for sync on the correct sync channel since the hop table has been changed
            return false;
        }
    }

    OTA_Sync_s otaSync;
    if (otaPktPtr->std.type == PACKET_TYPE_SYNC) {
        if (OtaIsFullRes) {
            memcpy(&otaSync, &otaPktPtr->full.sync.sync, sizeof(OTA_Sync_s));
        }
        else {
            memcpy(&otaSync, &otaPktPtr->std.sync, sizeof(OTA_Sync_s));
        }
        otaSync.rfRateEnum = rateIdxXform(OtaIsFullRes ? otaPktPtr->full.sync.sync.rateIndex : otaPktPtr->std.sync.rateIndex);
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

    esp_task_wdt_reset();

    #if 0
    // this will correct the CRC with the latest version salt
    if (OtaIsFullRes) {
        GeneratePacketCrcFull((OTA_Packet_s*)otaPktPtr);
    }
    else {
        GeneratePacketCrcStd((OTA_Packet_s*)otaPktPtr);
    }
    #endif

    bool ret = ProcessRFPacket(status, true);
    return ret;
}

static expresslrs_RFrates_e ICACHE_RAM_ATTR rateEnumXform_2G4(uint8_t x)
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
        default: return (expresslrs_RFrates_e)0xFF;
    }
}

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)
static expresslrs_RFrates_e ICACHE_RAM_ATTR rateEnumXform_900(uint8_t x)
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
        default: return (expresslrs_RFrates_e)0xFF;
    }
}
#endif

#if defined(RADIO_SX127X)
const static uint8_t rateXformTbl[RATE_MAX] = {
    RATE_v3_LORA_200HZ,     ,
    RATE_v3_LORA_100HZ_8CH, ,
    RATE_v3_LORA_100HZ,     ,
    RATE_v3_LORA_50HZ,      ,
    RATE_v3_LORA_25HZ,      ,
    RATE_v3_DVDA_50HZ,      };

#endif

#if defined(RADIO_LR1121)
const static uint8_t rateXformTbl[] = {
    RATE_v3_LORA_200HZ,         ,
    RATE_v3_LORA_100HZ_8CH,     ,
    RATE_v3_LORA_100HZ,         ,
    RATE_v3_LORA_50HZ,          ,
    RATE_v3_LORA_500HZ,         ,
    RATE_v3_LORA_333HZ_8CH,     ,
    RATE_v3_LORA_250HZ,         ,
    RATE_v3_LORA_150HZ,         ,
    RATE_v3_LORA_100HZ_8CH,     ,
    RATE_v3_LORA_50HZ,          ,
    RATE_v3_LORA_150HZ,         ,
    RATE_v3_LORA_100HZ_8CH,     ,
    RATE_v3_LORA_250HZ,         ,
    RATE_v3_LORA_200HZ_8CH,     ,
    RATE_v3_FSK_2G4_DVDA_500HZ, ,
    RATE_v3_FSK_900_1000HZ_8CH, };
#endif

#if defined(RADIO_SX128X)
const uint8_t rateXformTbl[] = {
    RATE_v3_FLRC_1000HZ,   
    RATE_v3_FLRC_500HZ,    
    RATE_v3_DVDA_500HZ,    
    RATE_v3_DVDA_250HZ,    
    RATE_v3_LORA_500HZ,    
    RATE_v3_LORA_333HZ_8CH,
    RATE_v3_LORA_250HZ,    
    RATE_v3_LORA_150HZ,    
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_50HZ,      };
#endif

static uint8_t ICACHE_RAM_ATTR rateIdxXform(uint8_t x)
{
    #if defined(RADIO_SX127X)
    return rateEnumXform_900(rateXformTbl[x]);
    #elif defined(RADIO_LR1121)
    uint8_t ret = rateEnumXform_2G4(rateXformTbl[x]);
    if (ret == 0xFF) {
        ret = rateEnumXform_900(rateXformTbl[x]);
    }
    return ret;
    #elif defined(RADIO_SX128X)
    return rateEnumXform_2G4(rateXformTbl[x]);
    #endif
}

void ota_cntNewVersionPkts()
{
    consecutive_new_pkt_cnt++;
    consecutive_old_pkt_cnt = 0;
    if (consecutive_new_pkt_cnt >= 5) { // enough packets with correct CRC
        if (ota_isLegacy != false) {
            // switch over to "normal" mode
            ota_isLegacy = false;
            // reset the search scan timer
            RFmodeLastCycled = millis();
            // regenerate the hop table with the correct seed
            FHSSrandomiseFHSSsequence(uidMacSeedGet());
            // reinitialize all radio parameters
            SetRFLinkRate(ExpressLRS_nextAirRateIndex, false);
            Radio.RXnb();
            // now when we hop we will be at the sync channel and actually get a sync packet
            DBGLN("ret to normal pkts");
        }
    }
}

void ota_resetPktVersionCounters()
{
    consecutive_old_pkt_cnt = 0;
    consecutive_new_pkt_cnt = 0;
}

uint32_t uidMacSeedGet_v3()
{
    const uint32_t macSeed = ((uint32_t)UID[2] << 24) + ((uint32_t)UID[3] << 16) +
                             ((uint32_t)UID[4] << 8) + (UID[5]^(OTA_VERSION_ID - 1));
    return macSeed;
}

void FHSSrandomiseFHSSsequence_v3(const uint32_t seed)
{
    sync_channel = (FHSSconfig->freq_count / 2) + 1;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;
    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfig->freq_count, sync_channel, FHSSsequence);

#if defined(RADIO_LR1121)
    sync_channel_DualBand = (FHSSconfigDualBand->freq_count / 2) + 1;
    freq_spread_DualBand = (FHSSconfigDualBand->freq_stop - FHSSconfigDualBand->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfigDualBand->freq_count - 1);
    secondaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfigDualBand->freq_count) * FHSSconfigDualBand->freq_count;
    FHSSusePrimaryFreqBand = false;
    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfigDualBand->freq_count, sync_channel_DualBand, FHSSsequence_DualBand);
    FHSSusePrimaryFreqBand = true;
#endif
}

void debug_sync_packet(void* pkt, int len)
{
    OTA_Packet_v3_s * const otaPktPtr = (OTA_Packet_v3_s * const)pkt;
    if (otaPktPtr->std.type != PACKET_TYPE_SYNC) {
        return;
    }
    OTA_Sync_v3_s * const syncPktPtr = ((len == sizeof(OTA_Packet4_v3_s)) ? &(otaPktPtr->std.sync) : &(otaPktPtr->full.sync.sync));
    if (syncPktPtr->UID4 != UID[4]) {
        return;
    }
    if ((syncPktPtr->UID5 & ~MODELMATCH_MASK) != (UID[5] & ~MODELMATCH_MASK)) {
        return;
    }
    DBG("SYNC ");
    if (len == sizeof(OTA_Packet4_v3_s)) {
        uint8_t crcH = otaPktPtr->std.crcHigh;
        uint16_t const inCRC = ((uint16_t)otaPktPtr->std.crcHigh << 8) + otaPktPtr->std.crcLow;
        otaPktPtr->std.crcHigh = 0;
        DBG("std %x=%x=%x ", inCRC, ota_crc.calc((uint8_t*)pkt, OTA4_CRC_CALC_LEN, OtaCrcInitializer), ota_crc.calc((uint8_t*)pkt, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3));
        otaPktPtr->std.crcHigh = crcH;
    }
    else {
        DBG("full %x=%x=%x ", otaPktPtr->full.crc, ota_crc.calc((uint8_t*)pkt, OTA8_CRC_CALC_LEN, OtaCrcInitializer), ota_crc.calc((uint8_t*)pkt, OTA8_CRC_CALC_LEN_v3, OtaCrcInitializer_v3));
    }
    DBG("uid %x=%x %x=%x ", syncPktPtr->UID4, UID[4], syncPktPtr->UID5, UID[5]);
    DBG("data ");
    for (int i = 0; i < len; i++) {
        DBG("%x ", ((uint8_t*)pkt)[i]);
    }
    DBGCR;
}
