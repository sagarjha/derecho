#include <iostream>
#include <chrono> // for sleep
#include <thread> // for sleep
#include <time.h>
#include <unistd.h>

// [kwang] how did you set up the relative path for header include?
#include "sst/poll_utils.h"
#include "sst/sst.h"
#include "initialize.h"
#define LOCAL sst.get_local_index()

// [kwang] why writing those?
using std::cin;
using std::cout;
using std::endl;
using std::map;
using std::string;
using std::vector;

using namespace sst;

// Construct my own SST structure.
// [kwang] SST<mySST>
// [kwang] recvd(_members.size())
class mySST : public SST<mySST> {
public:
  mySST(const vector<uint32_t>& _members, uint32_t my_id) :
    SST<mySST>(this, SSTParams{_members, my_id}),
    recvd(_members.size()) {
      SSTInit(recvd);
    }

    SSTFieldVector<int> recvd;
};

int main(int argc, char *argv[]) {
// Predicate is the condition of trigger.
// When predicate is true, the associated trigger works.
// predicate is a thread.
  if (argc < 2) {
    std::cout << "Error: Expected number of nodes in experiment as the first argument." << std::endl;
    return -1;
  }
  uint32_t num_nodes = std::atoi(argv[1]);
  cout << "num_nodes: " << num_nodes << endl;

  srand(getpid());
  const uint32_t node_id = derecho::getConfUInt32(CONF_DERECHO_LOCAL_ID);
  std::cout << "Input the IP addresses and ports" << std::endl;

#ifdef USE_VERBS_API
  verbs_initialize(initialize(num_nodes), node_id);
#else
  lf_initialize(initialize(num_nodes), node_id);
#endif

  vector<uint32_t> members(num_nodes);

  for (unsigned int i = 0; i < num_nodes; ++i) {
    members[i] = i;
  }

  mySST sst(members, node_id);
  for (unsigned int i = 0; i < num_nodes; ++i) {
    sst.recvd[LOCAL][i] = -1;
  }
  // [kwang] need to check
  // sst.put_with_completion((char*)std::addressof(sst.recvd[0]) - sst.getBaseAddress(), sizeof(sst.recvd[0][0]) * members.size());
  sst.put_with_completion();
  sst.sync_with_members();

  // Predicate
  for (unsigned int i = 0; i < num_nodes; ++i) {
    auto receiver_pred = [](const mySST& sst) {
      if ((rand() % 1000000) <= 100) {
        return true;
      }
      return false;
    };
    auto receiver_trig = [i](mySST& sst) {
      std::cout << LOCAL << " Received " <<  i <<  "'s msg!" << std::endl;
      ++sst.recvd[LOCAL][i];
      sst.put((char*)std::addressof(sst.recvd[0][i]) - sst.getBaseAddress(), sizeof(sst.recvd[0][i]));
    };
    sst.predicates.insert(receiver_pred, receiver_trig, PredicateType::RECURRENT);
  }

  // [kwang] what if do I insert delivery_pred and delivery_trig first and
  // receiver_pred and receiver_trig later?
  int last_delivered_worker = num_nodes - 1;
  int last_delivered_msg = -1;

  auto delivery_pred = [&last_delivered_worker, &last_delivered_msg, num_nodes]
    (const mySST& sst) {
    // Compute how many each node's msg(s) have been sent to the other nodes.
    /*
    vector<int> min(num_nodes);
    for (unsigned int i = 0; i < num_nodes; ++i) {
      min[i] = sst.recvd[0][i];
      for (unsigned int j = 1; j < num_nodes; ++j) {
        if (sst.recvd[j][i] < min[i]) {
          min[i] = sst.recvd[j][i];
        }
      }
    }

    unsigned int target_worker = last_delivered_worker + 1;
    unsigned int target_msg = last_delivered_msg;
    if (target_worker == num_nodes) {
      target_worker = 0;
      ++target_msg;
    }
    std::cout << "(" << target_worker << ", " << target_msg << ")" << std::endl;

    vector<int> (num_nodes);
    for (unsigned int i = 0; i < num_nodes; ++i) {
      if (i <= target_worker) {
        if (min[i] < target_msg) {
          return false;
        }
      } else {
        if (min[i] < target_msg - 1) {
          return false;
        }
      }
    }
    for (unsigned int i = 0; i < num_nodes; ++i) {
      std::cout << "min: " << min[i] << " ";
    }
    std::cout << std::endl;
    return true;
    */
    int next_delivering_worker = (last_delivered_worker + 1) % num_nodes;
    int next_delivering_msg = last_delivered_msg +
      ((next_delivering_worker == 0) ? 1 : 0);

    for (unsigned int row = 0; row < num_nodes; ++row) {
      if(sst.recvd[row][next_delivering_worker] < next_delivering_msg) {
        return false;
      }
    }
    return true;
  };

  auto delivery_trig = [&last_delivered_worker, &last_delivered_msg,
       num_nodes](mySST& sst) {
    /*
    unsigned int target_worker = last_delivered_worker + 1;
    unsigned int target_msg = last_delivered_msg;
    if (target_worker == num_nodes) {
      target_worker %= num_nodes;
      ++target_msg;
    }
    */
    int next_delivering_worker = (last_delivered_worker + 1) % num_nodes;
    int next_delivering_msg = last_delivered_msg +
      ((next_delivering_worker == 0) ? 1 : 0);

    std::cout << "I deliver "
      << "(" << next_delivering_worker << ", " << next_delivering_msg << ")."
      << std::endl;
    /*
    last_delivered_worker = target_worker;
    last_delivered_msg = target_msg;
    */
    last_delivered_worker = next_delivering_worker;
    last_delivered_msg = next_delivering_msg;
  };

  sst.predicates.insert(delivery_pred, delivery_trig, PredicateType::RECURRENT);

  while(true) {
  }
  return 0;
}
