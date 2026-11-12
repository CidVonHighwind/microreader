#pragma once

#include <cstdint>
#include <optional>

#include "Input.h"

namespace microreader {

class IRuntime {
 public:
  virtual ~IRuntime() = default;

  virtual bool should_continue() const = 0;
  virtual uint32_t frame_time_ms() const = 0;
  virtual void wait_next_frame() = 0;

  // Read battery level (0-100 percentage).
  // Returns empty optional if the platform does not have a battery.
  virtual std::optional<uint8_t> battery_percentage() const = 0;

  // Optional step-mode support for debugging.
  // step_mode() returns true when the loop should pause between ticks.
  // consume_step() returns true (exactly once per press) when the user
  // has requested one tick to advance.  Default: always run freely.
  virtual bool step_mode() const {
    return false;
  }
  virtual bool consume_step() {
    return false;
  }

  // Yield to the OS/RTOS scheduler.  Call inside tight loops to avoid
  // starving background tasks (e.g. the ESP32 watchdog).
  virtual void yield() {}

  // ── Clocks (used by reading statistics) ─────────────────────────────────
  // All have safe defaults so platforms/test stubs without a clock keep
  // working; statistics are simply not recorded there.

  // Milliseconds from a monotonic clock (wraps after ~49 days; callers use
  // unsigned subtraction). 0 = no monotonic clock available.
  virtual uint32_t monotonic_ms() const {
    return 0;
  }

  // Wall-clock time as seconds since 1970-01-01 00:00, expressed in LOCAL
  // time. The device never deals with time zones: the clock simply holds the
  // local wall time it was last set to. 0 = no wall clock available.
  virtual int64_t local_time() const {
    return 0;
  }

  // Set the wall clock (local time, see local_time()). Returns false if the
  // platform has no settable clock.
  virtual bool set_local_time(int64_t /*seconds*/) {
    return false;
  }
};

}  // namespace microreader
