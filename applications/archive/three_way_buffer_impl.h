#pragma once

#include "three_way_buffer.h"
#include <algorithm>
#include <iostream>

using namespace derecho;
using namespace sst;

template <typename SSTType>
ThreeWayBufferForServer<SSTType>::ThreeWayBufferForServer(uint32_t my_id, const std::vector<uint32_t>& members,
                                                          size_t buf_size, std::shared_ptr<SSTType> sst)
        : members(members),
          my_id(my_id),
          cur_seq(0),
          buf_size(buf_size),
          buf_seq_size(sizeof(uint32_t) + buf_size),
          source_buffer(std::make_unique<volatile char[]>(buf_seq_size)),
          res_vec0(members.size()),
          res_vec1(members.size()),
          res_vec2(members.size()),
          seq_vec0(members.size(), 0),
          seq_vec1(members.size(), 0),
          seq_vec2(members.size(), 0),
          sst(sst) {
    for(int worker_index = 0; worker_index < (int)members.size(); ++worker_index) {
        if(members[worker_index] == my_id) {
            continue;
        }
        res_vec0[worker_index] = std::make_unique<resources>(members[worker_index], const_cast<char*>(source_buffer.get()), const_cast<char*>(source_buffer.get()), buf_seq_size, buf_seq_size, my_id < members[worker_index]);
        res_vec1[worker_index] = std::make_unique<resources>(members[worker_index], const_cast<char*>(source_buffer.get()), const_cast<char*>(source_buffer.get()), buf_seq_size, buf_seq_size, my_id < members[worker_index]);
        res_vec2[worker_index] = std::make_unique<resources>(members[worker_index], const_cast<char*>(source_buffer.get()), const_cast<char*>(source_buffer.get()), buf_seq_size, buf_seq_size, my_id < members[worker_index]);
    }
}

template <typename SSTType>
volatile char* ThreeWayBufferForServer<SSTType>::getbuf() const {
    return ((volatile BufferWithSeq*)(source_buffer.get()))->buf;
}

template <typename SSTType>
void ThreeWayBufferForServer<SSTType>::write() {
    /* 
		TODO: poll for completion https://github.com/sagarjha/derecho/blob/rdma_for_ml/sst/sst_impl.h#L206
		lowered priority because of pratical reasons
		race condition: if the source_buffer gets updated
	*/
    uint32_t nseq = ((volatile BufferWithSeq*)(source_buffer.get()))->seq++;
    for(int worker_index = 0; worker_index < (int)members.size(); ++worker_index) {
        if(members[worker_index] == my_id) {
            continue;
        }

        //Choose the earliest buffer that is not being read
        uint32_t seq_nums[3] = {seq_vec0[worker_index], seq_vec1[worker_index], seq_vec2[worker_index]};
        uint32_t read_num = sst->read_num[worker_index];
        if(0 <= read_num && read_num <= 2) {
            seq_nums[read_num] = UINT_MAX;
        }
        int buffer_id = std::min_element(seq_nums, seq_nums + 3) - seq_nums;
        std::unique_ptr<resources>* buf = NULL;
        if(buffer_id == 0) {
            buf = &res_vec0[worker_index];
            seq_vec0[worker_index] = nseq;
        } else if(buffer_id == 1) {
            buf = &res_vec1[worker_index];
            seq_vec1[worker_index] = nseq;
        } else if(buffer_id == 2) {
            buf = &res_vec2[worker_index];
            seq_vec2[worker_index] = nseq;
        }
        std::cout << "TWB.write() " << buf_seq_size << std::endl;
        (*buf)->post_remote_write(buf_seq_size);
    }
}

template <typename SSTType>
ThreeWayBufferForWorker<SSTType>::ThreeWayBufferForWorker(uint32_t my_id, uint32_t server_id, size_t buf_size,
                                                          std::shared_ptr<SSTType> sst)
        : my_id(my_id),
          server_id(server_id),
          buf_size(buf_size),
          buf_seq_size(sizeof(uint32_t) + buf_size),
          buffer0(std::make_unique<volatile char[]>(buf_seq_size)),
          buffer1(std::make_unique<volatile char[]>(buf_seq_size)),
          buffer2(std::make_unique<volatile char[]>(buf_seq_size)),
          sst(sst) {
    res0 = std::make_unique<resources>(server_id, const_cast<char*>(buffer0.get()), const_cast<char*>(buffer0.get()), buf_seq_size, buf_seq_size, my_id == server_id);
    res1 = std::make_unique<resources>(server_id, const_cast<char*>(buffer1.get()), const_cast<char*>(buffer1.get()), buf_seq_size, buf_seq_size, my_id == server_id);
    res2 = std::make_unique<resources>(server_id, const_cast<char*>(buffer2.get()), const_cast<char*>(buffer2.get()), buf_seq_size, buf_seq_size, my_id == server_id);
}

template <typename SSTType>
const char* ThreeWayBufferForWorker<SSTType>::read() const {
    uint32_t seq_nums[3] = {
            ((volatile BufferWithSeq*)(buffer0.get()))->seq,
            ((volatile BufferWithSeq*)(buffer1.get()))->seq,
            ((volatile BufferWithSeq*)(buffer2.get()))->seq};
    int buffer_id = std::max_element(seq_nums, seq_nums + 3) - seq_nums;
    sst->read_num[my_id] = buffer_id;
    sst->put_with_completion((char*)std::addressof(sst->read_num[0]) - sst->getBaseAddress(), sizeof(sst->read_num[0]));
    const char* buf = NULL;
    if(buffer_id == 0) {
        buf = (const char*)(((volatile BufferWithSeq*)(buffer0.get()))->buf);
    } else if(buffer_id == 1) {
        buf = (const char*)(((volatile BufferWithSeq*)(buffer1.get()))->buf);
    } else if(buffer_id == 2) {
        buf = (const char*)(((volatile BufferWithSeq*)(buffer2.get()))->buf);
    }
    return buf;
}
