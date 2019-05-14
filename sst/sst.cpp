#include "sst.h"

namespace sst {
char* create_shared_memory(char* name, size_t len) {
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    char* p = NULL;
    std::cout << "name: " << name << " length:" << len << " fd: " << fd << std::endl;
    if(fd >= 0 && ftruncate(fd, len) == 0) {
        p = (char*)mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    } else {
        p = new char[len];
    }
    return p;
}
}  // namespace sst
