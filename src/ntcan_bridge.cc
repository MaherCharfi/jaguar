#include <cassert>
#include <cstring>
#include "ntcan_bridge.h"

namespace can {

NTCANBridge::NTCANBridge(int net)
{
    NTCAN_RESULT result;
    result = canOpen(net, 0, m_tx_queue, m_rx_queue, m_tx_timeout, m_rx_timeout, &m_handle);
    switch (result) {
    case NTCAN_SUCCESS:
        break;

    case NTCAN_INVALID_DRIVER:
        throw CANException("Incompatible device driver and NTCAN versions.");

    case NTCAN_INVALID_HARDWARE:
    case NTCAN_INVALID_FIRMWARE:
        throw CANException("Incompatible device driver and firmware versions.");

    case NTCAN_NET_NOT_FOUND:
        throw CANException("Invalid network number.");

    default:
        throw CANException("An unknown error has occurred.");
    }

    canIdAdd(m_handle, NTCAN_20B_BASE);
}

NTCANBridge::~NTCANBridge(void)
{
    canClose(m_handle);
}

void NTCANBridge::send(uint32_t id, void const *data, size_t payload_length)
{
    assert((id & ~0x1fffffff) == 0);
    assert(payload_length <= 8);

    CMSG ntcan_msg;
    ntcan_msg.id  = id | NTCAN_20B_BASE;
    ntcan_msg.len = payload_length;
    memcpy(ntcan_msg.data, data, payload_length);

    int32_t msg_length = sizeof(CMSG);
    NTCAN_RESULT result = canWrite(m_handle, &ntcan_msg, &msg_length, NULL);

    switch (result) {
    case NTCAN_SUCCESS:
        break;

    case NTCAN_CONTR_OFF_BUS:
    case NTCAN_CONTR_WARN:
        throw CANException("Too many CAN error frames have been received.");

    case NTCAN_TX_ERROR:
    case NTCAN_TX_TIMEOUT:
        throw CANException("Transmission timeout exceeded.");

    default:
        throw CANException("An unknown error has occurred.");
    }
}

uint32_t NTCANBridge::recv(void *data, size_t payload_length)
{
    int32_t msg_length = sizeof(CMSG);
    NTCAN_RESULT result = canRead(m_handle, &m_buffer, &msg_length, NULL);

    switch (result) {
    case NTCAN_SUCCESS:
        break;

    //case NTCAN_RX_ERROR:
    case NTCAN_RX_TIMEOUT:
        throw CANException("Receive timeout exceeded.");

    default:
        throw CANException("An unknown error has occurred.");
    }

    // Any timeout should be indicated in the return code, so we can assume
    // that one message was received.
    assert(msg_length == sizeof(CMSG));
    assert(m_buffer.len == payload_length);
    memcpy(data, m_buffer.data, payload_length);
    return m_buffer.id;
}

};
