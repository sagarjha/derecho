#pragma once

#include <vector>

#include "derecho/derecho.h"

#include "sst/lf.h"

using namespace derecho;
using namespace sst;

// TODO: Use a new type which wraps a header that includes the seq_num of the buffer

struct BufferWithSeq {
	uint32_t seq;
	// variable size array
	char buf[0];

	BufferWithSeq() : seq(0) {}
};

template <typename SSTType>
class ThreeWayBufferForServer {
    std::vector<uint32_t> members;
    uint32_t my_id, cur_seq;
    size_t buf_size, buf_seq_size;
    std::unique_ptr<volatile char[]> source_buffer;
    std::vector<std::unique_ptr<resources>> res_vec0, res_vec1, res_vec2;
	// local sequence numbers
	std::vector<uint32_t> seq_vec0, seq_vec1, seq_vec2;
    // assuming that sst has a field called read_num
    std::shared_ptr<SSTType> sst;

public:
    ThreeWayBufferForServer(uint32_t my_id, const std::vector<uint32_t>& members, size_t buf_size,
                            std::shared_ptr<SSTType> sst);
	volatile char* getbuf() const;
    void write();
};

template <typename SSTType>
class ThreeWayBufferForWorker {
	uint32_t my_id, server_id;
    size_t buf_size, buf_seq_size;
    std::unique_ptr<volatile char[]> buffer0, buffer1, buffer2;
    std::unique_ptr<resources> res0, res1, res2;
    std::shared_ptr<SSTType> sst;

public:
    ThreeWayBufferForWorker(uint32_t my_id, uint32_t server_id, size_t buf_size, 
							std::shared_ptr<SSTType> sst);
    const char* read() const;
};
