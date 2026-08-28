#include "engine/editor_ui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
// Phase 14a: imgui_internal.h is needed for the DockBuilder* functions
// (DockBuilderAddNode/SplitNode/DockWindow/Finish) and
// ImGuiDockNodeFlags_DockSpace -- these are explicitly still "not yet a
// stable public API" per imgui_internal.h's own comment on them, but
// programmatically setting up an initial dock layout (rather than requiring
// a user to manually drag every panel into place on first launch) has no
// public-API equivalent; this is the documented, expected way every
// Dear ImGui docking application does this (see imgui_demo.cpp's own
// ShowExampleAppDockSpace()/DockBuilder usage, which this class's layout
// setup below mirrors).
#include <imgui_internal.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include "engine/asset_browser.hpp"
#include "engine/camera_component.hpp"
#include "engine/light.hpp"
#include "engine/log.hpp"
#include "engine/material.hpp"
#include "engine/material_override.hpp"
#include "engine/model.hpp"
#include "engine/paths.hpp"
#include "engine/physics.hpp"
#include "engine/scene_hierarchy.hpp"
#include "engine/texture.hpp"
#include "engine/transform.hpp"
#include "engine/transform_hierarchy.hpp"

namespace engine {

namespace {

// Same GLSL-version-string reasoning as DebugUI's own (removed, Phase 14a --
// see debug_ui.hpp) copy of this helper: must not exceed the GL context's
// own core-profile ceiling (window.hpp's __APPLE__ note).
const char* glslVersionString() {
#ifdef __APPLE__
    return "#version 410 core";
#else
    return "#version 430 core";
#endif
}

// Phase 15g: the Dear ImGui drag-and-drop "type" tag shared between the
// Assets panel's own drag source (renderAssetTreeNode(), below) and the
// Viewport panel's own drop target (renderDockspaceShell(), further down) --
// SetDragDropPayload()/AcceptDragDropPayload()'s own contract (imgui.h) is
// "must match exactly, at most 32 characters," so this one constant is what
// keeps the two ends from ever silently drifting apart, the same reason this
// file already shares fixed popup-id strings ("SceneCreateMenu", "Choose
// Diffuse Texture") between whatever opens each popup and its own matching
// Begin*() call.
constexpr const char* kAssetDragDropPayloadType = "ASSET_PATH";

// Phase 15f: recursively collects every non-directory AssetTreeNode
// (asset_browser.hpp) reachable under `node` into `out`, in the same
// directories-before-files/alphabetical order buildAssetTree() already
// sorted them into -- the Material Inspector's texture-picker popup
// (renderInspectorPanel()'s own Material section below) needs a flat list
// of PICKABLE entries (leaf files, e.g. "checker.png" or
// "skybox/right.png"), not the nested tree renderAssetTreeNode() renders for
// the Assets panel itself; walking the already-built assetTree_ this way
// (rather than re-walking the filesystem a second time) is exactly the
// "asset tree is a cache" discipline asset_browser.hpp's own header comment
// establishes for the Assets panel, applied to a second consumer.
void collectTextureFiles(const AssetTreeNode& node, std::vector<const AssetTreeNode*>& out) {
    if (node.isDirectory) {
        for (const AssetTreeNode& child : node.children) {
            collectTextureFiles(child, out);
        }
    } else {
        out.push_back(&node);
    }
}

// Phase 15f: the Material Inspector's texture-picker popup body -- opened by
// the Material section's own "Browse..." button (see renderInspectorPanel()
// below) via a matching ImGui::OpenPopup("Choose Diffuse Texture") call
// there. Deliberately a flat ImGui::Selectable() list inside a small fixed-
// height scrolling child, not a nested tree/a searchable-filterable
// control: this phase's own brief explicitly calls for "just a working
// list" (drag-and-drop, a fancier picker, and thumbnails are all separate,
// later scope -- see asset_browser.hpp's own Phase 15d "Deliberately not
// done this phase" list for the identical reasoning already applied to the
// Assets panel itself). Clicking an entry records its assets/-relative path
// (the "textures/..." AssetTreeNode::relativePath, prefixed with "assets/"
// to match ModelComponent::path/MaterialOverride::diffuseTexturePath's own
// convention -- see ecs.hpp/material_override.hpp) into
// `textureAssignRequested` and closes the popup; nothing here touches
// ResourceManager/registry directly (see renderInspectorPanel()'s own Phase
// 15f comment for why that's Application::render()'s job instead).
void renderTextureBrowsePopup(const std::vector<AssetTreeNode>& assetTree,
                               std::optional<std::string>& textureAssignRequested) {
    if (!ImGui::BeginPopup("Choose Diffuse Texture")) {
        return;
    }

    std::vector<const AssetTreeNode*> textureFiles;
    for (const AssetTreeNode& top : assetTree) {
        // "textures" -- see asset_browser.hpp's own header comment for why
        // this is one of exactly two top-level categories buildAssetTree()
        // ever produces (the other, "models", isn't a texture and has no
        // business appearing in this specific picker).
        if (top.isDirectory && top.relativePath == "textures") {
            collectTextureFiles(top, textureFiles);
        }
    }

    if (textureFiles.empty()) {
        // Genuinely reachable, not just defensive: a fresh checkout missing
        // assets/textures/ entirely (asset_browser.hpp's own "a missing
        // category directory is silently skipped, not an error") would
        // leave this popup with nothing to offer -- telling the user that
        // plainly beats an empty, unexplained scrollbox.
        ImGui::TextDisabled("No textures found under assets/textures/.");
    } else {
        ImGui::BeginChild("TextureBrowseList", ImVec2(320.0f, 200.0f), true);
        for (const AssetTreeNode* file : textureFiles) {
            ImGui::PushID(file->relativePath.c_str());
            if (ImGui::Selectable(file->relativePath.c_str())) {
                textureAssignRequested = "assets/" + file->relativePath;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::EndPopup();
}

// Phase 14d: recursively renders one Scene-Hierarchy row (and, if expanded,
// its own children) as an ImGui::TreeNodeEx() -- real parent/child nesting
// via ImGui's own tree indentation, matching this engine's own choice of
// real Parent-component grouping over a flat "folder" label (see
// scene_hierarchy.hpp's own header comment). `##<index>` is folded into the
// node's own ImGui id via PushID(entity index) rather than appended to the
// visible label text, so two differently-parented entities that happen to
// share a NameComponent string can't collide as far as ImGui's own
// id-stack-based widget identity is concerned, without the visible label
// itself growing a stray "##123" suffix.
//
// No icon glyphs (folder/mesh/light/camera, per the approved mockup): Dear
// ImGui's default font (no custom font atlas is built anywhere in this
// engine) only carries the ASCII/Latin-1 glyph range, nowhere near the
// Unicode private-use/symbol code points an icon font would need -- adding
// one is real, separate scope (a new vendored font asset + atlas
// configuration) this phase's brief doesn't ask for. Indentation, the
// tree-node's own expand/collapse arrow, and ImGuiTreeNodeFlags_Selected's
// highlight are what carry "this is a group vs. a leaf" and "this row is
// selected" instead -- functionally equivalent to the mockup's own icons/
// highlight for this phase's purpose (real tree + click-to-select), just
// without the pixel-identical iconography.
void renderSceneTreeNode(const SceneTreeNode& node, std::optional<EntityId>& selectedEntity) {
    ImGui::PushID(static_cast<int>(node.id.index()));

    const bool isSelected = selectedEntity.has_value() && *selectedEntity == node.id;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty()) {
        // ImGuiTreeNodeFlags_Leaf: no expand arrow drawn for a childless
        // entity -- NoTreePushOnOpen means TreeNodeEx() doesn't push an
        // indentation level for it either, so this row doesn't need a
        // matching TreePop() below.
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool opened = ImGui::TreeNodeEx(node.name.c_str(), flags);
    // IsItemClicked() covers a click anywhere on this row's own label/
    // background (not the expand arrow specifically, which TreeNodeEx()
    // already handles internally for open/close) -- exactly "click this row
    // to select it", independent of whether the click also happened to
    // toggle this node open/closed.
    if (ImGui::IsItemClicked()) {
        selectedEntity = node.id;
    }
    if (opened && !node.children.empty()) {
        for (const SceneTreeNode& child : node.children) {
            renderSceneTreeNode(child, selectedEntity);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// Phase 15d: the Assets panel's own row-drawing helper -- deliberately mirrors
// renderSceneTreeNode() above almost line for line (same TreeNodeEx() flag
// set, same "click anywhere on the row selects it" IsItemClicked() check,
// same no-icon-glyphs reasoning that helper's own comment already gives in
// full), just walking asset_browser.hpp's AssetTreeNode forest instead of
// scene_hierarchy.hpp's SceneTreeNode one, and keying selection by
// `node.relativePath` (a stable string identity for a filesystem row)
// instead of an EntityId. Kept as its own separate function rather than
// templating renderSceneTreeNode() over "anything tree-shaped": the two
// trees' node types share no base/interface, their selection state has
// different types and different owners (Application's selectedEntity_ vs.
// this class's own selectedAssetPath_ -- see this file's own header comment
// on why), and the two panels are likely to diverge further once either
// grows real functionality -- Phase 15g's own drag-and-drop source below,
// added only here, not to renderSceneTreeNode(), is exactly that
// divergence actually happening (see this phase's own README section for
// why the Scene panel does NOT gain a matching drag source this phase) --
// collapsing them into one generic helper now would buy nothing today at
// the cost of a genuinely awkward abstraction.
void renderAssetTreeNode(const AssetTreeNode& node, std::optional<std::string>& selectedAssetPath) {
    ImGui::PushID(node.relativePath.c_str());

    const bool isSelected = selectedAssetPath.has_value() && *selectedAssetPath == node.relativePath;
    // Deliberately NO ImGuiTreeNodeFlags_DefaultOpen here, unlike
    // renderSceneTreeNode() above: this engine's own scene has a handful of
    // entities total, so starting every group expanded costs nothing and
    // shows everything at a glance, but assets/models/ and assets/textures/
    // can plausibly grow into real per-category folder structure a level
    // designer would want collapsed by default (this project's own
    // assets/textures/skybox/ and assets/textures/hdri/ subfolders already
    // show the shape of that) -- an all-expanded-by-default asset tree gets
    // worse, not better, as a project's content grows, which an all-expanded
    // scene tree does not.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty()) {
        // Same "no expand arrow / no matching TreePop() needed" shape
        // renderSceneTreeNode() above uses for a childless entity -- here,
        // a file (isDirectory == false always has empty children, but an
        // empty directory does too, and both should render as a plain leaf
        // row rather than a permanently-unopenable arrow).
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool opened = ImGui::TreeNodeEx(node.name.c_str(), flags);
    if (ImGui::IsItemClicked()) {
        selectedAssetPath = node.relativePath;
    }

    // Phase 15g: every row -- file or folder alike -- is a real Dear ImGui
    // drag source now, not just files. Attached to the SAME item
    // TreeNodeEx()/IsItemClicked() above just acted on (BeginDragDropSource()'s
    // own contract -- imgui.h -- is "call right after submitting the item it
    // applies to"), so this can't accidentally attach to some other row.
    // Deliberately uniform across files AND folders rather than special-cased
    // per node.isDirectory: this function's whole job is drawing one row, not
    // deciding what a drop of it means downstream -- that classification
    // (model vs. texture vs. "not a real draggable file at all," which is
    // exactly what dragging a bare category folder like "models" itself, or
    // one of assets/textures/'s own skybox/hdri subfolders, produces) is
    // engine::classifyAssetDropPath() (asset_drop.hpp)'s job alone, run once
    // a drop actually lands on the Viewport (see this file's own
    // renderDockspaceShell() Phase 15g comment below) -- keeping that
    // decision out of this row-drawing function is the same "EditorUI reports
    // intent, Application decides what it means" split this class already
    // follows for createRequest/textureAssignRequested.
    //
    // The payload itself is one flat, null-terminated path string --
    // "assets/" + node.relativePath, matching AssetTreeNode::relativePath's
    // OWN "relative to assets/ itself" convention prefixed the identical way
    // renderTextureBrowsePopup()'s own "assets/" + file->relativePath already
    // is (this file's own Phase 15f comment) -- not a small POD struct
    // bundling path+category+isDirectory: SetDragDropPayload() copies its
    // `data` argument by raw byte value (imgui.cpp), so anything containing a
    // std::string/std::vector (owning heap memory) would be unsafe to hand it
    // this way; a flat char buffer has no such hazard, and the one piece of
    // information a drop target actually needs -- WHICH asset -- is fully
    // captured by the path alone (category is re-derived from it, not
    // shipped alongside it, so there's no second field that could ever drift
    // out of sync with the path itself).
    if (ImGui::BeginDragDropSource()) {
        const std::string payloadPath = "assets/" + node.relativePath;
        ImGui::SetDragDropPayload(kAssetDragDropPayloadType, payloadPath.c_str(), payloadPath.size() + 1);
        // The default drag preview tooltip (Dear ImGui's own "..." fallback
        // -- imgui.h's own BeginDragDropSource() comment) isn't very useful;
        // this is the identical "just the row's own visible label" preview
        // pattern imgui_demo.cpp's own drag-and-drop examples use.
        ImGui::TextUnformatted(node.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (opened && !node.children.empty()) {
        for (const AssetTreeNode& child : node.children) {
            renderAssetTreeNode(child, selectedAssetPath);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// Phase 14d: the approved mockup's dashed-rectangle-plus-corner-brackets
// selection look (a simple 2D screen-space gizmo, deliberately NOT a fancy
// inverted-hull silhouette shader -- see this phase's own brief). Both
// helpers draw directly into `drawList` in already-resolved screen-pixel
// coordinates (topLeft/bottomRight), leaving all NDC-to-panel-pixel mapping
// to their one call site below.
void addDashedRect(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, ImU32 color) {
    constexpr float kDashLength = 6.0f;
    constexpr float kGapLength = 4.0f;
    constexpr float kThickness = 1.5f;

    auto dashedLine = [&](ImVec2 from, ImVec2 to) {
        const ImVec2 delta(to.x - from.x, to.y - from.y);
        const float length = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
        if (length < 1.0f) {
            return;
        }
        const ImVec2 direction(delta.x / length, delta.y / length);
        float traveled = 0.0f;
        bool drawing = true;
        while (traveled < length) {
            const float segment = std::min(drawing ? kDashLength : kGapLength, length - traveled);
            if (drawing) {
                const ImVec2 segmentStart(from.x + (direction.x * traveled), from.y + (direction.y * traveled));
                const ImVec2 segmentEnd(from.x + (direction.x * (traveled + segment)),
                                         from.y + (direction.y * (traveled + segment)));
                drawList->AddLine(segmentStart, segmentEnd, color, kThickness);
            }
            traveled += segment;
            drawing = !drawing;
        }
    };

    dashedLine(topLeft, ImVec2(bottomRight.x, topLeft.y));
    dashedLine(ImVec2(bottomRight.x, topLeft.y), bottomRight);
    dashedLine(bottomRight, ImVec2(topLeft.x, bottomRight.y));
    dashedLine(ImVec2(topLeft.x, bottomRight.y), topLeft);
}

void addCornerBrackets(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, ImU32 color) {
    // Each bracket's own two short, solid arms -- not dashed, so they read as
    // a distinct "handle" accent against the dashed outline itself, matching
    // the approved mockup's own corner-bracket look.
    constexpr float kArmLength = 10.0f;
    constexpr float kThickness = 2.0f;

    const ImVec2 corners[4] = {
        topLeft,
        ImVec2(bottomRight.x, topLeft.y),
        bottomRight,
        ImVec2(topLeft.x, bottomRight.y),
    };
    // Sign of each arm's own direction along x/y, pointing INWARD from that
    // corner (e.g. the top-left corner's arms extend right and down) so the
    // brackets sit just inside the dashed rectangle rather than outside it.
    const float armX[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    const float armY[4] = {1.0f, 1.0f, -1.0f, -1.0f};

    for (int i = 0; i < 4; ++i) {
        const ImVec2& corner = corners[i];
        drawList->AddLine(corner, ImVec2(corner.x + (armX[i] * kArmLength), corner.y), color, kThickness);
        drawList->AddLine(corner, ImVec2(corner.x, corner.y + (armY[i] * kArmLength)), color, kThickness);
    }
}

// Phase 14f: the Scene panel's Create menu -- its own item list, shared
// between the "+" button's popup and the panel's right-click-background
// popup (both open the SAME popup id -- see renderDockspaceShell()'s own
// Phase 14f comment below for how). Returns whichever CreateEntityKind
// (editor_ui.hpp) was clicked THIS call, or kNone the overwhelmingly common
// case: nothing clicked, including every frame this menu isn't even open
// (ImGui::MenuItem() itself is only ever true on the exact frame it's
// clicked). Doesn't call ImGui::CloseCurrentPopup() itself -- ImGui already
// closes a popup automatically the instant one of its own MenuItem()s is
// clicked, the default behavior this engine's other popups (if any existed
// yet) would already rely on too.
CreateEntityKind renderCreateEntityMenuItems() {
    CreateEntityKind result = CreateEntityKind::kNone;

    if (ImGui::MenuItem("Cube")) {
        result = CreateEntityKind::kCube;
    }
    if (ImGui::MenuItem("Sphere")) {
        result = CreateEntityKind::kSphere;
    }
    if (ImGui::MenuItem("Plane")) {
        result = CreateEntityKind::kPlane;
    }
    if (ImGui::MenuItem("Empty / New Folder")) {
        result = CreateEntityKind::kEmpty;
    }

    ImGui::Separator();

    // Phase 15a: "Point Light" is real now -- light.hpp's new PointLight
    // component plus application.cpp's collectPointLights()-based upload
    // (see that file's own Phase 15a comment) means a created point light
    // actually lights the scene, the same "real, working" treatment
    // Cube/Sphere/Plane/Empty above already got in Phase 14f.
    if (ImGui::MenuItem("Point Light")) {
        result = CreateEntityKind::kPointLight;
    }

    // Phase 15b: "Directional Light" is real now too -- light.hpp's new
    // DirectionalLight component plus application.cpp's
    // resolveActiveDirectionalLight()-based upload (see that header's own
    // Phase 15b comment) means a created-and-active directional light
    // actually replaces the fixed kLightDirection/kLightColor "sun" this
    // frame, cascaded shadows included. This one WAS the harder of the two
    // Phase 15a deferred (see that phase's own README section): unlike point
    // lights, basic.frag/pbr.frag read a single fixed uLightDirection/
    // uLightColor pair, not a uNumDirectionalLights-counted array, and it's
    // also this engine's one shadow-casting light (renderShadowPass()/
    // computeCascades(), application.cpp) -- so making it ECS-driven needed
    // an "active sun" concept this engine had no notion of before this
    // phase. That's Application's own activeDirectionalLight_
    // (application.hpp) now -- "the most recently Create'd Directional
    // Light entity" (set in spawnEntityFromCreateMenu(), application.cpp),
    // the simplest rule that fits an engine with no multi-select UI concept
    // at all yet. Camera (below) turned out NOT to need an equivalent
    // "active" concept at all, despite looking structurally similar at
    // first glance -- see that item's own Phase 15c comment for why.
    if (ImGui::MenuItem("Directional Light")) {
        result = CreateEntityKind::kDirectionalLight;
    }

    // Phase 15c: "Camera" is real now too -- the third and last of this
    // Create menu's own Phase 14f-inherited BeginDisabled()'d gaps. Unlike
    // the two lights above, a Camera entity doesn't compete for any shared
    // rendering resource (nothing in this engine's rendering pipeline reads
    // a CameraComponent at all yet), so there is no "active camera"
    // resolution to build here the way Directional Light needed -- see
    // camera_component.hpp's own header comment for the full reasoning, and
    // application.hpp's Camera-related comments for what this deliberately
    // does NOT wire up (this engine's actual rendered view stays entirely
    // Application's own free-fly camera_ object, untouched by this phase).
    if (ImGui::MenuItem("Camera")) {
        result = CreateEntityKind::kCamera;
    }

    return result;
}

// Phase 14e: the Inspector panel's real content -- replaces the placeholder
// "Inspector -- coming in Phase 14e" TextWrapped() line at this file's one
// "Inspector" Begin/End call site below. Reads/writes `registry` directly
// (this class's pre-existing "just a Dear ImGui wrapper over data
// Application owns" role, same as renderSceneTreeNode() above) -- there is
// no EditorUI-owned Inspector state at all, matching selectedEntity_'s own
// application.hpp comment on why Application, not EditorUI, is the one
// long-term home for what's selected.
//
// Three sections, matching the approved mockup: Transform (always fully
// live -- Transform is a genuinely per-entity ECS component, no
// shared-mutation concern), Material (read-only display this phase -- see
// material.hpp's own Phase 14e comment for exactly why editing it here would
// be a footgun), and Physics (the real static/dynamic split, wired through
// physics.hpp's setEntityStatic() -- see that function's own header comment
// for the full mechanism).
// Phase 15b: `activeDirectionalLight` -- see editor_ui.hpp's own Phase 15b
// header comment and renderDockspaceShell()'s own Phase 15b comment for what
// this is (Application's own activeDirectionalLight_, read-only here) and
// why it's threaded through as a third parameter rather than looked up some
// other way.
//
// Phase 15f: two more parameters, both specifically for the Material
// section's newly-real "Browse..." button. `assetTree` is EditorUI's own
// `assetTree_` (Phase 15d, built once in the constructor) -- the Material
// section's texture-picker popup below walks its "textures" subtree to list
// candidates, reusing that already-built forest rather than re-walking the
// filesystem a second time (see asset_browser.hpp's own "Caching" comment
// for why assetTree_ is a cache in the first place). `textureAssignRequested`
// mirrors `saveSceneRequested`'s own "EditorUI reports intent, Application
// acts on it" shape (editor_ui.hpp's own Phase 15e comment) -- EditorUI has
// no ResourceManager to actually load a Texture through, so clicking a
// popup entry only records WHICH asset-relative path was picked; only
// Application::render() turns that into a real resources_.getTexture() call
// and a MaterialOverride component (material_override.hpp) on the selected
// entity, right after this whole call returns.
void renderInspectorPanel(EntityRegistry& registry, std::optional<EntityId>& selectedEntity,
                           std::optional<EntityId> activeDirectionalLight,
                           const std::vector<AssetTreeNode>& assetTree,
                           std::optional<std::string>& textureAssignRequested) {
    if (!selectedEntity.has_value()) {
        // Matches this panel's pre-14e placeholder tone (see the removed
        // "Inspector -- coming in Phase 14e" line) rather than an empty/
        // blank-looking panel -- this phase's own brief explicitly calls
        // for that.
        ImGui::TextWrapped("Inspector -- select an entity in the Scene panel to view/edit it.");
        return;
    }

    const EntityId id = *selectedEntity;
    Transform* transform = registry.getComponent<Transform>(id);
    if (transform == nullptr) {
        // Defensive only: every entity buildSceneTree() can ever select
        // (scene_hierarchy.hpp's own "which entities appear at all" comment)
        // has a Transform by construction, and nothing in this engine
        // destroys entities today (ecs.hpp's own EntityId comment) -- so a
        // selection can't currently outlive the Transform it pointed at.
        // Kept rather than assumed away so a future entity-destruction phase
        // (14f+) can't turn a stale selectedEntity_ into a null dereference
        // here.
        ImGui::TextWrapped("Selected entity no longer has a Transform.");
        return;
    }

    const NameComponent* nameComponent = registry.getComponent<NameComponent>(id);
    const std::string name =
        nameComponent != nullptr ? nameComponent->name : ("entity_" + std::to_string(id.index()));
    const ModelComponent* modelComponent = registry.getComponent<ModelComponent>(id);

    ImGui::TextUnformatted(name.c_str());
    ImGui::TextDisabled("%s", modelComponent != nullptr ? "Mesh entity" : "Empty entity (no model)");
    ImGui::Separator();

    // --- Transform -----------------------------------------------------
    // Fully live: every DragFloat/DragFloat3 below writes straight back into
    // the selected entity's own Transform component the instant it's
    // dragged, every frame it's being edited -- safe with no shared-cache
    // caveat at all, since (unlike ModelComponent's Model) Transform is a
    // genuinely per-entity ComponentPool<Transform> entry (ecs.hpp).
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 position = transform->position();
        if (ImGui::DragFloat3("Position", &position.x, 0.01f)) {
            transform->setPosition(position);
        }

        // Rot Y only, matching the approved mockup's own single "Rot Y"
        // field -- not full XYZ Euler like DebugUI's pre-existing "Scene
        // Entities" panel (Application::renderDebugUI(), application.cpp)
        // uses. Checked, not assumed: every entity in
        // assets/scenes/default.json today stores a rotation quaternion
        // whose only nonzero imaginary component is y (a pure rotation
        // around world Y -- "scene"'s own ~12-degree tilt included), so a
        // single Rot-Y field is an honest, lossless representation of every
        // rotation this scene actually contains, not a simplification that
        // silently drops information. glm::eulerAngles() is the same
        // decomposition DebugUI's panel already uses (proven correct there);
        // reading only its .y component here is exactly as correct for a
        // pure-Y quaternion, and the standard Euler gimbal-lock caveat still
        // applies in general (a non-pure-Y rotation reached some other way
        // -- there is no UI path to create one today -- would decompose
        // ambiguously/lossily through this one-axis readout, and EDITING
        // this field always REPLACES the whole rotation with a pure
        // angleAxis(Y) quaternion, discarding any pitch/roll component that
        // might otherwise be present). A future phase that needs to author
        // non-Y rotations from the Inspector should grow this into a full
        // DragFloat3, the same way DebugUI's own panel already does it.
        float rotationYDeg = glm::degrees(glm::eulerAngles(transform->rotation()).y);
        if (ImGui::DragFloat("Rot Y", &rotationYDeg, 0.5f)) {
            transform->setRotation(glm::angleAxis(glm::radians(rotationYDeg), glm::vec3(0.0f, 1.0f, 0.0f)));
        }

        glm::vec3 scale = transform->scale();
        if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.01f, 100.0f)) {
            transform->setScale(scale);
        }
    }

    // --- Material --------------------------------------------------------
    // Phase 15f: the diffuse texture is real/live now -- "Browse..." opens a
    // popup listing every file under assets/textures/ (reusing `assetTree`,
    // Phase 15d's own already-built forest -- see renderTextureBrowsePopup()
    // above), and picking one installs a MaterialOverride component
    // (material_override.hpp) on JUST this entity, never mutating the
    // shared, cached Model/Material every other entity loading the same
    // asset path also points at -- see material_override.hpp's own header
    // comment for the full "per-entity override, not clone-on-edit" design
    // this sidesteps the shared-cache hazard with. Tint/shininess stay
    // exactly as read-only as Phase 14e left them, deliberately -- see
    // material.hpp's own Phase 15f update to its Phase 14e comment for why
    // (briefly: the identical shared-cache hazard still applies to them, and
    // this phase's own brief only asks for the texture-assignment slice).
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (modelComponent != nullptr && modelComponent->model != nullptr) {
            const Material& material = modelComponent->model->primaryMaterial();

            ImGui::BeginDisabled();
            glm::vec3 tint = material.tint;
            ImGui::ColorEdit3("Tint", &tint.x, ImGuiColorEditFlags_NoInputs);
            ImGui::EndDisabled();
            ImGui::Text("Shininess: %.1f", static_cast<double>(material.shininess));

            // Phase 15f: prefer this entity's own MaterialOverride diffuse
            // texture, if it has one, over the shared Model's own baked-in
            // material.diffuseTexture() -- the exact same "override wins if
            // present, else fall back to the shared Model" rule
            // resolveDiffuseTextureOverride() (material_override.hpp) applies
            // at draw time, just read here for DISPLAY instead of for a
            // texture unit bind.
            const MaterialOverride* materialOverride = registry.getComponent<MaterialOverride>(id);
            const bool hasOverride = materialOverride != nullptr && materialOverride->diffuseTexture != nullptr;
            const Texture& diffuse = hasOverride ? *materialOverride->diffuseTexture : material.diffuseTexture();
            ImGui::TextWrapped("Texture: %s (%dx%d)%s",
                                diffuse.path().empty() ? "(unknown)" : diffuse.path().c_str(), diffuse.width(),
                                diffuse.height(), hasOverride ? " [override]" : "");
            ImGui::TextDisabled("Model asset: %s", modelComponent->path.c_str());

            if (ImGui::Button("Browse...")) {
                ImGui::OpenPopup("Choose Diffuse Texture");
            }
            renderTextureBrowsePopup(assetTree, textureAssignRequested);

            if (hasOverride) {
                ImGui::SameLine();
                // Reverts this one entity back to the shared Model's own
                // material -- removeComponent<MaterialOverride>(), not a
                // null-out-in-place, so resolveDiffuseTextureOverride()'s own
                // getComponent() lookup simply finds nothing here again,
                // exactly like an entity that never had an override at all
                // (and, per scene_serialization.hpp's own Phase 15f comment,
                // so the NEXT Save Scene correctly omits this entity's
                // "materialOverride" block instead of writing a stale one).
                if (ImGui::Button("Clear Override")) {
                    registry.removeComponent<MaterialOverride>(id);
                }
            }

            // See material.hpp's own Phase 14e comment (updated for Phase
            // 15f) for the full reasoning on why tint/shininess stay
            // read-only: this Model (and therefore this Material) is cached
            // and shared across every entity that loaded the same asset
            // path, so editing THOSE two fields here would still silently
            // repaint every other entity using the same model -- a real
            // footgun, not a hypothetical one, given "parented_demo_cube"
            // and "falling_cube" already share assets/models/
            // falling_cube.obj in this project's own default scene. The
            // diffuse texture above no longer has that problem -- it goes
            // through the per-entity MaterialOverride above instead.
            ImGui::TextWrapped(
                "Tint/Shininess are read-only: shared by every entity using the same model asset (see "
                "material.hpp). Diffuse texture above is a per-entity override -- safe to change.");
        } else {
            ImGui::TextDisabled("No model on this entity.");
        }
    }

    // --- Physics -----------------------------------------------------
    // The real static/dynamic split -- see physics.hpp's own setEntityStatic()
    // comment for the full mechanism this toggle drives, and this file's own
    // Phase 14e comment above for the section-by-section design summary.
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool hadRigidBody = registry.hasComponent<RigidBody>(id);
        const bool hadCollider = registry.hasComponent<Collider>(id);
        // "Static" in this engine's own architecture already IS "has a
        // Collider but no RigidBody" -- see physics.hpp's own top comment
        // and setEntityStatic()'s comment. An entity with NEITHER component
        // reads as unchecked here too (not static, not dynamic -- just
        // nothing yet), distinguished for the user via the label suffix
        // below rather than a separate widget.
        bool isStatic = !hadRigidBody && hadCollider;

        std::string staticLabel = "Static (Immovable)";
        if (!hadRigidBody && !hadCollider) {
            staticLabel += " (no physics yet)";
        }
        // Checking this ON removes any RigidBody and ensures a Collider
        // exists (added with its own struct default halfExtent, 0.25, if
        // missing) -- this is how "scene"/"parented_demo_cube" (neither has
        // ANY physics component today) become this phase's new
        // Collider-only static state for the first time. Unchecking it adds
        // a default RigidBody (zero velocity, gravity on) back, leaving any
        // existing Collider untouched -- see setEntityStatic()'s own header
        // comment for the exact mechanism.
        if (ImGui::Checkbox(staticLabel.c_str(), &isStatic)) {
            setEntityStatic(registry, id, isStatic);
        }

        if (RigidBody* body = registry.getComponent<RigidBody>(id)) {
            ImGui::Checkbox("Use Gravity", &body->useGravity);
        }

        // Shown for BOTH the static and dynamic states (whenever a Collider
        // exists), unlike Use Gravity above -- a static entity's Collider is
        // just as real and just as editable as a dynamic one's.
        if (Collider* collider = registry.getComponent<Collider>(id)) {
            ImGui::DragFloat("Collider Half-Extent", &collider->halfExtent, 0.01f, 0.01f, 10.0f);
        } else if (hadRigidBody) {
            // Phase 8e's own test coverage exercises exactly this
            // combination (a RigidBody with no Collider falls straight
            // through the ground, never colliding) -- a real, valid state,
            // just with nothing here to edit a half-extent for.
            ImGui::TextDisabled("No collider on this entity.");
        }

        if (!hadRigidBody && !hadCollider) {
            ImGui::TextWrapped(
                "This entity has no physics components yet. Check \"Static (Immovable)\" above to give it a "
                "Collider (an immovable physics object); uncheck it afterward to make it fall under gravity "
                "instead.");
        }

        // The caveat this phase's own brief explicitly requires stating
        // plainly, not hiding: see physics.hpp's own "What this deliberately
        // IS / IS NOT" comment -- this engine has no general entity-vs-
        // entity collision system yet, only per-entity gravity + a single
        // flat ground plane for RigidBody entities. A Collider-only static
        // entity is a real, correct architectural state (nothing ever
        // iterates it to move it, by construction -- stepPhysics() only
        // walks entities with a RigidBody), but nothing currently collides
        // against it either; it isn't yet load-bearing for gameplay.
        ImGui::TextWrapped(
            "Note: this engine has no entity-vs-entity collision yet. A static Collider here is a real "
            "physics state, but nothing currently collides against it -- only dynamic (RigidBody) entities "
            "get ground-plane collision.");
    }

    // --- Light -------------------------------------------------------------
    // Phase 15a: shown only for an entity that actually has a PointLight
    // component -- i.e. one created via the Create menu's new "Point Light"
    // item (or ENGINE_DEBUG_CREATE=pointlight), never for
    // "scene"/"falling_cube"/etc, which have no PointLight and so show no
    // Light section at all, the same "opt-in per entity, section only
    // appears when the component does" shape the Physics section above
    // already establishes for RigidBody/Collider.
    //
    // Fully live, like Transform -- not read-only like Material: a PointLight
    // is a genuinely per-entity ComponentPool<PointLight> entry (ecs.hpp),
    // with none of Material's "this Model, and therefore this Material, is
    // shared/cached across every entity using the same asset" caveat (see
    // this file's own Material section comment above), so there is no
    // shared-mutation footgun here to guard against.
    if (PointLight* light = registry.getComponent<PointLight>(id)) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &light->color.x);
            // Same (constant, linear, quadratic) attenuation profile
            // application.cpp's own kPointLights table already exposes as
            // three independent floats -- see that table's own comment for
            // what they mean. Range/step match the Inspector's existing
            // "Collider Half-Extent" DragFloat just above (Physics section).
            //
            // Constant's minimum is 0.01f, not 0.0f, for the same
            // "avoid a degenerate zero value" reason Collider Half-Extent's
            // own 0.01f floor exists just above: basic.frag/pbr.frag compute
            // point-light attenuation as 1.0/(constant + linear*distance +
            // quadratic*distance^2), and a fragment very close to the light
            // (distance ~ 0) with constant == 0.0 would divide by
            // (near-)zero, producing Inf/NaN that can corrupt the bloom
            // pass around that light. No prior code path could ever reach
            // constant == 0 before this live-editable field -- kPointLights'
            // own hardcoded table always uses 1.0.
            //
            // Linear and Quadratic keep 0.0f as their own minimum -- unlike
            // Constant, zero is a legitimate value for either of them alone
            // (it just turns off that one term of the attenuation curve) --
            // but a NEGATIVE linear or quadratic is just as hazardous as
            // constant == 0.0: it can still drive that same denominator to
            // exactly zero (or past it, flipping the light's contribution
            // sign) at some nonzero distance, even with constant floored at
            // 0.01 above. Typing is the only way in either case (a negative
            // drag-speed step never crosses 0.0f from a non-negative start).
            //
            // ImGuiSliderFlags_AlwaysClamp is required on all three fields,
            // not optional: the v_min/v_max pair alone only clamps
            // mouse-drag movement -- ImGui does NOT clamp a value typed
            // directly into the field (double-click or Ctrl+Click to type)
            // unless this flag is set (imgui.h, ImGuiSliderFlags_AlwaysClamp
            // = ClampOnInput | ClampZeroRange), so without it a user could
            // still type a value past any of these floors -- 0 or negative
            // for Constant, negative for Linear/Quadratic -- and hit the
            // same div-by-near-zero/sign-flip hazard above.
            ImGui::DragFloat("Constant", &light->constant, 0.01f, 0.01f, 10.0f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Linear", &light->linear, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Quadratic", &light->quadratic, 0.01f, 0.0f, 10.0f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextDisabled("Position comes from this entity's own Transform above.");
        }
    }

    // --- Light (Directional) ----------------------------------------------
    // Phase 15b: shown only for an entity with a DirectionalLight component --
    // same "opt-in per entity" shape as the PointLight section just above,
    // and just as fully live (a genuinely per-entity ComponentPool<
    // DirectionalLight> entry, no Material-style shared-cache caveat).
    //
    // Unlike PointLight's section, this one also surfaces something that has
    // no point-light equivalent: whether THIS entity is the one entity (if
    // any) actually driving the scene's single uLightDirection/uLightColor
    // pair and shadow frustum right now -- see light.hpp's own
    // resolveActiveDirectionalLight() comment and application.hpp's own
    // activeDirectionalLight_ comment for the full "why only one, and which
    // one" design. Without this line, two Directional Light entities would
    // render IDENTICAL Inspector sections while only one of them is actually
    // doing anything -- confusing in a way no other component in this
    // engine's Inspector has to account for, since every other component
    // type here (Transform, Material, Physics, PointLight) affects its own
    // entity only, never competes with a sibling entity for one shared
    // uniform slot.
    if (DirectionalLight* dirLight = registry.getComponent<DirectionalLight>(id)) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &dirLight->color.x);
            // No ImGuiSliderFlags_AlwaysClamp/min-max floor here, unlike
            // PointLight's Constant field just above -- see light.hpp's own
            // DirectionalLight comment for why a per-AXIS floor would be
            // actively wrong for a direction (any single axis can
            // legitimately be exactly 0.0 for a purely axis-aligned
            // direction). The one real hazard -- the whole VECTOR
            // degenerating to (at or near) zero -- is guarded where the
            // value is actually CONSUMED instead (resolveActiveDirectionalLight(),
            // light.cpp), which is the only place that can tell "the whole
            // vector" apart from "one axis" in the first place.
            ImGui::DragFloat3("Direction", &dirLight->direction.x, 0.01f);
            ImGui::TextDisabled(
                "Points FROM the light TOWARD the scene (same convention as this engine's fixed \"sun\").");

            const bool isActive = activeDirectionalLight.has_value() && *activeDirectionalLight == id;
            if (isActive) {
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
                                    "Active: this entity is currently lighting the scene (and casting its "
                                    "shadows).");
            } else {
                ImGui::TextDisabled(
                    "Inactive: only the most recently created Directional Light entity is active at a time; "
                    "this one exists but isn't currently affecting shading/shadows.");
            }
        }
    }

    // --- Camera --------------------------------------------------------------
    // Phase 15c: shown only for an entity with a CameraComponent -- the same
    // "opt-in per component" shape as the two Light sections just above, and
    // just as fully live (a genuinely per-entity ComponentPool<
    // CameraComponent> entry, no Material-style shared-cache caveat).
    //
    // Unlike either Light section, there is no "is this the active one"
    // line here -- see camera_component.hpp's own header comment and
    // CreateEntityKind::kCamera's own editor_ui.hpp comment for why: nothing
    // in this engine's rendering pipeline reads a CameraComponent at all
    // yet, so there is no shared resource for one entity to be "active" for
    // in the first place, unlike DirectionalLight's real competition over
    // this engine's one uLightDirection/uLightColor pair. This caption spells
    // that out explicitly rather than leaving it as a silent omission a user
    // could easily read as a bug -- the same "documented gap, not a quiet
    // one" treatment this whole phase's own scope decisions get (see
    // README.md's own Phase 15c section).
    if (CameraComponent* cameraComponent = registry.getComponent<CameraComponent>(id)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Range/step mirror engine::Camera's own setFov()/setClipPlanes()
            // intent (camera.hpp) -- an ordinary vertical FOV in degrees, and
            // a near plane that must stay strictly positive and (for the
            // AlwaysClamp floor below) comfortably less than the far plane so
            // glm::perspective() never receives a degenerate near>=far range.
            // ImGuiSliderFlags_AlwaysClamp for the same reason PointLight's
            // own Constant field needs it above: without it, typing a value
            // (double-click / Ctrl+Click) bypasses the v_min/v_max pair
            // entirely, which a plain mouse drag alone would respect.
            ImGui::DragFloat("Field of View", &cameraComponent->fovYDeg, 0.25f, 1.0f, 179.0f, "%.1f deg",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Near Plane", &cameraComponent->nearPlane, 0.01f, 0.001f, 1000.0f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Far Plane", &cameraComponent->farPlane, 1.0f, 0.001f, 100000.0f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextDisabled("Position/orientation come from this entity's own Transform above.");
            ImGui::TextDisabled(
                "Not yet wired to this engine's actual rendered view -- that still comes entirely from "
                "the engine's own free-fly camera, independent of this entity (see camera_component.hpp).");
        }
    }

    ImGui::Separator();
    // Phase 14f: real deletion. destroyEntityOrphaningChildren()
    // (transform_hierarchy.hpp) removes `id` from EVERY component pool that
    // currently exists (ecs.hpp's own generic EntityRegistry::
    // destroyEntity()) after first orphaning any direct children to their
    // own current world transform -- see that function's own header comment
    // for the full orphan-vs-cascade design this phase settled on (and
    // README.md's own Phase 14f section for the short version).
    //
    // `selectedEntity` is cleared in the SAME click, immediately after --
    // not left for next frame's defensive "Selected entity no longer has a
    // Transform" branch (this function's own early-return above, added
    // Phase 14e specifically anticipating this) to catch. That branch stays
    // exactly as it was regardless (a real, still-useful safety net for any
    // OTHER way selectedEntity could ever end up stale), but relying on it
    // here instead of clearing eagerly would leave the Inspector showing a
    // "no longer has a Transform" message for one full frame after a delete
    // rather than immediately falling back to its normal "nothing selected"
    // placeholder -- a needless, avoidable flash of the wrong message when
    // the right one is one line away.
    //
    // This is the LAST thing renderInspectorPanel() does: nothing below (in
    // fact, nothing else in this function at all, after this) reads `id`/
    // `transform`/`modelComponent`/`nameComponent`/etc. again this call, so
    // clicking Delete here can safely mutate `registry` out from under those
    // now-stale local pointers without any further use-after-free risk
    // within this same invocation.
    if (ImGui::Button("Delete Object")) {
        destroyEntityOrphaningChildren(registry, id);
        selectedEntity.reset();
    }
}

}  // namespace

EditorUI::EditorUI(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini persisted to disk -- same reasoning as DebugUI's own
    // constructor comment (Phase 8c): this phase's dockspace layout is
    // rebuilt programmatically every run anyway (see buildInitialLayout()
    // below), so there is nothing useful an on-disk layout would preserve
    // yet, and it avoids a stray file appearing next to wherever engine_app
    // happens to be launched from (including this project's own headless
    // verification harness's build/ directory). A later Phase 14 sub-phase
    // that actually wants cross-run layout persistence (once panels hold
    // real, resizable content worth remembering the arrangement of) can
    // revisit this.
    io.IniFilename = nullptr;

    // Phase 14a: this is the one flag this whole phase exists to flip on --
    // see CMakeLists.txt's own Phase 14a comment for why the vendored ImGui
    // tag had to change (v1.92.9b -> v1.92.9b-docking) for this flag/the
    // DockBuilder* API/DockSpaceOverViewport() to even exist.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // install_callbacks = true: see this class's own header comment on why
    // there can only be one ImGui context's worth of GLFW callbacks active
    // for this window, and why this -- not DebugUI -- is the one that
    // installs them. Safe for the same underlying reason DebugUI's own
    // Phase 8c comment already established: this engine has never
    // registered any GLFW callbacks of its own (input.hpp's InputState
    // polls Window::isKeyPressed()/getCursorPos() every frame instead), so
    // there is nothing pre-existing for ImGui's callbacks to clobber.
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersionString());

    // Phase 15d: the Assets panel's file/folder tree, built exactly once here
    // -- see this class's own header comment and asset_browser.hpp's own
    // "Caching" comment for why a constructor-time build (not a rebuild every
    // renderDockspaceShell() call) is the correct match for a filesystem
    // tree that cannot change at runtime today. resolveAssetPath("assets")
    // resolves relative to this executable's own directory, not the process
    // cwd -- the same mechanism every other asset path in this engine
    // already goes through (see paths.hpp), so this works correctly
    // regardless of where engine_app was actually launched from. Needs no GL
    // context (unlike the ImGui backend init just above) -- ordered after it
    // here only because it belongs conceptually with "Phase 15d's own
    // constructor-time setup," not because of any actual dependency between
    // the two.
    assetTree_ = buildAssetTree(resolveAssetPath("assets"));

    LOG_INFO("Editor UI initialized (always-on dockspace shell, Phase 14a)");
}

EditorUI::~EditorUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUI::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::buildInitialLayout(ImGuiID dockspaceId) {
    // Standard DockBuilder recipe (mirrors imgui_demo.cpp's own
    // ShowExampleAppDockSpace()): tear down and recreate the node
    // DockSpaceOverViewport() just created for `dockspaceId` so it can be
    // split into the approved-mockup layout below, rather than left as one
    // single undivided node the four Begin() calls in
    // renderDockspaceShell() would otherwise all pile into as overlapping
    // tabs.
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    // Left column (~22% width): split off first, then split again
    // vertically into Scene (upper) / Assets (lower). `centerId` keeps
    // being reassigned to "whatever's left after the most recent split" --
    // DockBuilderSplitNode's own contract (see imgui_internal.h).
    ImGuiID centerId = dockspaceId;
    ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.22f, nullptr, &centerId);
    // Right column (~28% of what remains after the left split): Inspector.
    ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.28f, nullptr, &centerId);
    // Left column split again, top/bottom: Scene above Assets.
    ImGuiID leftBottomId = ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Down, 0.45f, nullptr, &leftId);

    ImGui::DockBuilderDockWindow("Scene", leftId);
    ImGui::DockBuilderDockWindow("Assets", leftBottomId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    // Whatever's left in the center (the majority of the window) is the
    // Viewport -- see this class's own header comment on why it's placeholder
    // text only in this phase, not a render-to-texture 3D view yet.
    ImGui::DockBuilderDockWindow("Viewport", centerId);

    ImGui::DockBuilderFinish(dockspaceId);
}

CreateEntityKind EditorUI::renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                                                 std::optional<EntityId>& selectedEntity,
                                                 const SelectionOutline* outline,
                                                 std::optional<EntityId> activeDirectionalLight,
                                                 bool& saveSceneRequested,
                                                 std::optional<std::string>& textureAssignRequested,
                                                 std::optional<std::string>& assetDropRequested) {
    // Phase 15f: unconditionally reset at the top of every call, same
    // "false/empty every frame except the one where the real thing actually
    // happened" discipline saveSceneRequested (just above, Phase 15e) and
    // createRequest (below) both already follow -- a click on a popup
    // Selectable() sets this to a real path; every other frame, including
    // every frame the popup isn't even open, it stays empty.
    textureAssignRequested.reset();
    // Phase 15g: the identical reset, for the identical reason -- see this
    // method's own Phase 15g comment further down (at the Viewport's new
    // BeginDragDropTarget() block) for when this actually becomes non-empty.
    assetDropRequested.reset();
    // Phase 15e: this engine's first menu bar -- see this class's own Phase
    // 15e header comment for what/why. ImGui::BeginMainMenuBar() (a real
    // top-level menu bar spanning the whole viewport width, internally
    // implemented via BeginViewportSideBar() -- NOT a menu bar on the
    // dockspace host window DockSpaceOverViewport() builds just below, which
    // takes no window_flags parameter to request one through at all) is
    // called FIRST, before DockSpaceOverViewport(), so its own reservation
    // of screen space (shrinking ImGui::GetMainViewport()->WorkPos/WorkSize
    // by the menu bar's height) is already in effect by the time
    // DockSpaceOverViewport() reads that same viewport's work rect to size
    // its own host window -- confirmed visually, not just assumed (see this
    // phase's own README section): the four docked panels start just below
    // the menu bar rather than underneath/overlapping it. Deliberately
    // outside the `if (!layoutBuilt_)` guard below -- unlike the dockspace's
    // own one-time DockBuilder split, a menu bar is ordinary immediate-mode
    // content that must be resubmitted every single frame like any other
    // ImGui:: call, the same way every panel's own Begin()/End() pair below
    // already is.
    saveSceneRequested = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // The shortcut string ("Ctrl+S") is display-only -- ImGui
            // MenuItem() shortcut text is never itself an input binding (see
            // Dear ImGui's own documentation for MenuItem()); the actual
            // keyboard shortcut is handled entirely outside this class, in
            // application.cpp's own run() (see this class's own Phase 15e
            // header comment for why: a Ctrl+S chord doesn't fit
            // InputActionMap's existing "OR of alternate single keys"
            // binding shape). Clicking this item and pressing the real
            // Ctrl+S chord both end up calling the exact same
            // Application::saveCurrentScene() either way -- this is just the
            // second of its two real trigger paths, not a parallel one.
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                saveSceneRequested = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // DockSpaceOverViewport() is the built-in "just cover the whole main
    // viewport" helper (creates its own invisible host window internally) --
    // simpler than manually building a host window + ImGui::DockSpace()
    // call, and sufficient for this phase's single-viewport, single-monitor
    // scope. ImGuiDockNodeFlags_PassthruCentralNode lets the still-directly-
    // rendered 3D scene (see this class's own header comment) show through
    // any part of the dockspace nothing is currently docked over, matching
    // this phase's documented "Viewport panel floats over/alongside the
    // existing 3D render, not yet a texture of it" intermediate state.
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    // Built exactly once per process run (io.IniFilename == nullptr above
    // means there's no on-disk layout to restore instead) -- guarded by
    // layoutBuilt_, not re-run every frame, so a user's own later drag-to-
    // resize/rearrange of these four panels isn't stomped on the next
    // frame.
    if (!layoutBuilt_) {
        buildInitialLayout(dockspaceId);
        layoutBuilt_ = true;
    }

    // Phase 14f: this frame's Create-menu request, if any -- see
    // renderCreateEntityMenuItems()'s own comment above and
    // editor_ui.hpp's own CreateEntityKind comment for why this is returned
    // from this whole function rather than acted on here (EditorUI has no
    // ResourceManager/Shader/Camera to build a new entity from -- only
    // Application does).
    CreateEntityKind createRequest = CreateEntityKind::kNone;

    ImGui::Begin("Scene");
    {
        // Phase 14f: the Create menu -- a small "+" button plus, reaching
        // the exact same popup, a right-click anywhere else in this panel's
        // own background (ImGuiPopupFlags_NoOpenOverItems keeps a
        // right-click ON an existing tree row from ALSO opening this menu,
        // so that row's own click-to-select handling (renderSceneTreeNode()
        // above) stays completely undisturbed -- confirmed by this phase's
        // own headless verification, not just assumed). Both share one popup id
        // ("SceneCreateMenu"): ImGui::OpenPopup() and
        // BeginPopupContextWindow() each independently compute that id as
        // `ImGui::GetCurrentWindow()->GetID("SceneCreateMenu")` -- the same
        // current window (this "Scene" Begin/End block) both calls run
        // inside -- so they resolve to the same popup regardless of which
        // one actually triggers it opening this frame.
        if (ImGui::SmallButton("+ Create")) {
            ImGui::OpenPopup("SceneCreateMenu");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(right-click for the same menu)");
        if (ImGui::BeginPopupContextWindow("SceneCreateMenu", ImGuiPopupFlags_NoOpenOverItems)) {
            createRequest = renderCreateEntityMenuItems();
            ImGui::EndPopup();
        }
        ImGui::Separator();

        // Phase 14d: rebuilt fresh every frame -- this engine's own scene has
        // a handful of entities (three today, see assets/scenes/default.json),
        // so there is no reason to cache/diff buildSceneTree()'s own small
        // allocation against a previous frame's tree the way a much larger
        // scene's editor might need to. Real Parent-component nesting (not a
        // flat "folder" label) -- see scene_hierarchy.hpp's own header
        // comment for why, and renderSceneTreeNode() above for how each
        // root/child row is actually drawn/selected.
        const std::vector<SceneTreeNode> tree = buildSceneTree(registry);
        for (const SceneTreeNode& root : tree) {
            renderSceneTreeNode(root, selectedEntity);
        }
    }
    ImGui::End();

    ImGui::Begin("Assets");
    {
        if (assetTree_.empty()) {
            // Genuinely empty forest (see asset_browser.hpp's own "Which
            // assets/ subdirectories are browsable" comment) -- neither
            // assets/models/ nor assets/textures/ exists under this
            // executable's own resolved assets/ directory. Not expected in
            // this project's own tree (both exist -- see the actual
            // directory listing this phase's own README section cites), but
            // a defensive, explicit message here is better than a
            // silently-blank panel if a future run is ever missing both.
            ImGui::TextWrapped("No browsable assets found under assets/models/ or assets/textures/.");
        }
        for (const AssetTreeNode& root : assetTree_) {
            renderAssetTreeNode(root, selectedAssetPath_);
        }
    }
    ImGui::End();

    ImGui::Begin("Viewport");
    {
        // Phase 14c: the panel's own available content-region size -- both
        // what ImGui::Image() below is sized to fill and what
        // viewportWidth()/viewportHeight() report for Application to read
        // next frame (see this class's own header comment on why next
        // frame, not this one). Recorded every call, even when the image
        // below is skipped.
        const ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        viewportWidth_ = static_cast<int>(contentRegion.x);
        viewportHeight_ = static_cast<int>(contentRegion.y);
        // Phase 14d: the Viewport panel's own on-screen top-left corner, in
        // absolute screen pixels -- captured here, BEFORE ImGui::Image()
        // below advances the cursor down by the image's own height, since
        // that's the point at which ImGui::GetCursorScreenPos() reports this
        // panel's content-region origin rather than somewhere past it. This
        // (plus contentRegion above) is the panel's whole on-screen
        // rectangle -- the outline projection below needs both, since the
        // Viewport panel does NOT fill the whole window (Scene/Assets/
        // Inspector occupy the rest, see this class's own Phase 14a layout).
        const ImVec2 panelScreenPos = ImGui::GetCursorScreenPos();

        if (viewportColorTexture != 0 && contentRegion.x > 0.0f && contentRegion.y > 0.0f) {
            // uv0=(0,1)/uv1=(1,0): flips vertically. OpenGL's texture
            // coordinate origin is the bottom-left texel, matching how
            // every one of this engine's own fullscreen shader passes
            // already samples a Framebuffer's color texture (e.g.
            // postprocess.frag) -- that convention reproduces the rendered
            // image right-side-up when drawn as a fullscreen NDC quad. Dear
            // ImGui's ImGui::Image(), however, treats uv (0,0) as the
            // image's top-left corner (the same convention its own font
            // atlas and every other UI texture use) -- so handing it this
            // texture with the default uv0=(0,0)/uv1=(1,1) would display
            // Application's 3D render upside down. Confirmed visually via
            // this phase's own headless screenshot, not just assumed --
            // see README.md's Phase 14c section.
            ImGui::Image(static_cast<ImTextureID>(viewportColorTexture), contentRegion, ImVec2(0.0f, 1.0f),
                         ImVec2(1.0f, 0.0f));

            // Phase 15g: the Viewport's own drop target -- attached to the
            // SAME ImGui::Image() item just submitted above
            // (BeginDragDropTarget()'s own contract, imgui.h, is identical to
            // BeginDragDropSource()'s: it targets whatever item was most
            // recently submitted). Accepts exactly the "ASSET_PATH" payload
            // renderAssetTreeNode() above now sets on every Assets-panel
            // row's own drag source -- AcceptDragDropPayload()'s own `type`
            // argument must match SetDragDropPayload()'s exactly, which is
            // why both ends share the one kAssetDragDropPayloadType constant
            // (this file's own top-of-file comment) rather than each hand-
            // writing "ASSET_PATH" separately where a typo in either copy
            // would silently mean drops here never fire at all.
            //
            // Deliberately just "any drop anywhere on this Image() widget,"
            // not a raycast into the 3D scene at the actual drop pixel --
            // this class draws no 3D content of its own to raycast against
            // (viewportColorTexture is an opaque, already-rendered color
            // texture by the time it reaches here, see this class's own
            // Phase 14c comment), and the one-frame render-to-texture lag
            // that same phase's own comment documents (this frame's image is
            // sized to the panel's PREVIOUS frame's dimensions) has no
            // bearing on this feature either way: there is no pixel-position
            // reasoning happening here at all, only "was anything dropped on
            // this panel this frame." See application.cpp's own
            // spawnEntityFromDroppedModel() comment for why an actual
            // raycast-based drop-exactly-here placement is a deliberately
            // separate, out-of-scope feature, not an oversight.
            //
            // EditorUI does no classification or mutation of its own here --
            // `payload->Data` is handed straight back out through
            // `assetDropRequested` verbatim (still just the flat path
            // string SetDragDropPayload() copied in, reinterpreted back to a
            // const char*), the same "report intent, let Application decide
            // and act" shape createRequest/textureAssignRequested already
            // establish for this exact reason.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragDropPayloadType)) {
                    assetDropRequested = std::string(static_cast<const char*>(payload->Data));
                }
                ImGui::EndDragDropTarget();
            }
        }
        // else: nothing rendered into viewportColorTexture yet, or the
        // panel's content region is currently degenerate (0 in some
        // dimension -- the very first frame before ImGui's docking layout
        // has settled, or a user dragging a divider all the way shut).
        // Leaving the panel body blank this frame is the documented,
        // simple choice here (see application.hpp's own Phase 14c comment
        // on the matching degenerate-size guard for Application's own
        // render targets) -- there is nothing meaningful to show yet
        // either way.

        // Phase 14d: the selection outline, drawn on top of the image above
        // via THIS SAME "Viewport" window's own draw list
        // (ImGui::GetWindowDrawList()) -- not the global foreground draw
        // list. Both compose on top of ImGui::Image() (a window's own draw
        // commands are submitted, and therefore rasterized, in the order
        // they're issued within that window, and the foreground draw list is
        // drawn on top of every window besides), but only the WINDOW draw
        // list is automatically clipped to this window's own visible
        // rectangle by Dear ImGui -- the foreground list is not clipped to
        // any one window at all, so a selection near the Viewport panel's own
        // edge could otherwise paint a stray fragment of dashed line over
        // whatever panel happens to be docked next to it. Confirmed by this
        // phase's own headless screenshot (see README.md's Phase 14d
        // section), not just assumed.
        if (outline != nullptr && contentRegion.x > 0.0f && contentRegion.y > 0.0f) {
            // NDC ([-1,1], +Y up) -> this panel's own screen pixels (+Y
            // down): the standard "u/v in [0,1], then scale by the panel's
            // own size and offset by its own screen-space origin" mapping --
            // note the Y flip (1.0f - v), same direction (though a distinct
            // reason) as ImGui::Image()'s own uv0/uv1 flip just above: NDC's
            // own +Y-up convention is the opposite of ImGui's own +Y-down
            // screen-pixel convention.
            const auto ndcToPanelScreen = [&](float ndcX, float ndcY) {
                const float u = (ndcX * 0.5f) + 0.5f;
                const float v = 1.0f - ((ndcY * 0.5f) + 0.5f);
                return ImVec2(panelScreenPos.x + (u * contentRegion.x), panelScreenPos.y + (v * contentRegion.y));
            };
            // outline->ndcMaxY is the NDC-space TOP edge (+Y up), which maps
            // to the smaller screen-Y (closer to the panel's own top) --
            // i.e. topLeft pairs ndcMinX with ndcMaxY, not ndcMinY.
            const ImVec2 topLeft = ndcToPanelScreen(outline->ndcMinX, outline->ndcMaxY);
            const ImVec2 bottomRight = ndcToPanelScreen(outline->ndcMaxX, outline->ndcMinY);

            // Teal accent, matching the approved mockup's own selection
            // color direction (a modern dark/teal-accented style) -- not a
            // pixel-perfect match to any one specific hex value (this
            // phase's brief explicitly doesn't require that), just a bright,
            // clearly-not-part-of-the-3D-scene color against this engine's
            // own rendered content.
            const ImU32 accentColor = IM_COL32(56, 217, 197, 255);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            addDashedRect(drawList, topLeft, bottomRight, accentColor);
            addCornerBrackets(drawList, topLeft, bottomRight, accentColor);
        }
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    renderInspectorPanel(registry, selectedEntity, activeDirectionalLight, assetTree_, textureAssignRequested);
    ImGui::End();

    return createRequest;
}

void EditorUI::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace engine
