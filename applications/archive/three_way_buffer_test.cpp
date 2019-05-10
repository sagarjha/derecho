#include <iostream>

#include "sst/sst.h"

class mySST : public SST<mySST> {
    SSTField<uint32_t> read_num;
    // write the constructor
};

int main() {
  // initialization here


  if (my_id == server_id) {
    ThreeWayBufferForServer twb;
  }
  else {

  }
}
