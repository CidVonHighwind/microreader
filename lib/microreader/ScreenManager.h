#pragma once

#include <cstdio>

#include "HeapLog.h"
#include "display/DrawBuffer.h"
#include "screens/IScreen.h"

namespace microreader {

// Stack-based screen manager. Screens are pushed/popped; only the top screen
// is active (receives start/stop and update calls).
class ScreenManager {
 public:
  static constexpr int kMaxDepth = 8;

  // Push a new screen. Pauses the current top, starts the new one.
  // The new screen draws into buf; caller handles the actual refresh.
  void push(IScreen* screen, DrawBuffer& buf, IRuntime& runtime) {
    if (depth_ >= kMaxDepth)
      return;
    if (depth_ > 0)
      stack_[depth_ - 1]->pause();
    stack_[depth_++] = screen;
    screen->start(buf, runtime);
  }

  // Pop the top screen(s). Stops all removed screens, then resumes the new top.
  void pop(int count, DrawBuffer& buf, IRuntime& runtime) {
    if (count <= 0 || depth_ == 0)
      return;
    if (count > depth_)
      count = depth_;

    // Stop all screens being removed from the stack.
    for (int i = depth_ - 1; i >= depth_ - count; --i)
      stack_[i]->stop();
    HEAP_LOG("pop: after stop");
    depth_ -= count;
    if (depth_ > 0) {
      stack_[depth_ - 1]->resume(buf, runtime);
      HEAP_LOG("pop: after prev resume");
    }
  }

  void pop(DrawBuffer& buf, IRuntime& runtime) {
    pop(1, buf, runtime);
  }

  // Restart the top screen (stop + start).
  void restart_top(DrawBuffer& buf, IRuntime& runtime) {
    if (depth_ == 0)
      return;
    stack_[depth_ - 1]->stop();
    stack_[depth_ - 1]->start(buf, runtime);
  }

  IScreen* top() const {
    return depth_ > 0 ? stack_[depth_ - 1] : nullptr;
  }
  IScreen* bottom() const {
    return depth_ > 0 ? stack_[0] : nullptr;
  }

  // Swap the bottom (root) screen while another screen is on top of it. The
  // root is paused then, so the new one simply starts when it's uncovered.
  // Returns false when the root is the active screen (nothing is swapped).
  bool replace_bottom(IScreen* screen) {
    if (depth_ < 2 || !screen)
      return false;
    stack_[0] = screen;
    return true;
  }
  bool contains(const IScreen* screen) const {
    for (int i = 0; i < depth_; ++i)
      if (stack_[i] == screen)
        return true;
    return false;
  }
  int depth() const {
    return depth_;
  }
  bool empty() const {
    return depth_ == 0;
  }

 private:
  IScreen* stack_[kMaxDepth] = {};
  int depth_ = 0;
};

}  // namespace microreader
