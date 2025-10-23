#pragma once

#include <cstdint>

namespace network_structs {

    enum class PacketType : uint16_t {
        DISCOVERY = 1,
        REQUEST = 2,
        DISCOVERY_ACK = 3,
        REQUEST_ACK = 4
    };

    struct RequestPayload {
        uint32_t dest_addr;
        uint32_t value;
    };

    struct AckPayload {
        uint32_t new_balance;
    };

    struct Packet {
        PacketType type;
        uint32_t sequence_number;

        union {
            RequestPayload req;
            AckPayload ack;
        } data;
    };
    struct Stats {
        uint64_t num_transactions = 0;
        uint64_t total_transferred = 0;
        int64_t total_balance = 0;
    };

    struct Client {
        uint32_t address;
        uint32_t last_req = 0;
        int64_t balance = 0;
    };

}