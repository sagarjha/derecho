#include "twbsst.h"

TWBSST::TWBSST(const vector<uint32_t> &_members, uint32_t myid, size_t size) :
	SST<TWBSST> (this, SSTParams{_members, myid}),
	buf0(size), buf1(size), buf2(size) {
	SSTInit(buf0, buf1, buf2, read_num, seq_num0, seq_num1, seq_num2);
	//TODO: Initial values
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
