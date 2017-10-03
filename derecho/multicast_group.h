#pragma once

#include <assert.h>
#include <condition_variable>
#include <experimental/optional>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

#include "connection_manager.h"
#include "derecho_internal.h"
#include "derecho_modes.h"
#include "derecho_ports.h"
#include "derecho_sst.h"
#include "filewriter.h"
#include "mutils-serialization/SerializationMacros.hpp"
#include "mutils-serialization/SerializationSupport.hpp"
#include "rdmc/rdmc.h"
#include "spdlog/spdlog.h"
#include "sst/multicast.h"
#include "sst/sst.h"
#include "subgroup_info.h"

namespace derecho {

/** Type alias for the internal subgroup IDs generated by ViewManager. */
// moved to derecho_internal.h
// using subgroup_id_t = uint32_t;
// using message_id_t = int64_t;

/** Alias for the type of std::function that is used for message delivery event callbacks. */
using message_callback_t = std::function<void(subgroup_id_t, node_id_t, long long int, char*, long long int)>;
using persistence_callback_t = std::function<void(subgroup_id_t, persistence_version_t)>;
using rpc_handler_t = std::function<void(subgroup_id_t, node_id_t, char*, uint32_t)>;

/**
 * Bundles together a set of callback functions for message delivery events.
 * These will be invoked by DerechoGroup to hand control back to the client
 * when it needs to handle message delivery.
 */
struct CallbackSet {
    message_callback_t global_stability_callback;
    persistence_callback_t local_persistence_callback = nullptr;
    persistence_callback_t global_persistence_callback = nullptr;
};

struct DerechoParams : public mutils::ByteRepresentable {
    long long unsigned int max_payload_size;
    long long unsigned int block_size;
    std::string filename = std::string();
    unsigned int window_size = 3;
    unsigned int timeout_ms = 1;
    rdmc::send_algorithm type = rdmc::BINOMIAL_SEND;
    uint32_t rpc_port = derecho_rpc_port;

    DerechoParams(long long unsigned int max_payload_size,
                  long long unsigned int block_size,
                  std::string filename = std::string(),
                  unsigned int window_size = 3,
                  unsigned int timeout_ms = 1,
                  rdmc::send_algorithm type = rdmc::BINOMIAL_SEND,
                  uint32_t rpc_port = derecho_rpc_port)
            : max_payload_size(max_payload_size),
              block_size(block_size),
              filename(filename),
              window_size(window_size),
              timeout_ms(timeout_ms),
              type(type),
              rpc_port(rpc_port) {
    }

    DEFAULT_SERIALIZATION_SUPPORT(DerechoParams, max_payload_size, block_size, filename, window_size, timeout_ms, type, rpc_port);
};

struct __attribute__((__packed__)) header {
    uint32_t header_size;
    uint32_t pause_sending_turns;
    uint32_t index;
    uint64_t timestamp;
    bool cooked_send;
};

/**
 * Represents a block of memory used to store a message. This object contains
 * both the array of bytes in which the message is stored and the corresponding
 * RDMA memory region (which has registered that array of bytes as its buffer).
 * This is a move-only type, since memory regions can't be copied.
 */
struct MessageBuffer {
    std::unique_ptr<char[]> buffer;
    std::shared_ptr<rdma::memory_region> mr;

    MessageBuffer() {}
    MessageBuffer(size_t size) {
        if(size != 0) {
            buffer = std::unique_ptr<char[]>(new char[size]);
            mr = std::make_shared<rdma::memory_region>(buffer.get(), size);
        }
    }
    MessageBuffer(const MessageBuffer&) = delete;
    MessageBuffer(MessageBuffer&&) = default;
    MessageBuffer& operator=(const MessageBuffer&) = delete;
    MessageBuffer& operator=(MessageBuffer&&) = default;
};

struct RDMCMessage {
    /** The unique node ID of the message's sender. */
    uint32_t sender_id;
    /** The message's index (relative to other messages sent by that sender). */
    //long long int index;
    message_id_t index;
    /** The message's size in bytes. */
    long long unsigned int size;
    /** The MessageBuffer that contains the message's body. */
    MessageBuffer message_buffer;
};

struct SSTMessage {
    /** The unique node ID of the message's sender. */
    uint32_t sender_id;
    /** The message's index (relative to other messages sent by that sender). */
    long long int index;
    /** The message's size in bytes. */
    long long unsigned int size;
    /** Pointer to the message */
    volatile char* buf;
};

/**
 * A collection of settings for a single subgroup that this node is a member of.
 * Mostly extracted from SubView, but tailored specifically to what MulticastGroup
 * needs to know about subgroups and shards.
 */
struct SubgroupSettings {
    /** This node's shard number within the subgroup */
    uint32_t shard_num;
    /** This node's rank within its shard of the subgroup */
    uint32_t shard_rank;
    /** The members of the subgroup */
    std::vector<node_id_t> members;
    /** The "is_sender" flags for members of the subgroup */
    std::vector<int> senders;
    /** This node's sender rank within the subgroup (as defined by SubView::sender_rank_of) */
    int sender_rank;
    /** The offset of this node's num_received counter within the subgroup's SST section */
    uint32_t num_received_offset;
    /** The operation mode of the subgroup */
    Mode mode;
};

/** Implements the low-level mechanics of tracking multicasts in a Derecho group,
 * using RDMC to deliver messages and SST to track their arrival and stability.
 * This class should only be used as part of a Group, since it does not know how
 * to handle failures. */
class MulticastGroup {
private:
    std::shared_ptr<spdlog::logger> logger;
    /** vector of member id's */
    std::vector<node_id_t> members;
    /** inverse map of node_ids to sst_row */
    std::map<node_id_t, uint32_t> node_id_to_sst_index;
    /**  number of members */
    const unsigned int num_members;
    /** index of the local node in the members vector, which should also be its row index in the SST */
    const int member_index;

public:  //consts can be public, right?
    /** Block size used for message transfer.
     * we keep it simple; one block size for messages from all senders */
    const long long unsigned int block_size;
    // maximum size of any message that can be sent
    const long long unsigned int max_msg_size;
    /** Send algorithm for constructing a multicast from point-to-point unicast.
     *  Binomial pipeline by default. */
    const rdmc::send_algorithm type;
    const unsigned int window_size;

private:
    /** Message-delivery event callbacks, supplied by the client, for "raw" sends */
    const CallbackSet callbacks;
    uint32_t total_num_subgroups;
    /** Maps subgroup IDs (for subgroups this node is a member of) to an immutable
     * set of configuration options for that subgroup. */
    const std::map<subgroup_id_t, SubgroupSettings> subgroup_settings;
    /** Used for synchronizing receives by RDMC and SST */
    std::vector<std::list<long long int>> received_intervals;
    /** Maps subgroup IDs for which this node is a sender to the RDMC group it should use to send.
     * Constructed incrementally in create_rdmc_sst_groups(), so it can't be const.  */
    std::map<subgroup_id_t, uint32_t> subgroup_to_rdmc_group;
    /** These two callbacks are internal, not exposed to clients, so they're not in CallbackSet */
    rpc_handler_t rpc_callback;

    /** Offset to add to member ranks to form RDMC group numbers. */
    uint16_t rdmc_group_num_offset;
    /** false if RDMC groups haven't been created successfully */
    bool rdmc_sst_groups_created = false;
    /** Stores message buffers not currently in use. Protected by
     * msg_state_mtx */
    std::map<uint32_t, std::vector<MessageBuffer>> free_message_buffers;

    /** Index to be used the next time get_sendbuffer_ptr is called.
     * When next_message is not none, then next_message.index = future_message_index-1 */
    std::vector<long long int> future_message_indices;

    /** next_message is the message that will be sent when send is called the next time.
     * It is boost::none when there is no message to send. */
    std::vector<std::experimental::optional<RDMCMessage>> next_sends;
    /** Messages that are ready to be sent, but must wait until the current send finishes. */
    std::vector<std::queue<RDMCMessage>> pending_sends;
    /** Vector of messages that are currently being sent out using RDMC, or boost::none otherwise. */
    /** one per subgroup */
    std::vector<std::experimental::optional<RDMCMessage>> current_sends;

    /** Messages that are currently being received. */
    std::map<std::pair<uint32_t, long long int>, RDMCMessage> current_receives;

    /** Messages that have finished sending/receiving but aren't yet globally stable */
    std::map<uint32_t, std::map<long long int, RDMCMessage>> locally_stable_rdmc_messages;
    /** Parallel map for SST messages */
    std::map<uint32_t, std::map<long long int, SSTMessage>> locally_stable_sst_messages;
    std::map<uint32_t, std::set<uint64_t>> pending_message_timestamps;
    std::map<uint32_t, std::map<int64_t, uint64_t>> pending_persistence;
    /** Messages that are currently being written to persistent storage */
    std::map<uint32_t, std::map<long long int, RDMCMessage>> non_persistent_messages;
    /** Messages that are currently being written to persistent storage */
    std::map<uint32_t, std::map<long long int, SSTMessage>> non_persistent_sst_messages;

    std::vector<long long int> next_message_to_deliver;
    std::mutex msg_state_mtx;
    std::condition_variable sender_cv;

    /** The time, in milliseconds, that a sender can wait to send a message before it is considered failed. */
    unsigned int sender_timeout;

    /** Indicates that the group is being destroyed. */
    std::atomic<bool> thread_shutdown{false};
    /** The background thread that sends messages with RDMC. */
    std::thread sender_thread;

    std::thread timeout_thread;

    /** The SST, shared between this group and its GMS. */
    std::shared_ptr<DerechoSST> sst;

    /** The SSTs for multicasts **/
    std::vector<std::unique_ptr<sst::multicast_group<DerechoSST>>> sst_multicast_group_ptrs;

    using pred_handle = typename sst::Predicates<DerechoSST>::pred_handle;
    std::list<pred_handle> receiver_pred_handles;
    std::list<pred_handle> stability_pred_handles;
    std::list<pred_handle> delivery_pred_handles;
    std::list<pred_handle> persistence_pred_handles;
    std::list<pred_handle> sender_pred_handles;

    std::vector<bool> last_transfer_medium;

    std::unique_ptr<FileWriter> file_writer;

    /** persistence manager callbacks */
    persistence_manager_callbacks_t persistence_manager_callbacks;

    /** Continuously waits for a new pending send, then sends it. This function
     * implements the sender thread. */
    void send_loop();

    uint64_t get_time();

    /** Checks for failures when a sender reaches its timeout. This function
     * implements the timeout thread. */
    void check_failures_loop();

    std::function<void(persistence::message)> make_file_written_callback();
    bool create_rdmc_sst_groups();
    void initialize_sst_row();
    void register_predicates();

    void deliver_message(RDMCMessage& msg, uint32_t subgroup_num);
    void deliver_message(SSTMessage& msg, uint32_t subgroup_num);

    uint32_t get_num_senders(std::vector<int> shard_senders) {
        uint32_t num = 0;
        for(const auto i : shard_senders) {
            if(i) {
                num++;
            }
        }
        return num;
    };

    long long int resolve_num_received(long long beg_index, long long end_index, uint32_t num_received_entry) {
        // std::cout << "num_received_entry = " << num_received_entry << std::endl;
        // std::cout << "beg_index = " << beg_index << std::endl;
        // std::cout << "end_index = " << end_index << std::endl;
        auto it = received_intervals[num_received_entry].end();
        it--;
        while(*it > beg_index) {
            it--;
        }
        if(std::next(it) == received_intervals[num_received_entry].end()) {
            if(*it == beg_index - 1) {
                *it = end_index;
            } else {
                received_intervals[num_received_entry].push_back(beg_index);
                received_intervals[num_received_entry].push_back(end_index);
            }
        } else {
            auto next_it = std::next(it);
            if(*it != beg_index - 1) {
                received_intervals[num_received_entry].insert(next_it, beg_index);
                if(*next_it != end_index + 1) {
                    received_intervals[num_received_entry].insert(next_it, end_index);
                } else {
                    received_intervals[num_received_entry].erase(next_it);
                }
            } else {
                if(*next_it != end_index + 1) {
                    received_intervals[num_received_entry].insert(next_it, end_index);
                } else {
                    received_intervals[num_received_entry].erase(next_it);
                }
                received_intervals[num_received_entry].erase(it);
            }
        }
        // std::cout << "Returned value: "
        //           << *std::next(received_intervals[num_received_entry].begin()) << std::endl;
        return *std::next(received_intervals[num_received_entry].begin());
    }

public:
    MulticastGroup(
            std::vector<node_id_t> _members, node_id_t my_node_id,
            std::shared_ptr<DerechoSST> _sst,
            CallbackSet callbacks,
            uint32_t total_num_subgroups,
            const std::map<subgroup_id_t, SubgroupSettings>& subgroup_settings_by_id,
            const DerechoParams derecho_params,
            const persistence_manager_callbacks_t& _persistence_manager_callbacks,
            std::vector<char> already_failed = {});
    /** Constructor to initialize a new MulticastGroup from an old one,
     * preserving the same settings but providing a new list of members. */
    MulticastGroup(
            std::vector<node_id_t> _members, node_id_t my_node_id,
            std::shared_ptr<DerechoSST> _sst,
            MulticastGroup&& old_group,
            uint32_t total_num_subgroups,
            const std::map<subgroup_id_t, SubgroupSettings>& subgroup_settings_by_id,
            const persistence_manager_callbacks_t& _persistence_manager_callbacks,
            std::vector<char> already_failed = {}, uint32_t rpc_port = derecho_rpc_port);

    ~MulticastGroup();

    /**
     * Registers a function to be called upon receipt of a multicast RPC message
     * @param handler A function that will handle RPC messages.
     */
    void register_rpc_callback(rpc_handler_t handler) { rpc_callback = std::move(handler); }

    void deliver_messages_upto(const std::vector<long long int>& max_indices_for_senders, uint32_t subgroup_num, uint32_t num_shard_senders);
    /** Get a pointer into the current buffer, to write data into it before sending */
    char* get_sendbuffer_ptr(subgroup_id_t subgroup_num, long long unsigned int payload_size,
                             int pause_sending_turns = 0,
                             bool cooked_send = false, bool null_send = false);
    /** Note that get_sendbuffer_ptr and send are called one after the another - regexp for using the two is (get_sendbuffer_ptr.send)*
     * This still allows making multiple send calls without acknowledgement; at a single point in time, however,
     * there is only one message per sender in the RDMC pipeline */
    bool send(subgroup_id_t subgroup_num);

    const uint64_t compute_global_stability_frontier(uint32_t subgroup_num);

    /** Stops all sending and receiving in this group, in preparation for shutting it down. */
    void wedge();
    /** Debugging function; prints the current state of the SST to stdout. */
    void debug_print();
    static long long unsigned int compute_max_msg_size(
            const long long unsigned int max_payload_size,
            const long long unsigned int block_size);

    const std::map<subgroup_id_t, SubgroupSettings>& get_subgroup_settings() {
        return subgroup_settings;
    }
    std::vector<uint32_t> get_shard_sst_indices(uint32_t subgroup_num);
};
}  // namespace derecho
