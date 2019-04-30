#pragma once

#include<vector>

#include "derecho/derecho.h"

#include "sst/sst.h"

using namespace derecho;
using namespace sst;
using namespace std;

class TWBSST : public SST<TWBSST> {
public:

	TWBSST(const vector<uint32_t> &_members, uint32_t myid, size_t buf_size);

	const SSTFieldVector<char>& read(uint32_t myid);
	SSTFieldVector<char>& write_begin(uint32_t wid, void* &p);
	void write_end(uint32_t wid, void* p);

private:
	SSTFieldVector<char> buf0, buf1, buf2;
	SSTField<uint32_t> read_num, seq_num0, seq_num1, seq_num2;

};

