#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <unistd.h>
#include <vector>

#include "derecho/derecho.h"

#include "initialize.h"
#include "sst/poll_utils.h"
#include "sst/sst.h"

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
const int PERMISSION = 0666;  //-rw-rw-rw-

using namespace derecho;
using namespace sst;
using namespace std;

class MLSST : public SST<MLSST> {
public:
    MLSST(const std::vector<uint32_t>& members, uint32_t my_id, uint32_t dimension)
            : SST<MLSST>(this, SSTParams{members, my_id}),
              ml_parameters(dimension) {
        SSTInit(ml_parameters, round);
    }
    SSTFieldVector<double> ml_parameters;
    SSTField<uint64_t> round;
};

void print(const MLSST& sst) {
    for(uint row = 0; row < sst.get_num_rows(); ++row) {
        for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
            cout << sst.ml_parameters[row][param] << " ";
        }
        cout << endl;

        cout << sst.round[row] << endl;
    }
    cout << endl;
}

void* shmalloc(size_t size, int& shmid, key_t shkey) {
    if((shmid = shmget(shkey, size, IPC_CREAT | PERMISSION)) == -1) {
        perror("shmget: shmget failed");
        exit(-1);
    }
    void* ret;
    if((ret = shmat(shmid, NULL, 0)) == (void*)-1) {
        perror("shmat: shmat failed");
        exit(-1);
    }
    return ret;
}

void shdelete(void* maddr, int shmid) {
    if(shmdt(maddr) == -1) {
        perror("shmdt: shmdt failed");
        exit(-1);
    }
    if(shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl: shmctl failed");
        exit(-1);
    }
}
int semalloc(key_t key, int dimension, int permission) {
    int ret = semget(key, dimension, permission);
    if(ret < 0) {
        perror("Cannot find semaphore, exiting.\n");
        exit(-1);
    }
    return ret;
}

void semacquire(int semid) {
    struct sembuf op[1];
    op[0].sem_num = 0;
    op[0].sem_op = -1;
    op[0].sem_flg = 0;
    if(semop(semid, op, 1) != 0) {
        perror("Sem acquire failed, exiting. \n");
        exit(-1);
    }
}

void semrelease(int semid) {
    struct sembuf op[1];
    op[0].sem_num = 0;
    op[0].sem_op = 1;
    op[0].sem_flg = 0;
    if(semop(semid, op, 1) != 0) {
        perror("Sem release failed, exiting. \n");
        exit(-1);
    }
}

int main(int argc, char* argv[]) {
    ios_base::sync_with_stdio(false);
    srand(getpid());
    std::cerr << "Derecho program starting up" << std::endl;

    if(argc < 5) {
        cout << "Usage: " << argv[0] << " <num_nodes> <num_params> <sem_shk> <sm_shk>" << endl;
        return -1;
    }

    // the number of nodes for this test
    const uint32_t num_nodes = std::stoi(argv[1]);
    const uint32_t num_params = std::stoi(argv[2]);
    const key_t sem_shk = std::stoi(argv[3]);
    const key_t sm_shk = std::stoi(argv[4]);

    uint32_t my_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);

    std::cout << my_id << endl;
    
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
    for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
        sst.ml_parameters[my_rank][param] = 0;
    }
    sst.round[my_rank] = 0;
    sst.sync_with_members();

    int semid = 0;
    double* sm_ptr = NULL;
    uint32_t server_rank = 0;
    int size = num_params, shmid;
    double alpha = 0.05;

    sm_ptr = (double*)shmalloc(sizeof(double) * size, shmid, sm_shk);
    semid = semalloc(sem_shk, 1, PERMISSION);

    if(my_rank == server_rank) {
        for(uint row = 0; row < num_nodes; ++row) {
            if(row == my_rank) {
                continue;
            }

            std::function<bool(const MLSST&)> worker_gradient_updated = [row, last_round = (uint64_t)0](const MLSST& sst) mutable {
                if(sst.round[row] > last_round) {
                    last_round = sst.round[row];
                    std::cerr << "Detected worker" << row << "'s gradient updated" << endl;
                    return true;
                }
                return false;
            };

            std::function<void(MLSST&)> update_parameter = [row, my_rank, alpha](MLSST& sst) {
                for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                    sst.ml_parameters[my_rank][param] -= (alpha / (sst.get_num_rows() - 1)) * sst.ml_parameters[row][param];
                }
                sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
                sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
            };

            sst.predicates.insert(worker_gradient_updated, update_parameter, PredicateType::RECURRENT);
        }
    } else {
        std::cerr << "Im a worker with keys: " << sm_shk << " " << sem_shk << endl;
        // shared stuff setup

        std::cerr << "releasing the lock" << endl;
        semrelease(semid);
        std::cerr << "acquiring the lock" << endl;
        semacquire(semid);
        std::cerr << "loading new paras" << endl;
        for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
            sst.ml_parameters[my_rank][param] = sm_ptr[param];
            // double tmp;
            // std::cin >> tmp;
            // sst.ml_parameters[my_rank][param] = tmp;
            //sst.ml_parameters[my_rank][param] = rand() % 100;
        }

        sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
        sst.round[my_rank]++;
        sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));

        std::function<bool(const MLSST&)> server_done = [](const MLSST& sst) {
            return true;
        };

        std::function<void(MLSST&)> compute_new_parameters = [my_rank, server_rank, semid, sm_ptr](MLSST& sst) {
            //print(sst);
            std::cerr << "updating new parameter " << semid << " " << sm_ptr << endl;
            for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                sm_ptr[param] = sst.ml_parameters[server_rank][param];
            }
            std::cerr << " " << sst.ml_parameters.size() << " " << sst.ml_parameters[server_rank][0] << std::endl;
            semrelease(semid);
            std::cerr << "Compute new gradient with paritioned data and optimizer in python" << semid << " " << sm_ptr << endl;
            semacquire(semid);
            std::cerr << "reading the new updated grandient by python" << endl;
            for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                sst.ml_parameters[my_rank][param] = sm_ptr[param];
                // double tmp;
                // std::cin >> tmp;
                // sst.ml_parameters[my_rank][param] = tmp;
                //sst.ml_parameters[my_rank][param] = rand() % 100;
            }

            sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
            sst.round[my_rank]++;
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
        };

        sst.predicates.insert(server_done, compute_new_parameters, PredicateType::RECURRENT);
    }

    while(true) {
    }
}
