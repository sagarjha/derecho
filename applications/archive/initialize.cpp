#include <iostream>
#include "initialize.h"

using namespace derecho;
using namespace tcp;

// [kwang] what is gms_port?
// [kwang] gdb
// [kwang] Instead of cout, log function in cpp
void initialize(uint32_t &node_id, const uint32_t num_nodes,
    std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> &ip_addrs_and_ports) {
    /** Get conf file parameters
     */
    // Global information
    const std::string leader_ip = getConfString(CONF_DERECHO_LEADER_IP);
    uint32_t leader_gms_port = getConfUInt32(CONF_DERECHO_LEADER_GMS_PORT);
    // Local information
    uint32_t local_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);
    const std::string local_ip = getConfString(CONF_DERECHO_LOCAL_IP);
    uint32_t sst_port = getConfUInt32(CONF_DERECHO_SST_PORT);
    uint32_t gms_port = getConfUInt32(CONF_DERECHO_GMS_PORT);

    node_id = local_id;
    std::cout
      << leader_ip << " "
      << leader_gms_port << " "
      << local_id << " "
      << local_ip << " "
      << sst_port << " "
      << std::endl;

    bool is_leader;
    if (leader_ip == local_ip && leader_gms_port == gms_port) {
      is_leader = true;
    } else {
      is_leader = false;
    }

    if (is_leader) {
        std::vector<socket> fifo_s_vec(num_nodes - 1);
        std::map<uint32_t, socket> id_s_map;
        tcp::connection_listener c_l(leader_gms_port);

        for (uint32_t i = 0; i < num_nodes - 1; ++i) {
          fifo_s_vec[i] = c_l.accept();
          std::cout << "Connected to Client" << i << std::endl;
        }

        std::cout << "[Update map]" << std::endl;
        /** Update map: {key, value} = {id, {ip, port}}
         */
        ip_addrs_and_ports[local_id] = {local_ip, sst_port};

        for (uint32_t i = 0; i < num_nodes - 1; ++i) {
          // c_id, c_ip and c_port will be converted to below three.
          uint32_t id;
          std::string ip;
          uint16_t port;

          std::cout << "I have received from worker " << i << "'s " <<
            std::endl;
          fifo_s_vec[i].read(id);
          std::cout << "id " << id << std::endl;

          size_t size;
          fifo_s_vec[i].read(size);
          char* c_ip = new char[size];
          fifo_s_vec[i].read(c_ip, size);
          ip = c_ip;
          std::cout << "ip " << ip << std::endl;

          fifo_s_vec[i].read(port);
          std::cout << "port " << port << std::endl;

          ip_addrs_and_ports[id] = {ip, port};
          id_s_map[id] = std::move(fifo_s_vec[i]);
        }

        std::cout << "[Distribute map]" << std::endl;
        /** Distribute the map
         */
        for (std::map<uint32_t, std::pair<ip_addr_t, uint16_t>>::iterator
           it = ip_addrs_and_ports.begin(); it != ip_addrs_and_ports.end(); ++it) {

          uint32_t recvr_id = it->first;
          if (recvr_id == local_id) {
            continue;
          }
          std::cout << "recvr_id " << recvr_id << std::endl;
          for (std::map<uint32_t, std::pair<ip_addr_t, uint16_t>>::iterator
             _it = ip_addrs_and_ports.begin(); _it != ip_addrs_and_ports.end(); ++_it) {
            uint32_t id = _it->first;
            std::string ip = std::get<0>(_it->second);
            uint16_t port = std::get<1>(_it->second);
            std::cout << " id " << id << std::endl;
            std::cout << " ip " << ip << std::endl;
            std::cout << " port " << port << std::endl;
            id_s_map[recvr_id].write(id);
            size_t size = sizeof(ip);
            id_s_map[recvr_id].write(size);
            id_s_map[recvr_id].write(ip.c_str(), size);
            id_s_map[recvr_id].write(port);
        }
      }
    } else {
        tcp::socket s(leader_ip, leader_gms_port);
        std::cout << "Connected to Leader" << std::endl;

        std::cout << "I am sending my id, ip, and port to the leader." << std::endl;
        // Send local_id, local_ip, and sst_port to the leader.
        s.write(local_id);
        size_t size = local_ip.size() + 1;
        s.write(size);
        s.write(local_ip.c_str(), size);
        s.write(sst_port);

        std::cout << "I just sent the leader my "
          << "id " << local_id << ", "
          << "ip " << local_ip.c_str() << ", and "
          << "port " << sst_port << "."
          << std::endl;

        std::cout << "I am receiving the map from the leader." << std::endl;
        // Receive the map from the leader
        for (uint32_t i = 0; i < num_nodes; ++i) {
          // c_id, c_ip and c_port will be converted to below three.
          uint32_t id;
          std::string ip;
          uint16_t port;

          s.read(id);
          std::cout << "id " << id << std::endl;
          size_t size;
          s.read(size);
          char* c_ip = new char[size];
          s.read(c_ip, size);
          ip = c_ip;
          std::cout << "ip " << ip << std::endl;
          s.read(port);
          std::cout << "port " << port << std::endl;

          ip_addrs_and_ports[id] = {ip, port};
        }

        std::cout << "Done!" << std::endl;
    }
}
