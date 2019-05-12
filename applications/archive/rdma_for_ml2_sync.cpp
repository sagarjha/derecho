#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <unistd.h>
#include <vector>

#include "derecho/derecho.h"

#include "sst/poll_utils.h"
#include "sst/sst.h"
#include "initialize.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <cstdint>
const int PERMISSION= 0666; //-rw-rw-rw-
const int PERMISSION_RW = 0666; //-rw-rw-rw-
const int PERMISSION_READONLY = 0666;

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

int setshm(volatile void* ptr, size_t size, key_t shkey, int PERMISSION)
{
    int offset = (std::uintptr_t)ptr % SHMLBA;
    cerr << (std::uintptr_t) ptr << " " << offset << endl;
    size += offset;
    void* aligned_ptr = (char*)ptr - offset;
    int shmid;
	if ((shmid = shmget(shkey, size, IPC_CREAT | PERMISSION)) == -1) {
		perror("shmget: shmget failed");
		exit(-1);
	}
    //cerr << "aligend address: " << *((double*) aligned_ptr) << endl;
	void *ret;
   	if ((ret = shmat(shmid, const_cast<void*>(aligned_ptr), SHM_REMAP)) == (void*)-1) {
        cerr << (std::uintptr_t) aligned_ptr << " " << SHMLBA << " " << (std::uintptr_t) aligned_ptr % SHMLBA << endl;
		perror("shmat: shmat failed");
		exit(-1);
	}
    return offset;
}

void* shmalloc(size_t size, int &shmid, key_t shkey) {
	if ((shmid = shmget(shkey, size, IPC_CREAT | PERMISSION)) == -1) {
		perror("shmget: shmget failed");
		exit(-1);
	}
	void *ret;
   	if ((ret = shmat(shmid, NULL, 0)) == (void*)-1) {
		perror("shmat: shmat failed");
		exit(-1);
	}
	return ret;
}

void shdelete(void *maddr, int shmid) {
	if (shmdt(maddr) == -1) {
		perror("shmdt: shmdt failed");
		exit(-1);
	}
	if (shmctl(shmid, IPC_RMID, NULL) == -1) {
		perror("shmctl: shmctl failed");
		exit(-1);
	}
}
int semalloc(key_t key, int dimension, int permission)
{
    int ret = semget(key, dimension, permission);
    if (ret < 0) {
        perror("Cannot find semaphore, exiting.\n");
        exit(-1);
    }
    return ret;
}

void semacquire(int semid)
{
    struct sembuf op[1];
    op[0].sem_num = 0;
    op[0].sem_op = -1;
    op[0].sem_flg = 0;
    if (semop(semid, op, 1) != 0) {
        perror("Sem acquire failed, exiting. \n");
        exit(-1);
    }
}

void semrelease(int semid)
{
    struct sembuf op[1];
    op[0].sem_num = 0;
    op[0].sem_op = 1;
    op[0].sem_flg = 0;
    if (semop(semid, op, 1) != 0) {
        perror("Sem release failed, exiting. \n");
        exit(-1);
    }
}

int main(int argc, char* argv[]) {
    ios_base::sync_with_stdio(false);
    srand(getpid());
    std::cerr << "Derecho program starting up" << std::endl;

    if(argc < 6) {
        cout << "Usage: " << argv[0] << " <num_nodes> <num_params> <sem_shk_cpp> <sem_shk_py> <sm_shk2cpp> <sm_shk2py>" << endl;
        return -1;
    }

    // the number of nodes for this test
    const uint32_t num_nodes = std::stoi(argv[1]);
    const uint32_t num_params = std::stoi(argv[2]);
    const key_t sem_shk_cpp = std::stoi(argv[3]);
    const key_t sem_shk_py = std::stoi(argv[4]);
    const key_t sm_shk2cpp = std::stoi(argv[5]);
    const key_t sm_shk2py = std::stoi(argv[6]);

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

    int semidcpp = 0, semidpy = 0;
    //double *sm_ptr = NULL;
    uint32_t server_rank = 0;
    int size = num_params * sizeof(double), offset2py, offset2cpp;
    //char* testp = static_cast<char*>(std::aligned_alloc(4 * 1024, size));
//    free(testp);
    //offset2py = setshm(testp, size, sm_shk2py, PERMISSION_READONLY);
    if (my_rank != server_rank) {
        offset2py = setshm(sst.ml_parameters[server_rank], size * members.size(), sm_shk2py, PERMISSION_RW);
    
    //offset2cpp = setshm(sst.ml_parameters[my_rank], size, sm_shk2cpp, PERMISSION_RW);
//    offset = setshm(sst.ml_parameters[my_rank], size, sm_shk2cpp);
    //sm_ptr = (double*)shmalloc(sizeof(double) * size, shmid, sm_shk);
    offset2cpp = offset2py + size;
    semidcpp = semalloc(sem_shk_cpp, 0, PERMISSION);
    semidpy = semalloc(sem_shk_py, 0, PERMISSION);
    std::cout << offset2cpp << endl;
    std::cerr << offset2cpp << " verify: " << sst.ml_parameters[server_rank][0] << " " << sst.ml_parameters[my_rank][0] << endl;
    std::cout << offset2py << endl;
    std::cerr << offset2py << " verify: " << sst.ml_parameters[server_rank][0] << " " << sst.ml_parameters[my_rank][0] << endl;
    std::cerr << "acquiring semaphore" << endl;
    semacquire(semidcpp);
    }

    // sst initialization
    for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
        sst.ml_parameters[my_rank][param] = 0;
    }
    sst.round[my_rank] = 0;
    sst.put_with_completion();
    sst.sync_with_members();

    if(my_rank == server_rank) {
        std::function<bool(const MLSST&)> round_complete = [my_rank] (const MLSST& sst) {
            for(uint row = 0; row < sst.get_num_rows(); ++row) {
                // ignore server row
                if(row == my_rank) {
                    continue;
                }
                if(sst.round[row] == sst.round[my_rank]) {
                    return false;
                }
            }
            //[kwang] If we know the worker's round is mnotonically increasing,
            // then, if the server's round is different from all the worker's
            // round, that implies the round has been completed by all workers.
            std::cerr << "round #" << sst.round[my_rank] << " complete" << endl;
            std::cerr << sst.round[1] << endl;
            return true;
        };

        std::function<void(MLSST&)> compute_average = [my_rank](MLSST& sst) {
            //print(sst);
            for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                double sum = 0;
                for(uint row = 0; row < sst.get_num_rows(); ++row) {
                    // ignore server row
                    if(row == my_rank) {
                        continue;
                    }
                    sum += sst.ml_parameters[row][param];
                }
                sst.ml_parameters[my_rank][param] = sum / (sst.get_num_rows() - 1);
                if (param <= 10) {
                    std::cerr << sst.ml_parameters[my_rank][param] << endl;
                }
            }
            sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
            sst.round[my_rank]++;
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
        };

        sst.predicates.insert(round_complete, compute_average, PredicateType::RECURRENT);
    }
    else {
        std::function<bool(const MLSST&)> server_done = [my_rank, server_rank](const MLSST& sst) {
            return sst.round[server_rank] == sst.round[my_rank];
        };

        std::function<void(MLSST&)> compute_new_parameters = [my_rank, server_rank, semidcpp, semidpy](MLSST& sst) {
            //print(sst);
            std::cerr << "updating new paras " << semidcpp << endl;
            for(uint param = 0; param <= 10 && param < sst.ml_parameters.size(); ++param) {
                std::cerr << sst.ml_parameters[server_rank][param] << " ";
            }
            std::cerr << endl;
            std::cerr << " " << sst.ml_parameters.size() << " " << sst.ml_parameters[server_rank][0] << std::endl;
            semrelease(semidpy);
        std::cerr << "acquiring the lock" <<endl;
            semacquire(semidcpp);
            std::cerr << "reading new paras" << endl;
            for(uint param = 0; param <= 10 && param < sst.ml_parameters.size(); ++param) {
                std::cerr << sst.ml_parameters[my_rank][param] << " ";
            }
            std::cerr << endl;

            sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
            sst.round[my_rank]++;
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
        };

        //std::cerr << "Im a worker with keys: " << sm_shk << " " << sem_shk << endl;
        // shared stuff setup

        std::cerr << "releasing the lock" << endl;
        semrelease(semidpy);
        std::cerr << "acquiring the lock" <<endl;
        semacquire(semidcpp);
        std::cerr << "loading new paras" << endl;
        for(uint param = 0; param <= 10 && param < sst.ml_parameters.size(); ++param) {
            std::cerr << sst.ml_parameters[my_rank][param] << " ";
            // double tmp;
            // std::cin >> tmp;
            // sst.ml_parameters[my_rank][param] = tmp;
            //sst.ml_parameters[my_rank][param] = rand() % 100;
        }
        std::cerr << endl;

        sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
        sst.round[my_rank]++;
        sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
        sst.predicates.insert(server_done, compute_new_parameters, PredicateType::RECURRENT);
    }

    while(true) {
    }
}
