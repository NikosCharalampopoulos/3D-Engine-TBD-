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
// Component pools are stored type-erased (std::shared_ptr<void>, keyed by
// std::type_index) inside EntityRegistry so pool<T>() can be a template that
// works for any T a caller defines, without EntityRegistry needing a
// hardcoded member per component type. shared_ptr (not unique_ptr) only
// because std::unordered_map's mapped type has to be one concrete,
// copy/move-constructible-on-rehash type -- unique_ptr<void> would technically
// work too (it's movable), but shared_ptr<void> is the more common idiom for
// this exact pattern and costs nothing extra here (exactly one owner, the
// map itself, ever holds a reference).

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
// recycling counter -- this engine only ever creates entities (nothing calls
// a "destroy this entity" that would free its index for reuse today), so the
// classic ECS "stale handle into a recycled index" hazard can't happen yet:
// EntityRegistry::create()'s indices increase monotonically and are never
// recycled. A later phase that adds real entity destruction (and wants an
// old handle to a destroyed-then-recycled index to fail safely rather than
// silently pointing at a different, newer entity) is the right place to add
// a generation field -- adding one now, unused, would be exactly the kind of
// speculative complexity this engine's own established style avoids.
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
template <typename T>
class ComponentPool {
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

    bool remove(EntityId id) {
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
    // resource_manager.hpp): pools are stored as shared_ptr<void> keyed by
    // the pool's own type, and nothing in this engine needs to duplicate or
    // relocate a whole registry out from under callers that hold EntityIds
    // into it.
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

    // Direct pool access, for a caller that only needs to walk one component
    // type itself rather than the (T, look-up-a-second-component) pattern
    // each() below covers.
    template <typename T>
    ComponentPool<T>& pool() {
        std::shared_ptr<void>& slot = pools_[std::type_index(typeid(T))];
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
    std::unordered_map<std::type_index, std::shared_ptr<void>> pools_;
};

}  // namespace engine

#endif  // ENGINE_ECS_HPP
