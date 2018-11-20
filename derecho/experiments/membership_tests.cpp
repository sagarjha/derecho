#include <derecho/derecho.h>
#include <derecho/view.h>
#include <algorithm>
#include <vector>

using namespace derecho;

enum Tests {MIGRATION, INIT_EMPTY, INTER_EMPTY, DISJOINT_MEM};

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
    if (value == v)
      return false;
    else {
      value = v;
      return true;
    }
  }

  DEFAULT_SERIALIZATION_SUPPORT(State, value);
  REGISTER_RPC_FUNCTIONS(State, read_value, change_value);
};

/**
 * Helpers to generate layouts.
*/
void migration_layout(std::vector<SubView>& layout, const View& view) {
  unsigned int n = view.members.size();
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

void init_empty_layout(std::vector<SubView>& layout, const View& view) {
  unsigned int n = view.members.size();
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

void inter_empty_layout(std::vector<SubView>& layout, const View& view) {
  unsigned int n = view.members.size();
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

void dis_mem_layout(std::vector<SubView>& layout, const View& view) {
  unsigned int n = view.members.size();
  if (n == 3) {
    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[0], view.members[1]}));
  } else if (n == 4) {
    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[2], view.members[3]}));
  } else if (n == 5) {
    layout.push_back(view.make_subview(std::vector<node_id_t>{view.members[2], view.members[3]}));
  }
}

/**
 * Return handles of subgroups that current node is a member of.
 * 
 * TODO: Refactor to return int vector of subgroup indexes instead of handles. (why?)
 */
std::vector<Replicated<State>> get_subgroups(Group<State> *group) {
  std::vector<Replicated<State>> subgroups;
  for (int i = 0; i < 4; i++) {
    try {
      auto& handle = group->get_subgroup<State>(i);
      subgroups.push_back(std::move(handle));
    }
    catch (const invalid_subgroup_exception e) {
      continue;
    }
    catch (const subgroup_provisioning_exception e) {
      // if view not provisioned, return empty array
      return std::vector<Replicated<State>>();
    }
  }
  return subgroups;
}

/**
 * Tests membership by reading initial state, changing it, and reading again.
 * 
 * TODO: refactor function so it takes in the index of the subgroup to read state from
 *       instead of the state's handle. (why?)
 */
void test_state(Replicated<State>& stateHandle) {
  // Read initial state
  auto initial_results = stateHandle.ordered_query<RPC_NAME(read_value)>();
  auto& initial_replies = initial_results.get();
  for (auto& reply_pair : initial_replies) {
     auto other_state = reply_pair.second.get();
     if (other_state != 100)
       std::cout << "Failure from node " << reply_pair.first
                 << ": Expected 100 but got " << other_state << std::endl;
     else
       std::cout << "Reply from node " << reply_pair.first << ": " << other_state << std::endl;
  }

  // Change state
  const int new_state = 42;
  stateHandle.ordered_query<RPC_NAME(change_value)>(new_state);

  // Read new state
  auto new_results = stateHandle.ordered_query<RPC_NAME(read_value)>();
  auto& new_replies = new_results.get();
  for (auto& reply_pair : new_replies) {
    auto other_state = reply_pair.second.get();
    if (other_state != new_state)
      std::cout << "Failure from node " << reply_pair.first
		<< ": Expected " << new_state << " but got " << other_state << std::endl;
    else
      std::cout << "Reply from node " << reply_pair.first << ": " << other_state << std::endl;
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
  std::vector<bool> tests = {true, false, false, false};

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
      [tests](const View& view, int&, bool) {
	if (view.members.size() < 3) {
	  throw subgroup_provisioning_exception();
        }
	
	subgroup_shard_layout_t layout(std::count(tests.begin(), tests.end(), true));
	
        int test_idx = 0;
        if (tests[Tests::MIGRATION]) migration_layout(layout[test_idx++], view);
        if (tests[Tests::INIT_EMPTY]) init_empty_layout(layout[test_idx++], view);
        if (tests[Tests::INTER_EMPTY]) inter_empty_layout(layout[test_idx++], view);
        if (tests[Tests::DISJOINT_MEM]) dis_mem_layout(layout[test_idx++], view);
        
	return layout;
      }
    }
  };

  auto state_subgroup_factory = [](PersistentRegistry *) { return std::make_unique<State>(100); };
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

  while (true) {

    // stupid hack to wait for user to prompt start of each test
    std::string input;
    std::cout << "Waiting for input to start... " << std::endl;
    std::getline(std::cin, input);

    int n = group->get_members().size();
    int test_idx = 0;
    auto subgroups = get_subgroups(group);

    if (my_id == 0 || my_id == 1) {
      if (n == 3) {
	if (tests[Tests::MIGRATION]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INTER_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::DISJOINT_MEM]) test_state(subgroups[test_idx++]);
      } else if (n == 4 || n == 5) {
	if (tests[Tests::MIGRATION]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INIT_EMPTY]) test_state(subgroups[test_idx++]);
      }
    }

    else if (my_id == 2) {
      if (n == 3) {
	if (tests[Tests::MIGRATION]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INTER_EMPTY]) test_state(subgroups[test_idx++]);
      } else if (n == 4) {
	if (tests[Tests::MIGRATION]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INIT_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::DISJOINT_MEM]) test_state(subgroups[test_idx++]);
      } else if (n == 5) {
	if (tests[Tests::MIGRATION]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INIT_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INTER_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::DISJOINT_MEM]) test_state(subgroups[test_idx++]);
      }
    }

    else if (my_id == 3) {
      if (n == 4) {
	if (tests[Tests::INIT_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::DISJOINT_MEM]) test_state(subgroups[test_idx++]);
      } else if (n == 5) {
	if (tests[Tests::INIT_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::INTER_EMPTY]) test_state(subgroups[test_idx++]);
	if (tests[Tests::DISJOINT_MEM]) test_state(subgroups[test_idx++]);
      }
    }

    else if (my_id == 4) {
      if (n == 5) {
	if (tests[Tests::INTER_EMPTY]) test_state(subgroups[test_idx++]);
      }
    } 
  }

}
