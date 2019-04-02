#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>

#include "initialize.h"
#include "sst/poll_utils.h"
#include "sst/sst.h"
//Since all SST instances are named sst, we can use this convenient hack
#define LOCAL sst.get_local_index()

using std::cin;
using std::cout;
using std::endl;
using std::map;
using std::ofstream;
using std::string;
using std::vector;

using namespace sst;

class mySST : public SST<mySST> {
public:
    mySST(const vector<uint32_t>& _members, uint32_t my_rank) : SST<mySST>(this, SSTParams{_members, my_rank}) {
        SSTInit(a, heartbeat);
    }
    SSTField<int> a;
    SSTField<bool> heartbeat;
};

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cout << "Error: Expected number of nodes as the first argument" << std::endl;
        return -1;
    }
    const uint32_t num_nodes = std::atoi(argv[1]);
    const uint32_t node_id = derecho::getConfUInt32(CONF_DERECHO_LOCAL_ID);
    // initialize the rdma resources
#ifdef USE_VERBS_API
    verbs_initialize(initialize(num_nodes), node_id);
#else
    lf_initialize(initialize(num_nodes), node_id);
#endif
    
    // form a group with a subset of all the nodes
    vector<uint32_t> members(num_nodes);
    for(unsigned int i = 0; i < num_nodes; ++i) {
        members[i] = i;
    }

    // create a new shared state table with all the members
    mySST sst(members, node_id);
    sst.a[node_id] = 0;
    sst.put((char*)std::addressof(sst.a[0]) - sst.getBaseAddress(), sizeof(int));

    auto check_failures_loop = [&sst]() {
        pthread_setname_np(pthread_self(), "check_failures");
        while(true) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            sst.put_with_completion((char*)std::addressof(sst.heartbeat[0]) - sst.getBaseAddress(), sizeof(bool));
        }
    };

    std::thread failures_thread = std::thread(check_failures_loop);

    bool if_exit = false;
    // wait till all a's are 0
    while(if_exit == false) {
        if_exit = true;
        for(unsigned int i = 0; i < num_nodes; ++i) {
            if(sst.a[i] != 0) {
                if_exit = false;
            }
        }
    }

    for(unsigned int i = 0; i < num_nodes; ++i) {
        if(i == node_id) {
            continue;
        }
        sync(i);
    }

    struct timespec start_time;

    // the predicate
    auto f = [num_nodes](const mySST& sst) {
        for(unsigned int i = 0; i < num_nodes; ++i) {
            if(sst.a[i] < sst.a[LOCAL]) {
                return false;
            }
        }
        return true;
    };

    // trigger. Increments self value
    auto g = [&start_time](mySST& sst) {
        ++(sst.a[LOCAL]);
        sst.put((char*)std::addressof(sst.a[0]) - sst.getBaseAddress(), sizeof(int));
        if(sst.a[LOCAL] == 1000000) {
            // end timer
            struct timespec end_time;
            clock_gettime(CLOCK_REALTIME, &end_time);
            // my_time is time taken to count
            double my_time = ((end_time.tv_sec * 1e9 + end_time.tv_nsec) - (start_time.tv_sec * 1e9 + start_time.tv_nsec)) / 1e9;
            int node_id = sst.get_local_index();
            // node 0 finds the average by reading all the times taken by remote nodes
            // Anyway, the values will be quite close as the counting is synchronous
            if(node_id == 0) {
                int num_nodes = sst.get_num_rows();
                resources* res;
                double times[num_nodes];
                const auto tid = std::this_thread::get_id();
                // get id first
                uint32_t id = util::polling_data.get_index(tid);
                util::polling_data.set_waiting(tid);

                // read the other nodes' time
                for(int i = 0; i < num_nodes; ++i) {
                    if(i == node_id) {
                        times[i] = my_time;
                    } else {
                        res = new resources(i, (char*)&my_time, (char*)&times[i], sizeof(double), sizeof(double), node_id < i);
                        res->post_remote_read(id, sizeof(double));
                        free(res);
                    }
                }
                for(int i = 0; i < num_nodes; ++i) {
                    util::polling_data.get_completion_entry(tid);
                }
                util::polling_data.reset_waiting(tid);

                double sum = 0.0;
                // compute the average
                for(int i = 0; i < num_nodes; ++i) {
                    sum += times[i];
                }
                ofstream fout;
                fout.open("data_count_write", ofstream::app);
                fout << num_nodes << " " << sum / num_nodes << endl;
                fout.close();

                // sync to tell other nodes to exit
                for(int i = 0; i < num_nodes; ++i) {
                    if(i == node_id) {
                        continue;
                    }
                    sync(i);
                }
            } else {
                resources* res;
                double no_need;
                res = new resources(0, (char*)&my_time, (char*)&no_need, sizeof(double), sizeof(double), 0);
                sync(0);
                free(res);
            }
#ifdef USE_VERBS_API
            verbs_destroy();
#else
            lf_destroy();
#endif
            exit(0);
        }
    };

    // start timer
    clock_gettime(CLOCK_REALTIME, &start_time);

    // register as a recurring predicate
    sst.predicates.insert(f, g, PredicateType::RECURRENT);

    while(true) {
    }
    return 0;
}
