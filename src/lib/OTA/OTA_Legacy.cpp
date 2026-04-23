#include "OTA_Legacy.h"
#include "OTA.h"
#include "common.h"
#include "logging.h"
#include "options.h"
#include "config.h"
#include "CRSFRouter.h"
#include "stubborn_sender.h"
#include "stubborn_receiver.h"
#include "deferred.h"
#include "FHSS.h"
#include "LBT.h"
#include <stdlib.h>
#if defined(PLATFORM_ESP32)
#include "esp_task_wdt.h"
#endif

bool ota_isLegacy = false; // this is a globally accessible indicator that we are in legacy mode
uint32_t ota_legacySyncHoldUntilMs = 0; // used to prevent hopping and rate changes to increase chances of hearing a sync packet

extern uint8_t ExpressLRS_nextAirRateIndex; // used to call SetRFLinkRate
extern uint32_t RFmodeLastCycled; // reset the scan timer
extern bool OtaIsFullRes;
extern StubbornSender DataDlSender;
extern uint8_t NextTelemetryType;
extern uint8_t telemetryBurstCount;
extern uint8_t telemetryBurstMax;
extern uint8_t geminiMode;
extern bool alreadyTLMresp;
extern StubbornReceiver DataUlReceiver;

// used to determine if we are getting legacy packets or new packets
static uint32_t consecutive_old_pkt_cnt = 0;
static uint32_t consecutive_new_pkt_cnt = 0;

extern Crc2Byte ota_crc;
uint16_t OtaCrcInitializer_v3;

bool ICACHE_RAM_ATTR ValidatePacketCrcFull_v3(OTA_Packet_v3_s * otaPktPtr);
bool ICACHE_RAM_ATTR ValidatePacketCrcStd_v3(OTA_Packet_v3_s * otaPktPtr);
void ICACHE_RAM_ATTR GeneratePacketCrcFull_v3(OTA_Packet_v3_s * const otaPktPtr);
void ICACHE_RAM_ATTR GeneratePacketCrcStd_v3(OTA_Packet_v3_s * const otaPktPtr);

extern bool ICACHE_RAM_ATTR ProcessRFPacket(SX12xxDriverCommon::rx_status const status, bool skip_crc);
extern void SetRFLinkRate(uint8_t index, bool bindMode);
extern void FHSSrandomiseFHSSsequence(const uint32_t seed);
extern void FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, uint32_t freqCount, uint_fast8_t syncChannel, uint8_t *inSequence);
extern void ICACHE_RAM_ATTR GeneratePacketCrcFull(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR GeneratePacketCrcStd(OTA_Packet_s * const otaPktPtr);
extern void ICACHE_RAM_ATTR LinkStatsToOta(OTA_LinkStats_s * const ls);

static uint8_t rateIdxXform(uint8_t x);
static void FHSSrandomiseFHSSsequence_v3(const uint32_t seed);
static bool mapSyncPacketV3ToV4(OTA_Packet_v3_s * const otaPktPtr);
static void rewriteDataUplinkHeaderV3ToV4(OTA_Packet_v3_s * const otaPktPtr);
static void LinkStatsToOta_v3(OTA_LinkStats_v3_s * const ls);

static constexpr uint32_t LEGACY_SYNC_HOLD_TIMEOUT_MS = 2000U;

bool ota_isLegacySyncHoldActive()
{
    if (ota_legacySyncHoldUntilMs == 0)
    {
        return false;
    }

    if ((millis() - ota_legacySyncHoldUntilMs) < LEGACY_SYNC_HOLD_TIMEOUT_MS)
    {
        return true;
    }

    ota_legacySyncHoldUntilMs = 0;
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
            DBGLN("CRC err v3 full");
            return false;
        }
    }
    else {
        if (!ValidatePacketCrcStd_v3(otaPktPtr)) {
            DBGLN("CRC err v3 std");
            return false;
        }
    }

    //DBGLN("CRC v3 good");

    #if defined(PLATFORM_ESP32)
    // this code actually sometimes crashes due to WDT if debug is enabled
    esp_task_wdt_reset();
    #endif

    consecutive_old_pkt_cnt++;
    consecutive_new_pkt_cnt = 0;
    if (consecutive_old_pkt_cnt >= 3U) { // got enough evidence to conclude we are hearing a V3 transmitter
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
            // Enter a temporary hold state: stay on sync channel to lock quickly before hopping starts.
            ota_legacySyncHoldUntilMs = millis();
            Radio.SetFrequencyReg(FHSSgetInitialFreq(), SX12XX_Radio_1, false);
#if defined(RADIO_LR1121)
            if (FHSSuseDualBand)
            {
                Radio.SetFrequencyReg(FHSSgetInitialGeminiFreq(), SX12XX_Radio_2, false);
            }
#endif
            Radio.RXnb();
            // now we should be listening for sync on the correct sync channel since the hop table has been changed
            return false;
        }
    }

    if (otaPktPtr->std.type == PACKET_TYPE_SYNC) {
        // Translate the sync packet explicitly so field packing changes between V3/V4
        // cannot corrupt switch mode / tlm ratio information.
        if (!mapSyncPacketV3ToV4(otaPktPtr))
        {
            return false;
        }

        // Sync found, leave the temporary hold state and allow normal hopping/scanning behaviour.
        if (ota_legacySyncHoldUntilMs != 0)
        {
            DBGLN("legacy sync acquired, resume normal hopping");
            ota_legacySyncHoldUntilMs = 0;
        }
    }
    else if (otaPktPtr->std.type == PACKET_TYPE_DATA)
    {
        // PACKET_TYPE_DATA on V3 is MSP uplink. Standard-mode bit layout is compatible,
        // but full-resolution bitfield order is different and must be translated.
        rewriteDataUplinkHeaderV3ToV4(otaPktPtr);
    }

    #if defined(PLATFORM_ESP32)
    // this code actually sometimes crashes due to WDT if debug is enabled
    esp_task_wdt_reset();
    #endif

    #if 0
    // this will correct the CRC with the latest version salt
    // this is inefficient, it's better to just skip CRC check in the later processing
    if (OtaIsFullRes) {
        GeneratePacketCrcFull((OTA_Packet_s*)otaPktPtr);
    }
    else {
        GeneratePacketCrcStd((OTA_Packet_s*)otaPktPtr);
    }
    #endif

    bool ret = ProcessRFPacket(status, true); // use original packet processing, we've already checked the CRC so skip checking CRC again
    return ret;
}

bool ICACHE_RAM_ATTR HandleSendTelemetryResponse_v3()
{
    #ifdef TARGET_RX
    // this function tries to mimic the old telemetry sending function, and in v4 it is replaced with HandleSendDataDl
    // this functionality doesn't work yet, TODO: fix me

    uint8_t modresult = OtaNonce % ExpressLRS_currTlmDenom;

    if ((connectionState == disconnected) || (ExpressLRS_currTlmDenom == 1) || (alreadyTLMresp == true) || (modresult != 0) || !teamraceHasModelMatch)
    {
        return false; // don't bother sending tlm if disconnected or TLM is off
    }

    // ESP requires word aligned buffer
    WORD_ALIGNED_ATTR OTA_Packet_v3_s otaPkt = {0};
    alreadyTLMresp = true;
    otaPkt.std.type = 0x03; // old PACKET_TYPE_TLM definition

    bool tlmQueued = false;
    //if (firmwareOptions.is_airport)
    //{
    //    tlmQueued = ((SerialAirPort *)serialIO)->isTlmQueued();
    //}
    //else
    {
        tlmQueued = DataDlSender.IsActive();
    }

    if (NextTelemetryType == PACKET_TYPE_LINKSTATS || !tlmQueued)
    {
        OTA_LinkStats_v3_s * ls;
        if (OtaIsFullRes)
        {
            otaPkt.full.tlm_dl.containsLinkStats = 1;
            ls = &otaPkt.full.tlm_dl.ul_link_stats.stats;
            // Include some advanced telemetry in the extra space
            // Note the use of `ul_link_stats.payload` vs just `payload`
            otaPkt.full.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                otaPkt.full.tlm_dl.ul_link_stats.payload,
                sizeof(otaPkt.full.tlm_dl.ul_link_stats.payload));
        }
        else
        {
            otaPkt.std.tlm_dl.type = 0x01; // ELRS_TELEMETRY_TYPE_LINK
            ls = &otaPkt.std.tlm_dl.ul_link_stats.stats;
        }
        LinkStatsToOta_v3(ls);

        NextTelemetryType = PACKET_TYPE_DATA; // ELRS_TELEMETRY_TYPE_DATA
        // Start the count at 1 because the next will be DATA and doing +1 before checking
        // against Max below is for some reason 10 bytes more code
        telemetryBurstCount = 1;
    }
    else
    {
        if (telemetryBurstCount < telemetryBurstMax)
        {
            telemetryBurstCount++;
        }
        else
        {
            NextTelemetryType = PACKET_TYPE_LINKSTATS; // ELRS_TELEMETRY_TYPE_LINK;
        }

        if (DataDlSender.IsActive())
        {
            if (OtaIsFullRes)
            {
                otaPkt.full.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                    otaPkt.full.tlm_dl.payload,
                    sizeof(otaPkt.full.tlm_dl.payload));
            }
            else
            {
                otaPkt.std.tlm_dl.type = 0x02; // ELRS_TELEMETRY_TYPE_DATA
                otaPkt.std.tlm_dl.packageIndex = DataDlSender.GetCurrentPayload(
                    otaPkt.std.tlm_dl.payload,
                    sizeof(otaPkt.std.tlm_dl.payload));
            }
        }
    }

    if (OtaIsFullRes)
    {
        GeneratePacketCrcFull_v3(&otaPkt);
    }
    else
    {
        GeneratePacketCrcStd_v3(&otaPkt);
    }

    SX12XX_Radio_Number_t transmittingRadio;
    if (config.GetForceTlmOff())
    {
        transmittingRadio = SX12XX_Radio_NONE;
    }
    else
    {
        transmittingRadio = LbtChannelIsClear(SX12XX_Radio_All);   // weed out the radio(s) if channel in use
        if (isDualRadio() && !geminiMode && transmittingRadio == SX12XX_Radio_All) // If the receiver is in diversity mode, only send TLM on a single radio.
        {
            transmittingRadio = Radio.GetStrongestReceivingRadio(); // Pick the radio with best rf connection to the tx.
        }
    }

    Radio.TXnb((uint8_t*)&otaPkt, false, nullptr, transmittingRadio);

    if (transmittingRadio == SX12XX_Radio_NONE)
    {
        // No packet will be sent due to LBT / Telem forced off.
        // Defer TXdoneCallback() to prepare for TLM when the IRQ is normally triggered.
        deferExecutionMicros(ExpressLRS_currAirRate_RFperfParams->TOA, Radio.TXdoneCallback);
    }

    #endif

    return true;
}

void OtaUpdateCrcInitFromUid_v3()
{
    OtaCrcInitializer_v3 = (UID[4] << 8) | UID[5];
    OtaCrcInitializer_v3 ^= 3;
    DBGV("OTAv3 %u %u %u", UID[0], UID[1], UID[2]);
    DBGVLN(" %u %u %u %u", UID[3], UID[4], UID[5], OtaCrcInitializer_v3);
}

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
    bool crcValid = false;

    // For smHybrid the CRC only has packet type in byte 0.
    // For smWide standard RC packets, the FHSS slot is mixed into byte 0 before CRC.
    // During initial acquisition (before sync), OtaNonce is unknown and using only one
    // slot value causes many false negatives. To improve reliability, try all possible
    // slot values for this mode.
#if defined(TARGET_RX)
    if (otaPktPtr->std.type == PACKET_TYPE_RCDATA && OtaSwitchModeCurrent == smWideOr8ch)
    {
        uint8_t const fhssHopInterval = ExpressLRS_currAirRate_Modparams->FHSShopInterval;
        if (fhssHopInterval > 0)
        {
            // First try the expected value from current OtaNonce (fast path when already in sync).
            otaPktPtr->std.crcHigh = (OtaNonce % fhssHopInterval) + 1;
            uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
            crcValid = (inCRC == calculatedCRC);

            // If not matched, brute-force the slot contribution 1..FHSShopInterval.
            if (!crcValid)
            {
                for (uint8_t slot = 1; slot <= fhssHopInterval; ++slot)
                {
                    otaPktPtr->std.crcHigh = slot;
                    uint16_t const slotCrc = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
                    if (inCRC == slotCrc)
                    {
                        crcValid = true;
                        DBGVLN("legacy std CRC matched via slot search: slot=%u", slot);
                        break;
                    }
                }
            }
        }
    }
    else
#endif
    {
        otaPktPtr->std.crcHigh = 0;
        uint16_t const calculatedCRC = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
        crcValid = (inCRC == calculatedCRC);
    }

    otaPktPtr->std.crcHigh = preserveCrcHigh;
    return crcValid;
}

void ICACHE_RAM_ATTR GeneratePacketCrcFull_v3(OTA_Packet_v3_s * const otaPktPtr)
{
    otaPktPtr->full.crc = ota_crc.calc((uint8_t*)otaPktPtr, OTA8_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
}

void ICACHE_RAM_ATTR GeneratePacketCrcStd_v3(OTA_Packet_v3_s * const otaPktPtr)
{
    uint16_t crc = ota_crc.calc((uint8_t*)otaPktPtr, OTA4_CRC_CALC_LEN_v3, OtaCrcInitializer_v3);
    otaPktPtr->std.crcHigh = (crc >> 8);
    otaPktPtr->std.crcLow  = crc;
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
    RATE_v3_LORA_200HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_100HZ,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_25HZ,
    RATE_v3_DVDA_50HZ,
};

#endif

#if defined(RADIO_LR1121)
const static uint8_t rateXformTbl[] = {
    RATE_v3_LORA_200HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_100HZ,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_500HZ,
    RATE_v3_LORA_333HZ_8CH,
    RATE_v3_LORA_250HZ,
    RATE_v3_LORA_150HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_50HZ,
    RATE_v3_LORA_150HZ,
    RATE_v3_LORA_100HZ_8CH,
    RATE_v3_LORA_250HZ,
    RATE_v3_LORA_200HZ_8CH,
    RATE_v3_FSK_2G4_DVDA_500HZ,
    RATE_v3_FSK_900_1000HZ_8CH,
};
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
    RATE_v3_LORA_50HZ,
};
#endif

static uint8_t ICACHE_RAM_ATTR rateIdxXform(uint8_t x)
{
    // Legacy rateIndex is translated through a target-specific lookup table first,
    // then converted to the active V4 enum.
    if (x >= (sizeof(rateXformTbl) / sizeof(rateXformTbl[0])))
    {
        DBGLN("legacy sync rate index out of bounds: %u", x);
        return 0xFF;
    }

    uint8_t const rateV3 = rateXformTbl[x];

    #if defined(RADIO_SX127X)
    return rateEnumXform_900(rateV3);
    #elif defined(RADIO_LR1121)
    uint8_t ret = rateEnumXform_2G4(rateV3);
    if (ret == 0xFF) {
        ret = rateEnumXform_900(rateV3);
    }
    return ret;
    #elif defined(RADIO_SX128X)
    return rateEnumXform_2G4(rateV3);
    #endif
}

static bool ICACHE_RAM_ATTR mapSyncPacketV3ToV4(OTA_Packet_v3_s * const otaPktPtr)
{
    OTA_Sync_v3_s const * const syncV3 = OtaIsFullRes ? &otaPktPtr->full.sync.sync : &otaPktPtr->std.sync;
    OTA_Sync_s syncV4 = {};
    uint8_t const transformedRateEnum = rateIdxXform(syncV3->rateIndex);
    if (transformedRateEnum == 0xFF)
    {
        DBGLN("legacy sync rate unsupported: %u", syncV3->rateIndex);
        return false;
    }
    DBGLN("legacy sync rate map v3=%u v4=%u tlm=%u sw=%u", syncV3->rateIndex, transformedRateEnum, syncV3->newTlmRatio, syncV3->switchEncMode);

    syncV4.fhssIndex = syncV3->fhssIndex;
    syncV4.nonce = syncV3->nonce;
    syncV4.rfRateEnum = transformedRateEnum;
    syncV4.switchEncMode = syncV3->switchEncMode;
    syncV4.newTlmRatio = syncV3->newTlmRatio;
    syncV4.geminiMode = 0; // V3 packet has no Gemini flag.
    syncV4.otaProtocol = 0; // Keep serial mode controlled by receiver configuration.
    syncV4.free = 0;
    syncV4.UID4 = syncV3->UID4;
    syncV4.UID5 = syncV3->UID5;

    // Write the converted sync structure into the same RX buffer, but through the V4 packet view.
    // ProcessRFPacket() is called later and interprets this memory as OTA_Packet_s.
    OTA_Packet_s * const otaPktV4Ptr = (OTA_Packet_s * const)otaPktPtr;
    if (OtaIsFullRes)
    {
        otaPktV4Ptr->full.sync.sync = syncV4;
    }
    else
    {
        otaPktV4Ptr->std.sync = syncV4;
    }
    return true;
}

static void ICACHE_RAM_ATTR rewriteDataUplinkHeaderV3ToV4(OTA_Packet_v3_s * const otaPktPtr)
{
    if (!OtaIsFullRes)
    {
        // Standard packet mode uses compatible bit placement:
        // V3 msp_ul { packageIndex:7, tlmFlag:1 } and V4 data_ul { packageIndex:7, stubbornAck:1 }.
        return;
    }

    // Full-res V3 uplink data layout:
    //   packetType:2, packageIndex:5, tlmFlag:1
    // Full-res V4 uplink data layout:
    //   packetType:2, stubbornAck:1, packageIndex:5
    // Repack byte 0 so ProcessRFPacket() sees correct V4 field positions.
    uint8_t const packetType = otaPktPtr->full.msp_ul.packetType;
    uint8_t const packageIndex = otaPktPtr->full.msp_ul.packageIndex;
    uint8_t const stubbornAck = otaPktPtr->full.msp_ul.tlmFlag;

    // Re-interpret as V4 packet view and rewrite only the first byte fields needed by V4 parser.
    OTA_Packet_s * const otaPktV4Ptr = (OTA_Packet_s * const)otaPktPtr;
    otaPktV4Ptr->full.data_ul.packetType = packetType;
    otaPktV4Ptr->full.data_ul.stubbornAck = stubbornAck;
    otaPktV4Ptr->full.data_ul.packageIndex = packageIndex;

    DBGVLN("legacy full uplink hdr remap pidx=%u ack=%u", packageIndex, stubbornAck);
}

static void ICACHE_RAM_ATTR LinkStatsToOta_v3(OTA_LinkStats_v3_s * const ls)
{
    ls->uplink_RSSI_1 = linkStats.uplink_RSSI_1;
    ls->uplink_RSSI_2 = linkStats.uplink_RSSI_2;
    ls->antenna = linkStats.active_antenna;
    ls->modelMatch = connectionHasModelMatch;
    ls->lq = linkStats.uplink_Link_quality;
    ls->mspConfirm = DataUlReceiver.GetCurrentConfirm() ? 1 : 0;
    ls->SNR = linkStats.uplink_SNR;
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

#ifdef TARGET_TX

static void ICACHE_RAM_ATTR PackChannelDataHybridCommon_v3(OTA_Packet4_v3_s * const ota4, const uint32_t *channelData)
{
    ota4->type = PACKET_TYPE_RCDATA;
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota4->dbg_linkstats.packetNum = packetCnt++;
#else
    // CRSF input is 11bit and OTA will carry only 10bit. Discard the Extended Limits (E.Limits)
    // range and use the full 10bits to carry only 998us - 2012us
    PackUInt11ToChannels4x10(&channelData[0], &ota4->rc.ch, &Decimate11to10_Limit);
    ota4->rc.ch4 = CRSF_to_BIT(channelData[4]);
#endif /* !DEBUG_RCVR_LINKSTATS */
}

void ICACHE_RAM_ATTR GenerateChannelData8ch12ch_v3(OTA_Packe_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, bool isHighAux)
{
    // All channel data is 10 bit apart from AUX1 which is 1 bit
    ota8->rc.packetType = PACKET_TYPE_RCDATA;
    ota8->rc.telemetryStatus = TelemetryStatus;
    // uplinkPower has 8 items but only 3 bits, but 0 is 0 power which we never use, shift 1-8 -> 0-7
    ota8->rc.uplinkPower = constrain(CRSF::LinkStatistics.uplink_TX_Power, 1, 8) - 1;
    ota8->rc.isHighAux = isHighAux;
    ota8->rc.ch4 = CRSF_to_BIT(channelData[4]);
#if defined(DEBUG_RCVR_LINKSTATS)
    // Incremental packet counter for verification on the RX side, 32 bits shoved into CH1-CH4
    ota8->dbg_linkstats.packetNum = packetCnt++;
#else
    // Sources:
    // 8ch always: low=0 high=5
    // 12ch isHighAux=false: low=0 high=5
    // 12ch isHighAux=true:  low=0 high=9
    // 16ch isHighAux=false: low=0 high=4
    // 16ch isHighAux=true:  low=8 high=12
    uint8_t chSrcLow;
    uint8_t chSrcHigh;
    if (OtaSwitchModeCurrent == smHybridOr16ch)
    {
        // 16ch mode
        if (isHighAux)
        {
            chSrcLow = 8;
            chSrcHigh = 12;
        }
        else
        {
            chSrcLow = 0;
            chSrcHigh = 4;
        }
    }
    else
    {
        chSrcLow = 0;
        chSrcHigh = isHighAux ? 9 : 5;
    }
    PackUInt11ToChannels4x10(&channelData[chSrcLow], &ota8->rc.chLow, &Decimate11to10_Div2);
    PackUInt11ToChannels4x10(&channelData[chSrcHigh], &ota8->rc.chHigh, &Decimate11to10_Div2);
#endif
}

void ICACHE_RAM_ATTR GenerateChannelDataHybridWide_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    OTA_Packet4_v3_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon_v3(ota4, channelData);

    uint8_t telemBit = TelemetryStatus << 6;
    uint8_t nextSwitchIndex = HybridWideNonceToSwitchIndex(OtaNonce);
    uint8_t value;
    // Using index 7 means the telemetry bit will always be sent in the packet
    // preceding the RX's telemetry slot for all tlmDenom >= 8
    // For more frequent telemetry rates, include the bit in every
    // packet and degrade the value to 6-bit
    // (technically we could squeeze 7-bits in for 2 channels with tlmDenom=4)
    if (nextSwitchIndex == 7)
    {
        value = telemBit | CRSF::LinkStatistics.uplink_TX_Power;
    }
    else
    {
        bool telemInEveryPacket = (tlmDenom > 1) && (tlmDenom < 8);
        value = HybridWideSwitchToOta(channelData, nextSwitchIndex + 1, telemInEveryPacket);
        if (telemInEveryPacket)
            value |= telemBit;
    }

    ota4->rc.switches = value;
}

void ICACHE_RAM_ATTR GenerateChannelDataHybrid8_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    (void)tlmDenom;

    OTA_Packet4_v3_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon_v3(ota4, channelData);

    // Actually send switchIndex - 1 in the packet, to shift down 1-7 (0b111) to 0-6 (0b110)
    // If the two high bits are 0b11, the receiver knows it is the last switch and can use
    // that bit to store data
    uint8_t bitclearedSwitchIndex = Hybrid8NextSwitchIndex;
    uint8_t value;
    // AUX8 is High Resolution 16-pos (4-bit)
    if (bitclearedSwitchIndex == 6)
        value = CRSF_to_N(channelData[6 + 1 + 4], 16);
    else
        value = CRSF_to_SWITCH3b(channelData[bitclearedSwitchIndex + 1 + 4]);

    ota4->rc.switches =
        TelemetryStatus << 6 |
        // tell the receiver which switch index this is
        bitclearedSwitchIndex << 3 |
        // include the switch value
        value;

    // update the sent value
    Hybrid8NextSwitchIndex = (bitclearedSwitchIndex + 1) % 7;
}

void ICACHE_RAM_ATTR OtaPackChannelData_v3(OTA_Packet_v3_s * const otaPktPtr, const uint32_t *channelData, bool const TelemetryStatus, uint8_t const tlmDenom)
{
    if (OtaIsFullRes)
    {
        bool isHighAux = false;
        if (OtaSwitchModeCurrent != smWideOr8ch) {
            isHighAux = FullResIsHighAux;
        }
        GenerateChannelData8ch12ch_v3(otaPktPtr, channelData, TelemetryStatus, isHighAux);
        if (OtaSwitchModeCurrent != smWideOr8ch) {
            FullResIsHighAux = !FullResIsHighAux;
        }
    }
    else
    {
        if (OtaSwitchModeCurrent == smWideOr8ch)
        {
            GenerateChannelDataHybridWide_v3(otaPktPtr, channelData, TelemetryStatus, tlmDenom);
        }
        else
        {
            GenerateChannelDataHybrid8_v3(otaPktPtr, channelData, TelemetryStatus, tlmDenom);
        }
    }
}

void SetRFLinkRate_v3(uint8_t index)
{
  expresslrs_mod_settings_s *const ModParams = get_elrs_airRateConfig(index);
  expresslrs_rf_pref_params_s *const RFperf = get_elrs_RFperfParams(index);
  // Binding always uses invertIQ
  bool invertIQ = InBindingMode || (UID[5] & 0x01);
  OtaSwitchMode_e newSwitchMode = (OtaSwitchMode_e)config.GetSwitchMode();

  bool subGHz = FHSSconfig->freq_center < 1000000000;
#if defined(RADIO_LR1121)
  if (FHSSuseDualBand && subGHz)
  {
      subGHz = FHSSconfigDualBand->freq_center < 1000000000;
  }
#endif

  if ((ModParams == ExpressLRS_currAirRate_Modparams)
    && (RFperf == ExpressLRS_currAirRate_RFperfParams)
    && (subGHz || invertIQ == Radio.IQinverted)
    && (OtaSwitchModeCurrent == newSwitchMode)
    && (!InBindingMode))  // binding mode must always execute code below to set frequency
    return;

  DBGLN("set rate %u", index);
  uint32_t interval = ModParams->interval;
#if defined(DEBUG_FREQ_CORRECTION) && defined(RADIO_SX128X)
  interval = interval * 12 / 10; // increase the packet interval by 20% to allow adding packet header
#endif
  hwTimer::updateInterval(interval);

  FHSSusePrimaryFreqBand = !(ModParams->radio_type == RADIO_TYPE_LR1121_LORA_2G4) && !(ModParams->radio_type == RADIO_TYPE_LR1121_GFSK_2G4);
  FHSSuseDualBand = ModParams->radio_type == RADIO_TYPE_LR1121_LORA_DUAL;

  Radio.Config(ModParams->bw, ModParams->sf, ModParams->cr, FHSSgetInitialFreq(),
               ModParams->PreambleLen, invertIQ, ModParams->PayloadLength
#if defined(RADIO_SX128X)
               , uidMacSeedGet_v3(), OtaCrcInitializer_v3, (ModParams->radio_type == RADIO_TYPE_SX128x_FLRC)
#endif
#if defined(RADIO_LR1121)
               , (ModParams->radio_type == RADIO_TYPE_LR1121_GFSK_900 || ModParams->radio_type == RADIO_TYPE_LR1121_GFSK_2G4), (uint8_t)UID[5], (uint8_t)UID[4]
#endif
               );

#if defined(RADIO_LR1121)
  if (FHSSuseDualBand)
  {
    Radio.Config(ModParams->bw2, ModParams->sf2, ModParams->cr2, FHSSgetInitialGeminiFreq(),
                ModParams->PreambleLen2, invertIQ, ModParams->PayloadLength,
                (ModParams->radio_type == RADIO_TYPE_LR1121_GFSK_900 || ModParams->radio_type == RADIO_TYPE_LR1121_GFSK_2G4),
                (uint8_t)UID[5], (uint8_t)UID[4], SX12XX_Radio_2);
  }
#endif

  Radio.FuzzySNRThreshold = (RFperf->DynpowerSnrThreshUp == DYNPOWER_SNR_THRESH_NONE) ? 0 : (RFperf->DynpowerSnrThreshUp - RFperf->DynpowerSnrThreshDn);

  if ((isDualRadio() && config.GetAntennaMode() == TX_RADIO_MODE_GEMINI) || FHSSuseDualBand) // Gemini mode
  {
    Radio.SetFrequencyReg(FHSSgetInitialGeminiFreq(), SX12XX_Radio_2, false);
  }

  // InitialFreq has been set, so lets also reset the FHSS Idx and Nonce.
  FHSSsetCurrIndex(0);
  OtaNonce = 0;

  // OtaUpdateSerializers(newSwitchMode, ModParams->PayloadLength); // not called, we pick the serializers via if statements later instead of using pointers
  OtaSwitchModeCurrent = newSwitchMode
  OtaIsFullRes = (ModParams->PayloadLength == OTA8_PACKET_SIZE);
  if (OtaIsFullRes) {
    ota_crc.init(16, ELRS_CRC16_POLY);
  }
  else {
    ota_crc.init(14, ELRS_CRC14_POLY);
  }

  DataUlSender.setMaxPackageIndex(ELRS_MSP_MAX_PACKAGES);
  DataDlReceiver.setMaxPackageIndex(OtaIsFullRes ? ELRS8_DATA_DL_MAX_PACKAGES : ELRS4_DATA_DL_MAX_PACKAGES);

  ExpressLRS_currAirRate_Modparams = ModParams;
  ExpressLRS_currAirRate_RFperfParams = RFperf;
  CRSF::LinkStatistics.rf_Mode = ModParams->enum_rate;

  handset->setPacketInterval(interval * ExpressLRS_currAirRate_Modparams->numOfSends);
  connectionState = disconnected;
  rfModeLastChangedMS = millis();
}

bool ICACHE_RAM_ATTR ProcessDownlinkPacket_v3(SX12xxDriverCommon::rx_status const status)
{
  // NOTE: in v3, this is called ProcessTMLpacket
  OTA_Packet_v3_s * const otaPktPtr = (OTA_Packetv3_s * const)Radio.RXdataBuffer;
  if ((OtaIsFullRes && !ValidatePacketCrcFull_v3(otaPktPtr)) || (!OtaIsFullRes && !ValidatePacketCrcStd_v3(otaPktPtr)))
  {
    DBGLN("TLM v3 crc error");
    return false;
  }

  if (otaPktPtr->std.type != PACKET_TYPE_TLM)
  {
    DBGLN("TLM type error %d", otaPktPtr->std.type);
    return false;
  }

  LastTLMpacketRecvMillis = millis();
  LQCalc.add();

  Radio.GetLastPacketStats();
  CRSF::LinkStatistics.downlink_SNR = SNR_DESCALE(Radio.LastPacketSNRRaw);
  CRSF::LinkStatistics.downlink_RSSI_1 = Radio.LastPacketRSSI;
  CRSF::LinkStatistics.downlink_RSSI_2 = Radio.LastPacketRSSI2;

  // Full res mode
  if (OtaIsFullRes)
  {
    OTA_Packet8_v3_s * const ota8_v3 = (OTA_Packet8_v3_s * const)otaPktPtr;
    OTA_Packet8_s ota8_mem;
    OTA_Packet8_s* ota8 = (OTA_Packet8_s*)&ota8_mem;

    // TODO: carefully translate the data of `ota8_v3` into `ota8` so that the new LinkStatsFromOta can work with it, see if any of the code already existing in this file is useful

    uint8_t *telemPtr;
    uint8_t dataLen;
    if (ota8->tlm_dl.containsLinkStats)
    {
      LinkStatsFromOta(&ota8->tlm_dl.ul_link_stats.stats);
      telemPtr = ota8->tlm_dl.ul_link_stats.payload;
      dataLen = sizeof(ota8->tlm_dl.ul_link_stats.payload);
    }
    else
    {
      if (firmwareOptions.is_airport)
      {
        // OtaUnpackAirportData(otaPktPtr, &apOutputBuffer); // DO NOT ATTEMPT, backward compatibility with Airport mode is not going to be implemented
        return true;
      }
      telemPtr = ota8->tlm_dl.payload;
      dataLen = sizeof(ota8->tlm_dl.payload);
    }
    //DBGLN("pi=%u len=%u", ota8->tlm_dl.packageIndex, dataLen);
    // TelemetryReceiver.ReceiveData(ota8->tlm_dl.packageIndex & ELRS8_TELEMETRY_MAX_PACKAGES, telemPtr, dataLen);
    // TODO: TelemetryReceiver doesn't exist in v4 and is replaced with DataDlReceiver, so use DataDlReceiver appropriately
  }
  // Std res mode
  else
  {
    switch (otaPktPtr->std.tlm_dl.type)
    {
      case ELRS_TELEMETRY_TYPE_LINK:
        LinkStatsFromOta(&otaPktPtr->std.tlm_dl.ul_link_stats.stats);
        break;

      case ELRS_TELEMETRY_TYPE_DATA:
        if (firmwareOptions.is_airport)
        {
          // OtaUnpackAirportData(otaPktPtr, &apOutputBuffer); // DO NOT ATTEMPT, backward compatibility with Airport mode is not going to be implemented
          return true;
        }
        //TelemetryReceiver.ReceiveData(otaPktPtr->std.tlm_dl.packageIndex & ELRS4_TELEMETRY_MAX_PACKAGES,
        //  otaPktPtr->std.tlm_dl.payload,
        //  sizeof(otaPktPtr->std.tlm_dl.payload));
        // TODO: TelemetryReceiver doesn't exist in v4 and is replaced with DataDlReceiver, so use DataDlReceiver appropriately
        break;
    }
  }

  return true;
}

void ICACHE_RAM_ATTR SendRCdataToRF_v3()
{
  // Do not send a stale channels packet to the RX if one has not been received from the handset
  // *Do* send data if a packet has never been received from handset and the timer is running
  // this is the case when bench testing and TXing without a handset
  bool dontSendChannelData = false;
  uint32_t lastRcData = handset->GetRCdataLastRecv();
  if (lastRcData && (micros() - lastRcData > 1000000))
  {
    // The tx is in Mavlink mode and without a valid crsf or RC input.  Do not send stale or fake zero packet RC!
    // Only send sync and MSP packets.
    if (config.GetLinkMode() == TX_MAVLINK_MODE)
    {
      dontSendChannelData = true;
    }
    else
    {
      return;
    }
  }

  busyTransmitting = true;

  uint32_t const now = millis();
  // ESP requires word aligned buffer
  WORD_ALIGNED_ATTR OTA_Packet_v3_s otaPkt = {0};
  static uint8_t syncSlot;

  const bool isTlmDisarmed = config.GetTlm() == TLM_RATIO_DISARMED;
  uint32_t SyncInterval = (connectionState == connected && !isTlmDisarmed) ? ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalConnected : ExpressLRS_currAirRate_RFperfParams->SyncPktIntervalDisconnected;
  bool skipSync = InBindingMode ||
    // TLM_RATIO_DISARMED keeps sending sync packets even when armed until the RX stops sending telemetry and the TLM=Off has taken effect
    (isTlmDisarmed && handset->IsArmed() && (ExpressLRS_currTlmDenom == 1));

  uint8_t NonceFHSSresult = OtaNonce % ExpressLRS_currAirRate_Modparams->FHSShopInterval;

  // Sync spam only happens on slot 1 and 2 and can't be disabled
  if ((syncSpamCounter || (syncSpamCounterAfterRateChange && FHSSonSyncChannel())) && (NonceFHSSresult == 1 || NonceFHSSresult == 2))
  {
    otaPkt.std.type = PACKET_TYPE_SYNC;
    GenerateSyncPacketData(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
    syncSlot = 0; // reset the sync slot in case the new rate (after the syncspam) has a lower FHSShopInterval
  }
  // Regular sync rotates through 4x slots, twice on each slot, and telemetry pushes it to the next slot up
  // But only on the sync FHSS channel and with a timed delay between them
  else if ((!skipSync) && ((syncSlot / 2) <= NonceFHSSresult) && (now - SyncPacketLastSent > SyncInterval) && FHSSonSyncChannel())
  {
    otaPkt.std.type = PACKET_TYPE_SYNC;
    GenerateSyncPacketData(OtaIsFullRes ? &otaPkt.full.sync.sync : &otaPkt.std.sync);
    syncSlot = (syncSlot + 1) % (ExpressLRS_currAirRate_Modparams->FHSShopInterval * 2);
  }
  else
  {
    if (firmwareOptions.is_airport)
    {
      // OtaPackAirportData(&otaPkt, &apInputBuffer); // DO NOT ATTEMPT, backward compatibility with Airport mode is not going to be implemented
    }
    else if ((NextPacketIsMspData && MspSender.IsActive()) || dontSendChannelData)
    {
      otaPkt.std.type = PACKET_TYPE_MSPDATA;
      if (OtaIsFullRes)
      {
        otaPkt.full.msp_ul.packageIndex = MspSender.GetCurrentPayload(
          otaPkt.full.msp_ul.payload,
          sizeof(otaPkt.full.msp_ul.payload));
        //if (config.GetLinkMode() == TX_MAVLINK_MODE)
        //  otaPkt.full.msp_ul.tlmFlag = TelemetryReceiver.GetCurrentConfirm();
        // It can't be in MAVLINK mode because the Legacy mode is mutually exclusive
      }
      else
      {
        otaPkt.std.msp_ul.packageIndex = MspSender.GetCurrentPayload(
          otaPkt.std.msp_ul.payload,
          sizeof(otaPkt.std.msp_ul.payload));
        //if (config.GetLinkMode() == TX_MAVLINK_MODE)
        //  otaPkt.std.msp_ul.tlmFlag = TelemetryReceiver.GetCurrentConfirm();
        // It can't be in MAVLINK mode because the Legacy mode is mutually exclusive
      }

      // send channel data next so the channel messages also get sent during msp transmissions
      NextPacketIsMspData = false;
      // counter can be increased even for normal msp messages since it's reset if a real bind message should be sent
      BindingSendCount++;
      // If not in TlmBurst, request a sync packet soon to trigger higher download bandwidth for reply
      if (syncTelemBoostState == stbIdle)
        syncSpamCounter = 1;
      syncTelemBoostState = stbRequested;
    }
    else
    {
      // always enable msp after a channel package since the slot is only used if MspSender has data to send
      NextPacketIsMspData = true;

      //injectBackpackPanTiltRollData(now); // unsupported in legacy mode
      OtaPackChannelData_v3(&otaPkt, ChannelData, TelemetryReceiver.GetCurrentConfirm(), ExpressLRS_currTlmDenom);
    }
  }

  ///// Next, Calculate the CRC and put it into the buffer /////
  if (OtaIsFullRes) {
    GeneratePacketCrcFull_v3(&otaPkt);
  }
  else {
    GeneratePacketCrcStd_v3(&otaPkt);
  }

  SX12XX_Radio_Number_t transmittingRadio = Radio.GetStrongestReceivingRadio();

  if (isDualRadio())
  {
    switch (config.GetAntennaMode())
    {
    case TX_RADIO_MODE_GEMINI:
      transmittingRadio = SX12XX_Radio_All; // Gemini mode
      break;
    case TX_RADIO_MODE_ANT_1:
      transmittingRadio = SX12XX_Radio_1; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    case TX_RADIO_MODE_ANT_2:
      transmittingRadio = SX12XX_Radio_2; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    case TX_RADIO_MODE_SWITCH:
      if(OtaNonce%2==0)   transmittingRadio = SX12XX_Radio_1; // Single antenna tx and true diversity rx for tlm receiption.
      else   transmittingRadio = SX12XX_Radio_2; // Single antenna tx and true diversity rx for tlm receiption.
      break;
    default:
      break;
    }
  }

#if defined(Regulatory_Domain_EU_CE_2400)
  transmittingRadio = LbtChannelIsClear(transmittingRadio);   // weed out the radio(s) if channel in use

  if (transmittingRadio == SX12XX_Radio_NONE)
  {
    // No packet will be sent due to LBT.
    // Defer TXdoneCallback() to prepare for TLM when the IRQ is normally triggered.
    deferExecutionMicros(ExpressLRS_currAirRate_RFperfParams->TOA, Radio.TXdoneCallback);
  }
  else
#endif
  {
    Radio.TXnb((uint8_t*)&otaPkt, transmittingRadio);
  }
}

#endif
