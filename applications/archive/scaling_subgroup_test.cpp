#include <iostream>
#include <memory>
#include <thread>
#include <typeindex>

#include "derecho/derecho.h"
#include "test_objects.h"
#include "conf/conf.hpp"

using derecho::ExternalCaller;
using derecho::Replicated;
using std::cout;
using std::endl;

int main(int argc, char** argv) {
    derecho::Conf::initialize(argc, argv);

    derecho::SubgroupInfo subgroup_function{[](const std::type_index& subgroup_type,
            const std::unique_ptr<derecho::View>& prev_view, derecho::View& curr_view) {
        if(subgroup_type == std::type_index(typeid(Foo))) {
            if(curr_view.num_members - curr_view.next_unassigned_rank < 3) {
                throw derecho::subgroup_provisioning_exception();
            }
            derecho::subgroup_shard_layout_t subgroup_vector(1);
            std::vector<node_id_t> first_3_nodes(&curr_view.members[curr_view.next_unassigned_rank],
                                                 &curr_view.members[curr_view.next_unassigned_rank] + 3);
            subgroup_vector[0].emplace_back(curr_view.make_subview(first_3_nodes));
            curr_view.next_unassigned_rank += 3;
            return subgroup_vector;
        } else { // subgroup_type == std::type_index(typeid(Bar))
            if(curr_view.num_members - curr_view.next_unassigned_rank < 3) {
                throw derecho::subgroup_provisioning_exception();
            }
            derecho::subgroup_shard_layout_t subgroup_vector(1);
            std::vector<node_id_t> first_3_nodes(&curr_view.members[curr_view.next_unassigned_rank],
                                                 &curr_view.members[curr_view.next_unassigned_rank] + 3);
            subgroup_vector[0].emplace_back(curr_view.make_subview(first_3_nodes));
            curr_view.next_unassigned_rank += 3;
            //If there are at least 3 more nodes left, make a second subgroup
            if(curr_view.num_members - curr_view.next_unassigned_rank >= 3) {
                std::vector<node_id_t> next_3_nodes(&curr_view.members[curr_view.next_unassigned_rank],
                                                    &curr_view.members[curr_view.next_unassigned_rank] + 3);
                subgroup_vector.emplace_back(std::vector<derecho::SubView>{curr_view.make_subview(next_3_nodes)});
                curr_view.next_unassigned_rank += 3;
            }
            return subgroup_vector;
        }
    }};

    auto foo_factory = [](PersistentRegistry*) { return std::make_unique<Foo>(-1); };
    auto bar_factory = [](PersistentRegistry*) { return std::make_unique<Bar>(); };

    derecho::Group<Foo, Bar> group({}, subgroup_function, nullptr,
                                   std::vector<derecho::view_upcall_t>{},
                                   foo_factory, bar_factory);

    cout << "Finished constructing/joining Group" << endl;
    uint32_t node_rank = group.get_my_rank();

    if(node_rank == 0) {
        Replicated<Foo>& foo_rpc_handle = group.get_subgroup<Foo>();
        ExternalCaller<Bar>& bar_rpc_handle = group.get_nonmember_subgroup<Bar>();
        foo_rpc_handle.ordered_send<RPC_NAME(change_state)>(0);
        cout << "Reading Foo's state from the group" << endl;
        derecho::rpc::QueryResults<int> foo_results = foo_rpc_handle.ordered_send<RPC_NAME(read_state)>();
        for(auto& reply_pair : foo_results.get()) {
            cout << "Node " << reply_pair.first << " says the state is: " << reply_pair.second.get() << endl;
        }
        cout << endl;
        int p2p_target = 3;
        derecho::rpc::QueryResults<std::string> bar_results = bar_rpc_handle.p2p_query<RPC_NAME(print)>(p2p_target);
        std::string response = bar_results.get().get(p2p_target);
        cout << "Node " << p2p_target << "'s state for Bar: " << response << endl;
    }
    if(node_rank == 1) {
        Replicated<Foo>& foo_rpc_handle = group.get_subgroup<Foo>();
        foo_rpc_handle.ordered_send<RPC_NAME(change_state)>(node_rank);
        cout << "Reading Foo's state from the group" << endl;
        derecho::rpc::QueryResults<int> foo_results = foo_rpc_handle.ordered_send<RPC_NAME(read_state)>();
        for(auto& reply_pair : foo_results.get()) {
            cout << "Node " << reply_pair.first << " says the state is: " << reply_pair.second.get() << endl;
        }
        cout << endl;
    }
    if(node_rank == 2) {
        Replicated<Foo>& foo_rpc_handle = group.get_subgroup<Foo>();
        ExternalCaller<Bar>& bar_rpc_handle = group.get_nonmember_subgroup<Bar>();
        foo_rpc_handle.ordered_send<RPC_NAME(change_state)>(node_rank);
        cout << "Reading Foo's state from the group" << endl;
        derecho::rpc::QueryResults<int> foo_results = foo_rpc_handle.ordered_send<RPC_NAME(read_state)>();
        for(auto& reply_pair : foo_results.get()) {
            cout << "Node " << reply_pair.first << " says the state is: " << reply_pair.second.get() << endl;
        }
        cout << endl;
        int p2p_target = 4;
        derecho::rpc::QueryResults<std::string> bar_results = bar_rpc_handle.p2p_query<RPC_NAME(print)>(p2p_target);
        std::string response = bar_results.get().get(p2p_target);
        cout << "Node " << p2p_target << "'s state for Bar: " << response << endl;
    }
    if(node_rank > 2 && node_rank < 6) {
        Replicated<Bar>& bar_rpc_handle = group.get_subgroup<Bar>(0);
        ExternalCaller<Foo>& foo_p2p_handle = group.get_nonmember_subgroup<Foo>();
        cout << "Sending updates to Bar object, subgroup 0" << endl;
        for(int i = 0; i < 10; ++i) {
            std::stringstream text;
            text << "Node " << node_rank << " update " << i;
            bar_rpc_handle.ordered_send<RPC_NAME(append)>(text.str());
        }
        derecho::rpc::QueryResults<std::string> bar_results = bar_rpc_handle.ordered_send<RPC_NAME(print)>();
        for(auto& reply_pair : bar_results.get()) {
            cout << "Node " << reply_pair.first << " says the log is: " << reply_pair.second.get() << endl;
        }
        int p2p_target = 1;
        derecho::rpc::QueryResults<int> foo_results = foo_p2p_handle.p2p_query<RPC_NAME(read_state)>(p2p_target);
        int response = foo_results.get().get(p2p_target);
        cout << "Node " << p2p_target << " says Foo's state is " << response << endl;
    }
    if(node_rank > 5) {
        Replicated<Bar>& bar_rpc_handle = group.get_subgroup<Bar>(1);
        cout << "Sending updates to Bar object, subgroup 1" << endl;
        for(int i = 0; i < 10; ++i) {
            std::stringstream text;
            text << "Node " << node_rank << " update " << i;
            bar_rpc_handle.ordered_send<RPC_NAME(append)>(text.str());
        }
        derecho::rpc::QueryResults<std::string> bar_results = bar_rpc_handle.ordered_send<RPC_NAME(print)>();
        for(auto& reply_pair : bar_results.get()) {
            cout << "Node " << reply_pair.first << " says the log is: " << reply_pair.second.get() << endl;
        }
    }

    cout << "Reached end of main(), entering infinite loop so program doesn't exit" << std::endl;
    while(true) {
    }
}
