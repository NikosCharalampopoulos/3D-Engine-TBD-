#ifndef ENGINE_ENTITY_HPP
#define ENGINE_ENTITY_HPP

// The smallest possible "thing in the scene" concept: a Transform plus an
// optional reference to renderable data (a Model). Phase 5 had exactly one
// such thing (Application's model_ + sceneTransform_ pair) as two loose,
// separately-declared members; Phase 6 pulls that pairing out into its own
// type so a scene is "a list of Entities" that can be enumerated, rather
// than a fixed set of named members that only works for exactly one object.
//
// This is deliberately NOT a general archetype/sparse-set ECS -- no
// component pools, no systems scheduler, no generic component-type
// registration. It's just enough structure to establish the pattern (things
// in the world have a transform + renderable data and can be iterated) that
// a real ECS could replace wholesale in a later phase, without this phase
// over-building one it doesn't need yet.
//
// model is a shared_ptr rather than an owned Model, because Model instances
// now come from ResourceManager's cache (see resource_manager.hpp) and are
// meant to be shared -- several Entities could in principle point at the
// same cached Model. Entity itself owns nothing but its own Transform and
// name; it's fine (and expected) for model to be null for a purely
// transform-only entity (e.g. a camera rig, an empty group node) even
// though nothing in this phase actually constructs one that way yet.
//
// Copyable and movable: every member (Transform, shared_ptr, string) already
// is, and unlike Shader/Texture/Mesh/Model there's no scarce GL handle
// living directly on Entity itself to make exclusive-ownership (move-only)
// semantics necessary here.

#include <memory>
#include <string>
#include <utility>

#include "engine/transform.hpp"

namespace engine {

class Model;

class Entity {
public:
    explicit Entity(std::string name, std::shared_ptr<Model> model = nullptr)
        : name_(std::move(name)), model_(std::move(model)) {}

    // Public and mutable, like Material's tint/shininess -- this is plain
    // per-entity placement data callers (Application, later gameplay code)
    // are expected to poke directly every frame, not something that needs
    // setter ceremony.
    Transform transform;

    const std::string& name() const { return name_; }
    const std::shared_ptr<Model>& model() const { return model_; }

private:
    std::string name_;
    std::shared_ptr<Model> model_;
};

}  // namespace engine

#endif  // ENGINE_ENTITY_HPP
