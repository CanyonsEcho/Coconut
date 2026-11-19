#include "core/CommandHistory.h"

namespace coconut {

void CommandHistory::PushAndExecute(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return;
    cmd->Execute();
    PushExecuted(std::move(cmd));
}

void CommandHistory::PushExecuted(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return;
    if (cursor_ < stack_.size()) {
        stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(cursor_), stack_.end());
        if (savedCursor_ > cursor_) {
            savedCursor_ = static_cast<size_t>(-1);
        }
    }
    stack_.push_back(std::move(cmd));
    cursor_ = stack_.size();
    if (stack_.size() > kMaxHistory) {
        stack_.erase(stack_.begin());
        --cursor_;
        if (savedCursor_ > 0 && savedCursor_ != static_cast<size_t>(-1)) {
            --savedCursor_;
        } else {
            savedCursor_ = static_cast<size_t>(-1);
        }
    }
}

bool CommandHistory::Undo() {
    if (!CanUndo()) return false;
    --cursor_;
    stack_[cursor_]->Undo();
    return true;
}

bool CommandHistory::Redo() {
    if (!CanRedo()) return false;
    stack_[cursor_]->Execute();
    ++cursor_;
    return true;
}

void CommandHistory::Clear() {
    stack_.clear();
    cursor_ = 0;
    savedCursor_ = 0;
}

std::string CommandHistory::NextUndoDescription() const {
    if (!CanUndo()) return {};
    return stack_[cursor_ - 1]->Description();
}

std::string CommandHistory::NextRedoDescription() const {
    if (!CanRedo()) return {};
    return stack_[cursor_]->Description();
}

} // namespace coconut
