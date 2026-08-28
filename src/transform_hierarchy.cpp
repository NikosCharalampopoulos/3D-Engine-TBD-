// Phase 14b: resolveWorldMatrix()'s own implementation -- see
// transform_hierarchy.hpp for the full design writeup (Parent's meaning,
// the physics-stays-local-space decision, and the cycle/dangling-parent
// safety contract this function implements below).

#include "engine/transform_hierarchy.hpp"

// Phase 14f: destroyEntityOrphaningChildren()'s own comment (transform_hierarchy.hpp)
// explains WHY each orphaned child's world transform is decomposed back into
// position/rotation/scale; glm::decompose() (an "experimental" -- i.e. not
// yet API-frozen, not unstable/buggy -- GLM extension, hence the explicit
// opt-in macro below) is what actually does that decomposition. Defined only
// in this .cpp, not transform_hierarchy.hpp, so the experimental-API opt-in
// doesn't leak into every other translation unit that merely #includes this
// header.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

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

void destroyEntityOrphaningChildren(EntityRegistry& registry, EntityId id) {
    // Every direct child's id plus its CURRENT world transform, snapshotted
    // BEFORE `id` is destroyed below (resolveWorldMatrix() needs `id`'s own
    // Transform, still present at this point, to compose each child's world
    // matrix) and applied AFTER the snapshot loop finishes, not inside it --
    // mutating a ComponentPool<Parent>/<Transform> (via removeComponent()/
    // getComponent() below) while registry.each<Parent>() is still iterating
    // that same pool's own dense storage would be exactly the "mutate a
    // container mid-iteration" bug ComponentPool's sparse-set doesn't
    // protect against (each<T>() indexes by position, and remove() below can
    // move a different entity's data into the slot the iterator hasn't
    // visited yet, or shrink the very range it's iterating).
    //
    // Post-14f bug-review fix: `hasNewWorldTransform` below (originally
    // absent -- every Orphan implicitly assumed decompose() had succeeded,
    // since only successes were ever pushed here at all) is what makes this
    // struct correct for BOTH glm::decompose() outcomes, not just the
    // succeeding one -- see the "unconditional orphan, conditional
    // Transform-overwrite" comment on the loop below for why that split
    // exists.
    struct Orphan {
        EntityId id;
        bool hasNewWorldTransform;
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };
    std::vector<Orphan> orphans;

    registry.each<Parent>([&](EntityId childId, Parent& parent) {
        if (parent.id != id) {
            return;
        }

        const glm::mat4 world = resolveWorldMatrix(registry, childId);

        // glm::decompose() can fail (returns false) for a fully degenerate
        // matrix, e.g. one with a zero-volume scale on some axis -- not
        // reachable through any path this engine's own editor UI can
        // currently produce (the Inspector's Scale field is clamped to
        // [0.01, 100], see editor_ui.cpp's Phase 14e comment), but checked
        // rather than assumed since this is exactly the kind of "should
        // never happen, but don't silently misbehave if it somehow does"
        // case this codebase consistently guards elsewhere (see e.g.
        // resolveWorldMatrix()'s own cycle/dangling-parent handling just
        // above).
        glm::vec3 scale(1.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 translation(0.0f);
        glm::vec3 skew(0.0f);
        glm::vec4 perspective(0.0f, 0.0f, 0.0f, 1.0f);
        const bool decomposed = glm::decompose(world, scale, rotation, translation, skew, perspective);
        // Post-14f bug-review fix: `childId` is pushed into `orphans`
        // UNCONDITIONALLY now -- decompose() succeeding or failing only
        // decides whether the apply loop below overwrites this child's
        // Transform, never whether the child gets orphaned at all. Before
        // this fix, a failed decompose() meant `childId` never made it into
        // `orphans`, which meant the loop below never reached its
        // `removeComponent<Parent>()` call for it either -- leaving this
        // child's Parent still pointing at `id` right up until `id` is
        // destroyed a few lines down, at which point resolveWorldMatrix()'s
        // own dangling-parent fallback (this file's header comment above)
        // would silently reinterpret the child's stale LOCAL transform as a
        // WORLD one: exactly the visible-teleport bug this whole function
        // exists to prevent, on the one path that was supposed to be
        // guarding against it.
        if (decomposed) {
            // glm::decompose()'s returned quaternion is used AS-IS here, no
            // conjugate/inverse -- confirmed, not assumed, by an offline
            // round-trip check: build M = T * mat4_cast(rot) * S from a
            // known non-identity rotation (this engine's own
            // Transform::getModelMatrix() composition, transform.hpp), run
            // it through glm::decompose(), and rebuild T' * mat4_cast(
            // decomposedRot) * S' -- the rebuilt matrix matches M exactly
            // (max per-element diff 0.0), while rebuilding with
            // glm::conjugate(decomposedRot) instead does NOT (diff ~2.5).
            orphans.push_back(Orphan{childId, true, translation, rotation, scale});
        } else {
            // decompose() failed: no usable world-space position/rotation/
            // scale to hand the apply loop below, so the position/rotation/
            // scale fields here are meaningless placeholders (identity) --
            // `hasNewWorldTransform = false` is what tells that loop to
            // ignore them and leave this child's existing Transform
            // untouched instead. The child still goes into `orphans` -- see
            // the comment above.
            orphans.push_back(Orphan{childId, false, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                      glm::vec3(1.0f)});
        }
    });

    for (const Orphan& orphan : orphans) {
        // Post-14f bug-review fix: the Transform overwrite below is now
        // gated on `hasNewWorldTransform` -- only decompose()-succeeded
        // orphans get their position/rotation/scale replaced with the
        // resolved world-space values. A decompose()-failed orphan falls
        // straight through to `removeComponent<Parent>()` below with its
        // existing Transform completely untouched: its current LOCAL values
        // simply become its new, if-slightly-wrong, effective values -- a
        // visible jump in this one unreachable-today edge case is strictly
        // better than silently discarding the child's transform data or
        // aborting the whole delete (same tradeoff this function's header
        // comment above already accepts for the decompose-succeeds case;
        // the only thing that's changed is that a decompose FAILURE no
        // longer also skips orphaning itself).
        if (orphan.hasNewWorldTransform) {
            if (Transform* transform = registry.getComponent<Transform>(orphan.id)) {
                transform->setPosition(orphan.position);
                transform->setRotation(orphan.rotation);
                transform->setScale(orphan.scale);
            }
        }
        // Removes the Parent component outright (rather than leaving it set
        // to an invalid/dangling EntityId) so this child is a REAL root from
        // here on -- not merely relying on resolveWorldMatrix()'s/
        // buildSceneTree()'s own dangling-parent safety net, which exists to
        // tolerate an unexpected/buggy dangling reference, not to be this
        // function's own primary mechanism for an entirely expected,
        // intentional state change. Unconditional regardless of
        // `hasNewWorldTransform` -- every direct child becomes a real root
        // either way, which is the actual fix here (see the push_back
        // comment above).
        registry.removeComponent<Parent>(orphan.id);
    }

    registry.destroyEntity(id);
}

}  // namespace engine
