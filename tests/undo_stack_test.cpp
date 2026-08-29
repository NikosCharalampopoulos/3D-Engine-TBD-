// Phase 18h adds undo_stack_test, testing engine::UndoStack's own push/undo/
// redo/truncate-on-new-push semantics and the small pure helpers alongside it
// (transformSnapshotsEqual(), the makeXyzCommand() constructors) in isolation
// -- same "plain executable, links only the pure logic file it's testing"
// shape as gizmo_test/camera_capture_test above. undo_stack.cpp depends only
// on GLM (TransformSnapshot's own position/rotation/scale) and ecs.hpp/
// scene_serialization.hpp for the EntityId/SceneEntityRecord TYPES a Command
// carries -- neither header pulls in any GL/ResourceManager/Shader
// dependency (see undo_stack.hpp's own header comment), so this needs no
// live GL context/GPU/EntityRegistry/ResourceManager whatsoever. The
// EntityRegistry/ResourceManager-facing half this stack's real undo/redo
// effects are applied through (Application::undo()/redo() and their own
// private helpers, application.cpp) is verified headlessly instead (see
// README.md's own Phase 18h Verify section) -- the identical split
// scene_serialization_test.cpp (pure) vs. no dedicated unit test for
// scene_loader.cpp (EntityRegistry/ResourceManager-facing, verified
// headlessly) already establishes.

#include "engine/undo_stack.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

using engine::Command;
using engine::CommandKind;
using engine::EntityId;
using engine::makeCreateEntityCommand;
using engine::makeDeleteEntityCommand;
using engine::makeTransformEditCommand;
using engine::SceneEntityRecord;
using engine::TransformSnapshot;
using engine::transformSnapshotsEqual;
using engine::UndoStack;

// A trivial helper so test call sites can build a distinguishable
// TransformSnapshot by a single float, rather than repeating a full
// glm::vec3/quat/vec3 literal at every call site below.
TransformSnapshot snapshotAt(float x) {
    TransformSnapshot snapshot;
    snapshot.position = glm::vec3(x, 0.0f, 0.0f);
    return snapshot;
}

}  // namespace

int main() {
    // --- transformSnapshotsEqual() ------------------------------------------
    {
        const TransformSnapshot a = snapshotAt(1.0f);
        const TransformSnapshot b = snapshotAt(1.0f);
        const TransformSnapshot c = snapshotAt(2.0f);
        assert(transformSnapshotsEqual(a, b));
        assert(!transformSnapshotsEqual(a, c));

        // Rotation/scale alone differing (position identical) also counts as
        // "not equal" -- this isn't just a position compare.
        TransformSnapshot d = a;
        d.scale = glm::vec3(2.0f, 1.0f, 1.0f);
        assert(!transformSnapshotsEqual(a, d));
    }

    // --- makeXyzCommand() constructors --------------------------------------
    {
        const EntityId entity(3);
        const Command transformCmd = makeTransformEditCommand(entity, snapshotAt(0.0f), snapshotAt(5.0f));
        assert(transformCmd.kind == CommandKind::kTransformEdit);
        assert(transformCmd.entity == entity);
        assert(transformSnapshotsEqual(transformCmd.before, snapshotAt(0.0f)));
        assert(transformSnapshotsEqual(transformCmd.after, snapshotAt(5.0f)));

        SceneEntityRecord record;
        record.name = "TestEntity";
        const Command createCmd = makeCreateEntityCommand(entity, record);
        assert(createCmd.kind == CommandKind::kCreateEntity);
        assert(createCmd.entity == entity);
        assert(createCmd.record.name == "TestEntity");

        const Command deleteCmd = makeDeleteEntityCommand(entity, record);
        assert(deleteCmd.kind == CommandKind::kDeleteEntity);
        assert(deleteCmd.entity == entity);
    }

    // --- UndoStack: fresh stack starts empty --------------------------------
    {
        UndoStack stack;
        assert(!stack.canUndo());
        assert(!stack.canRedo());
        assert(stack.size() == 0);
        assert(stack.position() == 0);

        // undo()/redo() on an empty stack are no-ops -- the callback must
        // never be invoked, and both report failure.
        bool called = false;
        assert(!stack.undo([&](Command&) { called = true; }));
        assert(!stack.redo([&](Command&) { called = true; }));
        assert(!called);
    }

    // --- UndoStack: push/undo/redo basic bookkeeping ------------------------
    {
        UndoStack stack;
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(0.0f), snapshotAt(1.0f)));
        assert(stack.size() == 1);
        assert(stack.position() == 1);
        assert(stack.canUndo());
        assert(!stack.canRedo());

        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(1.0f), snapshotAt(2.0f)));
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(2.0f), snapshotAt(3.0f)));
        assert(stack.size() == 3);
        assert(stack.position() == 3);

        // undo() walks position_ backward, one command at a time, handing
        // back each command in LAST-pushed-first order -- the caller's own
        // callback below records which `.after` value it saw so this test
        // can confirm the exact sequence, not just the final position.
        std::vector<float> undoneAfterValues;
        assert(stack.undo([&](Command& cmd) { undoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.position() == 2);
        assert(stack.canUndo());
        assert(stack.canRedo());

        assert(stack.undo([&](Command& cmd) { undoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.position() == 1);

        assert(stack.undo([&](Command& cmd) { undoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.position() == 0);
        assert(!stack.canUndo());
        assert(stack.canRedo());

        // Once more should be a no-op -- nothing left to undo.
        assert(!stack.undo([&](Command&) { undoneAfterValues.push_back(-1.0f); }));

        assert((undoneAfterValues == std::vector<float>{3.0f, 2.0f, 1.0f}));

        // redo() walks position_ forward again, FIRST-pushed-first this
        // time (the reverse traversal order of undo() above).
        std::vector<float> redoneAfterValues;
        assert(stack.redo([&](Command& cmd) { redoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.position() == 1);
        assert(stack.redo([&](Command& cmd) { redoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.redo([&](Command& cmd) { redoneAfterValues.push_back(cmd.after.position.x); }));
        assert(stack.position() == 3);
        assert(!stack.canRedo());
        assert((redoneAfterValues == std::vector<float>{1.0f, 2.0f, 3.0f}));

        // Once more should be a no-op -- nothing left to redo.
        bool calledAtEnd = false;
        assert(!stack.redo([&](Command&) { calledAtEnd = true; }));
        assert(!calledAtEnd);
    }

    // --- UndoStack: pushing after an undo truncates the stale redo tail ----
    {
        UndoStack stack;
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(0.0f), snapshotAt(1.0f)));
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(1.0f), snapshotAt(2.0f)));
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(2.0f), snapshotAt(3.0f)));
        assert(stack.size() == 3);

        // Undo twice -- position_ is now 1, with a two-command "redo tail"
        // (the commands whose `.after` are 2.0 and 3.0) still physically
        // present in the stack, unreachable except via redo().
        stack.undo([](Command&) {});
        stack.undo([](Command&) {});
        assert(stack.position() == 1);
        assert(stack.canRedo());

        // Pushing a brand-new command now must discard that stale tail --
        // standard command-stack semantics: the "future" undo() could have
        // redone into no longer describes what would actually happen
        // against the registry's current (already-reverted) state.
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(1.0f), snapshotAt(9.0f)));
        assert(stack.size() == 2);
        assert(stack.position() == 2);
        assert(!stack.canRedo());
        assert(stack.canUndo());

        // The new top-of-stack command is the one just pushed (after ==
        // 9.0), not either of the two stale, now-discarded ones (2.0/3.0).
        float undoneAfter = -1.0f;
        stack.undo([&](Command& cmd) { undoneAfter = cmd.after.position.x; });
        assert(undoneAfter == 9.0f);
    }

    // --- UndoStack: the apply callback may rewrite Command::entity in place
    // -- this is how Application::undo()/redo() (application.cpp) track a
    // create/delete command's CURRENTLY LIVE entity id across repeated
    // undo/redo cycles, since ecs.hpp's own EntityId never recycles indices
    // (see undo_stack.hpp's own header comment) -- a redo always allocates a
    // brand-new id, never resurrecting the original one. -----------------
    {
        UndoStack stack;
        SceneEntityRecord record;
        record.name = "Widget";
        stack.push(makeCreateEntityCommand(EntityId(5), record));

        // Simulate undo (destroying entity 5) then redo (recreating it as a
        // brand-new entity 42, exactly what a real EntityRegistry::create()
        // call after a destroy would hand back) -- the callback rewrites
        // cmd.entity, and that rewrite must be visible on this SAME
        // command's slot the next time it's targeted.
        assert(stack.undo([](Command& cmd) { assert(cmd.entity == EntityId(5)); }));
        assert(stack.redo([](Command& cmd) {
            assert(cmd.entity == EntityId(5));
            cmd.entity = EntityId(42);
        }));

        // A second undo of the SAME (now-redone) command must see the
        // rewritten id, not the original one.
        bool sawRewrittenId = false;
        assert(stack.undo([&](Command& cmd) {
            sawRewrittenId = (cmd.entity == EntityId(42));
        }));
        assert(sawRewrittenId);
    }

    // --- UndoStack: clear() resets both size and position -------------------
    {
        UndoStack stack;
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(0.0f), snapshotAt(1.0f)));
        stack.push(makeTransformEditCommand(EntityId(0), snapshotAt(1.0f), snapshotAt(2.0f)));
        stack.clear();
        assert(stack.size() == 0);
        assert(stack.position() == 0);
        assert(!stack.canUndo());
        assert(!stack.canRedo());
    }

    std::cout << "undo_stack_test: all checks passed" << std::endl;
    return 0;
}
