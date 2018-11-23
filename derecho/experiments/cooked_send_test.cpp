#include <derecho/derecho.h>
#include <derecho/view.h>

#include <map>
#include <vector>

using namespace derecho;
using std::map;
using std::pair;
using std::vector;

class CookedMessages : public mutils::ByteRepresentable {
    vector<pair<uint, uint>> msgs;  // vector of (nodeid, msg #)

public:
    CookedMessages() {
    }
    CookedMessages(const vector<pair<uint, uint>>& msgs) : msgs(msgs) {
    }

    void send(uint nodeid, uint msg) {
        msgs.push_back(std::make_pair(nodeid, msg));
        std::cout << "Node " << nodeid << " sent msg " << msg << std::endl;
    }

    vector<pair<uint, uint>> get_msgs() {
        return msgs;
    }

    // default state
    DEFAULT_SERIALIZATION_SUPPORT(CookedMessages, msgs);

    // what operations you want as part of the subgroup
    REGISTER_RPC_FUNCTIONS(CookedMessages, send, get_msgs);
};

bool verify_global_order(const vector<pair<uint, uint>>& v1, const vector<pair<uint, uint>>& v2) {
    return (v1 == v2);
}

bool verify_local_order(vector<pair<uint, uint>> msgs) {
  map<uint, uint> order;
  for(auto [nodeid, msg] : msgs) {
    if (msg != order[nodeid] + 1) { // order.count(nodeid) != 0 && <= order[nodeid]
      std::cout << "Local order error!" << std::endl;
      return false;
    }
    order[nodeid]++; // order[nodeid] = msg
  }
  std::cout << "Pass" << std::endl;
  return true;
}
    //     auto it = order.find(p.first);
    //     if(it == order.end()) {
    //         order[p.first] = p.second;
    //     } else {
    //         if(p.second <= it->second) {
    //             std::cout << "Local order error!" << std::endl;
    //             return false;
    //         }
    //         order[p.first] = p.second;
    //     }
    // }
    // std::cout << "Pass" << std::endl;
    // return true;

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cout << "Provide the number of nodes as a command line argument" << std::endl;
        exit(1);
    }
    const uint32_t num_nodes = atoi(argv[1]);

    node_id_t my_id;
    ip_addr leader_ip, my_ip;

    std::cout << "Enter my id: " << std::endl;
    std::cin >> my_id;

    std::cout << "Enter my ip: " << std::endl;
    std::cin >> my_ip;

    std::cout << "Enter leader's ip: " << std::endl;
    std::cin >> leader_ip;

    Group<CookedMessages>* group;

    auto delivery_callback = [my_id](subgroup_id_t subgroup_id, node_id_t sender_id, message_id_t index, char* buf, long long int size) {
        // null message filter
        if(size == 0) {
            return;
        }
        std::cout << "In the delivery callback for subgroup_id=" << subgroup_id << ", sender=" << sender_id << std::endl;
        std::cout << "Message: " << buf << std::endl;
        return;
    };

    CallbackSet callbacks{delivery_callback};

    std::map<std::type_index, shard_view_generator_t>
            subgroup_membership_functions{{std::type_index(typeid(CookedMessages)),
                                           [num_nodes](const View& view, int&, bool) {
                                               auto& members = view.members;
                                               auto num_members = members.size();
                                               // if (num_members < num_nodes) {
                                               //    throw subgroup_provisioning_exception();
                                               // }
                                               subgroup_shard_layout_t layout(num_members);
                                               // for (uint i = 0; i < num_members; ++i) {
                                               // layout[i].push_back(view.make_subview(vector<uint32_t>{members[i], members[(i+1)%num_members], members[(i+2)%num_members]}));
                                               // layout[i].
                                               layout[0].push_back(view.make_subview(vector<uint32_t>(members)));
                                               // }
                                               return layout;
                                           }}};

    auto ticket_subgroup_factory = [](PersistentRegistry*) { return std::make_unique<CookedMessages>(); };

    const unsigned long long int max_msg_size = 200;
    DerechoParams derecho_params{max_msg_size, max_msg_size, max_msg_size};

    SubgroupInfo subgroup_info(subgroup_membership_functions);

    auto view_upcall = [](const View& view) {
        std::cout << "The members are: " << std::endl;
        for(auto m : view.members) {
            std::cout << m << " ";
        }
        std::cout << std::endl;
    };

    if(my_id == 0) {
        group = new Group<CookedMessages>(my_id, my_ip, callbacks, subgroup_info, derecho_params, vector<view_upcall_t>{view_upcall}, ticket_subgroup_factory);
    } else {
        group = new Group<CookedMessages>(my_id, my_ip, leader_ip, callbacks, subgroup_info, vector<view_upcall_t>{view_upcall}, ticket_subgroup_factory);
    }

    std::cout << "Finished constructing/joining the group" << std::endl;
    auto group_members = group->get_members();
    uint32_t my_rank = -1;
    std::cout << "Members are" << std::endl;
    for(uint i = 0; i < group_members.size(); ++i) {
        std::cout << group_members[i] << " ";
        if(group_members[i] == my_id) {
            my_rank = i;
        }
    }
    std::cout << std::endl;
    if(my_rank == (uint32_t)-1) {
        exit(1);
    }

    Replicated<CookedMessages>& cookedMessagesHandle = group->get_subgroup<CookedMessages>();
    for(uint i = 1; i < 50; ++i) {
        cookedMessagesHandle.ordered_send<RPC_NAME(send)>(my_rank, i);
    }

    auto&& results = cookedMessagesHandle.ordered_query<RPC_NAME(get_msgs)>();
    auto& replies = results.get();
    bool is_first_reply = true;
    vector<pair<uint, uint>> first_reply;
    for(auto& reply_pair : replies) {
        vector<pair<uint, uint>> v = reply_pair.second.get();
        if(is_first_reply) {
            verify_local_order(v);
            first_reply = v;
            is_first_reply = false;
        } else {
            verify_global_order(first_reply, v);
        }
    }
    std::cout << "End of main. Waiting indefinitely" << std::endl;
    while(true) {
    }
    return 0;
}