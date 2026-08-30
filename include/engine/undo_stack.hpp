#ifndef ENGINE_UNDO_STACK_HPP
#define ENGINE_UNDO_STACK_HPP

// Phase 18h: real undo/redo for the three editing actions this editor
// actually supports -- transform edits (Inspector DragFloat3 fields AND the
// Phase 18e translate gizmo), entity creation (the Scene panel's Create
// menu), and entity deletion (the Inspector's "Delete Object" button). The
// Viewport toolbar's own "undo" button has been an inert BeginDisabled()'d
// stub since Phase 17c, explicitly waiting for this (see editor_ui.cpp's own
// Phase 17c comment on renderViewportToolbar()); this phase finally wires it
// up, plus a new "redo" button alongside it.
//
// --- Shape: a tagged struct, not a virtual ICommand hierarchy -------------
// Matching this codebase's established "plain structs/enums + free
// functions over class-hierarchy polymorphism" style (gizmo.hpp's
// GizmoAxis/GizmoDragState state machine, shading_mode.hpp's ShadingMode
// enum + pure decideNextEditShadingMode(), camera_capture.hpp's
// decideCameraCapture()) -- Command below is a kind enum plus per-kind
// captured before/after data, not a virtual ICommand::undo()/redo()
// interface with three concrete subclasses. There is no runtime-extensible
// set of command types here (exactly three, closed by this phase's own
// confirmed scope), so there is nothing virtual dispatch would buy that a
// `switch` on CommandKind doesn't already give just as well, with none of
// the extra indirection/heap-allocation a polymorphic command object would
// otherwise need for a stack of them.
//
// --- What lives here, and what doesn't -------------------------------------
// This header (and undo_stack.cpp) is the PURE half: Command's own plain
// data, and UndoStack -- a push/undo/redo stack with standard command-stack
// semantics (push truncates any stale "redo" tail; undo/redo simply walk a
// position index back and forth). Deliberately does NOT know how to actually
// APPLY a command's effect to a live EntityRegistry -- undo()/redo() below
// take a caller-supplied callback instead, invoked with a mutable reference
// to the target Command so the caller can rewrite its `entity` field in
// place (see Command::entity's own comment for why that's necessary). That
// keeps this whole file exactly as GL/ECS-agnostic as gizmo.hpp/
// camera_capture.hpp already are -- fully unit-testable (tests/
// undo_stack_test.cpp) with synthetic Commands and no live registry/
// ResourceManager/Shader/GL context at all, the same "pure logic, its own
// small file, fully unit-testable" shape this whole codebase already
// establishes for exactly this reason. The actual apply logic --
// Application::undo()/redo() and their own private helpers, application.cpp
// -- is real, EntityRegistry/ResourceManager/Shader-touching code, verified
// headlessly (tools/run_headless.sh) instead, the same split
// scene_serialization.hpp/.cpp (pure) vs. scene_loader.cpp
// (EntityRegistry/ResourceManager-facing) already establishes for the
// identical reason.
//
// --- Entity creation/deletion: reusing scene-serialization's own record,
// not a parallel snapshot mechanism ----------------------------------------
// Command's kCreateEntity/kDeleteEntity kinds both carry a
// SceneEntityRecord (scene_serialization.hpp) -- the exact same plain-data
// snapshot type saveScene()/loadScene() already use to (de)serialize an
// entity's full component set (Transform, ModelComponent, RigidBody,
// Collider, MaterialOverride, PointLight, DirectionalLight, CameraComponent,
// Name, Parent) to/from a scene JSON file. Rather than hand-rolling a
// second, parallel per-component snapshot struct for undo/redo, this phase
// extracts captureEntityRecord()/restoreEntityFromRecord() out of
// scene_loader.cpp's own saveScene()/loadScene() bodies (see that header's
// own updated comment) so BOTH scene save/load and this undo/redo feature
// call the identical, already-exercised capture/restore logic -- see
// application.cpp's own Application::deleteEntity()/recreateCommandEntity()
// for exactly how the undo path calls them.
//
// This also settles how entity CREATION is undone/redone: rather than
// storing just a CreateEntityKind + spawn position and re-running
// spawnEntityFromCreateMenu() on redo (which would recompute the spawn
// position from wherever the camera happens to be BY THEN, not where it
// actually was at creation time -- silently wrong the moment the camera has
// moved since), Application::spawnEntityFromCreateMenu() captures a full
// SceneEntityRecord of the entity it just built and stores THAT. Redoing a
// creation is then byte-for-byte the same restoreEntityFromRecord() call
// deletion-undo already needs -- one mechanism, not two -- and is strictly
// more correct: the record already carries the entity's real,
// as-built name/position/components, not a recipe that has to be
// re-executed against possibly-changed ambient state. This is the "simplest
// correct increment" this project's own established style prefers over a
// cleverer-but-narrower kind+position pair.
//
// --- EntityId never recycles indices -- Command::entity gets rewritten in
// place ----------------------------------------------------------------
// ecs.hpp's own EntityId comment: EntityRegistry::create() hands out
// monotonically increasing indices, NEVER recycled, even after an entity is
// destroyed. So undoing a creation (destroy) then redoing it (recreate from
// the stored record) does NOT resurrect the original id -- it allocates a
// brand-new one. Likewise, undoing a deletion (recreate) produces a new id,
// and redoing that same deletion (destroy again) must destroy THAT new id,
// not the long-gone original one. Command::entity is therefore not a fixed
// "which entity this command is about" label but a mutable "which live
// EntityId this command currently refers to" pointer, rewritten by
// Application's own apply callback every time a kCreateEntity/kDeleteEntity
// command's entity is destroyed/recreated -- documented here once rather
// than at every call site, since it's the one genuinely surprising piece of
// this design. kTransformEdit's own `entity` never changes (a transform
// edit never destroys/recreates anything), so it needs no such rewriting.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <functional>
#include <vector>

#include "engine/ecs.hpp"
#include "engine/scene_serialization.hpp"

namespace engine {

// A Transform's full position/rotation/scale state (transform.hpp), copied
// out by value -- exactly what a kTransformEdit command needs to remember
// both "before" and "after" an edit, and nothing more (no model-matrix
// caching, no entity reference of its own -- Command::entity above already
// carries that).
struct TransformSnapshot {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

// Exact-equality compare -- both snapshots this function is ever handed
// (renderInspectorPanel()'s/updateGizmo()'s own before/after pair, see
// editor_ui.cpp) are read straight from the SAME Transform's own
// position()/rotation()/scale() at two points in time with no arithmetic in
// between when nothing actually changed, so there is no float-drift case
// here that would call for an epsilon compare -- an edit session that never
// moved the value produces bit-identical snapshots, which is exactly the
// "don't push a no-op undo step" case this exists to detect (see
// editor_ui.cpp's own call sites).
bool transformSnapshotsEqual(const TransformSnapshot& a, const TransformSnapshot& b);

enum class CommandKind {
    kTransformEdit,
    kCreateEntity,
    kDeleteEntity,
};

// One undoable/redoable action, tagged by `kind` -- see this header's own
// top comment for why a plain tagged struct, not a virtual command
// hierarchy. Only the fields relevant to `kind` are meaningful at any given
// time (the other kind's fields are simply left at their defaults) --
// exactly the same "kind enum, per-kind payload, caller reads the field
// that matches" shape gizmo.hpp's own GizmoDragResult/AxisHitTestResult
// already establish.
struct Command {
    CommandKind kind = CommandKind::kTransformEdit;

    // The entity this command is about. For kTransformEdit, fixed for the
    // command's whole lifetime. For kCreateEntity/kDeleteEntity, the
    // CURRENTLY LIVE id this command refers to -- see this header's own top
    // comment ("EntityId never recycles indices") for why this has to be
    // rewritten in place across undo/redo cycles, and by whom.
    EntityId entity;

    // kTransformEdit only: the Transform's own state immediately before and
    // immediately after the edit session that produced this command (a
    // completed Inspector DragFloat3 interaction, or a completed gizmo
    // drag -- never a per-frame snapshot mid-drag, so a multi-frame drag is
    // always exactly one Command, not one per frame).
    TransformSnapshot before;
    TransformSnapshot after;

    // kCreateEntity/kDeleteEntity only: the full captured component record
    // -- see this header's own top comment for why this is a
    // SceneEntityRecord (scene_serialization.hpp), reused rather than a
    // second, parallel snapshot type.
    SceneEntityRecord record;
};

// Small, named constructors -- purely for readability at call sites
// (application.cpp/editor_ui.cpp), each just an aggregate-init one-liner.
Command makeTransformEditCommand(EntityId entity, const TransformSnapshot& before, const TransformSnapshot& after);
Command makeCreateEntityCommand(EntityId entity, const SceneEntityRecord& record);
Command makeDeleteEntityCommand(EntityId entity, const SceneEntityRecord& record);

// A standard linear undo/redo command stack: a list of Commands plus a
// "position" index -- the number of commands currently applied, i.e. how
// many (from the front) are "done" -- rather than two separate undo/redo
// stacks, so push()'s own "drop the stale redo tail" behavior is a single
// vector resize instead of also clearing a second container.
//
//   commands_ = [c0, c1, c2, c3], position_ = 2
//     -> c0, c1 are "done" (undo() would undo c1 next); c2, c3 are a stale
//        redo tail from before some earlier undo(), still present until
//        either redo() walks back into them or a fresh push() discards them.
//
// Deliberately owns no EntityRegistry/ResourceManager/Shader/GL access of
// any kind -- see this header's own top comment for why undo()/redo() take
// a caller-supplied apply callback instead of doing the work themselves.
class UndoStack {
public:
    // Appends `command` as the new "most recent" entry, first discarding
    // any stale redo tail (every command from `position_` onward) --
    // standard command-stack semantics: once a NEW action is taken, the
    // "future" that undo() could have redone into is no longer reachable
    // (it no longer describes what would actually happen if replayed
    // against the registry's now-different state), so it's dropped rather
    // than kept around unreachable.
    void push(Command command);

    bool canUndo() const { return position_ > 0; }
    bool canRedo() const { return position_ < commands_.size(); }

    // Applies the INVERSE of the most recently done command: calls
    // `applyInverse(commands_[position_ - 1])` (a mutable reference, so the
    // callback may rewrite Command::entity -- see this header's own top
    // comment), then moves `position_` back by one. No-op (`applyInverse`
    // never called, returns false) if canUndo() is false.
    bool undo(const std::function<void(Command&)>& applyInverse);

    // The redo counterpart: applies the FORWARD effect of
    // `commands_[position_]`, then moves `position_` forward by one. No-op
    // (returns false) if canRedo() is false.
    bool redo(const std::function<void(Command&)>& applyForward);

    // Drops every command and resets position_ to 0 -- used when a fresh
    // scene is loaded (see application.hpp's own Phase 18h comment on
    // undoStack_ for why history is deliberately NOT preserved across a
    // scene load).
    void clear();

    std::size_t size() const { return commands_.size(); }
    std::size_t position() const { return position_; }

private:
    std::vector<Command> commands_;
    std::size_t position_ = 0;
};

}  // namespace engine

#endif  // ENGINE_UNDO_STACK_HPP
