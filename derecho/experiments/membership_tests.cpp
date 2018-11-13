#include <derecho/derecho.h>
#include <derecho/view.h>
#include <algorithm>
#include <vector>

using namespace derecho;

/**
 * A simple class that maintains a single variable.
 */
class State : public mutils::ByteRepresentable
{
  int value;

public:
  State(int v) : value(v) {}
  State(const State&) = default;

  int read_value() { return value; }
  bool change_value(int v) {
    if (value != v) return false;
    value = v;
    return true;
  }

  DEFAULT_SERIALIZATION_SUPPORT(State, value);
  REGISTER_RPC_FUNCTIONS(State, read_value, change_value);
};

/**
 * Helpers to generate layouts.
*/
void migration_layout(std::vector<SubView> layout, const View& view, bool build_test) {
  unsigned int n = view.members.size();
  if (build_test) {
    if (n == 3) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[0], view.members[2]}));
	    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[1]}));
    } else if (n == 4) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[0]}));
	    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[1], view.members[2]}));
    } else if (n == 5) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[1], view.members[0]}));
	    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[2]}));
    }
  }
}

void init_empty_layout(std::vector<SubView> layout, const View& view, bool build_test) {
  unsigned int n = view.members.size();
  if (build_test) {
    if (n == 3) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{}));
    } else if (n == 4) {
      layout.push_back(view.make_subview(
        std::vector<node_id_t>{view.members[0], view.members[1], view.members[2], view.members[3]}));
    } else if (n == 5) {
      layout.push_back(view.make_subview(
        std::vector<node_id_t>{view.members[0], view.members[1], view.members[2], view.members[3]}));
    }
  }
}

void inter_empty_layout(std::vector<SubView> layout, const View& view, bool build_test) {
  unsigned int n = view.members.size();
  if (build_test) {
    if (n == 3) {
      layout.push_back(view.make_subview(
        std::vector<node_id_t>{view.members[0], view.members[1], view.members[2]}));
    } else if (n == 4) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{}));
    } else if (n == 5) {
      layout.push_back(view.make_subview(
        std::vector<node_id_t>{view.members[4], view.members[2], view.members[3]}));
    }
  }
}

void dis_mem_layout(std::vector<SubView> layout, const View& view, bool build_test) {
  unsigned int n = view.members.size();
  if (build_test) {
    if (n == 3) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[0], view.members[1]}));
    } else if (n == 4) {
      layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[2], view.members[3]}));
    } else if (n == 5) {
	    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[2], view.members[3]}));
    }
  }
}

/**
 * Count number of true values in bool array.
 */
int num_true(bool values[], int n) {
  return std::count (values, values + n, true);
}

/**
 * Return handles of subgroups that current node is a member of.
 */
std::vector<Replicated<State> &> get_subgroups(Group<State> *group) {
  std::vector<Replicated<State> &> subgroups;
  for (int i=0; i < 4; i++) {
    try {
      auto& handle = group->get_subgroup(i);
      subgroups.insert(handle);
    }
    catch (const invalid_subgroup_exception e) { continue; }
    catch(const subgroup_provisioning_exception e) {
      // if View not provisioned, return empty array
      return std::vector<Replicated<State> &>();
    }
  }
}

/**
 * Main.
 */
int main()
{
  node_id_t my_id;
  ip_addr leader_ip, my_ip;

  // could take these from command line
  const bool tests[4] = {
    false, // 0: migration
    false, // 1: initially empty
    false, // 2: intermediately empty
    false  // 3: disjoint_membership
  }

  std::cout << "Enter my id: " << std::endl;
  std::cin >> my_id;
  std::cout << "Enter my ip: " << std::endl;
  std::cin >> my_ip;
  std::cout << "Enter leader ip: " << std::endl;
  std::cin >> leader_ip;

  Group<State> *group;
  CallbackSet callbacks;

  std::map<std::type_index, shard_view_generator_t>
      subgroup_membership_functions{{std::type_index(typeid(State)),
                                     [](const View &view, int &, bool) {
        if (view.members.size() < 3) {
          std::cout << "Throwing subgroup exception: not enough members" << std::endl;
          throw subgroup_provisioning_exception();
        }
        subgroup_shard_layout_t layout(num_true(tests));
        int test_idx = 0;

        if (tests[0]) migration_layout(layout[test_idx++], view);
        if (tests[1]) init_empty_layout(layout[test_idx++], view);
        if (tests[2]) inter_empty_layout(layout[test_idx++], view);
        if (tests[3]) dis_mem_layout(layout[test_idx++], view);
        
	      return layout;
      }}};

  auto state_subgroup_factory = [](PersistentRegistry *) { return std::make_unique<State>(13); };
  unsigned long long int max_msg_size = 100;
  DerechoParams derecho_params{max_msg_size, max_msg_size, max_msg_size};
  SubgroupInfo subgroup_info{subgroup_membership_functions};

  if (my_id == 0)
    group = new Group<State>(
      my_id, my_ip, callbacks, subgroup_info, derecho_params, {}, state_subgroup_factory
    );
  else
    group = new Group<State>(
      my_id, my_ip, leader_ip, callbacks, subgroup_info, {}, state_subgroup_factory
    );

  // Start tests
  // If you are node i in test t, you should be a part of the subgroups in that scenario
  // 
  int n = group->members.size();
  int test_idx = 0;

  if (my_id == 0) {
    if (n == 3) {
      for (int i=0; i < 4; i++) {
        if (tests[i]) {
          // TODO
        }
      }
    }
  }
  // Replicated<State> &stateHandle = group->get_subgroup<State>(0);

  // stateHandle.ordered_query<RPC_NAME(change_value)>(new_state);

  // rpc::QueryResults<int> results = stateHandle.ordered_query<RPC_NAME(read_value)>();
  // rpc::QueryResults<int>::ReplyMap& replies = results.get();
  // for (auto& reply_pair: replies) {
  //   std::cout << "Reply from node " << reply_pair.first << ": " << reply_pair.second.get() << std::endl;
  // }

  std::cout << "Done checking state.";

  while (true) {}
}
