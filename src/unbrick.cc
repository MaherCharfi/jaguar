#include <stdint.h>
#include <iostream>
#include <fstream>
#include <boost/assert.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/fusion/algorithm.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/foreach.hpp>
#include <jaguar/jaguar_bridge.h>
#include <jaguar/jaguar_helper.h>


static uint32_t upd_id(uint16_t api)
{
    return jaguar::pack_id(
            0,
            jaguar::Manufacturer::kTexasInstruments,
            jaguar::DeviceType::kFirmwareUpdate,
            api);
}

#if 0
static void upd_send(can::CANBridge &can, uint16_t api)
{
    can.send(can::CANMessage(upd_id(api)));
}

static void upd_send(can::CANBridge &can, uint16_t api,
    std::vector<uint8_t> const &payload)
{
    can.send(can::CANMessage(upd_id(api), payload));
}


static can::TokenPtr send_ack(can::CANBridge &can,
        uint16_t api,
        std::vector<uint8_t> const &data,
        uint16_t ack_api = jaguar::FirmwareUpdate::kAck)
{
    can::TokenPtr tp = can.recv(upd_id(ack_api));
    upd_send(can, api, data);
    return tp;
}

template <typename G>
static can::TokenPtr send_ack(can::CANBridge &can,
        uint16_t api,
        G generator,
        uint16_t ack_api = jaguar::FirmwareUpdate::kAck)
{
    std::vector<uint8_t> obuf;
    BOOST_VERIFY(boost::spirit::karma::generate(obuf.end(), generator));
    return send_ack(can, api, obuf, ack_api);
}
#endif

class JaguarBootloader
{
public:
    JaguarBootloader(can::CANBridge &can)
    : can_(can)
    {}

    can::TokenPtr recv(uint16_t api)
    {
        return can_.recv(upd_id(api));
    }

    can::TokenPtr send_ack(uint16_t api, std::vector<uint8_t> const &data,
                           uint16_t ack_api = jaguar::FirmwareUpdate::kAck)
    {
        can::TokenPtr tp = recv(ack_api);
        send(api, data);
        return tp;
    }

    template <typename G>
    can::TokenPtr send_ack(uint16_t api, G generator,
                           uint16_t ack_api = jaguar::FirmwareUpdate::kAck)
    {
        std::vector<uint8_t> obuf;
        BOOST_VERIFY(boost::spirit::karma::generate(obuf.end(), generator));
        return send_ack(api, obuf, ack_api);
    }

    can::TokenPtr ping(void);
    can::TokenPtr prepare(uint32_t start_addr, uint32_t size)
    {
        return send_ack(jaguar::FirmwareUpdate::kDownload,
                boost::spirit::karma::little_dword(start_addr) <<
                boost::spirit::karma::little_dword(size));
    }

    can::TokenPtr send_data(std::vector<uint8_t> const &data)
    {
        assert(data.size() <= 8 && data.size() > 0);
        return send_ack(jaguar::FirmwareUpdate::kSendData, data);
    }

    void send(uint16_t api)
    {
        can_.send(can::CANMessage(upd_id(api)));
    }

    void send(uint16_t api, std::vector<uint8_t> const &payload)
    {
        can_.send(can::CANMessage(upd_id(api), payload));
    }

private:
    can::CANBridge &can_;
};

int main(int argc, char *argv[])
{
    try {
        if (argc <= 3) {
            char const *n = argc?argv[0]:"./unbrick";
            std::cerr << "err: lack args\n"
                      << "usage: " << n << " <serial> <fw.bin> <start addr>"
                        << std::endl;
            return 1;
        }

        std::string const io_path(argv[1]);
        std::string const fw_path(argv[2]);
        std::stringstream sa_stm;
        uint32_t fw_start;
        sa_stm << argv[3];
        sa_stm >> fw_start;

        can::JaguarBridge     can(io_path);
        JaguarBootloader bl(can);

        /* XXX: the hell is this, C++? */
        std::ifstream     fw_stream(fw_path.c_str());
        std::stringstream fw_buf;
        fw_buf << fw_stream.rdbuf();
        std::string const fw(fw_buf.str());

        /* XXX: spy on all recv'd data */
        can.attach_callback(0, 0,
                boost::lambda::var(std::cerr) << boost::lambda::_1);

        /* send PING */
        can::TokenPtr ping_token = bl.recv(jaguar::FirmwareUpdate::kPing);

        do {
            bl.send(jaguar::FirmwareUpdate::kPing);
            std::cout << "p" << std::endl;
        } while(!ping_token->timed_block(boost::posix_time::millisec(50)));

        /* set starting address and length */
        can::TokenPtr ack = bl.prepare(fw_start, fw.size());

        std::cout << 's';

        /* TODO: examine ack */
        ack->block();

        std::cout << 'a';

        /* send data block */
        std::vector<uint8_t> data;
        BOOST_FOREACH(uint8_t byte, fw) {
            if (data.size() == 8) {
                /* send `data` and reset */
                ack = bl.send_data(data);

                std::cout << 'd';

                /* TODO: look at ack payload (== status) */
                ack->block();

                std::cout << 'a';

                data.clear();
            }

            data.push_back(byte);
        }

        if (!data.empty()) {
            ack = bl.send_data(data);
            ack->block();
        }

        std::cout << std::endl << "Programming complete" << std::endl;

    } catch (can::CANException &e) {
        std::cerr << "error " << e.code() << ": " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

/* vim: set ts=4 et sts=4 sw=4: */

