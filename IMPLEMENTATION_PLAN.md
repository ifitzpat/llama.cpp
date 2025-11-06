# Model Management Implementation Plan (Revised)

**Version:** 2.0 (Production-Ready)
**Date:** 2025-11-06
**Status:** Ready for Implementation

This plan addresses all critical issues identified in the initial assessment and follows llama.cpp coding guidelines.

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Critical Fixes Addressed](#critical-fixes-addressed)
3. [Architecture Overview](#architecture-overview)
4. [Implementation Phases](#implementation-phases)
5. [API Specification](#api-specification)
6. [Code Examples](#code-examples)
7. [Testing Strategy](#testing-strategy)
8. [Coding Standards](#coding-standards)

## Executive Summary

Add HTTP endpoints for dynamic model loading/unloading to llama-server with:
- ✅ Asynchronous, non-blocking operations
- ✅ Thread-safe state management
- ✅ Security with path validation
- ✅ Graceful in-flight request handling
- ✅ Error recovery with rollback

### Key Features

| Feature | Status | Description |
|---------|--------|-------------|
| Async Jobs | ✅ Designed | Non-blocking HTTP with job tracking |
| Thread Safety | ✅ Designed | Atomic states, proper locking |
| Path Security | ✅ Designed | Whitelist validation, GGUF verification |
| Request Draining | ✅ Designed | RAII tracking, graceful shutdown |
| Error Recovery | ✅ Designed | Snapshot/rollback mechanism |
| Queue Management | ✅ Designed | Pause/resume coordination |

## Critical Fixes Addressed

### Issue #1: Thread Safety & Async Loading ✅

**Problem:** Synchronous loading blocks HTTP thread for 30+ seconds, race conditions in state transitions.

**Solution:**
- Job-based architecture with background worker thread
- Immediate `202 Accepted` response with `job_id`
- Atomic state transitions
- Progress tracking (0-100%)

**Components:**
```cpp
struct model_job {
    std::string job_id;
    model_job_type type;
    std::atomic<model_job_status> status;
    std::atomic<int> progress_pct;
    std::string progress_msg;
};

struct model_manager {
    std::thread worker_thread;
    std::deque<std::shared_ptr<model_job>> job_queue;
    std::unordered_map<std::string, std::shared_ptr<model_job>> jobs;
};
```

### Issue #2: Security & Path Validation ✅

**Problem:** Directory traversal attacks, arbitrary file access, loading non-GGUF files.

**Solution:**
- Whitelist-based directory access
- Path normalization (resolve symlinks, `..`)
- GGUF magic number validation
- File type verification

**Components:**
```cpp
struct path_validator {
    std::vector<std::string> allowed_directories;

    bool validate_model_path(const std::string & path, std::string & error_msg);
    bool is_valid_gguf(const std::string & path, std::string & error_msg);
    std::string normalize_path(const std::string & path);
};
```

### Issue #3: In-Flight Request Handling ✅

**Problem:** Active requests during model unload cause crashes.

**Solution:**
- RAII-based request tracking
- Stop accepting new requests before transition
- Wait for active requests to complete (with timeout)
- Force-cancel remaining requests

**Components:**
```cpp
struct server_context {
    std::atomic<int> active_request_count{0};
    std::atomic<bool> accepting_requests{true};

    struct request_guard {
        server_context * ctx;
        bool registered;
        request_guard(server_context * c);
        ~request_guard();
    };

    bool wait_for_requests_completion(int timeout_seconds);
    void force_cancel_all_slots();
};
```

### Issue #4: Error Recovery ✅

**Problem:** Failed loads leave server with no model, no recovery path.

**Solution:**
- Snapshot state before risky operations
- Automatic rollback on failure
- Manual reset endpoint
- Clear error messages

**Components:**
```cpp
struct server_context {
    struct model_snapshot {
        common_params params;
        std::string model_path;
        bool has_model;
    };

    model_snapshot last_good_state;

    void save_snapshot();
    bool restore_from_snapshot();
};
```

### Issue #5: Queue Management ✅

**Problem:** Inference tasks in queue become invalid when model changes.

**Solution:**
- Pause queue during transitions
- Clear pending tasks
- Resume after completion
- Compatible with existing queue implementation

**Components:**
```cpp
struct server_queue {
    std::atomic<bool> paused{false};

    void pause();
    void resume();
    int clear_pending_tasks();
    json get_queue_stats();
};
```

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                    HTTP Layer (Non-blocking)                  │
│  POST /v1/models/load → 202 Accepted {job_id}               │
│  GET  /v1/models/jobs/{job_id} → Job status                 │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              Model Management Controller                      │
│  - Job queue (thread-safe)                                   │
│  - Job status tracking                                       │
│  - Atomic state machine                                      │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────────┐
│           Model Management Worker Thread                      │
│  - Process jobs sequentially                                 │
│  - Drain in-flight requests                                  │
│  - Load/unload with progress tracking                        │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              Server Context (Thread-safe)                     │
│  - Fine-grained locking                                      │
│  - Graceful request completion                               │
│  - Resource cleanup                                          │
└──────────────────────────────────────────────────────────────┘
```

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1)

**Files:** `tools/server/server.cpp`

**Tasks:**
1. Add new server states (TRANSITIONING, NO_MODEL, ERROR)
2. Implement model_job struct
3. Implement model_manager with worker thread
4. Add path_validator with security checks
5. Add request_guard for RAII tracking

**Verification:**
- Unit tests for state transitions
- Path validation tests (directory traversal)
- Request tracking tests

**Coding Standards:**
```cpp
// Naming: snake_case, UPPER_CASE enums
enum server_state {
    SERVER_STATE_TRANSITIONING,   // ✅
};

struct model_job {                // ✅
    std::string job_id;           // ✅ longest common prefix
};
```

### Phase 2: Model Operations (Week 2)

**Tasks:**
1. Implement execute_load with progress tracking
2. Implement execute_unload with graceful draining
3. Implement execute_reload with rollback
4. Add model snapshot/restore

**Verification:**
- Load/unload cycle tests
- Rollback tests (simulate failures)
- Memory leak tests (valgrind)

**Critical Code Sections:**
```cpp
bool model_manager::execute_load(std::shared_ptr<model_job> job) {
    // 1. Validate paths
    // 2. Pause queue
    // 3. Stop accepting requests
    // 4. Drain active requests
    // 5. Save snapshot
    // 6. Unload current model (if any)
    // 7. Load new model
    // 8. Rollback on failure
    // 9. Resume accepting requests
    // 10. Resume queue
}
```

### Phase 3: HTTP Layer (Week 3)

**Tasks:**
1. Implement handle_model_load
2. Implement handle_model_unload
3. Implement handle_model_status
4. Implement handle_job_status
5. Implement handle_job_wait
6. Implement handle_model_reset
7. Update middleware_server_state
8. Register endpoints

**Verification:**
- API endpoint tests (pytest)
- Error response tests
- Concurrent request tests

**Endpoint Format:**
```
POST   /v1/models/load
POST   /v1/models/unload
GET    /v1/models/status
GET    /v1/models/jobs/{job_id}
GET    /v1/models/jobs/{job_id}/wait
POST   /v1/models/reset
```

### Phase 4: Queue Integration (Week 4)

**Tasks:**
1. Add pause/resume to server_queue
2. Integrate with model operations
3. Add queue statistics
4. Test queue behavior during transitions

**Verification:**
- Queue pause/resume tests
- Task clearing tests
- Stress tests (rapid load/unload)

### Phase 5: Polish & Production (Week 5)

**Tasks:**
1. Comprehensive error handling
2. Logging (SRV_INF, SRV_WRN, SRV_ERR)
3. Documentation updates
4. Performance optimization
5. Security audit
6. Code review

**Verification:**
- 1000 load/unload cycles (no leaks)
- Performance benchmarks
- Security penetration tests

## API Specification

### POST /v1/models/load

Load a new model (or reload with different parameters).

**Request:**
```json
{
  "model_path": "/path/to/model.gguf",
  "n_ctx": 4096,
  "n_gpu_layers": 32,
  "n_parallel": 4,
  "rope_freq_base": 10000.0,
  "rope_freq_scale": 1.0,
  "mmproj_path": "/path/to/mmproj.gguf"
}
```

**Response (202 Accepted):**
```json
{
  "job_id": "model_job_1699564823456_0",
  "status": "accepted",
  "message": "Model load request accepted"
}
```

**Error (400 Bad Request):**
```json
{
  "error": {
    "message": "Missing required field: model_path",
    "type": "invalid_request"
  }
}
```

### POST /v1/models/unload

Unload the currently loaded model.

**Response (202 Accepted):**
```json
{
  "job_id": "model_job_1699564823457_1",
  "status": "accepted",
  "message": "Model unload request accepted"
}
```

### GET /v1/models/status

Get current server and model status.

**Response:**
```json
{
  "server_state": "ready",
  "model_loaded": true,
  "accepting_requests": true,
  "active_requests": 2,
  "model_info": {
    "path": "/models/llama-7b.gguf",
    "n_ctx": 4096,
    "n_parallel": 4,
    "has_multimodal": false,
    "has_draft_model": false
  },
  "queue": {
    "pending_tasks": 3,
    "deferred_tasks": 0,
    "running": true,
    "paused": false
  }
}
```

### GET /v1/models/jobs/{job_id}

Get status of a specific job.

**Response:**
```json
{
  "job_id": "model_job_1699564823456_0",
  "status": "in_progress",
  "progress": 65,
  "progress_message": "Loading model layers",
  "elapsed_seconds": 12
}
```

**States:** `pending`, `in_progress`, `completed`, `failed`, `cancelled`

### GET /v1/models/jobs/{job_id}/wait?timeout=60

Wait for job completion (blocking, with timeout).

**Response:**
```json
{
  "job_id": "model_job_1699564823456_0",
  "status": "completed",
  "progress": 100,
  "progress_message": "Model loaded successfully",
  "duration_seconds": 45,
  "waited": true,
  "timeout_occurred": false
}
```

### POST /v1/models/reset

Reset server from error state (attempts snapshot restore).

**Response:**
```json
{
  "success": true,
  "message": "Server reset to last known good state",
  "model_path": "/models/llama-7b.gguf"
}
```

## Code Examples

### 1. Enhanced Server State

**Location:** `tools/server/server.cpp:53-56`

```cpp
enum server_state {
    SERVER_STATE_LOADING_MODEL,    // Initial startup
    SERVER_STATE_READY,            // Model loaded, accepting requests
    SERVER_STATE_TRANSITIONING,    // Model change in progress
    SERVER_STATE_NO_MODEL,         // No model loaded
    SERVER_STATE_ERROR,            // Recoverable error state
};
```

### 2. Model Job System

**Location:** After `server_metrics` struct (~line 2018)

```cpp
enum model_job_type {
    MODEL_JOB_LOAD,
    MODEL_JOB_UNLOAD,
    MODEL_JOB_RELOAD,
};

enum model_job_status {
    MODEL_JOB_PENDING,
    MODEL_JOB_IN_PROGRESS,
    MODEL_JOB_COMPLETED,
    MODEL_JOB_FAILED,
    MODEL_JOB_CANCELLED,
};

struct model_job {
    std::string job_id;
    model_job_type type;
    std::atomic<model_job_status> status;
    common_params params;
    std::atomic<int> progress_pct{0};
    std::string progress_msg;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string error_message;
    std::mutex job_mutex;
    std::condition_variable job_cv;

    void set_progress(int pct, const std::string & msg);
    void set_status(model_job_status new_status, const std::string & error = "");
    bool wait_for_completion(int timeout_ms = 0);
    json to_json() const;
};
```

### 3. Request Tracking (RAII)

**Location:** In `server_context` struct

```cpp
struct server_context {
    std::atomic<int> active_request_count{0};
    std::atomic<bool> accepting_requests{true};

    struct request_guard {
        server_context * ctx;
        bool registered;

        request_guard(server_context * c) : ctx(c), registered(false) {
            if (ctx->accepting_requests.load()) {
                ctx->active_request_count.fetch_add(1);
                registered = true;
            }
        }

        ~request_guard() {
            if (registered) {
                ctx->active_request_count.fetch_sub(1);
            }
        }

        bool is_active() const { return registered; }
    };
};
```

### 4. Path Validation

**Location:** New struct after `model_manager`

```cpp
struct path_validator {
    std::vector<std::string> allowed_directories;

    bool validate_model_path(const std::string & path, std::string & error_msg) {
        // 1. Normalize path (resolve .., symlinks)
        std::string normalized = normalize_path(path);

        // 2. Check if file exists
        if (!std::filesystem::exists(normalized)) {
            error_msg = "File does not exist";
            return false;
        }

        // 3. Check if within allowed directories
        bool allowed = false;
        for (const auto & allowed_dir : allowed_directories) {
            std::string norm_allowed = normalize_path(allowed_dir) + "/";
            if (normalized.find(norm_allowed) == 0) {
                allowed = true;
                break;
            }
        }

        if (!allowed) {
            error_msg = "Path not in allowed directories";
            return false;
        }

        // 4. Validate GGUF magic number
        return is_valid_gguf(normalized, error_msg);
    }

    bool is_valid_gguf(const std::string & path, std::string & error_msg) {
        std::ifstream file(path, std::ios::binary);
        char magic[4];
        file.read(magic, 4);

        if (std::memcmp(magic, "GGUF", 4) != 0) {
            error_msg = "Not a valid GGUF file";
            return false;
        }
        return true;
    }
};
```

## Testing Strategy

### Unit Tests (C++)

Location: `tests/test-model-management.cpp`

```cpp
// Test path validation
TEST(model_management, path_validation) {
    path_validator validator;
    validator.allowed_directories = {"./models"};

    std::string error;
    ASSERT_FALSE(validator.validate_model_path("../../etc/passwd", error));
    ASSERT_FALSE(validator.validate_model_path("./README.md", error));
}

// Test request tracking
TEST(model_management, request_tracking) {
    server_context ctx;
    ASSERT_EQ(ctx.active_request_count.load(), 0);

    {
        server_context::request_guard guard(&ctx);
        ASSERT_EQ(ctx.active_request_count.load(), 1);
    }

    ASSERT_EQ(ctx.active_request_count.load(), 0);
}
```

### Integration Tests (Python pytest)

Location: `tools/server/tests/unit/test_model_management.py`

Already created with 40+ tests covering:
- Basic functionality
- Model loading
- Job tracking
- Model unloading
- Graceful request handling
- Error recovery
- Concurrency
- Configuration
- Queue management
- Performance (slow tests)

**Run Tests:**
```bash
cd tools/server/tests
./tests.sh unit/test_model_management.py -v
```

### Memory Leak Tests

```bash
valgrind --leak-check=full --show-leak-kinds=all \
    ./build/bin/llama-server --endpoint-model-management \
    --model models/test.gguf

# Then run load/unload cycles via API
# Check for "definitely lost" in valgrind output
```

### Security Tests

```bash
# Directory traversal
curl -X POST http://localhost:8080/v1/models/load \
    -d '{"model_path":"../../etc/passwd"}'

# Invalid file
curl -X POST http://localhost:8080/v1/models/load \
    -d '{"model_path":"./README.md"}'

# Path with null bytes
curl -X POST http://localhost:8080/v1/models/load \
    -d '{"model_path":"models/test\x00.gguf"}'
```

## Coding Standards

Following `CODING_GUIDELINES_SUMMARY.md`:

### Naming Conventions

```cpp
// ✅ Good Examples
enum server_state {
    SERVER_STATE_READY,           // UPPER_CASE with prefix
};

struct model_job {                // snake_case
    std::string job_id;
    int timeout_seconds;          // longest common prefix
};

bool server_context::unload_model();  // class_method pattern

// ❌ Bad Examples
enum ServerState { Ready };       // Not UPPER_CASE
struct ModelJob {                 // Not snake_case
    int seconds_timeout;          // Wrong prefix order
};
```

### Code Style

```cpp
// ✅ Good
void my_function() {              // Brackets on same line
    int number_small = 0;         // 4 spaces, longest common prefix
    int number_big = 100;

    for (size_t i = 0; i < 10; i++) {  // Basic for loop
        process(i);
    }
}

// ❌ Avoid
void myFunction()                 // Not snake_case
{                                 // Bracket on new line
  int small_number = 0;           // 2 spaces, wrong prefix
  int big_number = 100;

  for (auto& x : range) { ... }   // Use basic for when index needed
}
```

### Comments & Documentation

```cpp
// Good: Explain why, not what
void unload_model() {
    // Save snapshot for rollback in case next load fails
    save_snapshot();

    // Must drain requests to prevent crashes during cleanup
    wait_for_requests_completion(timeout);
}

// Bad: State the obvious
void unload_model() {
    // Calling save_snapshot function
    save_snapshot();

    // Waiting for requests
    wait_for_requests_completion(timeout);
}
```

### Platform Compatibility

```cpp
// Path handling - platform-specific
#ifdef _WIN32
    // Windows: Use _fullpath
    char resolved[MAX_PATH];
    _fullpath(resolved, path, MAX_PATH);
#else
    // POSIX: Use realpath
    char resolved[PATH_MAX];
    realpath(path, resolved);
#endif
```

## Success Criteria

1. ✅ **Functionality** - Models can be loaded/unloaded via HTTP API
2. ✅ **Stability** - No crashes during 1000 load/unload cycles
3. ✅ **Performance** - < 5% overhead for request tracking
4. ✅ **Security** - All path validation attacks blocked
5. ✅ **Usability** - Clear errors, progress feedback
6. ✅ **Memory** - No leaks detected by valgrind
7. ✅ **Thread Safety** - No race conditions under stress
8. ✅ **Recovery** - Rollback works on all failure modes

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Memory leaks | RAII patterns, smart pointers, valgrind testing |
| Race conditions | Atomic operations, proper locking, stress tests |
| Security bypass | Comprehensive path validation, whitelist enforcement |
| Performance degradation | Request tracking overhead < 5%, benchmarking |
| Failed rollback | Test all failure scenarios, multiple validation layers |

## Next Steps

1. ✅ Infrastructure complete (scripts, tests, docs)
2. ⏭️ Start Phase 1 implementation (core infrastructure)
3. Follow TDD approach with existing tests
4. Use `./scripts/build-server.sh` for efficient builds
5. Commit frequently with format: `server : <description>`

---

**Document Version:** 2.0
**Last Updated:** 2025-11-06
**Status:** Ready for implementation
**Related Documents:**
- [CLAUDE.md](CLAUDE.md) - Project overview
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) - Development workflow
- [CODING_GUIDELINES_SUMMARY.md](CODING_GUIDELINES_SUMMARY.md) - Coding standards
