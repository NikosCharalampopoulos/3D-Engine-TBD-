#ifndef ENGINE_ECS_HPP
#define ENGINE_ECS_HPP

// Phase 8a promotes Phase 6's Entity (see the now-removed entity.hpp -- its
// own header comment said a real ECS could replace it "wholesale in a later
// phase") into an actual component-based entity system: entities are now
// opaque IDs (EntityId) that own no data of their own, and per-entity data
// lives in typed ComponentPool<T> pools owned by EntityRegistry, keyed by
// entity id. Application's entities_ (a std::vector<Entity>, each bundling a
// Transform and an optional Model as two hardcoded fields) becomes a
// registry_ (EntityRegistry): transform data lives in a
// ComponentPool<Transform> -- the existing Transform type reused directly as
// its own component payload, no wrapper struct needed since Transform is
// already just plain data -- and model data lives in a
// ComponentPool<ModelComponent>, a one-field wrapper around the existing
// shared_ptr<Model> kept as its own distinct type (rather than registering
// shared_ptr<Model> itself as a bare component) so a future component that
// also happens to want to store *a* shared_ptr<something-else> can't
// collide with "the" Model component by type alone.
//
// What this deliberately IS: a minimal but genuine component registry --
// entities are indices, not objects; components live in per-type pools, not
// as fields on a fixed Entity struct; a new component type (Phase 8b's
// debug-UI-inspectable data, Phase 8e's physics bodies/colliders, etc.) can
// be registered without touching EntityRegistry itself, just by calling
// addComponent<NewType>(...) with a new T. That property -- arbitrary
// caller-defined component types through one templated add/get/remove/
// iterate API -- is what makes this "a real ECS" rather than Entity with
// extra steps, and is concretely justified by Phase 8b-8e's own planned
// component-shaped needs, not merely speculative.
//
// What this deliberately is NOT: an archetype/sparse-set ECS in the
// AAA-engine sense. Each ComponentPool<T> is its own small sparse set (a
// dense std::vector<T> plus a parallel std::vector<EntityId> of owners, and
// a sparse entity-index -> dense-slot map for O(1) lookup/removal) -- that
// much structure is needed for get()/remove() to not be an O(n) scan once
// this engine's entity count grows past one. What's missing on purpose: no
// archetypes (entities that share the same component *set* aren't packed
// into one contiguous table together), no compile-time multi-component view/
// query type, no systems scheduler. As of Phase 8e this scene holds two
// entities (the static scene.obj model and Phase 8e's falling cube -- see
// application.cpp's constructor and assets/scenes/default.json) across five
// component types (Transform, ModelComponent, NameComponent, RigidBody,
// Collider) -- still few enough that "iterate every entity with a Model,
// then look up its Transform" (see EntityRegistry::each() below) is a
// one-line composition, exactly as fast and far simpler to read than a
// generic multi-type view would be at this entity count/variety. If a
// later phase's entity count/variety ever makes archetype packing pay for
// itself, this file is where to replace it -- the same way this file
// replaced entity.hpp.
//
// Component pools are stored type-erased (std::shared_ptr<ComponentPoolBase>,
// keyed by std::type_index) inside EntityRegistry so pool<T>() can be a
// template that works for any T a caller defines, without EntityRegistry
// needing a hardcoded member per component type. shared_ptr (not unique_ptr)
// only because std::unordered_map's mapped type has to be one concrete,
// copy/move-constructible-on-rehash type -- unique_ptr<ComponentPoolBase>
// would technically work too (it's movable), but shared_ptr is the more
// common idiom for this exact pattern and costs nothing extra here (exactly
// one owner, the map itself, ever holds a reference).
//
// Phase 14f: this was originally shared_ptr<void> -- a genuinely type-erased
// handle with no member functions of its own at all, only ever
// static_pointer_cast<ComponentPool<T>>'d back to something callable. That
// was enough until this phase needed one new capability no void* can ever
// provide: EntityRegistry::destroyEntity(EntityId), which has to remove a
// given entity from EVERY pool in this map, whatever concrete T each one
// happens to hold -- something no caller had needed before (see
// EntityId's own header comment on why real entity destruction didn't exist
// until now). shared_ptr<void> can't call a T-specific remove(EntityId)
// without EntityRegistry first knowing every T that's ever been registered
// (defeating the whole "new component type needs no EntityRegistry change"
// point of this design) -- so pools_ now holds shared_ptr<ComponentPoolBase>
// instead: a minimal polymorphic base with exactly one virtual method
// (remove(EntityId)) every ComponentPool<T> already needed to provide
// anyway. See ComponentPoolBase's own comment (below) for the full
// alternative-designs-considered writeup.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {

class Model;

// An opaque handle to an entity: nothing but an index into whatever
// component pools happen to hold data for it. Deliberately no generation/
// recycling counter -- EntityRegistry::create()'s indices increase
// monotonically and are NEVER recycled, even after Phase 14f's
// destroyEntity() below removes an entity from every pool that held it: a
// destroyed index's slot in each ComponentPool is gone, but nextIndex_ never
// steps backward or reuses it, so the classic ECS "stale handle into a
// recycled index silently now points at a different, newer entity" hazard
// still can't happen -- a stale EntityId after destroyEntity() instead reads
// as "valid() == true, but getComponent<T>() returns nullptr for every T",
// which every existing call site already treats safely today (see e.g.
// Application's own Phase 14e "Selected entity no longer has a Transform"
// Inspector fallback, editor_ui.cpp) rather than as a hard error. A
// generation field would only start earning its keep if this engine ever
// recycled indices to bound memory growth from repeated create()/
// destroyEntity() churn -- not a problem this project's own entity counts
// (a handful, hand-authored or user-created through the editor) have ever
// approached, so that stays exactly the kind of speculative complexity this
// engine's own established style avoids until it's a real need.
class EntityId {
public:
    EntityId() = default;
    explicit EntityId(std::uint32_t index) : index_(index) {}

    std::uint32_t index() const { return index_; }
    bool valid() const { return index_ != kInvalid; }

    friend bool operator==(EntityId a, EntityId b) { return a.index_ == b.index_; }
    friend bool operator!=(EntityId a, EntityId b) { return a.index_ != b.index_; }

private:
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t index_ = kInvalid;
};

// Phase 14f: a type-erased base every ComponentPool<T> inherits from, so
// EntityRegistry::destroyEntity() below can call remove(id) on EVERY pool it
// owns -- whatever T happens to be -- without knowing any of those T's at
// compile time. This is the piece Phase 8a's own header comment (further up
// this file) flagged as deliberately not built yet ("nothing calls a
// 'destroy this entity' that would free its index for reuse today"): pools_
// was a std::unordered_map<std::type_index, std::shared_ptr<void>>, and
// void* has no remove() to call -- there was no generic way to reach into
// every pool from one loop. Two designs were available to close that gap:
//   (a) this one -- a small polymorphic base, pools_ upgraded to hold
//       shared_ptr<ComponentPoolBase> instead of shared_ptr<void>, so
//       destroyEntity() is a one-line loop calling a virtual function; or
//   (b) a parallel std::vector<std::function<bool(EntityId)>> of type-erased
//       erasure callbacks, one recorded the first time each component type's
//       pool is created (in pool<T>() below), that destroyEntity() would
//       loop over instead.
// (a) was chosen: pools_ already exists and already needs a per-type-erased
// handle stored once per type (exactly what a vtable pointer already gives
// for free) -- reusing that one map instead of also maintaining a second,
// parallel list of callbacks that must never drift out of sync with it is
// less to keep consistent, and virtual dispatch is the ordinary C++ idiom
// for "call the right T-specific behavior through a type-erased handle",
// not a novel mechanism this codebase would be introducing just for this.
// ComponentPoolBase itself stays otherwise empty (destroyEntity() is the
// only caller that needs to reach a pool generically) rather than growing
// has()/size() virtuals too -- ecs.hpp's own established style avoids
// speculative API surface nothing yet calls (see NameComponent's own
// "nothing at runtime reads this" comment for the same instinct applied to
// a field instead of a method).
class ComponentPoolBase {
public:
    virtual ~ComponentPoolBase() = default;

    // Removes `id` from this pool if present; a no-op (returns false) if
    // this pool never held a component for `id` in the first place -- the
    // overwhelmingly common case destroyEntity() hits on every pool except
    // the handful of component types the destroyed entity actually had, so
    // this must be cheap and side-effect-free to call speculatively on every
    // pool in the registry, not just the ones known in advance to apply.
    virtual bool remove(EntityId id) = 0;
};

// One component type's storage: a dense std::vector<T> (data_), a parallel
// std::vector<EntityId> of which entity owns each dense slot (owners_), and
// a sparse entity-index -> dense-slot map (sparse_) so get()/has()/remove()
// are O(1) instead of an O(n) scan of data_. remove() swaps the removed slot
// with the last live slot before popping both vectors (the standard
// sparse-set removal trick), so add/remove/iterate all stay cheap rather
// than degrading as entities come and go -- the one piece of "real ECS"
// bookkeeping this file doesn't skip, since get() runs every frame (see
// EntityRegistry::each() below) and a linear scan there would make every
// render() pass O(entities^2) as this scene's entity count grows (two as of
// Phase 8e, up from Phase 8a's original one).
//
// Phase 14f: now inherits ComponentPoolBase (above) purely so
// EntityRegistry::pools_ can hold a shared_ptr<ComponentPoolBase> and
// destroyEntity() can call remove(id) on every pool generically -- see
// ComponentPoolBase's own comment for the full design rationale. Nothing
// about this class's own remove(EntityId) signature/behavior changes; it
// simply now satisfies (and is called through) an interface too.
template <typename T>
class ComponentPool : public ComponentPoolBase {
public:
    T& add(EntityId id, T value) {
        auto it = sparse_.find(id.index());
        if (it != sparse_.end()) {
            data_[it->second] = std::move(value);
            return data_[it->second];
        }
        sparse_.emplace(id.index(), data_.size());
        owners_.push_back(id);
        data_.push_back(std::move(value));
        return data_.back();
    }

    T* get(EntityId id) {
        auto it = sparse_.find(id.index());
        return it == sparse_.end() ? nullptr : &data_[it->second];
    }
    const T* get(EntityId id) const {
        auto it = sparse_.find(id.index());
        return it == sparse_.end() ? nullptr : &data_[it->second];
    }

    bool has(EntityId id) const { return sparse_.find(id.index()) != sparse_.end(); }

    bool remove(EntityId id) override {
        auto it = sparse_.find(id.index());
        if (it == sparse_.end()) {
            return false;
        }
        const std::size_t slot = it->second;
        const std::size_t last = data_.size() - 1;
        if (slot != last) {
            data_[slot] = std::move(data_[last]);
            owners_[slot] = owners_[last];
            sparse_[owners_[slot].index()] = slot;
        }
        data_.pop_back();
        owners_.pop_back();
        sparse_.erase(it);
        return true;
    }

    std::size_t size() const { return data_.size(); }
    EntityId owner(std::size_t denseIndex) const { return owners_[denseIndex]; }
    T& at(std::size_t denseIndex) { return data_[denseIndex]; }
    const T& at(std::size_t denseIndex) const { return data_[denseIndex]; }

private:
    std::vector<EntityId> owners_;
    std::vector<T> data_;
    std::unordered_map<std::uint32_t, std::size_t> sparse_;
};

// Phase 8a: wraps the existing shared_ptr<Model> (Models still come from
// ResourceManager's cache -- see resource_manager.hpp; shared_ptr because
// several entities can in principle share one cached Model, the same
// reasoning the now-removed entity.hpp gave for its own Model field) as its
// own component type -- see this file's own header comment for why this
// isn't just a bare ComponentPool<shared_ptr<Model>>.
//
// Named with a "Component" suffix (unlike Transform above, or RigidBody/
// Collider below) because it wraps a pointer to an existing engine type
// (Model) that already has its own unqualified name -- a bare `struct Model`
// component here would collide with the `Model` class itself. NameComponent
// keeps the same suffix for symmetry (both are Phase 8b-and-earlier
// bespoke, ECS-only wrapper structs with no standalone meaning outside an
// entity's data), whereas RigidBody/Collider (Phase 8e, see physics.hpp)
// deliberately drop it: each names a self-contained simulation concept in
// its own right -- exactly the same "reused directly, no wrapper suffix"
// treatment Transform above already gets, and for the same reason.
//
// Phase 8b adds `path`: the asset path `model` was loaded from (relative,
// e.g. "assets/models/scene.obj" -- the same string form ResourceManager's
// getModel() and resolveAssetPath() take, not an already-resolved absolute
// path -- see scene_serialization.cpp's saveScene()/loadScene()). Model
// itself has no notion of "the path it came from" (see model.hpp -- it
// just owns the meshes/materials Assimp produced from whatever path it was
// constructed with), so scene serialization -- which needs to write a
// *reloadable* reference back out, not the live GPU-resident Model object
// -- has nowhere else to recover that string from. Kept alongside `model`
// in this one component (rather than a second, parallel component) since
// the two fields describe the same fact -- "this entity's Model, and where
// it came from" -- and always change together (nothing ever assigns one of
// the two without the other, unlike distinct components on the same
// entity, which are independently optional by design).
struct ModelComponent {
    std::shared_ptr<Model> model;
    std::string path;
};

// Phase 8b: a one-field wrapper around a human-readable entity name, used
// by scene serialization (see scene_serialization.hpp) so a saved scene
// file's entities are identifiable/debuggable by name (e.g. "scene",
// "player_spawn") rather than only by EntityId index -- which is
// meaningless once reloaded, since create() just hands out fresh
// monotonically-increasing indices again on every run, not the same
// indices a previous save saw. Kept as its own opt-in component (like
// ModelComponent), not a mandatory field on EntityId itself, matching this
// registry's "components are opt-in per entity" design -- nothing at
// runtime reads NameComponent today (only scene save/load does), so an
// entity created directly via C++ code that never calls
// addComponent<NameComponent>() is just as valid as one that does.
struct NameComponent {
    std::string name;
};

// Owns every component pool this engine's entities use, type-erased so that
// registering a new component type never requires touching this class --
// see this file's own header comment for the full design rationale.
class EntityRegistry {
public:
    EntityRegistry() = default;

    // Not copied or moved -- same reasoning as ResourceManager (see
    // resource_manager.hpp): pools are stored as shared_ptr<ComponentPoolBase>
    // keyed by the pool's own type, and nothing in this engine needs to
    // duplicate or relocate a whole registry out from under callers that
    // hold EntityIds into it.
    EntityRegistry(const EntityRegistry&) = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;
    EntityRegistry(EntityRegistry&&) = delete;
    EntityRegistry& operator=(EntityRegistry&&) = delete;

    // Allocates a new, monotonically-increasing entity index. The returned
    // id has no components until addComponent<T>() is called for it -- there
    // is no implicit "every entity gets a Transform" behavior, matching this
    // registry's "components are opt-in per entity" design.
    EntityId create() { return EntityId(nextIndex_++); }

    template <typename T, typename... Args>
    T& addComponent(EntityId id, Args&&... args) {
        return pool<T>().add(id, T(std::forward<Args>(args)...));
    }

    template <typename T>
    T* getComponent(EntityId id) {
        return pool<T>().get(id);
    }
    template <typename T>
    const T* getComponent(EntityId id) const {
        const ComponentPool<T>* p = findPool<T>();
        return p == nullptr ? nullptr : p->get(id);
    }

    template <typename T>
    bool hasComponent(EntityId id) const {
        const ComponentPool<T>* p = findPool<T>();
        return p != nullptr && p->has(id);
    }

    template <typename T>
    bool removeComponent(EntityId id) {
        return pool<T>().remove(id);
    }

    // Phase 14f: removes `id` from EVERY component pool this registry
    // currently owns -- i.e. every component type any entity has ever had
    // addComponent<T>() called for, on this registry, before now (pool<T>()
    // below lazily creates a type's pool the first time it's touched, and
    // that pool then lives in pools_ for the registry's whole lifetime, even
    // once empty) -- closing exactly the gap Phase 8a's own header comment
    // flagged: there was previously no way to delete a whole entity across
    // every component type it might have, only removeComponent<T>() one type
    // at a time, which silently breaks the day a new component type is added
    // and some call site forgets to also remove it there. This is generic on
    // purpose: a future component type someone adds by calling
    // addComponent<NewType>(...) (ecs.hpp's own established "no
    // EntityRegistry change needed" contract, see this file's top comment)
    // is automatically covered here too, with no edit to this function
    // required -- see ComponentPoolBase's own comment for the mechanism
    // (virtual remove(EntityId) dispatch) that makes that true.
    //
    // A no-op on an id this registry never actually created (or one already
    // destroyed) -- every pool's own remove(id) already tolerates "id not
    // present" as a plain false return (see ComponentPool::remove() above),
    // so there is nothing here to guard against calling this twice on the
    // same id, or on an EntityId that was never valid to begin with.
    //
    // Deliberately does NOT know about Parent (transform_hierarchy.hpp) or
    // any other higher-level relationship between entities -- ecs.hpp has no
    // #include of transform_hierarchy.hpp and must not gain one (Phase 8a's
    // own "entities are opaque, components are opt-in, this file is the one
    // place a new component type never has to touch" layering would break
    // the moment ecs.hpp depended on one specific component's own header).
    // A caller that also needs to decide what happens to a destroyed
    // entity's CHILDREN (orphan vs. cascade) wants
    // transform_hierarchy.hpp's own destroyEntityOrphaningChildren() instead
    // of calling this directly -- see that function's own header comment for
    // the full orphan-vs-cascade design this phase settled on. This function
    // is the generic, Parent-unaware primitive that one is built on top of.
    void destroyEntity(EntityId id) {
        for (auto& [type, pool] : pools_) {
            pool->remove(id);
        }
    }

    // Direct pool access, for a caller that only needs to walk one component
    // type itself rather than the (T, look-up-a-second-component) pattern
    // each() below covers.
    template <typename T>
    ComponentPool<T>& pool() {
        std::shared_ptr<ComponentPoolBase>& slot = pools_[std::type_index(typeid(T))];
        if (!slot) {
            slot = std::make_shared<ComponentPool<T>>();
        }
        return *std::static_pointer_cast<ComponentPool<T>>(slot);
    }

    // Calls fn(EntityId, T&) once per entity that currently has a T
    // component, in T's pool's dense storage order. This is the "system"
    // pattern every Phase 8a render() call site uses -- e.g.
    // registry.each<ModelComponent>([&](EntityId id, ModelComponent& mc) {
    //   const Transform* xf = registry.getComponent<Transform>(id);
    //   ...
    // }) -- to visit every (Transform, Model) pair without a dedicated
    // two-component view type; see this file's own header comment for why
    // that composition is enough for this engine's current entity count
    // instead of a generic multi-type view/query API.
    template <typename T, typename Fn>
    void each(Fn&& fn) {
        ComponentPool<T>& p = pool<T>();
        for (std::size_t i = 0; i < p.size(); ++i) {
            fn(p.owner(i), p.at(i));
        }
    }

private:
    template <typename T>
    const ComponentPool<T>* findPool() const {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) {
            return nullptr;
        }
        return static_cast<const ComponentPool<T>*>(it->second.get());
    }

    std::uint32_t nextIndex_ = 0;
    // Phase 14f: shared_ptr<ComponentPoolBase>, not shared_ptr<void> -- see
    // ComponentPoolBase's own comment above for why this changed and
    // destroyEntity()'s own comment for the one new thing it lets this class
    // do. shared_ptr (still, not unique_ptr) for the exact same reason this
    // member's own pre-Phase-14f comment already gave: the map's mapped type
    // has to be one concrete, copy/move-constructible-on-rehash type, and
    // shared_ptr is the more common idiom for this pattern even though
    // exactly one owner (this map) ever holds a reference.
    std::unordered_map<std::type_index, std::shared_ptr<ComponentPoolBase>> pools_;
};

}  // namespace engine

#endif  // ENGINE_ECS_HPP
