#include "twbsst.h"

template <typename SSTType>
ThreeWayBufferForServer<SSTType>::ThreeWayBufferForServer(uint32_t my_id, const vector<uint32_t>& members,
                                                          size_t buf_size, std::shared_ptr<SSTType> sst)
        : members(members),
          my_id(my_id),
          source_buffer(make_unique<volatile char[]>(buf_size)),
          res_vec0(members.size()),
          res_vec1(members.size()),
          res_vec2(members.size()),
          seq_num0(members.size()),
          seq_num1(members.size()),
          seq_num2(members.size()),
          sst(sst) {
    for(int worker_index = 0; i < members.size(); ++worker_index) {
        if(members[worker_index] == my_id) {
            continue;
        }
        res_vec0[worker_index] = std::make_unique(members[worker_index], source_buffer.get(), source_buffer.get(), buf_size, buf_size, my_id < members[workder_index]);
        res_vec1[worker_index] = std::make_unique(members[worker_index], source_buffer.get(), source_buffer.get(), buf_size, buf_size, my_id < members[workder_index]);
        res_vec2[worker_index] = std::make_unique(members[worker_index], source_buffer.get(), source_buffer.get(), buf_size, buf_size, my_id < members[workder_index]);
    }
}

template <typename SSTType>
void ThreeWayBufferForServer<SSTType>::write() {
  for (int worker_index = 0; i < members.size(); ++worker_index) {
        if(members[worker_index] == my_id) {
            continue;
        }
	sst.read_num[worker_index]
}
}

const SSTFieldVector<char>& TWBSST::read(uint32_t myid) {
	uint32_t ts[3] = {seq_num0[myid], seq_num1[myid], seq_num2[myid]};
	int choice = max_element(ts, ts + 3) - ts;
	read_num[myid] = choice;
	put_with_completion((char*)std::addressof(read_num[0]) - getBaseAddress(), sizeof(read_num[0]));
	if (choice == 0) {
		return buf0;
	} else if (choice == 1) {
		return buf1;
	} else {
		return buf2;
	}
}

SSTFieldVector<char>& TWBSST::write_begin(uint32_t wid, void* &p) {
	uint32_t ts[3] = {seq_num0[wid], seq_num1[wid], seq_num2[wid]};
	uint32_t rn = read_num[wid];
	if (0 <= rn && rn <= 2) {
		ts[rn] = UINT_MAX;
	}
	int choice = min_element(ts, ts + 3) - ts;
	if (choice == 0) {
		p = &seq_num0;
		return buf0;
	} else if (choice == 1) {
		p = &seq_num1;
		return buf1;
	} else {
		p = &seq_num2;
		return buf2;
	}
}

void TWBSST::write_end(uint32_t wid, void* p) {
	uint32_t ts[3] = {seq_num0[wid], seq_num1[wid], seq_num2[wid]};
	uint32_t nts = *max_element(ts, ts + 3) + 1;
	SSTField<uint32_t> &seq = *((SSTField<uint32_t>*)p);	
	seq[wid] = nts;
	put_with_completion((char*)std::addressof(seq[0]) - getBaseAddress(), sizeof(seq[0]));
}

int main(int args, char* argv[]) {
	return 0;
}
