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
#include "engine/asset_drop.hpp"
#include "engine/camera_capture.hpp"
#include "engine/camera_component.hpp"
#include "engine/editor_icons.hpp"
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
#include "engine/window_chrome.hpp"

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

// Phase 17b: encodes one Unicode codepoint as a raw UTF-8 byte sequence, so
// it can be prepended directly onto an ImGui row label string. Every
// codepoint editor_icons.hpp actually names (Font Awesome Free Solid's own
// Private Use Area glyphs, 0xF030-0xF1B2) falls in the three-byte UTF-8
// range (0x0800-0xFFFF) -- this deliberately does NOT handle the one- or
// two-byte cases, or surrogate pairs for codepoints above 0xFFFF, since
// nothing in this engine ever calls it with anything outside that range;
// widening it "to be general" for inputs that can't occur would be exactly
// the kind of speculative generality this codebase's own established style
// avoids (see e.g. ecs.hpp's own EntityId "no generation counter until a
// real need exists" comment for the same instinct). Dear ImGui's own
// TextUnformatted()/TreeNodeEx() etc. all take raw UTF-8 and decode it back
// to a codepoint internally (ImTextCharFromUtf8(), imgui.cpp) to look the
// glyph up in whichever merged ImFont source actually has it -- see this
// file's own EditorUI constructor comment for the font-atlas merge that
// makes that lookup succeed. A small local helper rather than reaching for
// <codecvt> (deprecated since C++17, this project's own language target) --
// three-byte UTF-8 encoding is a handful of bit shifts, not enough logic to
// justify either a new dependency or std library machinery two revisions
// deprecated.
std::string iconGlyphUtf8(char32_t codepoint) {
    std::string out;
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    return out;
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
// Icon glyphs (folder/mesh/light/camera, per the approved mockup) -- CLOSED
// Phase 17b. Until this phase, Dear ImGui's default font (no custom font
// atlas was built anywhere in this engine) only carried the ASCII/Latin-1
// glyph range, nowhere near the Unicode private-use/symbol code points an
// icon font needs -- adding one was real, separate scope this project
// deliberately deferred through the whole Phase 14/15/16 arc (see
// README.md's own Phase 17b section for why icon-font work landed before
// 17a's base theme pass, out of this arc's own lettered order). This
// function now prepends one Font Awesome Solid glyph -- resolved by
// editor_icons.hpp's own sceneNodeIconGlyph() from this node's own
// hasModel/hasPointLight/hasDirectionalLight/hasCamera flags (set once, in
// buildSceneTree() -- scene_hierarchy.cpp) -- to the row's own label text,
// UTF-8-encoded by this file's own iconGlyphUtf8() above. Indentation, the
// tree-node's own expand/collapse arrow, and ImGuiTreeNodeFlags_Selected's
// highlight still carry "this is a group vs. a leaf"/"this row is
// selected" exactly as before -- the icon is additive, not a replacement
// for any of that.
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

    // Phase 17b: "<icon>  <name>" -- one space-padded icon glyph ahead of
    // the existing label text, not a separate ImGui::Image()/column. This
    // is still exactly one TreeNodeEx() call/one selectable row, so
    // IsItemClicked() below (and everything else this function already
    // does) needs no change at all to keep working with the icon folded in.
    const char32_t icon =
        sceneNodeIconGlyph(node.hasModel, node.hasPointLight, node.hasDirectionalLight, node.hasCamera);
    const std::string label = iconGlyphUtf8(icon) + "  " + node.name;
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
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
// same "<icon>  <name>" label-prefixing icons Phase 17b gave that helper --
// see this function's own body below for the one real difference: an asset
// row's icon comes from classifyAssetDropPath(), not a SceneTreeNode's own
// precomputed flags, since AssetTreeNode carries no such flags of its own),
// just walking asset_browser.hpp's AssetTreeNode forest instead of
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

    // Phase 17b: the row's own icon. `assetPath` -- "assets/" +
    // node.relativePath -- is the SAME string classifyAssetDropPath()
    // already expects (AssetTreeNode::relativePath's own "relative to
    // assets/ itself" convention -- see this function's own Phase 15g
    // comment below for the identical prefixing on the drag payload), so
    // this reuses that one function rather than re-deriving "model or
    // texture" from the path a second, possibly-drifting way.
    // classifyAssetDropPath()'s own result is irrelevant for a directory
    // row (editor_icons.hpp's own assetNodeIconGlyph() checks isDirectory
    // FIRST and returns the folder icon outright), but it's still computed
    // unconditionally here rather than only for files -- branching on
    // node.isDirectory to skip it would save one cheap string classify call
    // at the cost of a second, separate code path this function does not
    // otherwise need.
    const std::string assetPath = "assets/" + node.relativePath;
    const char32_t icon = assetNodeIconGlyph(node.isDirectory, classifyAssetDropPath(assetPath));
    const std::string label = iconGlyphUtf8(icon) + "  " + node.name;
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
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
    // `assetPath` above (already "assets/" + node.relativePath, computed
    // once for this function's own Phase 17b icon lookup and reused here
    // rather than rebuilding an identical string a second time), matching
    // AssetTreeNode::relativePath's OWN "relative to assets/ itself"
    // convention the identical way renderTextureBrowsePopup()'s own
    // "assets/" + file->relativePath already is (this file's own Phase 15f
    // comment) -- not a small POD struct bundling path+category+isDirectory:
    // SetDragDropPayload() copies its `data` argument by raw byte value
    // (imgui.cpp), so anything containing a std::string/std::vector (owning
    // heap memory) would be unsafe to hand it this way; a flat char buffer
    // has no such hazard, and the one piece of information a drop target
    // actually needs -- WHICH asset -- is fully captured by the path alone
    // (category is re-derived from it, not shipped alongside it, so there's
    // no second field that could ever drift out of sync with the path
    // itself).
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kAssetDragDropPayloadType, assetPath.c_str(), assetPath.size() + 1);
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

// Phase 17a: the first item in the "Phase 17: visual design" arc's own
// LETTERED order to actually build (Phase 17b's icon font landed first, out
// of order, per that phase's own README section -- see this project's
// established "document the out-of-order landing, don't pretend it happened
// in sequence" precedent, e.g. Phase 15e's schema note). This phase is
// EXPLICITLY scoped to ImGui's own internal panel styling only -- the color
// palette, corner rounding, border treatment, and spacing every ImGui::
// Begin()'d panel/popup picks up automatically because ImGuiStyle is one
// global struct, not per-window state. It does NOT touch: the OS window's
// own title bar/border (that is borderless-window platform work, a much
// bigger separate Phase 17d), the toolbar row (Phase 17c, reuses this
// phase's colors once it exists), or any panel's actual rendering logic
// (renderInspectorPanel() etc. above are all completely unchanged by this
// function -- they inherit new colors purely because ImGui::Text()/
// ImGui::Button()/etc. always read the CURRENT ImGuiStyle, never a value
// baked in at the call site).
//
// Base + override, not "every ImGuiCol_ from scratch": ImGui::
// StyleColorsDark() (called immediately before this function, in EditorUI's
// constructor below) already gives every one of ImGuiStyle's ~50 Colors[]
// entries a coherent, battle-tested dark-theme value. Re-deriving all of
// them by hand here would both be far more code than this phase's actual
// visual delta justifies, and risk silently drifting some entry this phase
// never intended to touch. So this function runs strictly AFTER
// StyleColorsDark() and only overrides the specific entries that need to
// shift toward the reference mockup's dark/teal look; everything else
// (Text, PlotLines/PlotHistogram -- this engine draws no ImGui plots,
// Table* -- no ImGui tables anywhere in this codebase, TreeLines -- this
// engine's TreeNodeEx() calls never pass ImGuiTreeNodeFlags_DrawLines*,
// NavWindowing*/UnsavedMarker -- no multi-viewport Ctrl+Tab switcher or
// unsaved-document markers exist here) is left exactly as StyleColorsDark()
// set it, deliberately, rather than touched for its own sake.
//
// The one accent color, and where it came from: every "this is the
// highlighted/active thing" role below (a pressed button, a selected tree
// row, an active slider grab, a focused tab, a drag-drop target...) reuses
// the SAME teal, `kAccentTeal` below, rather than inventing a slightly
// different teal per widget family the way an unsystematic pass easily
// could. Its exact value -- RGB(45, 195, 178), hex #2DC3B2 -- was picked by
// sampling the reference mockup image directly (a small Python/Pillow
// script averaging a clean, JPEG-artifact-free patch of the mockup's
// "Use Gravity" toggle-on knob, the single most saturated, least
// gradient-blended teal swatch anywhere in that image -- the app-icon
// square and the toolbar's highlighted button are both visibly the same
// hue but rendered with a gradient/lower saturation that would have made a
// noisier reference point). `kAccentTealMuted` below is a SECOND directly-
// sampled value, not a formula-derived tint: it is the mockup's own
// selected-Scene-row background (`falling_cube`, RGB(28, 52, 54)) sampled
// the same way, used verbatim for `ImGuiCol_Header`/`ImGuiCol_TabSelected`
// (Dear ImGui's real "this row/tab is the selected one, at rest" colors --
// see imgui.h's own ImGuiCol_ enum comments) precisely because that IS what
// produced this exact pixel value in the mockup in the first place.
// Hover/active variants of both are DERIVED (linear lerp toward white/black
// by a fixed 15%) rather than independently eyeballed, so a hover state can
// never accidentally end up a visibly different hue from the color it is
// hovering over.
//
// Honest caveat this function's own README.md section (Phase 17a) repeats:
// this container's screenshot capture is software-rendered/llvmpipe, lower
// fidelity than the project owner's real Windows/GPU-accelerated build --
// close-to, not pixel-identical-to, the mockup is the actual bar this
// function is held to.
void applyEditorTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // The one accent teal, in its four roles (see header comment above).
    // Values are plain 0..1 floats (ImVec4's own convention -- imgui.h),
    // not 0..255 ints, so each is annotated with the 0..255 value it maps
    // from/to for anyone cross-checking against the mockup with a color
    // picker later.
    constexpr ImVec4 kAccentTeal        (0.176f, 0.765f, 0.698f, 1.00f);  // #2DC3B2 (45,195,178) - sampled
    constexpr ImVec4 kAccentTealHovered (0.300f, 0.800f, 0.740f, 1.00f);  // kAccentTeal lerped 15% toward white
    constexpr ImVec4 kAccentTealActive  (0.150f, 0.650f, 0.590f, 1.00f);  // kAccentTeal lerped 15% toward black
    constexpr ImVec4 kAccentTealMuted       (0.110f, 0.204f, 0.212f, 1.00f);  // #1C3436 (28,52,54) - sampled (mockup's own selected-row bg)
    constexpr ImVec4 kAccentTealMutedHovered(0.140f, 0.280f, 0.290f, 1.00f);  // a lighter hover shade of kAccentTealMuted --
                                                                              // eyeballed, not a strict 15% lerp like
                                                                              // kAccentTealHovered/kAccentTealActive above
    constexpr ImVec4 kAccentTealMutedActive (0.160f, 0.360f, 0.350f, 1.00f);  // a further step brighter, for the rarer mouse-held-down instant

    // Neutral dark backgrounds -- also sampled from the mockup (a flat
    // charcoal-navy, not pure black, with a second, slightly darker shade
    // for the menu bar/status bar/scrollbar track it uses for those
    // narrower always-on-screen strips).
    constexpr ImVec4 kBgPanel  (0.094f, 0.106f, 0.141f, 1.00f);  // #181B24 (24,27,36) - sampled panel/window background
    constexpr ImVec4 kBgSunken (0.059f, 0.071f, 0.090f, 1.00f);  // #0F1217 (15,18,23) - sampled menu-bar/status-bar background
    constexpr ImVec4 kBgField          (0.130f, 0.150f, 0.180f, 1.00f);  // one step lighter than kBgPanel -- reads as a distinct input box
    constexpr ImVec4 kBgFieldHovered   (0.160f, 0.200f, 0.220f, 1.00f);
    constexpr ImVec4 kBgControl        (0.160f, 0.190f, 0.220f, 1.00f);  // ordinary (non-accented) button/control resting color
    constexpr ImVec4 kBgControlHovered (0.220f, 0.260f, 0.290f, 1.00f);
    constexpr ImVec4 kBorderSubtle(0.250f, 0.280f, 0.320f, 0.50f);  // low-alpha separator/border line, not a hard edge

    colors[ImGuiCol_Text]         = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);  // off-white, not pure white -- matches the mockup's label color
    colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.47f, 0.50f, 1.00f);
    colors[ImGuiCol_TextLink]     = kAccentTeal;  // no hyperlink text exists in this engine today, but stays consistent if one ever does

    colors[ImGuiCol_WindowBg] = kBgPanel;
    colors[ImGuiCol_ChildBg]  = kBgPanel;  // flat -- children (e.g. the texture-picker popup's scroll list) don't visually nest a shade darker
    colors[ImGuiCol_PopupBg]  = ImVec4(0.086f, 0.098f, 0.129f, 0.98f);  // a touch darker + near-opaque so a floating popup reads as "above" its panel

    colors[ImGuiCol_Border]       = kBorderSubtle;
    colors[ImGuiCol_TitleBg]          = kBgSunken;
    colors[ImGuiCol_TitleBgActive]    = kBgSunken;  // this engine's dockspace has no un-focused-vs-focused title distinction worth drawing differently
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(kBgSunken.x, kBgSunken.y, kBgSunken.z, 0.75f);
    colors[ImGuiCol_MenuBarBg]        = kBgSunken;

    colors[ImGuiCol_FrameBg]        = kBgField;
    colors[ImGuiCol_FrameBgHovered] = kBgFieldHovered;
    colors[ImGuiCol_FrameBgActive]  = kAccentTealMuted;  // a field being actively dragged/typed into picks up the accent, not just a brighter neutral

    colors[ImGuiCol_ScrollbarBg]          = kBgSunken;
    colors[ImGuiCol_ScrollbarGrab]        = kBgControlHovered;
    colors[ImGuiCol_ScrollbarGrabHovered] = kAccentTealMuted;
    colors[ImGuiCol_ScrollbarGrabActive]  = kAccentTeal;

    colors[ImGuiCol_CheckMark]          = kAccentTeal;
    colors[ImGuiCol_CheckboxSelectedBg] = kAccentTealMuted;  // closest real Dear ImGui state to the mockup's teal-filled toggle switch (see Physics panel's Checkbox() calls) -- a real custom toggle-switch widget is not this phase's scope
    colors[ImGuiCol_SliderGrab]         = kAccentTealActive;  // resting grab: accent dimmed a step, so it doesn't fight the full-brightness Active state below
    colors[ImGuiCol_SliderGrabActive]   = kAccentTealHovered;  // brighter than resting while the grab is actually being dragged -- the clearest "you're mid-drag" cue

    colors[ImGuiCol_Button]       = kBgControl;
    colors[ImGuiCol_ButtonHovered] = kBgControlHovered;
    colors[ImGuiCol_ButtonActive]  = kAccentTeal;  // this phase's brief calls this one out explicitly -- matches the mockup's highlighted/active toolbar button

    // Header* drives CollapsingHeader/TreeNode/Selectable/MenuItem -- in
    // particular, a SELECTED-but-not-hovered Scene/Assets tree row (the
    // common case: mouse elsewhere, one entity selected) draws with plain
    // ImGuiCol_Header, exactly the mockup's own `falling_cube` row this
    // function's own kAccentTealMuted was sampled FROM -- so this one line
    // reproduces that pixel, not just something close to it.
    colors[ImGuiCol_Header]        = kAccentTealMuted;
    colors[ImGuiCol_HeaderHovered] = kAccentTealMutedHovered;
    colors[ImGuiCol_HeaderActive]  = kAccentTealMutedActive;

    colors[ImGuiCol_Separator]        = kBorderSubtle;
    colors[ImGuiCol_SeparatorHovered] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.60f);
    colors[ImGuiCol_SeparatorActive]  = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.90f);

    colors[ImGuiCol_ResizeGrip]        = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive]  = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.85f);

    // Tab* mirrors the Header* family's own reasoning one level up (a docked
    // panel sharing a dock node with another one, e.g. if a future run ever
    // tab-groups two of Scene/Assets/Viewport/Inspector): unselected tabs
    // stay a plain dark neutral, the selected tab picks up the same sampled
    // muted teal a selected TREE ROW uses, and TabSelectedOverline -- a thin
    // top-edge accent line Dear ImGui draws only on the focused, selected
    // tab -- gets the FULL bright accent, a small but real place a
    // full-saturation teal ends up visible even though this phase's actual
    // default layout (buildInitialLayout()) never puts two panels in one
    // tabbed dock node today.
    colors[ImGuiCol_Tab]                    = kBgSunken;
    colors[ImGuiCol_TabHovered]             = kAccentTealMutedHovered;
    colors[ImGuiCol_TabSelected]            = kAccentTealMuted;
    colors[ImGuiCol_TabSelectedOverline]    = kAccentTeal;
    colors[ImGuiCol_TabDimmed]              = kBgSunken;
    colors[ImGuiCol_TabDimmedSelected]      = kAccentTealMuted;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.60f);

    colors[ImGuiCol_DockingPreview] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.55f);
    colors[ImGuiCol_DockingEmptyBg] = kBgPanel;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.35f);

    // Phase 15g's own drag-and-drop (Assets tree -> Viewport) is the one
    // place this engine already draws these two colors every run that
    // actually drags an asset -- worth getting right, not leaving at
    // StyleColorsDark()'s generic yellow-orange.
    colors[ImGuiCol_DragDropTarget]   = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.90f);
    colors[ImGuiCol_DragDropTargetBg] = ImVec4(kAccentTeal.x, kAccentTeal.y, kAccentTeal.z, 0.15f);

    colors[ImGuiCol_NavCursor] = kAccentTeal;  // keyboard/gamepad nav highlight rect -- imgui.h notes ImGuiCol_NavHighlight is just this entry's pre-1.91.4 name

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);  // StyleColorsDark()'s own default here is a LIGHT gray dim, which would look wrong behind a dark theme's modal popups

    // Rounding: two families, matching the mockup's own visual hierarchy --
    // a slightly larger radius for "outer container" corners (a whole
    // panel, a popup) than for "control" corners (a button, an input field,
    // a scrollbar grab, a tab) sitting inside one, the same size
    // relationship the mockup's own panels-vs-buttons rounding shows at a
    // glance. ChildRounding sits between the two: a child region (e.g. the
    // texture-picker's scrolling list) reads as a sub-panel, not a full
    // outer window.
    style.WindowRounding    = 8.0f;
    style.PopupRounding     = 8.0f;
    style.ChildRounding     = 6.0f;
    style.TabRounding       = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;

    // Border treatment: the mockup's panels/input fields read as flat,
    // borderless shapes distinguished by FILL color (WindowBg vs FrameBg
    // above), not by a drawn edge -- so windows/children/frames all get
    // BorderSize 0. Popups are the one exception: PopupBorderSize stays at
    // its StyleColorsDark() default of 1.0f, a deliberate keep-not-a-miss --
    // a popup floats OVER a panel with the exact same fill color family
    // (kBgPanel-ish), so without SOME edge it would have no visible
    // boundary against whatever panel is behind it. This applies equally to
    // the Phase 14f Create-menu popup and the Phase 15f Material "Browse..."
    // popup -- neither's own rendering code needed a single line changed.
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize  = 0.0f;
    style.FrameBorderSize  = 0.0f;

    // Spacing: a bit more breathing room than StyleColorsDark()'s own
    // defaults (WindowPadding 8x8, FramePadding 4x3, ItemSpacing 8x4),
    // matching how much more generously spaced the mockup's own Properties
    // panel rows/toolbar buttons look next to Dear ImGui's stock density.
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding  = ImVec2(8.0f, 5.0f);
    style.ItemSpacing   = ImVec2(8.0f, 8.0f);
}

// Phase 17c: one Viewport-toolbar icon button -- a small shared helper
// rather than six near-identical ImGui::Button()/BeginDisabled()/
// SetTooltip() call sites, the same "extract once a widget+tooltip pattern
// repeats enough to warrant it" instinct Phase 14f's own
// disabledCreateMenuItem() lambda already established for the Create menu's
// Point Light/Directional Light/Camera items (that lambda no longer exists
// in this file -- Phase 15a/15b/15c made all three of its own items real,
// one by one -- but README.md's own Phase 14f section still records its
// exact original shape, which this helper's disabled branch below
// reproduces almost verbatim).
//
// `active`: true pushes ImGuiCol_Button to the CURRENT ImGuiStyle's own
// ImGuiCol_ButtonActive entry -- read back LIVE from ImGuiStyle rather than
// a second hardcoded accent-teal literal declared here, so this can never
// silently drift from whatever applyEditorTheme() (above) actually set that
// role to; applyEditorTheme() itself already documents ImGuiCol_ButtonActive
// as "matches the mockup's highlighted/active toolbar button" (see this
// file's own Phase 17a comment), so reading it back is not a repurposing,
// it's using that entry for precisely the role its own comment already
// names. false leaves the button its ordinary un-pushed color.
//
// `enabled`: false wraps the button in BeginDisabled()/EndDisabled() --
// this project's own established "shown per the approved mockup, but
// BeginDisabled()'d with an explanatory tooltip, because the real feature
// needs separate, larger scope" precedent. ImGuiHoveredFlags_
// AllowWhenDisabled is what lets IsItemHovered() still report a hover on a
// disabled item at all -- Dear ImGui's own default HoveredFlags do not, by
// design, since a disabled item is not normally meant to respond to the
// mouse in any way, tooltip included.
//
// `tooltip` is shown on hover either way, enabled or disabled -- an
// icon-only row (no visible text label anywhere) is exactly the kind of UI
// a tooltip is load-bearing for, not just a courtesy for the disabled
// half.
//
// Returns true the one frame this button was actually clicked. Always
// false while `enabled` is false -- BeginDisabled() itself already makes
// Button() report no click for a disabled item (Dear ImGui's own
// contract), not a second guard this helper adds on top.
bool toolbarIconButton(const char* strId, char32_t glyph, bool active, bool enabled, const char* tooltip) {
    const std::string label = iconGlyphUtf8(glyph) + "##" + strId;

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    }
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool clicked = ImGui::Button(label.c_str());
    if (!enabled) {
        ImGui::EndDisabled();
    }
    if (active) {
        ImGui::PopStyleColor();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return clicked;
}

// Phase 17c: the Viewport panel's own toolbar row -- six icon buttons
// (grid/lighting/texture-mode/undo/play/pause, left-to-right per the
// reference mockup -- see README.md's own Phase 17c section).
//
// Phase 18a: submits its six buttons, nothing else -- it no longer draws a
// trailing ImGui::Separator() (17c's original version did) or claims a row
// of its own inside the Viewport panel's layout. Originally this function
// WAS called as the very first thing inside the Viewport panel's Begin()/
// End() block, so the buttons occupied their own row above the rendered 3D
// image and pushed it down -- the project owner's own explicit complaint
// ("the toolbar is supposed to be on top of the scene not above it") is
// exactly that layout. Phase 18a moves the call site to AFTER the panel's
// ImGui::Image() (see renderDockspaceShell() below), inside
// renderViewportToolbarOverlay() (further below), which positions this same
// button row as a floating overlay layered on top of the already-submitted
// image instead -- see that function's own header comment for the
// mechanics. This function's own six ImGui::Button() calls, their
// active/enabled wiring, and their tooltips are otherwise byte-for-byte
// unchanged from 17c.
//
// Two of the six are REAL, wired to the exact same Application-owned bool
// members the F1 debug overlay's own "Render Passes" checkboxes already
// bind BY ADDRESS (Application::renderDebugUI(), application.cpp) --
// `ssaoDisabled`/`ssaoDebugMode` arrive here BY REFERENCE from
// renderDockspaceShell()'s own new parameters of the same name, so a click
// on either button mutates the identical runtime state the F1 overlay's own
// checkbox already reads/writes, not a second, parallel copy of it -- the
// same "EditorUI mutates Application's own state directly through a
// reference" shape `selectedEntity`/`cameraCaptureRequested` already
// establish elsewhere in this same function.
//
// The other four -- grid, undo, play, pause -- are shown (matching the
// mockup) but BeginDisabled()'d with an explanatory tooltip: this engine
// has no viewport ground-plane grid overlay, no undo/redo history, and no
// play/pause/restart simulation-state concept anywhere today. Verified, not
// assumed, by a whole-codebase search this phase's own README section
// records the results of -- every other hit for "grid" is the unrelated PBR
// sphere test-grid/cluster-culling grid (Phase 9/13a), and "undo"/"play"/
// "pause" turn up nowhere at all except editor_ui.hpp's own long-standing
// Phase 14a comment ("no real inspector..., no Play/Pause/Restart or other
// toolbar/menu-bar chrome") and transform_hierarchy.hpp's unrelated "there
// is no 'undo' in this editor" aside. Each would be real, separate,
// substantial scope this phase's own brief explicitly declines to
// half-build -- the identical judgment call Phase 14f's own Create-menu
// Point Light/Directional Light/Camera items already made (see README.md's
// own Phase 14f section).
//
// `pause` is additionally drawn in the active/teal state (`active=true`,
// unconditionally) -- matching the mockup's own pause button, shown
// highlighted -- while STILL `enabled=false`: this phase reproduces the
// mockup's visual exactly, without pretending a highlighted-but-inert
// button means this engine has some real "currently paused" state behind
// it (it does not -- see above).
void renderViewportToolbar(bool& ssaoDisabled, bool& ssaoDebugMode) {
    toolbarIconButton("grid", kIconGrid, /*active=*/false, /*enabled=*/false,
                       "Viewport grid overlay -- not implemented yet. This engine has no ground-plane "
                       "grid-drawing code anywhere today; a real one is separate, later scope.");

    ImGui::SameLine();
    // SSAO is fundamentally a LIGHTING technique (screen-space AMBIENT
    // OCCLUSION -- it darkens ambient/indirect light in creases and contact
    // points), so this "sun" button toggling ssaoDisabled is a real
    // rendering-mode concept honestly mapped to the mockup's own "lighting
    // toggle" slot, not a cosmetic reuse of the glyph for something
    // unrelated. `active` reads true when SSAO is currently DISABLED (the
    // non-default, "you toggled this" state) -- the same "highlighted = the
    // alternate state is currently engaged" reading a toggle button
    // ordinarily has, not "highlighted = the normal default."
    if (toolbarIconButton(
            "lighting", kIconDirectionalLight, /*active=*/ssaoDisabled, /*enabled=*/true,
            ssaoDisabled
                ? "Lighting: SSAO disabled (click to re-enable ambient occlusion). Same "
                  "Application::ssaoDisabled_ the F1 debug overlay's \"Disable SSAO\" checkbox already controls."
                : "Lighting: SSAO enabled (click to disable ambient occlusion). Same Application::ssaoDisabled_ "
                  "the F1 debug overlay's \"Disable SSAO\" checkbox already controls.")) {
        ssaoDisabled = !ssaoDisabled;
    }

    ImGui::SameLine();
    // The "image/texture-mode" slot maps to ssaoDebugMode -- toggling it
    // swaps the Viewport's own rendered picture between the ordinary shaded
    // scene and the raw SSAO occlusion buffer shown directly
    // (postprocess.frag's own Phase 13f uSSAODebug uniform, application.cpp)
    // -- a literal "which IMAGE is the Viewport showing" toggle, honestly
    // mapped rather than forced.
    if (toolbarIconButton(
            "texturemode", kIconTexture, /*active=*/ssaoDebugMode, /*enabled=*/true,
            ssaoDebugMode
                ? "Texture mode: showing the raw SSAO occlusion buffer (click to return to the normal shaded "
                  "view). Same Application::ssaoDebugMode_ the F1 debug overlay's \"SSAO debug view\" checkbox "
                  "already controls."
                : "Texture mode: click to show the raw SSAO occlusion buffer instead of the normal shaded view. "
                  "Same Application::ssaoDebugMode_ the F1 debug overlay's \"SSAO debug view\" checkbox already "
                  "controls.")) {
        ssaoDebugMode = !ssaoDebugMode;
    }

    ImGui::SameLine();
    toolbarIconButton("undo", kIconUndo, /*active=*/false, /*enabled=*/false,
                       "Undo -- not implemented yet. This engine has no edit-history/undo-stack concept "
                       "anywhere today; a real one is separate, later scope.");

    ImGui::SameLine();
    toolbarIconButton("play", kIconPlay, /*active=*/false, /*enabled=*/false,
                       "Play -- not implemented yet. This engine has no play/pause/restart simulation-state "
                       "concept -- physics simply runs continuously once the app starts; a real start/stop "
                       "concept is separate, later scope.");

    ImGui::SameLine();
    toolbarIconButton("pause", kIconPause, /*active=*/true, /*enabled=*/false,
                       "Pause -- not implemented yet (shown highlighted to match the reference mockup's own "
                       "default state, not because this engine actually tracks a paused/running flag). Same "
                       "missing simulation-state concept as \"Play\" above.");
}

// Phase 18a: draws renderViewportToolbar()'s six buttons as a floating
// overlay layered ON TOP of the Viewport panel's own already-submitted
// ImGui::Image() -- see renderViewportToolbar()'s own updated header
// comment above for what moved and why (the project owner's own complaint:
// "the toolbar is supposed to be on top of the scene not above it").
//
// Same "Viewport" ImGui::Begin()/End() window as the image itself, not a
// second, independent floating ImGui::Begin() window pinned over the same
// screen rectangle. Researched against this project's own vendored ImGui
// source (build/_deps/imgui-src/imgui.cpp/imgui.h), not assumed, before
// picking between the two techniques the project owner's own brief named:
//   - A second Begin() window would need ImGuiWindowFlags_NoDocking (this
//     Viewport panel is itself a dockable panel inside
//     DockSpaceOverViewport() -- an undecorated floating window positioned
//     over it would otherwise itself be a valid drop target the user could
//     accidentally dock something into, or that could itself get dragged),
//     and its own ImGui::Button() calls are items of a DIFFERENT ImGuiWindow
//     than the Viewport panel's -- IsAnyItemHovered() (imgui.cpp: `return
//     g.HoveredId != 0 || g.HoveredIdPreviousFrame != 0;`) is global, not
//     per-window, so it would actually still work for the double-click
//     guard below by accident, but only
//     because that one specific function happens to read global state, not
//     because the two-window design is otherwise sound here.
//   - Submitting the buttons as ordinary items of THIS SAME window instead
//     avoids all of that by construction: they are naturally part of the
//     Viewport panel's own hoverable content, IsAnyItemHovered() excludes
//     them from the double-click guard for the exact same reason it already
//     did in 17c (nothing about that check needed to change), and there is
//     no second window to accidentally dock into, resize, or have drift out
//     of sync with the panel's own current rectangle. This is the technique
//     used below.
//
// Re-anchoring: `originScreenPos` is the SAME `panelScreenPos` the caller
// (renderDockspaceShell(), below) already captures via
// ImGui::GetCursorScreenPos() fresh every single frame, right after this
// "Viewport" window's own Begin() -- this panel's actual on-screen
// rectangle can move or resize any frame the user redocks/resizes it (it is
// itself a dockable panel inside DockSpaceOverViewport()), so computing the
// overlay's position as an offset from THIS frame's `originScreenPos`,
// every frame, rather than caching a screen-space rectangle from any
// earlier frame, is what keeps it pinned to the panel's current top-left
// corner instead of drifting or clipping wrong after a redock.
//
// The background rect is drawn BEHIND the buttons despite being added to
// the draw list AFTER them, via ImDrawListSplitter (imgui.h) -- the same
// public two-channel split/merge idiom Dear ImGui's own Columns/Tables
// implementation uses for "know a group's own bounding box only after
// submitting it, but still need to paint something behind that group": the
// buttons are submitted into channel 1 first (so their own final size,
// read back via GetItemRectMin()/Max() on the enclosing BeginGroup()/
// EndGroup(), is known), then the translucent panel is painted into channel
// 0 sized to that now-known rect, and Merge() reorders the two channels'
// draw commands back into channel-0-before-channel-1 order in the final
// list -- behind, not on top of, the buttons -- without needing this
// function to hardcode the toolbar's own width up front. Both channels are
// still appended to THIS window's draw list after ImGui::Image()'s own
// draw command, so the whole overlay (background and buttons alike) still
// composes on top of the rendered 3D image either way.
// Post-review fix (after Phase 18a): `outBgMin`/`outBgMax` are new -- the
// caller's own double-click-to-capture guard (renderDockspaceShell(), below)
// needs this function's `bgMin`/`bgMax` (the translucent background rect
// computed further down) to close a real bug -- see that guard's own comment
// for the full story, and camera_capture.hpp's
// shouldRequestCameraCaptureFromDoubleClick() for the pure decision this rect
// now feeds. Written back through these two out-params rather than returned
// as a struct/pair so every existing call site keeps compiling unchanged
// except the one that now actually reads them.
void renderViewportToolbarOverlay(bool& ssaoDisabled, bool& ssaoDebugMode, ImVec2 originScreenPos,
                                   ImVec2& outBgMin, ImVec2& outBgMax) {
    // Matches applyEditorTheme()'s own WindowPadding (10,10) as the margin
    // from the panel's own top-left corner, and the same value again as the
    // background rect's own inset around the button group -- reusing an
    // already-established spacing constant rather than inventing a new
    // pixel literal for this one overlay.
    const float margin = ImGui::GetStyle().WindowPadding.x;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImDrawListSplitter splitter;
    splitter.Split(drawList, 2);

    // Channel 1: the real, interactive buttons -- exactly
    // renderViewportToolbar()'s own six ImGui::Button() calls, unmodified.
    splitter.SetCurrentChannel(drawList, 1);
    ImGui::SetCursorScreenPos(ImVec2(originScreenPos.x + margin, originScreenPos.y + margin));
    ImGui::BeginGroup();
    renderViewportToolbar(ssaoDisabled, ssaoDebugMode);
    ImGui::EndGroup();
    const ImVec2 groupMin = ImGui::GetItemRectMin();
    const ImVec2 groupMax = ImGui::GetItemRectMax();

    // Channel 0: a translucent panel behind the group just submitted --
    // reusing this theme's own ImGuiCol_WindowBg (applyEditorTheme()'s
    // `kBgPanel`, read back LIVE the same way toolbarIconButton()'s own
    // `active` path already reads ImGuiCol_ButtonActive back live, rather
    // than a second hardcoded color literal declared here) at a reduced
    // alpha -- translucent so the rendered 3D scene stays visible as the
    // actual viewport backdrop around and, faintly, through the bar, not a
    // large opaque strip obscuring it, while still dark enough that the
    // button icons/hover states stay legible over a bright rendered
    // background. ImGuiCol_Border (`kBorderSubtle`) frames it, the same
    // low-alpha separator treatment every other panel edge in this theme
    // already uses, so the overlay reads as a distinct floating surface
    // rather than a hard rectangle pasted over the image.
    splitter.SetCurrentChannel(drawList, 0);
    const ImVec4 panelBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const ImU32 overlayBg = ImGui::ColorConvertFloat4ToU32(ImVec4(panelBg.x, panelBg.y, panelBg.z, 0.72f));
    const ImU32 overlayBorder = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Border]);
    const ImVec2 bgMin(groupMin.x - margin * 0.5f, groupMin.y - margin * 0.5f);
    const ImVec2 bgMax(groupMax.x + margin * 0.5f, groupMax.y + margin * 0.5f);
    // WindowRounding (8.0f) -- the same "outer container" radius family
    // applyEditorTheme()'s own comment gives every full panel/popup, since
    // this overlay reads as one too, not a smaller "control" like an
    // individual button.
    drawList->AddRectFilled(bgMin, bgMax, overlayBg, ImGui::GetStyle().WindowRounding);
    drawList->AddRect(bgMin, bgMax, overlayBorder, ImGui::GetStyle().WindowRounding);

    splitter.Merge(drawList);

    outBgMin = bgMin;
    outBgMax = bgMax;
}

// Phase 17d: the custom title bar's own three window-control buttons
// (minimize/maximize-or-restore/close) draw plain geometric glyphs -- a
// line, a rectangle outline (or two overlapping ones for "restore"), and an
// X -- via ImDrawList primitives, rather than merging three more codepoints
// into this project's vendored icon font the way Phase 17b/17c's own tree-
// row/toolbar icons do. Deliberately NOT following that precedent this one
// time: a window-control glyph is one of the smallest, most universally
// recognized pieces of UI iconography that exists -- real Windows/macOS/
// GNOME/KDE title bars, and most cross-platform apps that draw their own
// (VS Code, Windows Terminal, JetBrains IDEs...), all draw these three
// shapes procedurally rather than shipping them as font glyphs, because
// three straight lines/a rectangle outline are simpler to draw directly than
// to hand-subset, vendor, and keep a THIRD generation of this project's own
// icon-font atlas synchronized with (re-running Phase 17b's own pyftsubset
// step a third time, regenerating assets/fonts/editor-icons.ttf again, three
// more editor_icons.hpp constants/kIconGlyphRanges entries) for shapes this
// simple. Sized/positioned relative to each button's own item rect
// (GetItemRectMin()/Max(), below) rather than a fixed pixel size, so they
// scale correctly if this project's UI font size ever changes -- the same
// "no hardcoded pixel literal that could drift out of sync with the rest of
// the UI" instinct editor_icons.hpp's own SizePixels=0.0f comment (Phase
// 17b) already established for the font-based icons.
void drawMinimizeGlyph(ImDrawList* drawList, ImVec2 rectMin, ImVec2 rectMax, ImU32 color) {
    const float centerY = (rectMin.y + rectMax.y) * 0.5f;
    const float pad = (rectMax.x - rectMin.x) * 0.28f;
    drawList->AddLine(ImVec2(rectMin.x + pad, centerY), ImVec2(rectMax.x - pad, centerY), color, 1.5f);
}

void drawMaximizeGlyph(ImDrawList* drawList, ImVec2 rectMin, ImVec2 rectMax, ImU32 color) {
    const float pad = (rectMax.x - rectMin.x) * 0.28f;
    drawList->AddRect(ImVec2(rectMin.x + pad, rectMin.y + pad), ImVec2(rectMax.x - pad, rectMax.y - pad), color, 0.0f,
                       0, 1.5f);
}

// Two overlapping rectangle outlines -- the standard cross-platform
// "restore" convention (a smaller square offset toward one corner of a
// larger one). Deliberately just two plain outlines, not an attempt to
// "punch a hole" where they overlap the way a real vector icon would --
// that would need filling the overlap with whatever this button's own
// current background color happens to be (which changes with hover/press
// state), a fragile thing to keep in sync versus two outlines that read
// clearly as "restore" on their own regardless of what's underneath.
void drawRestoreGlyph(ImDrawList* drawList, ImVec2 rectMin, ImVec2 rectMax, ImU32 color) {
    const float pad = (rectMax.x - rectMin.x) * 0.32f;
    const float offset = (rectMax.x - rectMin.x) * 0.16f;
    drawList->AddRect(ImVec2(rectMin.x + pad + offset, rectMin.y + pad),
                       ImVec2(rectMax.x - pad + offset, rectMax.y - pad - offset), color, 0.0f, 0, 1.3f);
    drawList->AddRect(ImVec2(rectMin.x + pad - offset, rectMin.y + pad + offset),
                       ImVec2(rectMax.x - pad - offset, rectMax.y - pad), color, 0.0f, 0, 1.3f);
}

void drawCloseGlyph(ImDrawList* drawList, ImVec2 rectMin, ImVec2 rectMax, ImU32 color) {
    const float pad = (rectMax.x - rectMin.x) * 0.30f;
    drawList->AddLine(ImVec2(rectMin.x + pad, rectMin.y + pad), ImVec2(rectMax.x - pad, rectMax.y - pad), color,
                       1.5f);
    drawList->AddLine(ImVec2(rectMin.x + pad, rectMax.y - pad), ImVec2(rectMax.x - pad, rectMin.y + pad), color,
                       1.5f);
}

// A square, otherwise-unlabeled ImGui::Button() -- the real interactive item
// each window-control button is built from (a real ImGui item with an ID, so
// IsAnyItemHovered() correctly excludes it from the title bar's own "empty
// area" drag/double-click detection below -- the identical guard Phase 17c's
// own Viewport double-click fix already established for the toolbar row's
// buttons, see this file's own Phase 17c comment on renderDockspaceShell()).
// Left un-styled (no PushStyleColor()) deliberately -- Dear ImGui's own stock
// Button/ButtonHovered/ButtonActive colors already give ordinary hover/press
// feedback for free; unlike toolbarIconButton()'s teal "this toggle is ON"
// state, a title-bar window-control button has no such persistent on/off
// state to indicate, so there is nothing here that calls for reusing (or,
// worse, inventing a new) accent color the way that helper does. In
// particular, the close button deliberately does NOT hover-highlight red the
// way many real OS title bars do -- this codebase has no "danger" color
// defined anywhere (confirmed: renderInspectorPanel()'s own "Delete Object"
// button, the one other genuinely destructive action in this whole UI, is a
// plain, unstyled ImGui::Button() too -- see that function's own comment),
// so inventing one here, for one button, would be exactly the kind of
// unscoped one-off visual choice Phase 17a's own "one accent teal, reused
// everywhere, not an unsystematic pass inventing a slightly different color
// per widget" discipline already rejected elsewhere in this file.
bool titleBarControlButton(const char* strId, float size) {
    return ImGui::Button(strId, ImVec2(size, size));
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

    // Phase 17a: the dark/teal editor theme (colors + rounding + spacing --
    // see applyEditorTheme()'s own header comment above for the full "why"
    // and the mockup-sampled color values). Must run AFTER StyleColorsDark()
    // (it overrides specific entries on top of that baseline, not instead
    // of it) and can run at any point before the first ImGui::NewFrame() --
    // placed immediately after StyleColorsDark() rather than after the font
    // setup below purely because that keeps every ImGuiStyle-related call in
    // this constructor contiguous; font atlas configuration and style
    // configuration are independent of each other either way.
    applyEditorTheme();

    // Phase 17b: this engine's first EXPLICIT font-atlas configuration.
    // Before this phase, io.Fonts held zero fonts of its own anywhere in
    // this codebase -- ImFontAtlasBuildMain() (imgui_draw.cpp) auto-calls
    // AddFontDefault() itself the first time the atlas is ever built if
    // nothing else already has (`if (atlas->Sources.Size == 0)
    // atlas->AddFontDefault();`) -- so every row's text has, until now,
    // always silently been that implicit fallback. Calling AddFontDefault()
    // here explicitly does two things at once: it gives MergeMode (below)
    // an existing ImFont to merge into (MergeMode's own contract --
    // imgui.h -- is "merge into PREVIOUS ImFont... make sure that a font
    // has already been added before"), and it makes this constructor --
    // not an implicit, easy-to-miss library fallback -- the one place this
    // engine's whole font setup actually lives, the natural spot a future
    // toolbar-icon phase (see editor_icons.hpp's own "Deliberately
    // general, not hardcoded to tree rows" comment) extends instead of
    // hunting for.
    io.Fonts->AddFontDefault();

    // A narrow, explicit glyph-ranges array covering exactly the six
    // codepoints editor_icons.hpp names -- each its own one-codepoint
    // {lo, hi} pair, zero-terminated per ImFontConfig::GlyphRanges' own
    // documented contract (imgui.h) -- built FROM those constants (see
    // editor_icons.hpp's own header comment for why the codepoints
    // themselves live there, not here) so this array can never list a
    // codepoint the row-drawing code doesn't also know about, or vice
    // versa. `static`: GlyphRanges' own contract is "THE ARRAY DATA NEEDS
    // TO PERSIST AS LONG AS THE FONT IS ALIVE" (imgui.h) -- a local
    // automatic-storage array would dangle the moment this constructor
    // returns, silently corrupting every later glyph lookup against it.
    //
    // Researched against the actual vendored ImGui, not assumed (the same
    // discipline Phase 16's own GLFW-cursor/ImGui-hover research applied):
    // this project's vendored v1.92.9b-docking build sets
    // ImGuiBackendFlags_RendererHasTextures (imgui_impl_opengl3.cpp's own
    // Init()), which routes font baking through a DYNAMIC per-glyph-on-
    // demand path (ImFontAtlasBuildMain(), imgui_draw.cpp) that bakes
    // whichever codepoint a draw call actually requests, from whichever
    // merged source actually has it -- GlyphRanges itself is only consulted
    // by the LEGACY eager-preload-everything path
    // (ImFontAtlasBuildLegacyPreloadAllGlyphRanges()), which is skipped
    // entirely once RendererHasTextures is true. So on THIS build, this
    // array's real effect is close to redundant -- the vendored subset
    // font below physically contains only these six glyphs anyway, dynamic
    // baking or not -- but it's still passed explicitly because (a) it's
    // the documented, standard MergeMode idiom every Dear ImGui icon-font
    // integration uses regardless of backend, (b) it costs nothing, and
    // (c) it keeps this code correct if this project's ImGui version, or
    // rendering backend, ever changes to one where RendererHasTextures is
    // false.
    // Phase 17c: four more one-codepoint pairs appended below, exactly the
    // way this array's own Phase 17b comment above anticipated ("a future
    // phase adds one more named char32_t constant... appends it to
    // editor_ui.cpp's own kIconGlyphRanges array") -- kIconGrid/kIconUndo/
    // kIconPlay/kIconPause, for the new Viewport toolbar row
    // (renderViewportToolbar(), above). No entries added for the toolbar's
    // "lighting"/"texture-mode" buttons -- they reuse kIconDirectionalLight/
    // kIconTexture verbatim (see ToolbarButton/toolbarButtonIconGlyph()'s
    // own editor_icons.hpp comment), both already listed below.
    static constexpr ImWchar kIconGlyphRanges[] = {
        static_cast<ImWchar>(kIconCamera),            static_cast<ImWchar>(kIconCamera),
        static_cast<ImWchar>(kIconTexture),           static_cast<ImWchar>(kIconTexture),
        static_cast<ImWchar>(kIconFolder),            static_cast<ImWchar>(kIconFolder),
        static_cast<ImWchar>(kIconPointLight),        static_cast<ImWchar>(kIconPointLight),
        static_cast<ImWchar>(kIconDirectionalLight),  static_cast<ImWchar>(kIconDirectionalLight),
        static_cast<ImWchar>(kIconMesh),              static_cast<ImWchar>(kIconMesh),
        static_cast<ImWchar>(kIconGrid),              static_cast<ImWchar>(kIconGrid),
        static_cast<ImWchar>(kIconUndo),              static_cast<ImWchar>(kIconUndo),
        static_cast<ImWchar>(kIconPlay),              static_cast<ImWchar>(kIconPlay),
        static_cast<ImWchar>(kIconPause),             static_cast<ImWchar>(kIconPause),
        0,
    };

    // MergeMode = true targets the AddFontDefault() ImFont just added above
    // (Fonts.back(), per MergeMode's own implementation -- imgui_draw.cpp's
    // ImFontAtlas::AddFont()) -- no ImGui::PushFont() anywhere in this
    // file's row-drawing code, since the icon glyphs live IN the one
    // default ImFont every row already draws with. SizePixels is left at
    // its ImFontConfig default (0.0f, "implicit reference size") rather
    // than a fixed pixel size -- matching AddFontDefault()'s own implicit
    // sizing keeps the icon glyphs auto-scaling in lockstep with the base
    // UI font instead of needing this constructor to hardcode/maintain a
    // second size constant that could drift out of sync with it.
    ImFontConfig iconFontConfig;
    iconFontConfig.MergeMode = true;
    const std::string iconFontPath = resolveAssetPath("assets/fonts/editor-icons.ttf");
    ImFont* iconFont =
        io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 0.0f, &iconFontConfig, kIconGlyphRanges);
    if (iconFont == nullptr) {
        // Missing/corrupt font file (e.g. a checkout that somehow lost
        // assets/fonts/) degrades to "rows draw with no icon glyph" -- the
        // exact pre-Phase-17b look -- rather than crashing the whole
        // engine over a cosmetic asset, the same "a cosmetic asset failure
        // is a LOG_WARN, not a fatal error" instinct asset_browser.cpp's
        // own unreadable-entry handling already established for the
        // Assets panel itself.
        LOG_WARN("Editor UI: failed to load icon font \"" + iconFontPath +
                  "\" -- Scene Hierarchy/Assets Browser rows will render without icon glyphs");
    }

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

// Phase 17d: the custom title bar row -- see this class's own header-comment
// Phase 17d paragraph and TitleBarAction's own comment (editor_ui.hpp) for
// the full design. Reserves its own strip of screen space at the TOP of the
// main viewport via ImGui::BeginViewportSideBar(ImGuiDir_Up, ...) -- the
// exact same internal mechanism ImGui::BeginMainMenuBar() itself uses
// (imgui_widgets.cpp: `BeginViewportSideBar("##MainMenuBar", viewport,
// ImGuiDir_Up, height, window_flags)`) -- called from renderDockspaceShell()
// BEFORE that method's own ImGui::BeginMainMenuBar() call, so the two
// reservations correctly ADD rather than overlap (BeginViewportSideBar()'s
// own implementation accumulates each call's height into the viewport's
// shared BuildWorkInsetMin, imgui_widgets.cpp) -- the File menu bar ends up
// docked directly beneath this new title bar, and DockSpaceOverViewport()
// (called after both) sizes the four docked panels into whatever work-rect
// space remains under both, exactly the same "reserve space first, dockspace
// reads the shrunk work rect second" relationship the File menu bar already
// had with the dockspace before this phase -- confirmed by inspection of
// BeginViewportSideBar()'s own source (this project's vendored
// build/_deps/imgui-src/imgui_widgets.cpp), not merely assumed, since this
// headless environment cannot itself distinguish "two bars stacked
// correctly" from "one bar drawn over the other" in a screenshot as
// unambiguously as a human eye could (see README.md's own Phase 17d section
// for the full verification-ceiling accounting).
void EditorUI::renderTitleBar(bool showCustomTitleBar, bool windowMaximized, std::pair<int, int> windowPos,
                               TitleBarAction& action) {
    // See this method's own editor_ui.hpp comment for why NOT drawing
    // anything at all (not even reserving screen space) is correct here,
    // rather than e.g. drawing the row but disabling its buttons the way
    // Phase 17c's own grid/undo/play/pause buttons stay visible-but-inert:
    // those four are genuinely missing FEATURES this engine could still grow
    // into later; a native-decorated window is not "missing" a custom title
    // bar, it already has a real, working one the OS itself drew -- there is
    // nothing this method could show here that wouldn't just be a confusing,
    // redundant duplicate sitting directly below/inside the real one.
    if (!showCustomTitleBar) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Taller than the plain File-menu-bar row below it (GetFrameHeight()
    // alone, what BeginMainMenuBar() itself uses) -- this row needs to fit
    // the app icon/name comfortably, matching the reference mockup's own
    // visibly taller top strip. A multiplier of GetFrameHeight(), not a fixed
    // pixel literal, so it scales in lockstep with the base UI font size the
    // same way drawMinimizeGlyph()/etc.'s own relative sizing above already
    // does.
    const float height = ImGui::GetFrameHeight() * 1.6f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginViewportSideBar("##TitleBar", viewport, ImGuiDir_Up, height, flags)) {
        ImGui::End();
        return;
    }

    // --- Left: app icon + name --------------------------------------------
    // This project ships no dedicated app-icon image asset anywhere (no
    // .ico/.png app icon exists in assets/ today) -- a small filled, rounded
    // square standing in for one, the same "geometry, not a vendored image/
    // font glyph" choice this file's own drawMinimizeGlyph()/etc. above
    // already make for the window-control buttons, for the identical reason
    // (simpler than sourcing/vendoring a real icon asset for one small
    // swatch). Colored via the SAME live-read accent teal
    // toolbarIconButton() already reads (ImGuiCol_ButtonActive) -- reusing
    // the one established accent rather than a second hardcoded literal,
    // matching applyEditorTheme()'s own "one accent teal, reused everywhere"
    // discipline (Phase 17a).
    const float iconSize = 18.0f;
    const ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
    const ImVec2 iconMin(cursorScreenPos.x + 12.0f, cursorScreenPos.y + (height - iconSize) * 0.5f);
    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
    ImGui::GetWindowDrawList()->AddRectFilled(iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f);
    ImGui::SetCursorPosX(12.0f + iconSize + 8.0f);
    ImGui::SetCursorPosY((height - ImGui::GetFontSize()) * 0.5f);
    // "Engine Studio" -- this project's reference mockup's own name for
    // itself (README.md's own Phase 17a section already refers to it by this
    // name descriptively: "a target 'Engine Studio' editor look"), reused
    // verbatim here as the one place this project actually RENDERS that name
    // in its own UI. Deliberately NOT the same string as the native OS
    // window title (main.cpp's own "3D Engine", passed to glfwCreateWindow()
    // -- still what a taskbar/Alt-Tab switcher shows, since hiding the
    // native TITLE BAR via GLFW_DECORATED=false does not stop the OS from
    // tracking the window's title string for those other surfaces) -- a
    // real, honest, minor inconsistency this phase leaves as-is rather than
    // silently renaming main.cpp's own long-standing window title to match a
    // string that only ever existed as a mockup's own on-image label before
    // this phase, out of scope for what this phase's own brief actually
    // asked for (custom in-window chrome, not a rebrand of this whole
    // project's own external-facing window title).
    ImGui::TextUnformatted("Engine Studio");

    // --- Right: minimize / maximize-restore / close ------------------------
    const float buttonSize = height;
    const float buttonsWidth = buttonSize * 3.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - buttonsWidth);
    ImGui::SetCursorPosY(0.0f);

    const ImU32 glyphColor = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (titleBarControlButton("##titlebar_minimize", buttonSize)) {
        action.minimizeRequested = true;
    }
    drawMinimizeGlyph(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), glyphColor);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimize");
    }

    ImGui::SameLine();
    if (titleBarControlButton("##titlebar_maximize_restore", buttonSize)) {
        action.maximizeToggleRequested = true;
    }
    if (windowMaximized) {
        drawRestoreGlyph(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), glyphColor);
    } else {
        drawMaximizeGlyph(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), glyphColor);
    }
    if (ImGui::IsItemHovered()) {
        // "%s", not the ternary's raw `const char*` passed directly as
        // `fmt` -- the same non-literal-format-string guard
        // toolbarIconButton()'s own SetTooltip() call above already follows,
        // for the identical reason (a computed, not compile-time-constant,
        // format string).
        ImGui::SetTooltip("%s", windowMaximized ? "Restore" : "Maximize");
    }

    ImGui::SameLine();
    if (titleBarControlButton("##titlebar_close", buttonSize)) {
        action.closeRequested = true;
    }
    drawCloseGlyph(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), glyphColor);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Close");
    }

    // --- Drag-to-move + double-click-to-maximize on the empty backdrop ----
    // `overEmptyArea`: hovering this title-bar window but NOT hovering any
    // of the three buttons above -- the identical
    // `ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()` guard Phase
    // 17c's own Viewport double-click fix already established for
    // distinguishing "the empty backdrop" from "an interactive item sitting
    // on top of it" (see this file's own Phase 17c comment on
    // renderDockspaceShell(), at the Viewport panel's double-click check).
    const bool overEmptyArea = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();

    if (titleBarDragging_) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            titleBarDragging_ = false;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // See window_chrome.hpp's own header comment for exactly why
            // this frame's raw io.MouseDelta (not a remembered drag-start
            // anchor) is the correct input here, and why `windowPos` is
            // re-queried by Application fresh every frame rather than
            // tracked locally. Only actually requests a move when the mouse
            // moved at all this frame -- an idle held-down click reports
            // std::nullopt, so Application never issues a redundant
            // glfwSetWindowPos() call to the position the window is already
            // at.
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f) {
                const WindowPosition newPos = applyDragDelta(windowPos.first, windowPos.second, delta.x, delta.y);
                action.requestedWindowPos = std::make_pair(newPos.x, newPos.y);
            }
        } else {
            // Defensive: the button is no longer down but no
            // IsMouseReleased() edge was observed this frame either (e.g.
            // this window lost input focus mid-drag). Ends the drag rather
            // than leaving titleBarDragging_ stuck true forever, the same
            // "don't trust an edge you might have missed, fall back to the
            // level-triggered truth" instinct this codebase's own Phase 16
            // Escape/capture bug-fix comment already established for a
            // different missed-edge hazard.
            titleBarDragging_ = false;
        }
    } else if (overEmptyArea && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        titleBarDragging_ = true;
    }

    // A double-click's first press also satisfies the IsMouseClicked() check
    // just above (Dear ImGui's own documented behavior: "note that a
    // double-click will also report IsMouseClicked() == true" --
    // imgui.h) -- so a double-click on the empty area briefly starts (and,
    // one frame later, on the release between the two clicks, immediately
    // ends) a drag alongside setting maximizeToggleRequested below. Harmless:
    // the two clicks of a real double-click land within a few pixels of each
    // other, so the drag this briefly starts moves the window by at most a
    // few pixels of human hand-jitter before ending on its own -- the same
    // negligible jitter a real OS's own title bar exhibits on a double-click
    // in practice, not a bug introduced here.
    if (overEmptyArea && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        action.maximizeToggleRequested = true;
    }

    ImGui::End();
}

CreateEntityKind EditorUI::renderDockspaceShell(unsigned int viewportColorTexture, EntityRegistry& registry,
                                                 std::optional<EntityId>& selectedEntity,
                                                 const SelectionOutline* outline,
                                                 std::optional<EntityId> activeDirectionalLight,
                                                 bool& saveSceneRequested,
                                                 std::optional<std::string>& textureAssignRequested,
                                                 std::optional<std::string>& assetDropRequested,
                                                 bool cameraCaptured, bool& cameraCaptureRequested,
                                                 bool& ssaoDisabled, bool& ssaoDebugMode, bool showCustomTitleBar,
                                                 bool windowMaximized, std::pair<int, int> windowPos,
                                                 TitleBarAction& titleBarAction) {
    // Phase 17d: the identical "false/empty every frame except the one where
    // the real thing actually happened" reset every other out-parameter in
    // this function already follows (see e.g. cameraCaptureRequested just
    // below). renderTitleBar() (private, above) is called FIRST, before
    // BeginMainMenuBar() -- see that method's own top comment for exactly why
    // that ordering is what makes the title bar's own screen-space
    // reservation stack correctly ABOVE the File menu bar's, rather than
    // overlapping it.
    titleBarAction.minimizeRequested = false;
    titleBarAction.maximizeToggleRequested = false;
    titleBarAction.closeRequested = false;
    titleBarAction.requestedWindowPos.reset();
    renderTitleBar(showCustomTitleBar, windowMaximized, windowPos, titleBarAction);

    // Phase 16: the identical "false every frame except the one where the
    // real thing actually happened" reset saveSceneRequested/
    // textureAssignRequested/assetDropRequested below already follow.
    cameraCaptureRequested = false;
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
    // called before DockSpaceOverViewport(), so its own reservation of screen
    // space (shrinking ImGui::GetMainViewport()->WorkPos/WorkSize by the menu
    // bar's height) is already in effect by the time DockSpaceOverViewport()
    // reads that same viewport's work rect to size its own host window --
    // confirmed visually, not just assumed (see this phase's own README
    // section): the four docked panels start just below the menu bar rather
    // than underneath/overlapping it. Deliberately outside the
    // `if (!layoutBuilt_)` guard below -- unlike the dockspace's own one-time
    // DockBuilder split, a menu bar is ordinary immediate-mode content that
    // must be resubmitted every single frame like any other ImGui:: call, the
    // same way every panel's own Begin()/End() pair below already is.
    //
    // Phase 17d update: no longer literally the first thing this method
    // does -- renderTitleBar() above now runs before it -- but the ordering
    // relationship THIS comment is actually about (menu bar reserved before
    // DockSpaceOverViewport() reads the work rect) is unchanged; the title
    // bar's own reservation simply stacks on top of both, via the identical
    // BeginViewportSideBar() mechanism (see renderTitleBar()'s own top
    // comment above for the full three-way stacking order: title bar, then
    // File menu bar, then whatever's left for the dockspace).
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
        //
        // Phase 18a: captured here, BEFORE anything else this panel
        // submits -- previously (17c) renderViewportToolbar() ran first and
        // claimed a row of its own, so this call actually measured whatever
        // vertical space the toolbar row had already eaten into, shrinking
        // the 3D view by exactly one toolbar row's height. The toolbar is
        // now a floating overlay drawn AFTER ImGui::Image() below (see
        // renderViewportToolbarOverlay(), further down in this same block)
        // rather than a layout row, so this is once again the panel's own
        // FULL content region, and the image fills the whole Viewport panel
        // the way it did before 17c's toolbar ever existed.
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
        // Phase 18a: also exactly the origin renderViewportToolbarOverlay()
        // below re-anchors the floating toolbar to every frame -- see that
        // function's own header comment for why a value re-captured here,
        // fresh every call, rather than cached from any earlier frame, is
        // what keeps the overlay correctly pinned after a redock/resize.
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

        // Phase 18a: the floating toolbar overlay -- called here, AFTER
        // ImGui::Image() above (present or skipped), so its own draw
        // commands land later in this window's draw list and therefore
        // paint ON TOP of the rendered 3D image, not underneath it. Drawn
        // unconditionally, the same as 17c's original always-visible
        // toolbar row was, regardless of whether a real image was actually
        // submitted this frame above (viewportColorTexture == 0 / a
        // degenerate content region are both early-run/edge conditions --
        // see the comment on the missing `else` just above -- not states
        // that should also hide the toolbar itself).
        // Post-review fix (after Phase 18a): `toolbarBgMin`/`toolbarBgMax`
        // are new -- written back by renderViewportToolbarOverlay() itself
        // (its own header comment), and fed into the double-click guard just
        // below to close a real bug that guard's own comment describes.
        ImVec2 toolbarBgMin;
        ImVec2 toolbarBgMax;
        renderViewportToolbarOverlay(ssaoDisabled, ssaoDebugMode, panelScreenPos, toolbarBgMin, toolbarBgMax);

        // Phase 16: the camera-capture trigger -- a double-click anywhere in
        // this panel's own content region, gated to only fire while NOT
        // already captured (see this class's own renderDockspaceShell()
        // header comment above on `cameraCaptured` for why this exact gate,
        // and why it's one of three redundant layers against a re-trigger,
        // not the only one). IsWindowHovered() with no flags reports
        // whether the mouse is over THIS window's content region
        // specifically (not blocked by a popup, not actually over a docked
        // sibling panel that merely overlaps this one on screen), which is
        // exactly the "scoped to this ONE panel" requirement this feature's
        // own brief calls out: only Dear ImGui itself knows that, given the
        // dockspace's current layout, which this project's own scripted
        // DockBuilder split (buildInitialLayout(), above) can even change
        // panel boundaries for at runtime via a user's own later
        // drag-to-rearrange. IsWindowHovered() answers "is the mouse over
        // this WINDOW," not "over this one ITEM," so submission order
        // relative to it doesn't itself matter for correctness.
        //
        // Phase 17c: `!ImGui::IsAnyItemHovered()` guards against a
        // double-click landing on a toolbar BUTTON also satisfying
        // IsWindowHovered() and spuriously ALSO requesting camera capture
        // on top of whatever that button click already did.
        // IsAnyItemHovered() reports whether Dear ImGui currently considers
        // ANY item hovered (imgui.cpp's own definition:
        // `return g.HoveredId != 0 || g.HoveredIdPreviousFrame != 0;`) --
        // global state, not scoped to one window -- true for a toolbar
        // button the mouse is over and false over the plain image backdrop.
        //
        // Phase 18a: this check moved from immediately after
        // renderViewportToolbar() (17c's own placement, when the toolbar
        // was the first thing submitted) to here, immediately after
        // renderViewportToolbarOverlay() above, for the identical reason
        // 17c's own comment already gave: IsAnyItemHovered() only correctly
        // excludes the toolbar's buttons for a double-click landing on one
        // of THEM if they have already been submitted THIS frame by the
        // time this check runs. renderViewportToolbarOverlay() still
        // submits those same ImGui::Button() calls as items of this exact
        // "Viewport" window (see that function's own header comment for why
        // that, rather than a second floating ImGui::Begin() window, was
        // chosen) -- so this guard needed no change of its own beyond moving
        // to stay after wherever the toolbar's buttons now happen to be
        // submitted. (That conclusion turned out to be incomplete -- see the
        // post-review fix immediately below.)
        //
        // Post-review fix (after Phase 18a): `!ImGui::IsAnyItemHovered()`
        // alone only excludes the toolbar's six BUTTON item rects. It does
        // NOT exclude renderViewportToolbarOverlay()'s own translucent
        // BACKGROUND rectangle (that function's `bgMin`/`bgMax`, now handed
        // back here as `toolbarBgMin`/`toolbarBgMax`) -- the rounded-corner
        // margin around the buttons and the small ImGui::SameLine() gaps
        // between them are covered by no button's hover ID at all. A
        // double-click landing in one of those gaps visually lands on
        // toolbar chrome, sitting directly on top of the 3D image since
        // Phase 18a, but was falling through this guard as if it had landed
        // on empty viewport space and incorrectly requesting camera capture.
        // `!ImGui::IsMouseHoveringRect(toolbarBgMin, toolbarBgMax)` closes
        // that gap the same way `!IsAnyItemHovered()` already closes the
        // button case, so the two together now cover the toolbar's entire
        // visible footprint, not just its individual buttons. The combined
        // decision is pulled out pure and testable as
        // engine::shouldRequestCameraCaptureFromDoubleClick()
        // (camera_capture.hpp) -- see tests/camera_capture_test.cpp for the
        // case exercising exactly this scenario (a point inside the
        // background rect but outside every button) headlessly, since a real
        // double-click gesture landing precisely in a toolbar gap is not
        // reproducible in this project's own Xvfb environment.
        if (shouldRequestCameraCaptureFromDoubleClick(
                cameraCaptured, ImGui::IsAnyItemHovered(),
                ImGui::IsMouseHoveringRect(toolbarBgMin, toolbarBgMax), ImGui::IsWindowHovered(),
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
            cameraCaptureRequested = true;
        }

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
