#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace coconut {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual std::string Description() const = 0;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

class LambdaCommand final : public ICommand {
public:
    LambdaCommand(std::string desc, std::function<void()> doFn, std::function<void()> undoFn)
        : desc_(std::move(desc)), doFn_(std::move(doFn)), undoFn_(std::move(undoFn)) {}

    std::string Description() const override { return desc_; }
    void Execute() override { if (doFn_) doFn_(); }
    void Undo() override { if (undoFn_) undoFn_(); }

private:
    std::string desc_;
    std::function<void()> doFn_;
    std::function<void()> undoFn_;
};

class CommandHistory {
public:
    void PushAndExecute(std::unique_ptr<ICommand> cmd);
    void PushExecuted(std::unique_ptr<ICommand> cmd);

    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < stack_.size(); }
    bool IsDirty() const { return cursor_ != savedCursor_; }
    void MarkSaved() { savedCursor_ = cursor_; }

    bool Undo();
    bool Redo();
    void Clear();

    size_t UndoDepth() const { return cursor_; }
    size_t RedoDepth() const { return stack_.size() - cursor_; }
    std::string NextUndoDescription() const;
    std::string NextRedoDescription() const;

private:
    std::vector<std::unique_ptr<ICommand>> stack_;
    size_t cursor_ = 0;
    size_t savedCursor_ = 0;
    static constexpr size_t kMaxHistory = 256;
};

} // namespace coconut
