#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <semaphore.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "derecho/derecho.h"

#include "initialize.h"
#include "sst/poll_utils.h"
#include "sst/sst.h"

#include "three_way_buffer.h"

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

using namespace derecho;
using namespace sst;
using namespace std;

namespace sst {
char* MSHM;
}

class MLSST : public SST<MLSST> {
public:
    MLSST(const std::vector<uint32_t>& members, uint32_t my_id, uint32_t dimension)
            : SST<MLSST>(this, SSTParams{members, my_id}),
              ml_models(dimension) {
        SSTInit(ml_models, round, read_num);
    }
    SSTFieldVector<float> ml_models;
    SSTField<uint64_t> round;
    SSTField<uint32_t> read_num;
};

void print(const MLSST& sst) {
    for(uint row = 0; row < sst.get_num_rows(); ++row) {
        for(uint param = 0; param < sst.ml_models.size(); ++param) {
            cout << sst.ml_models[row][param] << " ";
        }
        cout << endl;

        cout << sst.round[row] << endl;

        cout << sst.read_num[row] << endl;
    }
    cout << endl;
}

sem_t* init_sem(const char* sem_name) {
    return sem_open(sem_name, 0);
}

int main(int argc, char* argv[]) {
    ios_base::sync_with_stdio(false);
    srand(getpid());
    std::cout << "Derecho program starting up" << std::endl;

    if(argc < 7) {
        cout << "Usage: " << argv[0]
             << " <num_nodes> <num_params> <python_sem_name>"
             << " <cpp_sem_name> <model_shm_name> <grad_shm_name>"
             << endl;
        return 1;
    }

    // the number of nodes for this test
    const uint32_t num_nodes = std::stoi(argv[1]);
    const uint32_t num_params = std::stoi(argv[2]);
    const char* python_sem_name = argv[3];
    const char* cpp_sem_name = argv[4];
    sst::MSHM = argv[5];
    std::string msem(argv[5]);
    std::string buf0 = msem + "_BUF_0";
    std::string buf1 = msem + "_BUF_1";
    std::string buf2 = msem + "_BUF_2";

    sem_t* python_sem = init_sem(python_sem_name);
    sem_t* cpp_sem = init_sem(cpp_sem_name);

    uint32_t my_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);

    const std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> ip_addrs_and_ports = initialize(num_nodes);

    // initialize the rdma resources
#ifdef USE_VERBS_API
    verbs_initialize(ip_addrs_and_ports, my_id);
#else
    lf_initialize(ip_addrs_and_ports, my_id);
#endif

    std::cout << "init done!" << endl;

    uint32_t server_id = ip_addrs_and_ports.begin()->first;
    std::vector<uint32_t> members;
    if(my_id == server_id) {
        for(auto p : ip_addrs_and_ports) {
            members.push_back(p.first);
        }
    } else {
        members.push_back(server_id);
        members.push_back(my_id);
    }

    MLSST sst(members, my_id, num_params);
    uint32_t my_rank = sst.get_local_index();
    // initialization
    sst.round[my_rank] = 0;
    sst.read_num[my_rank] = 0;

    uint32_t server_rank = 0;
    double alpha = 0.05;

    // three way buffer initialization
    size_t buffer_size = sizeof(sst.ml_models[0][0]) * num_params;
    std::shared_ptr<MLSST> sst_p(&sst);

    std::unique_ptr<ThreeWayBufferForServer<MLSST>> twbs;
    std::unique_ptr<ThreeWayBufferForWorker<MLSST>> twbw;

    if(my_rank == server_rank) {
        twbs = std::make_unique<ThreeWayBufferForServer<MLSST>>(my_id, members, buffer_size, sst_p);
        sst.put_with_completion();
        sst.sync_with_members();
        for(uint row = 0; row < num_nodes; ++row) {
            if(row == my_rank) {
                continue;
            }

            std::function<bool(const MLSST&)> worker_gradient_updated = [row, last_round = (uint64_t)0](const MLSST& sst) mutable {
                if(sst.round[row] > last_round) {
                    last_round = sst.round[row];
                    return true;
                }
                return false;
            };

            std::function<void(MLSST&)> update_parameter = [row, my_rank, alpha, &twbs](MLSST& sst) {
                float* buf = (float*)twbs->getbuf();
                for(uint param = 0; param < sst.ml_models.size(); ++param) {
                    buf[param] -= (alpha / (sst.get_num_rows() - 1)) * sst.ml_models[row][param];
                }
                // push the model
                twbs->write();
            };

            sst.predicates.insert(worker_gradient_updated, update_parameter, PredicateType::RECURRENT);
        }
    } else {
        /**
	 * worker
	 */

        twbw = std::make_unique<ThreeWayBufferForWorker<MLSST>>(my_id, my_rank, server_id, buffer_size, sst_p);
        twbw->initBuffer((char*)buf0.c_str(), (char*)buf1.c_str(), (char*)buf2.c_str());
        sst.put_with_completion();
        sst.sync_with_members();

        std::function<bool(const MLSST&)> server_done = [](const MLSST& sst) {
            return true;
        };

        std::function<void(MLSST&)> compute_new_parameters = [my_rank, server_rank,
                                                              python_sem, cpp_sem,
                                                              &twbw](MLSST& sst) {
            twbw->read();

            sem_post(cpp_sem);
            sem_wait(python_sem);

            // push the gradient
            sst.put((char*)std::addressof(sst.ml_models[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_models[my_rank][0]) * sst.ml_models.size());
            sst.round[my_rank]++;
            // push the round number - only after the gradient has been pushed
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[my_rank]));
        };

        sst.predicates.insert(server_done, compute_new_parameters, PredicateType::RECURRENT);
    }

    while(true) {
    }
}
