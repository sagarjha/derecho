#include <derecho/derecho.h>
#include <derecho/view.h>

#include <chrono>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace derecho;
using std::ifstream;
using std::string;
using std::vector;

int main() {
    node_id_t node_id;
    ip_addr my_ip;
    ip_addr leader_ip;
    string my_input_file;
    std::map<node_id_t, ifstream*> input_file_map;
    uint32_t num_nodes;
    uint32_t num_msgs;
    uint32_t msg_size;

    std::cout << "Enter my node id: " << std::endl;
    std::cin >> node_id;

    std::cout << "Enter my ip address: " << std::endl;
    std::cin >> my_ip;

    std::cout << "Enter the leader's ip address: " << std::endl;
    std::cin >> leader_ip;

    std::cout << "Enter my input filename (absolute path): " << std::endl;
    std::cin >> my_input_file;

    std::cout << "Enter number of nodes: " << std::endl;
    std::cin >> num_nodes;

    std::cout << "Enter number of messages: " << std::endl;
    std::cin >> num_msgs;

    std::cout << "Enter size of messages: " << std::endl;
    std::cin >> msg_size;

    SubgroupInfo subgroup_info{
            {{std::type_index(typeid(RawObject)), [num_nodes](const View& curr_view, int& next_unassigned_rank, bool previous_was_successful) {
                  if(curr_view.members.size() < num_nodes) {
                      std::cout << "Waiting for all nodes to join. Throwing subgroup provisioning exception!" << std::endl;
                      throw subgroup_provisioning_exception();
                  }
                  return one_subgroup_entire_view(curr_view, next_unassigned_rank, previous_was_successful);
              }}}};

    const unsigned long long int max_msg_size = std::max((num_nodes * num_msgs), msg_size);
    DerechoParams derecho_params{max_msg_size, max_msg_size, max_msg_size};

    Group<>* group;

    auto get_next_line = [input_file_map](node_id_t sender_id) {
        string line;
        if(input_file_map.at(sender_id)->is_open()) {
            getline(*(input_file_map.at(sender_id)), line);
            return line;
        }
        std::cout << "Error: Input file not open!!!" << std::endl;
	exit(1);
    };

    volatile bool done = true;
    auto delivery_callback = [&, num_received_msgs_map = std::map<node_id_t, uint32_t>(),
                              received_msgs = std::vector<node_id_t>(),
                              finished_nodes = std::set<node_id_t>()](subgroup_id_t subgroup_id,
                                                                      node_id_t sender_id,
                                                                      message_id_t index,
                                                                      char* buf,
                                                                      long long int size) mutable {
        if(num_received_msgs_map[sender_id] == 0) {
            ifstream* input_file = new ifstream(buf);
            if(input_file->is_open()) {
                input_file_map[sender_id] = input_file;
            } else {
                std::cout << "Unable to open input file!" << std::endl;
                exit(1);
            }
        } else if(num_received_msgs_map[sender_id] <= num_msgs) {
            received_msgs.push_back(sender_id);
            if(get_next_line(sender_id) != buf) {
                std::cout << "Error: Message contents mismatch or violation of local order!!!" << std::endl;
                exit(1);
            }
            if(sender_id == node_id && num_received_msgs_map[sender_id] == num_msgs) {
                std::cout << "Local ordering test successful!" << std::endl;
                if(my_ip != leader_ip) {
                    std::thread temp([&]() {
                        RawSubgroup& group_as_subgroup = group->get_subgroup<RawObject>();
                        buf = group_as_subgroup.get_sendbuffer_ptr(received_msgs.size() * sizeof(node_id_t));
                        while(!buf) {
                            buf = group_as_subgroup.get_sendbuffer_ptr(received_msgs.size() * sizeof(node_id_t));
                        }
                        for(uint i = 0; i < received_msgs.size(); i++) {
                            (node_id_t&)buf[sizeof(node_id_t) * i] = received_msgs[i];
                        }
                        group_as_subgroup.send();
                    });
                    temp.detach();
                }
            }
        } else {
            if(my_ip == leader_ip) {
                std::cout << "Testing for global ordering" << std::endl;
                // verify the message against received_msgs
                for(auto node : received_msgs) {
                    node_id_t val = *((node_id_t*)buf);
                    buf += sizeof(node_id_t);
                    if(node != val) {
                        std::cout << "Error: Violation of global order!!!" << std::endl;
                        exit(1);
                    }
                }
            }
            finished_nodes.insert(sender_id);
            if(finished_nodes.size() == num_nodes - 1) {
                done = true;
            }
        }
        num_received_msgs_map[sender_id] = num_received_msgs_map[sender_id] + 1;
    };

    CallbackSet callbacks{delivery_callback};

    if(node_id == 0) {
        group = new Group<>(node_id, my_ip, callbacks, subgroup_info, derecho_params);
    } else {
        group = new Group<>(node_id, my_ip, leader_ip, callbacks, subgroup_info);
    }

    RawSubgroup& group_as_subgroup = group->get_subgroup<RawObject>();

    ifstream input_file(my_input_file);
    std::cout << "Constructed group and file handler" << std::endl;
    char* buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
    while(!buf) {
        buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
    }
    my_input_file.copy(buf, my_input_file.size());
    group_as_subgroup.send();

    string line;
    uint32_t msg_counter = 0;
    std::cout << "Attempting to send messages" << std::endl;
    while(msg_counter < num_msgs) {
        getline(input_file, line);
        std::cout << "Sending message: " << line << std::endl;
        buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
        while(!buf) {
            buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
        }
        line.copy(buf, line.size());
        group_as_subgroup.send();
        msg_counter = msg_counter + 1;
    }

    input_file.close();

    while(!done) {
    }
    if (my_ip == leader_ip) {
    std::cout << "Global ordering test successful!" << std::endl;      
    }
    group->barrier_sync();
    group->leave();
    return 0;
}
