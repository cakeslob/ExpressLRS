#if defined(TARGET_RX)

#include "TcpMspConnector.h"
#include "logging.h"

#include "CRSFRouter.h"
#include "crsf2msp.h"
#include "msp2crsf.h"

#define TCP_PORT_BETAFLIGHT 5761 //port 5761 as used by BF configurator

TcpMspConnector::TcpMspConnector() : CRSFConnector()
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    return;
#else
    addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI);
#endif
}

void TcpMspConnector::begin()
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    return;
#else
    crsfRouter.addConnector(this);

    TCPserver = new AsyncServer(TCP_PORT_BETAFLIGHT);
    TCPserver->onClient(handleNewClient, this);
    TCPserver->begin();
#endif
}

void TcpMspConnector::handleNewClient(void *arg, AsyncClient *client)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)arg;
    (void)client;
    return;
#else
    DBGLN("TCP(%x) connected ip %s", client, client->remoteIP().toString().c_str());
    ((TcpMspConnector *)arg)->clientConnect(client);
#endif
}

void TcpMspConnector::handleDataIn(void *arg, AsyncClient *client, void *data, const size_t len)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)arg;
    (void)client;
    (void)data;
    (void)len;
    return;
#else
    DBGLN("TCP(%x) read %u", client, len);
    ((TcpMspConnector *)arg)->processData(client, data, len);
#endif
}

void TcpMspConnector::handleDisconnect(void *arg, AsyncClient *client)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)arg;
    (void)client;
    return;
#else
    DBGLN("TCP(%x) disconnected", client);
    ((TcpMspConnector *)arg)->clientDisconnect(client);
#endif
}

void TcpMspConnector::handleTimeOut(void *arg, AsyncClient *client, uint32_t time)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)arg;
    (void)client;
    (void)time;
    return;
#else
    DBGLN("TCP(%x) timeout", client);
#endif
}

void TcpMspConnector::handleError(void *arg, AsyncClient *client, int8_t error)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)arg;
    (void)client;
    (void)error;
    return;
#else
    DBGLN("TCP(%x) connection error %s", client, client->errorToString(error));
    ((TcpMspConnector *)arg)->clientDisconnect(client);
#endif
}

void TcpMspConnector::clientConnect(AsyncClient *client)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)client;
    return;
#else
    if (crsf2msp == nullptr) {
        crsf2msp = new CROSSFIRE2MSP();
        msp2crsf = new MSP2CROSSFIRE();
    }
    if (TCPclient != nullptr)
    {
        crsf2msp->reset();
        TCPclient->close();
        TCPclient = client;
    }

    // register events
    client->onData(handleDataIn, this);
    client->onError(handleError, this);
    client->onDisconnect(handleDisconnect, this);
    client->onTimeout(handleTimeOut, this);
    client->setRxTimeout(clientTimeoutS);
#endif
}

void TcpMspConnector::clientDisconnect(AsyncClient *client)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)client;
    return;
#else
    if (client == TCPclient)
    {
        TCPclient = nullptr;
    }
    client->close();
    delete client;
#endif
}

void TcpMspConnector::processData(AsyncClient *client, void *data, const size_t len)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)client;
    (void)data;
    (void)len;
    return;
#else
    TCPclient = client;
    msp2crsf->parse(this, (uint8_t *)data, len, CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
#endif
}

void TcpMspConnector::forwardMessage(const crsf_header_t *message)
{
#if !defined(BUILD_SHREW_UNNECESSARY) && defined(PLATFORM_ESP8266)
    (void)message;
    return;
#else
    if (TCPclient != nullptr && (message->type == CRSF_FRAMETYPE_MSP_RESP || message->type == CRSF_FRAMETYPE_MSP_REQ))
    {
        DBGLN("TCP(CRSF) msg %u", message->frame_size);
        crsf2msp->parse((uint8_t *)message, [&](const uint8_t *data, const size_t len) {
            TCPclient->write((const char *)data, len);
            DBGLN("TCP(%x) write %u", TCPclient, len);
        });
    }
#endif
}

#endif
