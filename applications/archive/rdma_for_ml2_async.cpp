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
const int PERMISSION = 0666;  //-rw-rw-rw-

using namespace derecho;
using namespace sst;
using namespace std;

namespace sst {
	char *MSHM;
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

inline sem_t* sem_init(const char* name) {
    return sem_open(name, 0);
}

int main(int argc, char* argv[]) {
    ios_base::sync_with_stdio(false);
    srand(getpid());
    std::cerr << "Derecho program starting up" << std::endl;

    if(argc < 8) {
        cout << "Usage: " << argv[0] << " <num_nodes> <num_params> <itemsize> <model_sem_name> <grad_sem_name> <model_shm_name> <grad_shm_name>" << endl;
        return -1;
    }

    // the number of nodes for this test
    const uint32_t num_nodes = std::stoi(argv[1]);
    const uint32_t num_params = std::stoi(argv[2]);
    // const uint32_t itemsize = std::stoi(argv[3]);
    const char* MSEM = argv[4];
    const char* GSEM = argv[5];
    sst::MSHM = argv[6];
    std::string msem(argv[6]);
    std::string buf0 = msem + "_BUF_0";
    std::string buf1 = msem + "_BUF_1";
    std::string buf2 = msem + "_BUF_2";
    // const char* GSHM = argv[7];

    //std::cout << sst::MSHM << std::endl;

    sem_t* model_sem = sem_init(MSEM);
    sem_t* grad_sem = sem_init(GSEM);

    uint32_t my_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);

    //std::cout << my_id << endl;

    const std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> ip_addrs_and_ports = initialize(num_nodes);

    // initialize the rdma resources
#ifdef USE_VERBS_API
    verbs_initialize(ip_addrs_and_ports, my_id);
#else
    lf_initialize(ip_addrs_and_ports, my_id);
#endif

    std::cerr << "init done!" << endl;

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
    sst.put_with_completion();
    sst.sync_with_members();

    uint32_t server_rank = 0;
    double alpha = 0.05;

    // three way buffer initialization
    size_t buffer_size = sizeof(sst.ml_models[0][0]) * num_params;
    std::shared_ptr<MLSST> sst_p(&sst);

    std::unique_ptr<ThreeWayBufferForServer<MLSST>> twbs;
    std::unique_ptr<ThreeWayBufferForWorker<MLSST>> twbw;

    if(my_rank == server_rank) {
        twbs = std::make_unique<ThreeWayBufferForServer<MLSST>>(my_id, members, buffer_size, sst_p);
        // std::cout << "I'm a server" << std::endl;
        for(uint row = 0; row < num_nodes; ++row) {
            if(row == my_rank) {
                continue;
            }

            // std::cout << "row" << row << std::endl;
            std::function<bool(const MLSST&)> worker_gradient_updated = [row, last_round = (uint64_t)0](const MLSST& sst) mutable {
                if(sst.round[row] > last_round) {
                    last_round = sst.round[row];
		    // std::cerr << "Detected worker " << row << "'s gradient updated" << endl;
                    return true;
                }
                return false;
            };

            std::function<void(MLSST&)> update_parameter = [row, my_rank, alpha, &twbs](MLSST& sst) {
                float* buf = (float*)twbs->getbuf();
                for(uint param = 0; param < sst.ml_models.size(); ++param) {
                    //XXX: update gradient, - gradients?
                   buf[param] -= (alpha / (sst.get_num_rows() - 1)) * sst.ml_models[row][param];
                }
                // push the model
                twbs->write();
                //sst.put_with_completion((char*)std::addressof(sst.ml_models[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_models[0][0]) * sst.ml_models.size());
     //           std::cerr << "pushed models to clients" << endl;
            };

            sst.predicates.insert(worker_gradient_updated, update_parameter, PredicateType::RECURRENT);
        }
    } else {
        /**
		 * worker
		 */
        // wait until python objects are moved to shared memory region.
        twbw = std::make_unique<ThreeWayBufferForWorker<MLSST>>(my_id, my_rank, server_id, buffer_size, sst_p);
		twbw->initBuffer((char *)buf0.c_str(), (char *)buf1.c_str(), (char *)buf2.c_str());
        sem_wait(model_sem);
        sem_post(model_sem);
        // std::cerr << "I'm a worker" << endl;

        std::function<bool(const MLSST&)> server_done = [](const MLSST& sst) {
            return true;
        };

        std::function<void(MLSST&)> compute_new_parameters = [my_rank, server_rank, grad_sem, &twbw](MLSST& sst) {
            //std::cerr << "updating new parameter " << endl;

            //TODO: copy from TWB to the server row
            twbw->read();
            // char* tar = (char*)std::addressof(sst.ml_models[server_rank][0]);
            // memcpy(tar, src, sizeof(sst.ml_models[my_rank][0]) * sst.ml_models.size());

            sem_post(grad_sem);
            //std::cerr << "Python turn" << endl;
            sem_wait(grad_sem);

            // push the gradient
            // the reason to -sst.getBaseAddress() is that here we need an offset from base.
            sst.put((char*)std::addressof(sst.ml_models[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_models[my_rank][0]) * sst.ml_models.size());
            sst.round[my_rank]++;
            // push the round number - only after the gradient has been pushed
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[my_rank]));
            //std::cerr << "pushed gradients to server" << endl;
        };

        sst.predicates.insert(server_done, compute_new_parameters, PredicateType::RECURRENT);
    }

    while(true) {
    }
}
