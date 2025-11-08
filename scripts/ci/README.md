# Guix CI Scripts

Tools for monitoring and triggering Guix builds via GitHub Actions.

## Prerequisites

1. **GitHub CLI** (`gh`)
   ```bash
   # Ubuntu/Debian
   sudo apt install gh

   # macOS
   brew install gh

   # Or download from: https://cli.github.com/
   ```

2. **jq** (JSON processor)
   ```bash
   # Ubuntu/Debian
   sudo apt install jq

   # macOS
   brew install jq
   ```

3. **Authentication**
   ```bash
   gh auth login
   ```

## Scripts

### `trigger-guix-build.sh`

Manually trigger a Guix build workflow.

**Usage:**
```bash
# Trigger build on current branch
./scripts/ci/trigger-guix-build.sh

# Trigger build on specific branch
./scripts/ci/trigger-guix-build.sh --branch main

# Trigger and wait for completion
./scripts/ci/trigger-guix-build.sh --wait
```

**Options:**
- `--branch BRANCH` - Branch to build (default: claude/integration-scenarios-ffi-gstreamer)
- `--wait` - Wait for workflow to complete
- `--help` - Show help message

**Example:**
```bash
$ ./scripts/ci/trigger-guix-build.sh --branch claude/integration-scenarios-ffi-gstreamer --wait
Triggering Guix build workflow...
Repository: ggml-org/llama.cpp
Branch:     claude/integration-scenarios-ffi-gstreamer
Workflow:   guix-build.yml

✓ Workflow triggered successfully!

Waiting for workflow to start...
Watching run: 123456789
...
```

---

### `monitor-guix-build.sh`

Monitor the status of Guix build workflows.

**Usage:**
```bash
# Show current status (single check)
./scripts/ci/monitor-guix-build.sh

# Watch mode (refresh every 30 seconds)
./scripts/ci/monitor-guix-build.sh --watch

# Monitor specific branch
./scripts/ci/monitor-guix-build.sh --branch main --watch

# Custom refresh interval
./scripts/ci/monitor-guix-build.sh --watch --interval 10
```

**Options:**
- `--watch` - Continuous monitoring mode
- `--branch BRANCH` - Branch to monitor (default: claude/integration-scenarios-ffi-gstreamer)
- `--interval SECONDS` - Refresh interval in watch mode (default: 30)
- `--help` - Show help message

**Example Output:**
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Guix Build Status
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Branch:     claude/integration-scenarios-ffi-gstreamer
Run:        #42 (1234567890)
Commit:     a5b3c7e - docs : add comprehensive GStreamer implementation plan
Created:    2025-11-08T10:30:00Z
Updated:    2025-11-08T10:45:00Z
Status:     ⟳ IN PROGRESS
URL:        https://github.com/ggml-org/llama.cpp/actions/runs/1234567890

Jobs:
  Build llama-cpp-base: completed success
  Build llama-simple (C API): in_progress in_progress
  Build gst-llama plugin: queued in_progress
  Build complete suite: queued in_progress
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Refreshing in 30 seconds... (Ctrl+C to stop)
```

---

## Workflows

### Quick Status Check

```bash
./scripts/ci/monitor-guix-build.sh
```

### Trigger and Monitor

```bash
# Terminal 1: Trigger build
./scripts/ci/trigger-guix-build.sh

# Terminal 2: Watch progress
./scripts/ci/monitor-guix-build.sh --watch
```

### Download Artifacts

After a successful build, download artifacts:

```bash
# Via monitor script (prompts after completion in watch mode)
./scripts/ci/monitor-guix-build.sh --watch

# Manually via gh
gh run list --workflow=guix-build.yml --limit=1
gh run download <RUN_ID>
```

---

## CI Workflow Jobs

The Guix build workflow consists of:

1. **build-base** - Build llama-cpp-base (core libraries)
2. **build-simple** - Build llama-simple (C API wrapper)
3. **build-gstreamer** - Build gst-llama plugin
4. **build-all** - Build complete suite (meta-package)
5. **integration-test** - Run integration tests

Each job:
- Uses Guix for reproducible builds
- Caches build artifacts in `/gnu/store`
- Uploads build logs and artifacts
- Runs tests when applicable

---

## Troubleshooting

### "Not authenticated with GitHub CLI"

Run:
```bash
gh auth login
```

### "Could not find workflow"

Ensure the workflow file exists:
```bash
ls -la .github/workflows/guix-build.yml
```

### "No workflow runs found"

Make sure you've pushed the branch:
```bash
git push origin claude/integration-scenarios-ffi-gstreamer
```

### Build Failures

1. Check the workflow logs:
   ```bash
   gh run list --workflow=guix-build.yml --limit=5
   gh run view <RUN_ID>
   ```

2. Download build logs:
   ```bash
   gh run download <RUN_ID>
   ```

3. View specific job:
   ```bash
   gh run view <RUN_ID> --log
   ```

---

## Environment Variables

- `REPO` - GitHub repository (default: ggml-org/llama.cpp)
- `GITHUB_TOKEN` - Personal access token (optional, uses gh auth)

**Example:**
```bash
REPO="yourusername/llama.cpp" ./scripts/ci/monitor-guix-build.sh
```

---

## Local Guix Builds

If you have Guix installed locally:

```bash
# Build specific package
guix build -f guix.scm llama-simple

# Build all packages
guix build -f guix.scm

# Enter development shell
guix shell -D -f guix.scm

# Build and install locally
guix package -f guix.scm
```

---

## See Also

- GitHub Actions Workflow: `.github/workflows/guix-build.yml`
- Guix Package Definition: `guix.scm`
- Implementation Plan: `GSTREAMER_IMPLEMENTATION_PLAN.md`
