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
#include "three_way_buffer.h"

using namespace derecho;
using namespace sst;
using namespace std;

// TODO: Temporary Solution
namespace sst {
char* MSHM;
}

class TWBSST : public SST<TWBSST> {
public:
    TWBSST(const std::vector<uint32_t>& members, uint32_t my_id, uint32_t dimension)
            : SST<TWBSST>(this, SSTParams{members, my_id}),
              ml_parameters(dimension) {
        SSTInit(ml_parameters, round, read_num);
    }
    SSTFieldVector<double> ml_parameters;
    SSTField<uint64_t> round;
    SSTField<uint32_t> read_num;
};

void print(const TWBSST& sst) {
    for(uint row = 0; row < sst.get_num_rows(); ++row) {
        for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
            cout << sst.ml_parameters[row][param] << " ";
        }
        cout << endl;

        cout << sst.round[row] << endl;

        cout << sst.read_num[row] << endl;
    }
    cout << endl;
}

int main(int argc, char* argv[]) {
    //TODO
    MSHM = "some string";

    srand(getpid());

    if(argc < 3) {
        cout << "Usage: " << argv[0] << " <num_nodes> <num_params>" << endl;
        return -1;
    }

    // the number of nodes for this test
    const uint32_t num_nodes = std::stoi(argv[1]);
    const uint32_t num_params = std::stoi(argv[2]);

    uint32_t my_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);

    const std::map<uint32_t, std::pair<ip_addr_t, uint16_t>> ip_addrs_and_ports = initialize(num_nodes);

    // initialize the rdma resources
#ifdef USE_VERBS_API
    verbs_initialize(ip_addrs_and_ports, my_id);
#else
    lf_initialize(ip_addrs_and_ports, my_id);
#endif

    std::cout << "Finished the initialization" << std::endl;
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

    TWBSST sst(members, my_id, num_params);
    uint32_t my_rank = sst.get_local_index();
    // initialization
    for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
        sst.ml_parameters[my_rank][param] = 0;
    }
    sst.round[my_rank] = 0;
    sst.read_num[my_rank] = 0;
    sst.put_with_completion();
    sst.sync_with_members();

    // TWB initialization
    size_t buf_size = sizeof(double) * num_params;
    std::shared_ptr<TWBSST> sst_p(&sst);

    uint32_t server_rank = 0;
    cout << "My rank is " << my_rank << endl;
    if(my_rank == server_rank) {
        ThreeWayBufferForServer<TWBSST> twb(my_id, members, buf_size, sst_p);
        std::function<bool(const TWBSST&)> round_complete = [my_rank, server_rank](const TWBSST& sst) {
            for(uint row = 0; row < sst.get_num_rows(); ++row) {
                // ignore server row
                if(row == server_rank) {
                    continue;
                }
                if(sst.round[row] == sst.round[my_rank]) {
                    return false;
                }
            }
            return true;
        };

        std::function<void(TWBSST&)> compute_average = [my_rank, server_rank, &twb](TWBSST& sst) mutable {
            // cout << "compute_average" << endl;
            // print(sst);
            double* buf = (double*)twb.getbuf();
            for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                double sum = 0;
                for(uint row = 0; row < sst.get_num_rows(); ++row) {
                    // ignore server row
                    if(row == server_rank) {
                        continue;
                    }
                    sum += sst.ml_parameters[row][param];
                }
                buf[param] = sum / (sst.get_num_rows() - 1);
            }
            twb.write();
            sst.round[my_rank]++;
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
            /*
            sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
			*/
        };

        sst.predicates.insert(round_complete, compute_average, PredicateType::RECURRENT);
        while(true) {
        }
    }

    else {
        ThreeWayBufferForWorker<TWBSST> twb(my_id, my_rank, server_id, buf_size, sst_p);
        std::function<bool(const TWBSST&)> server_done = [my_rank, server_rank](const TWBSST& sst) {
            return sst.round[server_rank] == sst.round[my_rank];
        };

        std::function<void(TWBSST&)> compute_new_parameters = [my_rank, &twb](TWBSST& sst) {
            // cout << "compute_new_parameters" << endl;
            // print(sst);
            const double* buf = (const double*)twb.read();
            for(uint param = 0; param < sst.ml_parameters.size(); ++param) {
                sst.ml_parameters[my_rank][param] = buf[param] + rand() % 100;
            }
            sst.put_with_completion((char*)std::addressof(sst.ml_parameters[0][0]) - sst.getBaseAddress(), sizeof(sst.ml_parameters[0][0]) * sst.ml_parameters.size());
            sst.round[my_rank]++;
            sst.put_with_completion((char*)std::addressof(sst.round[0]) - sst.getBaseAddress(), sizeof(sst.round[0]));
        };

        sst.predicates.insert(server_done, compute_new_parameters, PredicateType::RECURRENT);
        while(true) {
        }
    }
}
