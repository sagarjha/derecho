#include "initialize.h"
#include <iostream>

using namespace derecho;
using namespace tcp;

void write_string(socket& s, const std::string str) {
    // We are going to write the null character
    s.write(str.size() + 1);
    s.write(str.c_str(), str.size() + 1);
}

std::string read_string(socket& s) {
    // Read the size of the null-terminated string
    size_t size;
    s.read(size);
    // Read into a character array
    char* ch_arr = new char[size];
    s.read(ch_arr, size);
    // Return the string constructed from the character array read from the socket
    return std::string(ch_arr);
}

std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> initialize(const uint32_t num_nodes) {
    //Get conf file parameters
    // Global information
    const std::string leader_ip = getConfString(CONF_DERECHO_LEADER_IP);
    const uint16_t leader_gms_port = getConfUInt16(CONF_DERECHO_LEADER_GMS_PORT);
    // Local information
    const uint32_t local_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);
    const std::string local_ip = getConfString(CONF_DERECHO_LOCAL_IP);
    const uint16_t sst_port = getConfUInt16(CONF_DERECHO_SST_PORT);
    const uint16_t gms_port = getConfUInt16(CONF_DERECHO_GMS_PORT);

    std::cout << leader_ip << " " << leader_gms_port << " " << local_id << " "
              << local_ip << " " << sst_port << " " << std::endl;

    bool is_leader = (leader_ip == local_ip && leader_gms_port == gms_port);

    if(is_leader) {
        // The map from id to IP and SST Port.
        std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> ip_addrs_and_ports{{local_id, {local_ip, sst_port}}};

        std::map<uint32_t, socket> id_sockets_map;
        tcp::connection_listener c_l(leader_gms_port);

        for(uint32_t i = 0; i < num_nodes - 1; ++i) {
            socket s = c_l.accept();
            std::cout << "Connected to Client" << i << std::endl;

            // c_id, c_ip and c_port will be converted to below three.
            uint32_t id;
            s.read(id);
            std::cout << "I have received from worker " << i << "'s " << std::endl
                      << "id " << id << std::endl;

            std::string ip = read_string(s);
            std::cout << "ip " << ip << std::endl;

            uint16_t port;
            s.read(port);
            std::cout << "port " << port << std::endl;

            ip_addrs_and_ports[id] = {ip, port};
            id_sockets_map[id] = std::move(s);
        }

        std::cout << "[Distribute map]" << std::endl;
        /** Distribute the map
         */
        // ip_port_pair
        for(auto& id_socket_pair : id_sockets_map) {
            socket& s = id_socket_pair.second;
            for(auto [id, ip_port_pair] : ip_addrs_and_ports) {
                auto ip = ip_port_pair.first;
                auto port = ip_port_pair.second;
                std::cout << " id " << id << std::endl;
                std::cout << " ip " << ip << std::endl;
                std::cout << " port " << port << std::endl;
                s.write(id);
                write_string(s, ip);
                s.write(port);
            }
        }
        return ip_addrs_and_ports;
    } else {
        tcp::socket s(leader_ip, leader_gms_port);
        std::cout << "Connected to Leader" << std::endl;

        std::cout << "I am sending my id, ip, and port to the leader." << std::endl;
        // Send local_id, local_ip, and sst_port to the leader.
        s.write(local_id);
        write_string(s, local_ip);
        s.write(sst_port);

        std::cout << "I just sent the leader my "
                  << "id " << local_id << ", "
                  << "ip " << local_ip.c_str() << ", and "
                  << "port " << sst_port << "."
                  << std::endl;

        std::cout << "I am receiving the map from the leader." << std::endl;

        // The map from id to IP and SST Port to be received from the leader.
        std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> ip_addrs_and_ports;
        for(uint32_t i = 0; i < num_nodes; ++i) {
            // c_id, c_ip and c_port will be converted to below three.
            uint32_t id;
            s.read(id);
            std::cout << "id " << id << std::endl;

            std::string ip = read_string(s);
            std::cout << "ip " << ip << std::endl;

            uint16_t port;
            s.read(port);
            std::cout << "port " << port << std::endl;

            ip_addrs_and_ports[id] = {ip, port};
        }

        std::cout << "Done!" << std::endl;
        return ip_addrs_and_ports;
    }
}
