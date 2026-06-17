// src/ResidentPersistentStore.h
#ifndef RESIDENT_PERSISTENT_STORE_H
#define RESIDENT_PERSISTENT_STORE_H

#include <Arduino.h>   // String
#include <cstddef>

namespace Resident {

// A place to persist the last successfully-loaded app source across reboots.
// Device builds use NVS (ResidentNvsStore.h); native tests inject a fake.
class PersistentStore {
public:
  // Open/prepare the backing store. Return false if unavailable (then the
  // Sandbox treats persistence as disabled). Default: nothing to do.
  virtual bool begin() { return true; }

  // Persist `len` bytes of `source`. Return false if it could not be stored
  // (e.g. too large for the medium) — the Sandbox emits `persist_too_big`.
  virtual bool save(const char* source, size_t len) = 0;

  // Return the stored source, or an empty String if nothing is stored.
  virtual String load() = 0;

  // Remove any stored source.
  virtual void clear() = 0;

  virtual ~PersistentStore() = default;
};

} // namespace Resident

#endif // RESIDENT_PERSISTENT_STORE_H
