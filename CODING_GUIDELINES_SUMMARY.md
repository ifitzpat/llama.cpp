# Coding Guidelines Summary for Model Management Feature

Based on llama.cpp's CONTRIBUTING.md, here are the key guidelines we must follow:

## 🎯 Commit Format

For server-related changes, use this format:
```
server : <description> (#<PR_number>)
```

Examples from recent commits:
- `server : disable checkpoints with mtmd (#17045)`
- `server : add props.model_alias (#16943)`
- `server : support unified cache across slots (#16736)`

## 📝 Code Style Guidelines

### General Rules
- ✅ **No third-party dependencies** - Use standard library only
- ✅ **Cross-platform compatibility** - Linux, Windows, macOS
- ✅ **Simple code** - Avoid fancy STL, templates; use basic `for` loops
- ✅ **4 spaces** for indentation (not tabs, except Makefiles)
- ✅ **Vertical alignment** for readability
- ✅ **No trailing whitespace**
- ✅ **Brackets on same line**: `void function() {`
- ✅ **Pointer style**: `void * ptr`, `int & a`

### Type Guidelines
- Use sized integer types: `int32_t`, `uint64_t`, not `int`
- `size_t` is appropriate for allocation sizes or byte offsets
- Declare structs: `struct foo {}` not `typedef struct foo {} foo`
- In C++, omit optional `struct` and `enum` keywords:
  ```cpp
  // OK
  llama_context * ctx;
  const llama_rope_type rope_type;

  // NOT OK
  struct llama_context * ctx;
  const enum llama_rope_type rope_type;
  ```

### Formatting
- Use `clang-format` (v15+) for anything not covered in guidelines
- EditorConfig settings:
  - UTF-8 charset
  - LF line endings
  - Insert final newline
  - Trim trailing whitespace
  - 4 spaces indent

## 🏷️ Naming Conventions

### snake_case for Everything
```cpp
// Functions, variables, types
int my_function();
int my_variable;
struct my_type {};
```

### Longest Common Prefix
```cpp
// NOT OK
int small_number;
int big_number;

// OK
int number_small;
int number_big;
```

### Enum Values
```cpp
enum llama_vocab_type {
    LLAMA_VOCAB_TYPE_NONE = 0,  // UPPER_CASE, prefixed with enum name
    LLAMA_VOCAB_TYPE_SPM  = 1,
    LLAMA_VOCAB_TYPE_BPE  = 2,
};
```

### Method Naming Pattern: `<class>_<method>`
```cpp
llama_model_init();           // class: "llama_model",         method: "init"
llama_sampler_chain_remove(); // class: "llama_sampler_chain", method: "remove"
llama_sampler_get_seed();     // class: "llama_sampler",       method: "get_seed"
```

- `get` action can be omitted
- `<noun>` can be omitted if not necessary
- Use `init`/`free` for constructor/destructor

### Opaque Types
Use `_t` suffix for opaque types:
```cpp
typedef struct llama_context * llama_context_t;
```

### Files
- C/C++ files: lowercase with dashes (`model-loader.cpp`)
- Headers: `.h` extension
- Source: `.c` or `.cpp`
- Python: lowercase with underscores (`test_model.py`)

## 📦 Our Naming for Model Management

Following the guidelines, here's how we should name our additions:

### Enums
```cpp
enum server_state {
    SERVER_STATE_LOADING_MODEL,
    SERVER_STATE_READY,
    SERVER_STATE_TRANSITIONING,
    SERVER_STATE_NO_MODEL,
    SERVER_STATE_ERROR,
};

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
```

### Structs
```cpp
struct model_job {
    // ...
};

struct model_manager {
    // ...
};

struct path_validator {
    // ...
};
```

### Functions/Methods
```cpp
// server_context methods (class: "server_context")
bool server_context::unload_model();
bool server_context::load_model(const common_params & params);
void server_context::save_snapshot();
bool server_context::restore_from_snapshot();

// model_manager methods (class: "model_manager")
std::shared_ptr<model_job> model_manager::create_job(...);
std::shared_ptr<model_job> model_manager::get_job(...);
bool model_manager::execute_load(...);

// path_validator methods (class: "path_validator")
bool path_validator::validate_model_path(...);
bool path_validator::is_valid_gguf(...);
```

### Variables
```cpp
// Follow longest common prefix
int job_timeout_seconds;    // NOT timeout_seconds_job
std::string model_path;     // NOT path_model
bool model_loaded;          // NOT loaded_model

// Atomic/state variables
std::atomic<server_state> state;
std::atomic<bool> running;
std::atomic<int> active_request_count;
```

## 🔧 Pre-commit Checks

Our pre-commit hook verifies:
1. ✅ clang-format compliance
2. ✅ clang-tidy static analysis
3. ✅ Syntax check (quick compilation)
4. ✅ No debug code (std::cout, printf DEBUG)
5. ✅ No trailing whitespace
6. ✅ TODOs reference issues

## 📋 Code Ownership

Server code is maintained by:
- `/tools/server/*` - @ngxson @ggerganov @ericcurtin

**Note:** For this feature, we should consider adding ourselves to CODEOWNERS for the model management code once it's merged.

## ✅ Pull Request Requirements

When creating PR:
1. Test changes locally (full CI if possible)
2. Verify no performance/perplexity regression
3. Create separate PRs for each feature
4. Allow write access to branch for faster reviews
5. Use squash-merge format: `server : <description> (#<PR_number>)`

## 🚫 What to Avoid

- ❌ Third-party dependencies
- ❌ Fancy modern C++ constructs
- ❌ Templates (unless necessary)
- ❌ Platform-specific code (without guards)
- ❌ Breaking existing APIs
- ❌ Trailing whitespace
- ❌ Inconsistent naming patterns

## 🔍 Style Configuration Files

The repo already has:
- `.clang-format` - Auto-formatting rules (maintained by @slaren)
- `.clang-tidy` - Static analysis rules (maintained by @slaren)
- `.editorconfig` - Editor configuration

Our pre-commit hook uses these automatically!

## 📖 References

- Full guidelines: `CONTRIBUTING.md`
- Code owners: `CODEOWNERS`
- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/

---

**Summary:** Follow simple, readable C++ with snake_case, 4-space indents, no fancy features, and cross-platform compatibility. Use clang-format for automatic styling.
