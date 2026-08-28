// Phase 15f: tests engine::resolveDiffuseTextureOverride()
// (src/material_override.cpp) in isolation -- same "plain executable, links
// only the pure logic file it's testing" shape as light_test/physics_test
// (see tests/CMakeLists.txt's own comment on each). material_override.cpp
// depends only on ecs.hpp (a plain, header-only ComponentPool/EntityRegistry
// -- see that header's own comment), so this needs no live GL context/GPU,
// no Dear ImGui frame, and -- specifically -- no real, GPU-resident Texture:
// see the fake-texture note below for how this test proves pointer identity
// round-trips through a MaterialOverride/resolveDiffuseTextureOverride()
// without ever constructing one.
//
// This is exactly "does entity X's material come from its own override or
// the shared Model's" -- the one decision material_override.hpp's own header
// comment calls out as the whole design this phase has to get right -- in
// isolation from Model::draw()/Application::render() actually consuming it.

#include "engine/material_override.hpp"

#include "engine/ecs.hpp"

#include <cassert>
#include <iostream>
#include <memory>

int main() {
    engine::EntityRegistry registry;

    // An entity with no MaterialOverride component at all -- the
    // overwhelmingly common case, and every entity before this phase.
    // resolveDiffuseTextureOverride() must return nullptr, meaning "draw
    // with whatever the shared Model's own Material already carries."
    const engine::EntityId noComponent = registry.create();
    assert(engine::resolveDiffuseTextureOverride(registry, noComponent) == nullptr);

    // A fake, non-owning "Texture*" sentinel -- Texture itself needs a live
    // GL context to construct for real (texture.hpp's own constructor calls
    // stbi_load + glGenTextures), which this test deliberately never stands
    // up (see material_override.hpp's own header comment for why
    // resolveDiffuseTextureOverride() has no GL dependency at all to begin
    // with). A custom no-op deleter lets std::shared_ptr<Texture> alias an
    // arbitrary non-null address without ever needing Texture's complete
    // type (the deleter never dereferences or deletes it) -- enough to prove
    // POINTER IDENTITY round-trips correctly through a MaterialOverride
    // component without a real, GPU-resident Texture object anywhere in this
    // test binary.
    auto fakeTexture =
        std::shared_ptr<engine::Texture>(reinterpret_cast<engine::Texture*>(0x1), [](engine::Texture*) {});

    // An entity with a MaterialOverride whose diffuseTexture is set --
    // resolveDiffuseTextureOverride() must return that exact pointer.
    const engine::EntityId withOverride = registry.create();
    registry.addComponent<engine::MaterialOverride>(
        withOverride, engine::MaterialOverride{fakeTexture, "assets/textures/fake.png"});
    assert(engine::resolveDiffuseTextureOverride(registry, withOverride) == fakeTexture.get());

    // A SIBLING entity that never had a MaterialOverride added at all (the
    // exact shape "falling_cube"/"parented_demo_cube" sharing one cached
    // Model has in this project's own default scene, see
    // material_override.hpp's own header comment) must still resolve to
    // nullptr -- overriding one entity must never leak into another entity's
    // own lookup, since ComponentPool<T>::get() (ecs.hpp) is keyed by
    // EntityId, not shared state.
    const engine::EntityId sibling = registry.create();
    assert(engine::resolveDiffuseTextureOverride(registry, sibling) == nullptr);

    // An entity with a MaterialOverride component present, but whose
    // diffuseTexture is null (e.g. a default-constructed MaterialOverride) --
    // this must ALSO resolve to nullptr, matching this header's own
    // documented "nullable in principle... one unambiguous way to say 'no
    // override, even though a MaterialOverride component happens to be
    // present'" contract, not crash or return a null Texture* wrapped in
    // some other truthy form.
    const engine::EntityId nullOverride = registry.create();
    registry.addComponent<engine::MaterialOverride>(nullOverride, engine::MaterialOverride{});
    assert(engine::resolveDiffuseTextureOverride(registry, nullOverride) == nullptr);

    // A second entity overridden with a DIFFERENT fake texture, to prove two
    // simultaneously-overridden entities resolve independently rather than
    // one clobbering the other's own lookup.
    auto secondFakeTexture =
        std::shared_ptr<engine::Texture>(reinterpret_cast<engine::Texture*>(0x2), [](engine::Texture*) {});
    const engine::EntityId secondOverride = registry.create();
    registry.addComponent<engine::MaterialOverride>(
        secondOverride, engine::MaterialOverride{secondFakeTexture, "assets/textures/fake2.png"});
    assert(engine::resolveDiffuseTextureOverride(registry, withOverride) == fakeTexture.get());
    assert(engine::resolveDiffuseTextureOverride(registry, secondOverride) == secondFakeTexture.get());

    std::cout << "material_override_test: all checks passed" << std::endl;
    return 0;
}
