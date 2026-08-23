// Phase 14b: resolveWorldMatrix()'s own implementation -- see
// transform_hierarchy.hpp for the full design writeup (Parent's meaning,
// the physics-stays-local-space decision, and the cycle/dangling-parent
// safety contract this function implements below).

#include "engine/transform_hierarchy.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "engine/log.hpp"
#include "engine/transform.hpp"

namespace engine {

namespace {

// LOG_WARN's every call site elsewhere in this engine (window.cpp,
// model.cpp, main.cpp, texture.cpp) fires at most a handful of times total,
// e.g. once per asset load or once at startup -- never from inside a
// per-frame render loop. resolveWorldMatrix() is called once per
// ModelComponent entity, per frame (see application.cpp's three call
// sites), so a cycle or a dangling parent -- both of which persist for the
// scene's entire remaining lifetime once they exist, they don't fix
// themselves next frame -- would otherwise re-log the exact same warning 60
// times a second for as long as the engine keeps running. Tracking which
// offending entity indices have already been warned about (module-level,
// not function-local-static-per-call-site, since both problems funnel
// through this one function) keeps the warning genuinely diagnostic --
// printed once, the first time it's detected -- without spamming stderr
// into uselessness. A std::unordered_set, not a bool flag: more than one
// entity can independently be the site of a cycle/dangling reference in a
// large enough scene, and each deserves its own one-time message naming it.
std::unordered_set<std::uint32_t> warnedCycle;
std::unordered_set<std::uint32_t> warnedDanglingParent;
std::unordered_set<std::uint32_t> warnedMaxDepth;

}  // namespace

glm::mat4 resolveWorldMatrix(EntityRegistry& registry, EntityId id) {
    const Transform* selfTransform = registry.getComponent<Transform>(id);
    if (selfTransform == nullptr) {
        // Matches every pre-existing render() call site's own
        // `transform != nullptr ? ... : glm::mat4(1.0f)` fallback -- see
        // this function's own header comment.
        return glm::mat4(1.0f);
    }

    // Walks from `id` up through Parent components, collecting each step's
    // own LOCAL matrix (chain[0] is id's own, chain[1] its parent's, etc.),
    // then folds them together ancestor-to-descendant below -- root's own
    // world matrix IS its local matrix (it has no parent), and each
    // descendant's world matrix is parentWorld * ownLocal, so folding from
    // the highest ancestor collected down to `id` itself (see the loop
    // below) builds exactly that composition in one pass without needing
    // actual recursion (which would make the max-depth guard a real
    // call-stack depth, not just a loop bound -- an iterative walk keeps
    // that guard cheap to enforce and impossible to stack-overflow on
    // regardless of how deep/cyclic a malformed chain is).
    std::vector<glm::mat4> chain;
    chain.push_back(selfTransform->getModelMatrix());

    // Every entity index visited so far on this walk, `id` itself included
    // -- the cycle guard: if the next parent to visit is already in here,
    // the chain loops back on itself (A -> B -> A, or any longer cycle) and
    // must stop rather than walk it forever.
    std::unordered_set<std::uint32_t> visited;
    visited.insert(id.index());

    EntityId current = id;
    bool hitMaxDepth = true;
    for (int depth = 0; depth < kMaxParentChainDepth; ++depth) {
        const Parent* parent = registry.getComponent<Parent>(current);
        if (parent == nullptr || !parent->id.valid()) {
            // No Parent component, or a Parent explicitly set to an invalid
            // EntityId -- `current` is a root. The normal, overwhelmingly
            // common case (every entity before this phase, and every
            // entity after it that doesn't opt into a Parent).
            hitMaxDepth = false;
            break;
        }

        if (visited.count(parent->id.index()) != 0) {
            // Cycle: parent->id is an ancestor we've already walked
            // through (possibly `id` itself, for a direct or indirect
            // self-parent). Stop here and treat everything collected so
            // far as if `current` were a root -- see this file's header
            // comment on why "skip parenting, log once" beats crashing or
            // hanging.
            if (warnedCycle.insert(current.index()).second) {
                LOG_WARN("resolveWorldMatrix: parent cycle detected at entity index " +
                          std::to_string(current.index()) + " (its Parent chain loops back to entity index " +
                          std::to_string(parent->id.index()) +
                          "); treating it as a root and ignoring the cyclic parent link");
            }
            hitMaxDepth = false;
            break;
        }

        const Transform* parentTransform = registry.getComponent<Transform>(parent->id);
        if (parentTransform == nullptr) {
            // Dangling parent reference: parent->id doesn't name a live
            // Transform (either the id was never valid data, or -- the
            // scenario this is really future-proofed against, see
            // transform_hierarchy.hpp's own header comment -- a later
            // phase's entity-destruction feature removed it). Same
            // "treat as root from here, warn once" handling as a cycle.
            if (warnedDanglingParent.insert(current.index()).second) {
                LOG_WARN("resolveWorldMatrix: entity index " + std::to_string(current.index()) +
                          " has a Parent referencing entity index " + std::to_string(parent->id.index()) +
                          ", which has no Transform (missing or destroyed); treating it as a root and ignoring "
                          "the dangling parent link");
            }
            hitMaxDepth = false;
            break;
        }

        chain.push_back(parentTransform->getModelMatrix());
        visited.insert(parent->id.index());
        current = parent->id;
    }

    if (hitMaxDepth) {
        // Fell out of the loop by exhausting kMaxParentChainDepth without
        // ever hitting a root/cycle/dangling-parent break above -- an
        // absurdly deep, non-cyclic parent chain (see
        // transform_hierarchy.hpp's own comment on why this bound exists as
        // a second, independent guard alongside the visited-set check
        // above). Whatever's collected in `chain` so far is used as-is;
        // any ancestors beyond this bound are silently ignored, the same
        // "treat as root from here" behavior as the other two guards.
        if (warnedMaxDepth.insert(id.index()).second) {
            LOG_WARN("resolveWorldMatrix: entity index " + std::to_string(id.index()) +
                      "'s parent chain exceeds " + std::to_string(kMaxParentChainDepth) +
                      " levels; truncating it there instead of walking further");
        }
    }

    glm::mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world = world * (*it);
    }
    return world;
}

}  // namespace engine
