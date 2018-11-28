#include <derecho/derecho.h>
#include <derecho/view.h>

#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <thread>
#include <chrono>

using namespace derecho;
using std::string;
using std::vector;
using std::ifstream;

int main() {

    node_id_t node_id;
    ip_addr my_ip;
    ip_addr leader_ip;
    string my_input_file;
    vector<string> my_input;
    vector<node_id_t> received_msgs;
    std::map<node_id_t, ifstream*> input_file_map;
    std::map<node_id_t, vector<char>*> received_msgs_map;
    std::map<node_id_t, int> num_received_msgs_map;
    int num_nodes;
    int num_msgs;
    int msg_size;

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

    for (int i = 0; i < num_nodes; i++) {
      num_received_msgs_map[i] = 0;
    }
    
    SubgroupInfo subgroup_info {
      {{std::type_index(typeid(RawObject)), [num_nodes](const View& curr_view, int& next_unassigned_rank, bool previous_was_successful) {
	    if (curr_view.num_members < num_nodes) {
	      std::cout << "Waiting for all nodes to join. Throwing subgroup provisioning exception!" << std::endl;
	      throw subgroup_provisioning_exception();
	    }
	    return one_subgroup_entire_view(curr_view, next_unassigned_rank, previous_was_successful);
         }
      }}
    };
    
    const unsigned long long int max_msg_size = std::max((num_nodes * num_msgs), msg_size);
    DerechoParams derecho_params{max_msg_size, max_msg_size, max_msg_size};

    Group<>* group;

    auto get_next_line = [input_file_map](node_id_t sender_id) {
      string line;
      if (input_file_map.at(sender_id)->is_open()) {
	getline(*(input_file_map.at(sender_id)), line);
	return line;
      }
    };
    
    auto delivery_callback = [&received_msgs, &input_file_map, &received_msgs_map, &num_received_msgs_map, msg_size, num_msgs](subgroup_id_t subgroup_id, node_id_t sender_id, message_id_t index, char* buf, long long int size) {
      if (num_received_msgs_map.at(sender_id) == 0) {
        ifstream *input_file = new ifstream(buf);
	vector<char> *receiving_msgs_vec = new vector<char>();
	if (input_file->is_open()) {
	  input_file_map[sender_id] = input_file;
	}
	else {
          std::cout << "Unable to open input file!" << std::endl;
          exit(1);
	}
	received_msgs_map[sender_id] = receiving_msgs_vec;
      }
      else if (num_received_msgs_map.at(sender_id) <= num_msgs) {
	received_msgs.push_back(sender_id);
	assert (get_next_line(sender_id) == buf); 
      }
      else {
	for (long long int i = 0; i < size; i++) {
	  received_msgs_map.at(sender_id)->push_back(buf[i]);
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

    RawSubgroup &group_as_subgroup = group->get_subgroup<RawObject>();

    ifstream input_file(my_input_file);
    std::cout << "Constructed group and file handler" << std::endl;
    char *buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
    while(!buf) {
      buf = group_as_subgroup.get_sendbuffer_ptr(msg_size);
    }
    my_input_file.copy(buf, my_input_file.size());
    group_as_subgroup.send();
    
    string line;
    int msg_counter = 0;
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

    std::cout << "Local ordering test successful!" << std::endl;
    std::cout << "Testing for global ordering" << std::endl;

    if (my_ip != leader_ip) {
      buf = group_as_subgroup.get_sendbuffer_ptr(num_nodes * num_msgs);
      while(!buf) {
	buf = group_as_subgroup.get_sendbuffer_ptr(num_nodes * num_msgs);
      }
      for (uint i = 0; i < received_msgs.size(); i++) {
	buf[i] = received_msgs[i];
      }
      group_as_subgroup.send();
    }
    else {
      std::cout << "Sleeping for 5 seconds to allow all messages to be received" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(5));
      std::cout << "Verifying global ordering" << std::endl;
      for (auto const& received_msgs_pair : received_msgs_map) {
	vector<char> msgs_to_compare = *received_msgs_pair.second;
	assert (msgs_to_compare == received_msgs);
      }
      std::cout << "Global ordering test successful!" << std::endl;
    }
    while(true) {
    }
}
