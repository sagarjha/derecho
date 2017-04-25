/**
 * @file subgroup_info.h
 *
 * @date Feb 17, 2017
 * @author edward
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <typeindex>
#include <vector>

#include "derecho_exception.h"

namespace derecho {

class View;
class SubView;

/**
 * An exception that indicates that a subgroup membership function was unable
 * to finish executing because its enclosing Group was not in a valid state.
 * Usually this means that the Group did not have enough members to completely
 * provision the subgroup, or specific nodes that the subgroup needed were not
 * available.
 */
class subgroup_provisioning_exception : derecho_exception {
public:
    subgroup_provisioning_exception(const std::string& message = "") : derecho_exception(message) {}
};

/** The type to use in the SubgroupInfo maps for a subgroup
 * that doesn't implement a Replicated Object */
struct RawObject {};

/**
 * The data structure used to store a subgroups-and-shards layout for a single
 * subgroup type (i.e. Replicated Object type). The outer vector represents
 * subgroups of the same type, and the inner vector represents shards of each
 * subgroup, so the vectors map subgroup index -> shard index -> sub-view of
 * that shard.
 */
using subgroup_shard_layout_t = std::vector<std::vector<std::unique_ptr<SubView>>>;

/** The type of a lambda function that generates subgroup and shard views
 * for a specific subgroup type */
using shard_view_generator_t = std::function<subgroup_shard_layout_t(const View&)>;

struct SubgroupInfo {
    /** subgroup type -> [](current view){subgroup index -> shard index -> SubView for shard}*/
    std::map<std::type_index, shard_view_generator_t> subgroup_membership_functions;
};
}