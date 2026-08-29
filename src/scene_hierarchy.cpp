#include "engine/scene_hierarchy.hpp"

#include "engine/camera_component.hpp"
#include "engine/light.hpp"
#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Phase 17b: camera_component.hpp/light.hpp are both header-only-as-far-as-
// this-file-needs -- SceneTreeNode's own hasModel/hasPointLight/
// hasDirectionalLight/hasCamera flags below (see scene_hierarchy.hpp's own
// Phase 17b comment) are set from plain registry.getComponent<T>() != nullptr
// checks, which only needs each T's own type definition, not any function
// light.cpp/camera_component.hpp's own .cpp-less design provides -- so this
// adds no new LINK dependency, and tests/CMakeLists.txt's own
// scene_hierarchy_test target (which links src/scene_hierarchy.cpp alone,
// no src/light.cpp) needs no change to keep building.

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

        // Bug-review fix (this phase's own review): `depth` counts how many
        // real Parent-link hops this call is below whichever id started the
        // current top-level build() (always 0 there -- see both call sites
        // below). build() recurses with an ACTUAL C++ call per tree level,
        // unlike resolveWorldMatrix()'s own iterative walk
        // (transform_hierarchy.cpp deliberately avoids real recursion
        // specifically so its own kMaxParentChainDepth guard bounds a loop
        // counter, not call-stack depth, "regardless of how deep/cyclic a
        // malformed chain is" -- see that file's own comment). Before this
        // fix, this function's only guard was the cycle check below, which
        // bounds each individual entity to being claimed once but does
        // nothing to bound a long, genuinely non-cyclic chain (or a cycle
        // longer than any sane depth) -- either could still recurse this
        // engine's real call stack as deep as the chain itself, an
        // inconsistency with resolveWorldMatrix()'s own explicit "second,
        // independent guard against a pathologically long chain" that this
        // function was supposed to mirror (see this file's own header
        // comment). Reusing kMaxParentChainDepth (the exact same bound
        // resolveWorldMatrix() uses, transform_hierarchy.hpp) keeps the two
        // guards from drifting apart instead of inventing a second constant.
        SceneTreeNode build(EntityId id, int depth) {
            SceneTreeNode node;
            node.id = id;
            node.name = nameOrFallback(registry, id);
            // Phase 17b: see this struct's own header comment (scene_hierarchy.hpp)
            // for why these four checks live here rather than in
            // editor_ui.cpp's row-drawing code -- registry is already in
            // scope for this exact call, and getComponent<T>() is the same
            // safe-on-any-id lookup every other per-entity UI in this
            // engine already uses (never UB on a stale/componentless id --
            // see ecs.hpp's own EntityId comment).
            node.hasModel = registry.getComponent<ModelComponent>(id) != nullptr;
            node.hasPointLight = registry.getComponent<PointLight>(id) != nullptr;
            node.hasDirectionalLight = registry.getComponent<DirectionalLight>(id) != nullptr;
            node.hasCamera = registry.getComponent<CameraComponent>(id) != nullptr;

            const auto it = childrenOf.find(id.index());
            if (it != childrenOf.end()) {
                node.children.reserve(it->second.size());
                for (EntityId childId : it->second) {
                    if (depth + 1 > kMaxParentChainDepth) {
                        // Depth guard: deliberately does NOT claim (insert
                        // into `visited`) `childId` here -- leaving it
                        // unclaimed is what lets the trailing "claim any
                        // still-unvisited id as an extra root" pass below
                        // pick it up and give it (and whatever of its own
                        // subtree is reachable within a FRESH kMaxParentChainDepth
                        // budget starting from there) a home, rather than
                        // this call recursing arbitrarily deep into a
                        // pathologically long chain and risking a real stack
                        // overflow -- the same "treat as an effective root
                        // from here, don't walk further" fallback
                        // resolveWorldMatrix() gives a chain that exceeds
                        // this exact same bound, just reusing this
                        // function's own pre-existing cycle-handling
                        // mechanism (below) instead of new plumbing. A
                        // single hierarchy panel row nested 64+ Parent-links
                        // deep would be unusable anyway, so displaying it as
                        // several separate top-level entries instead is not
                        // a meaningfully worse outcome -- see this file's
                        // own header comment on why there is no single
                        // "correct" tree shape once a well-formed one isn't
                        // possible.
                        continue;
                    }
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
                    node.children.push_back(build(childId, depth + 1));
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
            roots.push_back(builder.build(id, 0));
        }
    }
    // Anything left unvisited belongs either to a parent cycle, or (see the
    // depth guard above) to the tail of a chain longer than
    // kMaxParentChainDepth -- no entity in either case ever looked like a
    // root, so the loop above never claimed it. Claim and append each as an
    // extra root (build()'s own depth counter restarts at 0, giving it a
    // fresh kMaxParentChainDepth budget of its own -- see build()'s comment
    // above), in the same stable `ids` order, so every entity
    // registry.each<Transform>() visits still appears in the returned
    // forest exactly once, per this header's own contract, no matter how
    // many separate kMaxParentChainDepth-sized fragments a single
    // pathological chain needs to be split across.
    for (EntityId id : ids) {
        if (visited.count(id.index()) == 0) {
            visited.insert(id.index());
            roots.push_back(builder.build(id, 0));
        }
    }
    return roots;
}

}  // namespace engine
