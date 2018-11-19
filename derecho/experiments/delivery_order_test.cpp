#include <derecho/derecho.h>
#include <derecho/view.h>

#include <vector>
#include <string>
#include <fstream>

using namespace derecho;
using std::vector;

int main() {

    node_id_t node_id;
    ip_addr my_ip;
    ip_addr leader_ip;
    string my_input_file;
    vector<string> my_input;
    vector<node_id_t> received_msgs;
    uint num_nodes;
    uint msg_size;

    query_node_info(node_id, my_ip, leader_ip);

    std::cout << "Enter my input filename: " << std::endl;
    std::cin >> my_input_file;

    std::cout << "Enter number of nodes: " << std::endl;
    std::cin >> num_nodes;

    std::cout << "Enter size of messages: " << std::endl;
    std::cin >> msg_size;
    
    SubgroupInfo subgroup_info {
      {{std::type_index(typeid(RawObject)), [](const View& curr_view, int& next_unassigned_rank, bool previous_was_successful) {
	    if (curr_view.num_members < num_nodes) {
	      std::cout << "Waiting for all nodes to join. Throwing subgroup provisioning exception!" << std::endl;
	      throw subgroup_provisioning_exception();
	    }
	    return one_subgroup_entire_view(curr_view, next_unassigned_rank, previous_was_successful);
         }
      }}
    };
    
    const unsigned long long int max_msg_size = 200;
    DerechoParams derecho_params{max_msg_size, max_msg_size, max_msg_size};

    Group<>* group;

    auto get_next_line = [input_file_map](node_id_t sender_id) {
      ifstream input_file = input_files_map[sender_id];
      char* line;
      if (input_file.is_open()) {
	getline(input_file, line);
	return line;
      }
    }
    
    auto delivery_callback = [received_msgs, input_files_map](subgroup_id_t subgroup_id, node_id_t sender_id, message_id_t index, char* buf, long long int size) {
      if (size == 0) {
	return;
      }
      if (index == 0) {
        ifstream input_file(buf);
	if (input_file.is_open()) {
	  input_files_map[sender_id] = input_file;
	}
	else {
          std::cout << "Unable to open input file!" << std::endl;
          exit(1);
	}
      }
      else if (size == msg_size) {
	received_msgs.push_back(sender_id);
	assert (get_next_line(sender_id) == buf); 
      }
    }

    CallbackSet callbacks{delivery_callback};
    
    if(node_id == 0) {
        group = new Group<>(node_id, my_ip, callbacks, subgroup_info, derecho_params);
    } else {
        group = new Group<>(node_id, my_ip, leader_ip, callbac
			    ks, subgroup_info);
    }

    ifstream input_file(my_input_file);
    char *buf = group.get_sendbuffer_ptr(msg_size);
    while(!buf) {
      buf = group.get_sendbuffer_ptr(msg_size);
    }
    while(getline(input_file, buf)) {
      group.send();
      buf = group.get_sendbuffer_ptr(msg_size);
      while(!buf) {
	buf = group.get_sendbuffer_ptr(msg_size);
      }
    }
    input_file.close();

}
