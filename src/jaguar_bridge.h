#ifndef JAGUAR_BRIDGE_H_
#define JAGUAR_BRIDGE_H_

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/signal.hpp>
#include <boost/thread.hpp>
#include <map>
#include <vector>
#include <stdint.h>
#include "can_bridge.h"
#include "jaguar_helper.h"

typedef boost::asio::buffers_iterator<
    boost::asio::streambuf::const_buffers_type> asio_iterator;


namespace can {

enum ReceiveState {
    kWaiting,
    kLength,
    kPayload,
    kComplete
};

class JaguarToken;

class JaguarBridge : public CANBridge
{
public:
    typedef boost::function<void (boost::shared_ptr<CANMessage>)> recv_callback;

    JaguarBridge(std::string port);
    virtual ~JaguarBridge(void);

    virtual void send(uint32_t id, void const *data, size_t length);
    virtual TokenPtr recv(uint32_t id, void *data, size_t length);

    virtual void attach_callback(uint32_t id, recv_callback cb);
    //virtual bool detach_callback(uint32_t id, recv_callback cb);

private:
    typedef boost::signal<void (boost::shared_ptr<CANMessage>)> callback_signal;
    typedef boost::shared_ptr<callback_signal> callback_signal_ptr;
    typedef std::map<uint32_t, callback_signal_ptr> callback_table;

    typedef boost::shared_ptr<JaguarToken> token_ptr;
    typedef std::map<uint32_t, token_ptr>  token_table;

    static uint8_t const kSOF, kESC;
    static uint8_t const kSOFESC, kESCESC;
    static size_t const kReceiveBufferLength;

    boost::asio::io_service  io_;
    boost::asio::serial_port serial_;

    boost::thread recv_thread_;
    std::vector<uint8_t> recv_buffer_;
    callback_table callbacks_;
    boost::mutex callback_mutex_;
    token_table tokens_;

    std::vector<uint8_t> packet_;
    ReceiveState state_;
    size_t length_;
    bool escape_;

    boost::shared_ptr<CANMessage> recv_byte(uint8_t byte);
    void recv_handle(boost::system::error_code const& error, size_t count);
    void recv_message(boost::shared_ptr<CANMessage> msg);

    boost::shared_ptr<CANMessage> unpack_packet(std::vector<uint8_t> const &packet);
    size_t encode_bytes(uint8_t const *bytes, size_t length, std::vector<uint8_t> &buffer);
};

class JaguarToken : public Token {
public:
    virtual ~JaguarToken(void);
    virtual void block(void);
    virtual bool ready(void) const;

private:    
    boost::condition_variable cond_;
    boost::mutex mutex_;
    void *buffer_;
    size_t length_;
    bool done_;

    JaguarToken(void *buffer, size_t buffer_length);
    virtual void unblock(boost::shared_ptr<CANMessage> message);

    friend class JaguarBridge;
};

};

#endif
