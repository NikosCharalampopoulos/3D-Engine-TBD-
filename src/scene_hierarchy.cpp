#include "engine/scene_hierarchy.hpp"

#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace engine {

namespace {

// Matches scene_serialization.hpp's own saveScene() placeholder-name
// convention exactly (see that header's comment) -- an entity with no
// NameComponent (or an empty one) gets "entity_<index>" instead, so a
// hierarchy row's label is never surprising relative to what a save would
// have written for the same nameless entity.
std::string nameOrFallback(EntityRegistry& registry, EntityId id) {
    const NameComponent* nameComponent = registry.getComponent<NameComponent>(id);
    if (nameComponent != nullptr && !nameComponent->name.empty()) {
        return nameComponent->name;
    }
    return "entity_" + std::to_string(id.index());
}

}  // namespace

std::vector<SceneTreeNode> buildSceneTree(EntityRegistry& registry) {
    // Pass 1: every entity that "exists" for hierarchy purposes, in stable
    // dense-pool (creation) order -- see this header's own comment on why
    // Transform, not ModelComponent, is the enumeration used here.
    std::vector<EntityId> ids;
    registry.each<Transform>([&](EntityId id, Transform&) { ids.push_back(id); });

    std::unordered_set<std::uint32_t> known;
    known.reserve(ids.size());
    for (EntityId id : ids) {
        known.insert(id.index());
    }

    // Pass 2: each entity's *effective* parent -- an invalid EntityId if it
    // has no Parent component, or if its Parent points somewhere this same
    // call doesn't also see (a dangling reference, or an id with no
    // Transform) -- exactly resolveWorldMatrix()'s own "dangling parent means
    // an effective root" fallback (transform_hierarchy.hpp), just applied to
    // tree-building instead of matrix composition.
    std::unordered_map<std::uint32_t, EntityId> effectiveParent;
    effectiveParent.reserve(ids.size());
    for (EntityId id : ids) {
        const Parent* parent = registry.getComponent<Parent>(id);
        if (parent != nullptr && parent->id.valid() && known.count(parent->id.index()) != 0) {
            effectiveParent[id.index()] = parent->id;
        } else {
            effectiveParent[id.index()] = EntityId();
        }
    }

    // Pass 3: bucket every id under its effective parent's index, preserving
    // `ids`' own stable order within each bucket.
    std::unordered_map<std::uint32_t, std::vector<EntityId>> childrenOf;
    for (EntityId id : ids) {
        const EntityId parent = effectiveParent[id.index()];
        if (parent.valid()) {
            childrenOf[parent.index()].push_back(id);
        }
    }

    // `visited` tracks every id that has ALREADY been placed as a node
    // somewhere in the forest being built -- an id is inserted here at the
    // exact moment it's *claimed* (as a root, or as some other node's
    // child), strictly before build() recurses into it, so a Parent cycle's
    // second entity to be reached always finds its own id already claimed
    // and is skipped rather than re-added as a duplicate node. See Builder::
    // build()'s own comment below for the concrete case this guards.
    std::unordered_set<std::uint32_t> visited;
    visited.reserve(ids.size());

    // Recursively assembles one node (and, transitively, its whole subtree).
    // A small local struct (not a free function) so it can capture
    // registry/childrenOf/visited by reference without threading three
    // parameters through every recursive call. Precondition on every call:
    // `id.index()` has ALREADY been inserted into `visited`, by whichever
    // call site below is claiming it (either a root-level loop iteration, or
    // this same function's own per-child loop) -- build() itself never
    // inserts its own id, only its children's.
    struct Builder {
        EntityRegistry& registry;
        std::unordered_map<std::uint32_t, std::vector<EntityId>>& childrenOf;
        std::unordered_set<std::uint32_t>& visited;

        SceneTreeNode build(EntityId id) {
            SceneTreeNode node;
            node.id = id;
            node.name = nameOrFallback(registry, id);

            const auto it = childrenOf.find(id.index());
            if (it != childrenOf.end()) {
                node.children.reserve(it->second.size());
                for (EntityId childId : it->second) {
                    // Cycle guard: `childId` already being in `visited` means
                    // it was already claimed elsewhere -- either as a root
                    // (impossible for a real child, since childrenOf only
                    // ever lists ids with a valid effective parent) or,
                    // concretely, as an ancestor further up THIS SAME
                    // recursive call stack (a Parent chain that loops back
                    // on itself, e.g. A's parent is B and B's parent is A).
                    // Skipping it here -- rather than recursing into it again
                    // and appending a second, duplicate node for the same
                    // entity -- is what turns "would recurse forever, or at
                    // best double-list an entity" into "every entity appears
                    // in the returned forest exactly once", the same
                    // "detected, not crashed" treatment resolveWorldMatrix()
                    // gives an actual matrix-composition cycle
                    // (transform_hierarchy.hpp), just with the correctness
                    // bar for a cycle being "well-formed, no entity
                    // duplicated" rather than "produces one canonically
                    // correct answer" -- there isn't one for a cycle.
                    if (!visited.insert(childId.index()).second) {
                        continue;
                    }
                    node.children.push_back(build(childId));
                }
            }
            return node;
        }
    };
    Builder builder{registry, childrenOf, visited};

    std::vector<SceneTreeNode> roots;
    for (EntityId id : ids) {
        if (!effectiveParent[id.index()].valid()) {
            visited.insert(id.index());
            roots.push_back(builder.build(id));
        }
    }
    // Anything left unvisited belongs entirely to a parent cycle (no entity
    // in the cycle ever looked like a root, so the loop above never claimed
    // it) -- claim and append each as an extra root, in the same stable
    // `ids` order, so every entity registry.each<Transform>() visits still
    // appears in the returned forest exactly once, per this header's own
    // contract.
    for (EntityId id : ids) {
        if (visited.count(id.index()) == 0) {
            visited.insert(id.index());
            roots.push_back(builder.build(id));
        }
    }
    return roots;
}

}  // namespace engine
