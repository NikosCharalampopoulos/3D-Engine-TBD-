// Phase 18h: see undo_stack.hpp's own header comment for the full design.
// This file depends on nothing beyond its own header (which itself only
// pulls in GLM, <functional>, ecs.hpp for EntityId, and
// scene_serialization.hpp for SceneEntityRecord -- no ResourceManager/
// Shader/GL at all), the same minimal-dependency shape gizmo.cpp/
// camera_capture.cpp already have for the identical reason:
// tests/undo_stack_test.cpp links this file alone.

#include "engine/undo_stack.hpp"

namespace engine {

bool transformSnapshotsEqual(const TransformSnapshot& a, const TransformSnapshot& b) {
    return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}

Command makeTransformEditCommand(EntityId entity, const TransformSnapshot& before, const TransformSnapshot& after) {
    Command command;
    command.kind = CommandKind::kTransformEdit;
    command.entity = entity;
    command.before = before;
    command.after = after;
    return command;
}

Command makeCreateEntityCommand(EntityId entity, const SceneEntityRecord& record) {
    Command command;
    command.kind = CommandKind::kCreateEntity;
    command.entity = entity;
    command.record = record;
    return command;
}

Command makeDeleteEntityCommand(EntityId entity, const SceneEntityRecord& record) {
    Command command;
    command.kind = CommandKind::kDeleteEntity;
    command.entity = entity;
    command.record = record;
    return command;
}

void UndoStack::push(Command command) {
    // Drop any stale redo tail -- see this method's own header comment.
    commands_.resize(position_);
    commands_.push_back(std::move(command));
    position_ = commands_.size();
}

bool UndoStack::undo(const std::function<void(Command&)>& applyInverse) {
    if (!canUndo()) {
        return false;
    }
    applyInverse(commands_[position_ - 1]);
    --position_;
    return true;
}

bool UndoStack::redo(const std::function<void(Command&)>& applyForward) {
    if (!canRedo()) {
        return false;
    }
    applyForward(commands_[position_]);
    ++position_;
    return true;
}

void UndoStack::clear() {
    commands_.clear();
    position_ = 0;
}

}  // namespace engine
