#pragma once

#include <vector>

#include "derecho/derecho.h"

#include "sst/lf.h"

using namespace derecho;
using namespace sst;
using namespace std;

template <typename SSTType>
class ThreeWayBufferForServer {
    std::vector<uint32_t> members;
    uint32_t my_id;
    std::unique_ptr<volatile char[]> source_buffer;
    size_t buf_size;
    std::vector<std::unique_ptr<resources>> res_vec0, res_vec1, res_vec2;
    std::vector<size_t> seq_num0, seq_num1, seq_num2;
    // assuming that sst has a field called read_num
    std::shared_ptr<SSTType> sst;

public:
    ThreeWayBufferForServer(uint32_t my_id, const vector<uint32_t>& members, size_t buf_size,
                            std::shared_ptr<SSTType> sst);
    void write();
};

template <typename SSTType>
class ThreeWayBufferForWorker {
    std::unique_ptr<volatile char[]> buffer0, buffer1, buffer2;
    size_t buf_size;
    std::unique_ptr<resources> res0, res1, res2;
    std::shared_ptr<SSTType> sst;

public:
    ThreeWayBufferForWorker(uint32_t my_id, uint32_t server_id, size_t buf_size);
    const char* read();
};
