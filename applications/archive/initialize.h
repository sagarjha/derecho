#pragma once
#include "tcp/tcp.h"
#include "derecho/derecho.h"
void initialize(uint32_t &node_id, const uint32_t num_nodes,
    std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> &ip_addrs_and_ports);

