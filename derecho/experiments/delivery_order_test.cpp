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

    query_node_info(node_id, my_ip, leader_ip);

    std::cout << "Enter my input filename: " << std::endl;
    std::cin >> my_input_file;

    string line;
    ifstream input_file(my_input_file);
    if (input_file.is_open()) {
      while(getline(input_file, line)) {
	my_input.push_back(line);
      }
      input_file.close();
    }
    else {
      std::cout << "Unable to open input file!" << std::endl;
      exit(1);
    }
    
    SubgroupInfo subgroup_info {
      {{std::type_index(typeid(RawObject)), [](const View& curr_view, int& next_unassigned_rank, bool previous_was_successful) {
	    if (curr_view.num_members < 3) {
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
    if(node_id == 0) {
        group = new Group<>(node_id, my_ip, callbacks, subgroup_info, derecho_params);
    } else {
        group = new Group<>(node_id, my_ip, leader_ip, callbacks, subgroup_info);
    }

    // First message sent will be the input file name for the node
    // so all the other nodes can read in that file and keep track of the input for this node
    // After that, we can use the message index in the delivery callback to index into the vector of input messages for the sender to verify content accuracy and order
}
