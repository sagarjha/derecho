#pragma once

#include <atomic>
#include <bitset>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#include "predicates.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef USE_VERBS_API
#include "verbs.h"
#else  //LIBFABRIC
#include "lf.h"
#endif

using sst::resources;

namespace sst {

const int alignTo = sizeof(long);

constexpr int padded_len(const int& len) {
    return (len < alignTo) ? alignTo : (len + alignTo) | (alignTo - 1);
}

/** Internal helper class, never exposed to the client. */
class _SSTField {
public:
    volatile char* base;
    int rowLen;
    int field_len;

    _SSTField(const int field_len) : base(nullptr), rowLen(0), field_len(field_len) {}

    int set_base(volatile char* const base) {
        this->base = base;
        return padded_len(field_len);
    }

    char* get_base() {
        return const_cast<char*>(base);
    }

    void set_rowLen(const int& _rowLen) { rowLen = _rowLen; }
};

/**
 * Clients should use instances of this class with the appropriate template
 * parameter to declare fields in their SST; for example, SSTField<int> is the
 * type of an integer-valued SST field.
 */
template <typename T>
class SSTField : public _SSTField {
public:
    using _SSTField::base;
    using _SSTField::field_len;
    using _SSTField::rowLen;

    SSTField() : _SSTField(sizeof(T)) {
    }

    // Tracks down the appropriate row
    volatile T& operator[](const int row_idx) const { return ((T&)base[row_idx * rowLen]); }

    // Getter
    volatile T const& operator()(const int row_idx) const {
        return *(T*)(base + row_idx * rowLen);
    }

    // Setter
    void operator()(const int row_idx, T const v) { *(T*)(base + row_idx * rowLen) = v; }
};

/**
 * Clients should use instances of this class to declare vector-like fields in
 * their SST; the template parameter is the type of the vector's elements, just
 * like with std::vector. Unlike std::vector, these are fixed-size arrays and
 * cannot grow or shrink after construction.
 */
template <typename T>
class SSTFieldVector : public _SSTField {
private:
    const size_t length;

public:
    using _SSTField::base;
    using _SSTField::field_len;
    using _SSTField::rowLen;
    using value_type = T;

    SSTFieldVector(size_t num_elements) : _SSTField(num_elements * sizeof(T)), length(num_elements) {
    }

    // Tracks down the appropriate row
    volatile T* operator[](const int& idx) const { return (T*)(base + idx * rowLen); }

    /** Just like std::vector::size(), returns the number of elements in this vector. */
    size_t size() const { return length; }

    void __attribute__((noinline)) debug_print(int row_num) {
        volatile T* arr = (*this)[row_num];
        for(unsigned int j = 0; j < length; ++j) {
            std::cout << arr[j] << " ";
        }
        std::cout << std::endl;
    }
};

typedef std::function<void(uint32_t)> failure_upcall_t;

/** Constructor parameter pack for SST. */
struct SSTParams {
    const std::vector<uint32_t>& members;
    const uint32_t my_node_id;
    const failure_upcall_t failure_upcall;
    const std::vector<char> already_failed;
    const bool start_predicate_thread;

    /**
     *
     * @param _members A vector of node IDs, each of which represents a node
     * participating in the SST. The order of nodes in this vector is the order
     * in which their rows will appear in the SST.
     * @param my_node_id The ID of the local node
     * @param failure_upcall The function to call when SST detects that a
     * remote node has failed.
     * @param already_failed A boolean vector indicating whether a node
     * identified in members has already failed at the time this SST is
     * constructed (i.e. already_failed[i] is true if members[i] has failed).
     * @param start_predicate_thread Whether the predicate evaluation thread
     * should be started immediately on construction of the SST. If false,
     * predicate evaluation will not start until start_predicate_evalution()
     * is called.
     */
    SSTParams(const std::vector<uint32_t>& _members,
              const uint32_t my_node_id,
              const failure_upcall_t failure_upcall = nullptr,
              const std::vector<char> already_failed = {},
              const bool start_predicate_thread = true)
            : members(_members),
              my_node_id(my_node_id),
              failure_upcall(failure_upcall),
              already_failed(already_failed),
              start_predicate_thread(start_predicate_thread) {}
};

char* create_shared_memory(char* name, size_t len);

template <class DerivedSST>
class SST {
private:
    template <typename... Fields>
    void init_SSTFields(Fields&... fields) {
        rowLen = 0;
        compute_rowLen(rowLen, fields...);
        // rows = new char[rowLen * num_members];
        rows = sharedRows(rowLen * num_members);
        // snapshot = new char[rowLen * num_members];
        volatile char* base = rows;
        set_bases_and_rowLens(base, rowLen, fields...);
    }

    char* sharedRows(size_t len) {
		extern char *MSHM;
        return create_shared_memory(MSHM, len);
    }

    DerivedSST* derived_this;

    std::vector<std::thread> background_threads;
    std::atomic<bool> thread_shutdown;

    void detect();

public:
    Predicates<DerivedSST> predicates;
    friend class Predicates<DerivedSST>;

private:
    /** Pointer to memory where the SST rows are stored. */
    volatile char* rows;
    // char* snapshot;
    /** Length of each row in this SST, in bytes. */
    int rowLen;
    /** List of nodes in the SST; indexes are row numbers, values are node IDs. */
    const std::vector<uint32_t>& members;
    /** Equal to members.size() */
    const unsigned int num_members;
    std::vector<uint32_t> all_indices;
    /** Index (row number) of this node in the SST. */
    unsigned int my_index;
    /** Maps node IDs to SST row indexes. */
    std::map<uint32_t, int, std::greater<uint32_t>> members_by_id;
    /** ID of this node in the system. */
    uint32_t my_node_id;
    /** Map of queue pair number to row. Useful for detecting failures. */
    // std::map<int, int> qp_num_to_index;

    /** Array with one entry for each row index, tracking whether the row is
     *  marked frozen (meaning its corresponding remote node has crashed). */
    std::vector<bool> row_is_frozen;
    /** The number of rows that have been frozen. */
    int num_frozen{0};
    /** The function to call when a remote node appears to have failed. */
    failure_upcall_t failure_upcall;
    /** Mutex for failure detection and row freezing. */
    std::mutex freeze_mutex;

    /** RDMA resources vector, one for each member. */
    std::vector<std::unique_ptr<resources>> res_vec;

    /** Indicates whether the predicate evaluation thread should start after being
     * forked in the constructor. */
    bool thread_start;
    /** Mutex for thread_start_cv. */
    std::mutex thread_start_mutex;
    /** Notified when the predicate evaluation thread should start. */
    std::condition_variable thread_start_cv;

public:
    SST(DerivedSST* derived_class_pointer, const SSTParams& params)
            : derived_this(derived_class_pointer),
              thread_shutdown(false),
              members(params.members),
              num_members(members.size()),
              all_indices(num_members),
              my_node_id(params.my_node_id),
              row_is_frozen(num_members),
              failure_upcall(params.failure_upcall),
              res_vec(num_members),
              thread_start(params.start_predicate_thread) {
        //Figure out my SST index
        my_index = (uint)-1;
        for(uint32_t i = 0; i < num_members; ++i) {
            if(members[i] == my_node_id) {
                my_index = i;
                break;
            }
        }
        assert(my_index != (uint)-1);

        std::iota(all_indices.begin(), all_indices.end(), 0);

        if(!params.already_failed.empty()) {
            assert(params.already_failed.size() == num_members);
            for(size_t index = 0; index < params.already_failed.size(); ++index) {
                if(params.already_failed[index]) {
                    row_is_frozen[index] = true;
                }
            }
        }

        // sort members descending by node ID, while keeping track of their
        // specified index in the SST
        for(unsigned int sst_index = 0; sst_index < num_members; ++sst_index) {
            members_by_id[members[sst_index]] = sst_index;
        }
    }

    template <typename... Fields>
    void SSTInit(Fields&... fields) {
        //Initialize rows and set the "base" field of each SSTField
        init_SSTFields(fields...);

        //Initialize res_vec with the correct offsets for each row
        unsigned int node_rank, sst_index;
        for(auto const& rank_index : members_by_id) {
            std::tie(node_rank, sst_index) = rank_index;
            char *write_addr, *read_addr;
            write_addr = const_cast<char*>(rows) + rowLen * sst_index;
            read_addr = const_cast<char*>(rows) + rowLen * my_index;
            if(sst_index != my_index) {
                if(row_is_frozen[sst_index]) {
                    continue;
                }
#ifdef USE_VERBS_API
                res_vec[sst_index] = std::make_unique<resources>(
                        node_rank, write_addr, read_addr, rowLen, rowLen);
#else  // use libfabric api by default
                res_vec[sst_index] = std::make_unique<resources>(
                        node_rank, write_addr, read_addr, rowLen, rowLen, (my_node_id < node_rank));
#endif
                // update qp_num_to_index
                // qp_num_to_index[res_vec[sst_index].get()->qp->qp_num] = sst_index;
            }
        }

        std::thread detector(&SST::detect, this);
        background_threads.push_back(std::move(detector));

        std::cout << "Initialized SST and Started Threads" << std::endl;
    }

    ~SST();

    /** Starts the predicate evaluation loop. */
    void start_predicate_evaluation();

    /** Does a TCP sync with each member of the SST. */
    void sync_with_members() const;

    /** Syncs with a subset of the members */
    void sync_with_members(std::vector<uint32_t> row_indices) const;

    /** Marks a row as frozen, so it will no longer update, and its corresponding
     * node will not receive writes. */
    void freeze(int row_index);

    /** Returns the total number of rows in the table. */
    unsigned int get_num_rows() const { return num_members; }

    /** Gets the index of the local row in the table. */
    unsigned int get_local_index() const { return my_index; }

    const char* getBaseAddress() {
        return const_cast<char*>(rows);
    }

    /** Writes the entire local row to all remote nodes. */
    void put() {
        put(all_indices, 0, rowLen);
    }

    void put_with_completion() {
        put_with_completion(all_indices, 0, rowLen);
    }

    /** Writes the entire local row to some of the remote nodes. */
    void put(const std::vector<uint32_t> receiver_ranks) {
        put(receiver_ranks, 0, rowLen);
    }

    void put_with_completion(const std::vector<uint32_t> receiver_ranks) {
        put_with_completion(receiver_ranks, 0, rowLen);
    }

    /** Writes a contiguous subset of the local row to all remote nodes. */
    void put(long long int offset, long long int size) {
        put(all_indices, offset, size);
    }

    void put_with_completion(long long int offset, long long int size) {
        put_with_completion(all_indices, offset, size);
    }

    /** Writes a contiguous subset of the local row to some of the remote nodes. */
    void put(const std::vector<uint32_t> receiver_ranks, long long int offset, long long int size);

    void put_with_completion(const std::vector<uint32_t> receiver_ranks, long long int offset, long long int size);

private:
    using char_p = volatile char*;

    void compute_rowLen(int&) {}

    template <typename Field, typename... Fields>
    void compute_rowLen(int& rowLen, Field& f, Fields&... rest) {
        rowLen += padded_len(f.field_len);
        compute_rowLen(rowLen, rest...);
    }

    void set_bases_and_rowLens(char_p&, const int) {}

    template <typename Field, typename... Fields>
    void set_bases_and_rowLens(char_p& base, const int rlen, Field& f, Fields&... rest) {
        base += f.set_base(base);
        f.set_rowLen(rlen);
        set_bases_and_rowLens(base, rlen, rest...);
    }

    // void take_snapshot() {
    //   memcpy(snapshot, const_cast<char*>(rows), rowLen * num_members);
    // }

    // // returns snapshot == current
    // bool compare_snapshot_and_current() {
    //     int res = memcmp(const_cast<char*>(rows), snapshot, rowLen * num_members);
    //     if(res == 0) {
    //         return true;
    //     }
    //     return false;
    // }
};

} /* namespace sst */

#include "sst_impl.h"
