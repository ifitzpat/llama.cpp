# Model Management Testing Guide

## 📋 Overview

This guide provides comprehensive testing procedures for the dynamic model management feature in llama-server. Follow these steps to validate functionality, performance, security, and stability.

**Prerequisites:**
- llama-server compiled with model management feature
- At least one GGUF model file for testing
- `curl` or similar HTTP client
- Optional: `jq` for JSON parsing
- Optional: `ab` (Apache Bench) or `wrk` for load testing

---

## 🎯 Testing Phases

### Phase 1: Build Verification
### Phase 2: Functional Testing
### Phase 3: Security Testing
### Phase 4: Performance Testing
### Phase 5: Stress Testing
### Phase 6: Integration Testing

---

## 📦 Phase 1: Build Verification

### 1.1 Compile with Debug Symbols

```bash
cd /home/user/llama.cpp
mkdir -p build
cd build

# Configure with debug info
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLAMA_CURL=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build server
make llama-server -j$(nproc)

# Verify binary exists
ls -lh ../bin/llama-server
```

**Expected Output:**
```
-rwxr-xr-x 1 user user 25M Nov  6 12:00 ../bin/llama-server
```

### 1.2 Run Static Analysis

```bash
# Check for common issues
cd /home/user/llama.cpp

# Run clang-tidy on model management code
clang-tidy tools/server/server.cpp \
  --checks='-*,bugprone-*,clang-analyzer-*,modernize-*' \
  -- -I. -Icommon -Iggml/include -std=c++17

# Look for:
# - Memory leaks
# - Use-after-free
# - Thread safety issues
```

**Expected:** Zero critical warnings in new code (lines 2000-2500, 2900-3100, 6300-6600)

### 1.3 Verify No Regressions

```bash
# Test that server still starts normally
../bin/llama-server --help

# Should see new model management options
# (No new CLI flags for this feature, but help should work)
```

---

## 🧪 Phase 2: Functional Testing

### Test Environment Setup

```bash
# Set up test directory
export LLAMA_SERVER_PORT=8080
export LLAMA_SERVER_HOST=localhost
export TEST_MODEL="/path/to/your/model.gguf"  # Update this!
export API_KEY="test-key-12345"

# Helper function for curl requests
llama_curl() {
  curl -s -H "Authorization: Bearer ${API_KEY}" "$@"
}
```

---

### 2.1 Test: Server Startup (No Initial Model)

**Purpose:** Verify server can start without a model loaded.

```bash
# Start server in background
../bin/llama-server \
  --port ${LLAMA_SERVER_PORT} \
  --api-key ${API_KEY} \
  --log-disable \
  > /tmp/llama-server.log 2>&1 &

SERVER_PID=$!
echo "Server PID: ${SERVER_PID}"

# Wait for server to start
sleep 3

# Check if server is running
if ps -p ${SERVER_PID} > /dev/null; then
  echo "✅ Server started successfully"
else
  echo "❌ Server failed to start"
  cat /tmp/llama-server.log
  exit 1
fi
```

**Expected Result:** Server starts, logs show `SERVER_STATE_LOADING_MODEL` → no model specified → continues.

---

### 2.2 Test: Get Initial Status

**Purpose:** Verify status endpoint works and shows correct initial state.

```bash
# Get server status
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)

echo "Server Status:"
echo "${STATUS}" | jq .

# Verify state
STATE=$(echo "${STATUS}" | jq -r .state)
if [ "${STATE}" = "loading_model" ] || [ "${STATE}" = "no_model" ]; then
  echo "✅ Initial state correct: ${STATE}"
else
  echo "❌ Unexpected state: ${STATE}"
fi
```

**Expected Output:**
```json
{
  "state": "no_model",
  "model": "",
  "active_requests": 0
}
```

---

### 2.3 Test: Load Model (Happy Path)

**Purpose:** Verify model can be loaded successfully.

```bash
# Load model
echo "Loading model: ${TEST_MODEL}"

LOAD_RESPONSE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d "{
    \"model\": \"${TEST_MODEL}\",
    \"n_ctx\": 2048,
    \"n_gpu_layers\": 35
  }")

echo "Load Response:"
echo "${LOAD_RESPONSE}" | jq .

# Extract job ID
JOB_ID=$(echo "${LOAD_RESPONSE}" | jq -r .job_id)
echo "Job ID: ${JOB_ID}"

if [ -z "${JOB_ID}" ] || [ "${JOB_ID}" = "null" ]; then
  echo "❌ Failed to create load job"
  exit 1
fi

echo "✅ Load job created: ${JOB_ID}"
```

**Expected Output:**
```json
{
  "job_id": "job_1699123456789_0",
  "status": "accepted",
  "message": "Model load job created"
}
```

---

### 2.4 Test: Poll Job Status

**Purpose:** Verify job status endpoint and progress tracking.

```bash
# Poll job status every 2 seconds
echo "Polling job status..."

for i in {1..60}; do
  JOB_STATUS=$(llama_curl \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB_ID})

  STATUS=$(echo "${JOB_STATUS}" | jq -r .status)
  PROGRESS=$(echo "${JOB_STATUS}" | jq -r .progress)

  echo "[$i] Status: ${STATUS}, Progress: ${PROGRESS}%"

  if [ "${STATUS}" = "completed" ]; then
    echo "✅ Model loaded successfully"
    echo "${JOB_STATUS}" | jq .
    break
  elif [ "${STATUS}" = "failed" ]; then
    echo "❌ Model load failed"
    echo "${JOB_STATUS}" | jq .
    exit 1
  fi

  sleep 2
done

if [ "${STATUS}" != "completed" ]; then
  echo "❌ Model load timed out"
  exit 1
fi
```

**Expected Output:**
```
[1] Status: pending, Progress: 0%
[2] Status: in_progress, Progress: 15%
[3] Status: in_progress, Progress: 45%
[4] Status: in_progress, Progress: 78%
[5] Status: completed, Progress: 100%
✅ Model loaded successfully
```

**Expected JSON (completed):**
```json
{
  "job_id": "job_1699123456789_0",
  "status": "completed",
  "progress": 100,
  "progress_message": "",
  "duration_seconds": 12
}
```

---

### 2.5 Test: Verify Model Ready

**Purpose:** Confirm server transitioned to READY state.

```bash
# Check server status
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)

echo "Server Status After Load:"
echo "${STATUS}" | jq .

STATE=$(echo "${STATUS}" | jq -r .state)
MODEL=$(echo "${STATUS}" | jq -r .model)

if [ "${STATE}" = "ready" ]; then
  echo "✅ Server in READY state"
else
  echo "❌ Server not ready: ${STATE}"
  exit 1
fi

if [ "${MODEL}" = "${TEST_MODEL}" ]; then
  echo "✅ Correct model loaded: ${MODEL}"
else
  echo "⚠️  Model path mismatch: ${MODEL}"
fi
```

**Expected Output:**
```json
{
  "state": "ready",
  "model": "/path/to/your/model.gguf",
  "active_requests": 0
}
```

---

### 2.6 Test: Inference Request

**Purpose:** Verify inference works with loaded model.

```bash
# Send chat completion request
echo "Testing inference..."

INFERENCE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "messages": [
      {"role": "user", "content": "What is 2+2?"}
    ],
    "max_tokens": 50,
    "temperature": 0.7
  }')

echo "Inference Response:"
echo "${INFERENCE}" | jq .

# Check for response
CONTENT=$(echo "${INFERENCE}" | jq -r .choices[0].message.content)
if [ -n "${CONTENT}" ] && [ "${CONTENT}" != "null" ]; then
  echo "✅ Inference working"
  echo "Response: ${CONTENT}"
else
  echo "❌ Inference failed"
  echo "${INFERENCE}"
  exit 1
fi
```

**Expected Output:**
```json
{
  "id": "chatcmpl-123",
  "object": "chat.completion",
  "created": 1699123456,
  "model": "/path/to/your/model.gguf",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "2+2 equals 4."
      },
      "finish_reason": "stop"
    }
  ]
}
```

---

### 2.7 Test: Unload Model

**Purpose:** Verify graceful model unloading.

```bash
# Unload model
echo "Unloading model..."

UNLOAD_RESPONSE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)

echo "Unload Response:"
echo "${UNLOAD_RESPONSE}" | jq .

UNLOAD_JOB_ID=$(echo "${UNLOAD_RESPONSE}" | jq -r .job_id)

# Wait for unload to complete
llama_curl "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UNLOAD_JOB_ID}/wait?timeout=60"

# Verify state
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
STATE=$(echo "${STATUS}" | jq -r .state)

if [ "${STATE}" = "no_model" ]; then
  echo "✅ Model unloaded successfully"
else
  echo "❌ Unexpected state after unload: ${STATE}"
  exit 1
fi
```

**Expected Output:**
```json
{
  "job_id": "job_1699123456790_1",
  "status": "accepted",
  "message": "Model unload job created"
}

// After wait:
{
  "job_id": "job_1699123456790_1",
  "status": "completed",
  "progress": 100,
  "duration_seconds": 2
}
```

---

### 2.8 Test: Inference During NO_MODEL State

**Purpose:** Verify requests are blocked when no model loaded.

```bash
# Try inference without model
echo "Testing inference with no model..."

INFERENCE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "Hello"}]}')

ERROR=$(echo "${INFERENCE}" | jq -r .error.message)

if [[ "${ERROR}" == *"No model loaded"* ]]; then
  echo "✅ Request correctly blocked: ${ERROR}"
else
  echo "❌ Request should have been blocked"
  echo "${INFERENCE}"
  exit 1
fi
```

**Expected Output:**
```json
{
  "error": {
    "message": "No model loaded",
    "type": "unavailable",
    "code": 503
  }
}
```

---

### 2.9 Test: Wait Endpoint

**Purpose:** Test blocking job wait with timeout.

```bash
# Start another load
LOAD_RESPONSE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d "{\"model\": \"${TEST_MODEL}\"}")

JOB_ID=$(echo "${LOAD_RESPONSE}" | jq -r .job_id)

echo "Testing wait endpoint (120s timeout)..."

# This will block until job completes or timeout
WAIT_RESULT=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB_ID}/wait?timeout=120")

echo "Wait Result:"
echo "${WAIT_RESULT}" | jq .

STATUS=$(echo "${WAIT_RESULT}" | jq -r .status)
if [ "${STATUS}" = "completed" ]; then
  echo "✅ Wait endpoint worked"
else
  echo "⚠️  Job status: ${STATUS}"
fi
```

---

## 🔒 Phase 3: Security Testing

### 3.1 Test: Path Traversal Attack

**Purpose:** Verify directory traversal is blocked.

```bash
echo "Testing path traversal protection..."

# Attempt 1: Basic traversal
ATTACK1=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"model": "../../../etc/passwd"}')

ERROR1=$(echo "${ATTACK1}" | jq -r .error.message)

if [[ "${ERROR1}" == *"Directory traversal"* ]]; then
  echo "✅ Attack 1 blocked: ${ERROR1}"
else
  echo "❌ Path traversal not detected!"
  exit 1
fi

# Attempt 2: URL encoded
ATTACK2=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"model": "/models/subdir/../../../etc/passwd"}')

ERROR2=$(echo "${ATTACK2}" | jq -r .error.message)

if [[ "${ERROR2}" == *"traversal"* ]] || [[ "${ERROR2}" == *"Cannot open"* ]]; then
  echo "✅ Attack 2 blocked: ${ERROR2}"
else
  echo "⚠️  Attack 2 response: ${ERROR2}"
fi
```

**Expected:** Both attempts blocked with 400 Bad Request.

---

### 3.2 Test: Parameter Overflow

**Purpose:** Verify parameter validation prevents DoS.

```bash
echo "Testing parameter validation..."

# Test 1: Negative n_ctx
OVERFLOW1=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d "{\"model\": \"${TEST_MODEL}\", \"n_ctx\": -1}")

ERROR1=$(echo "${OVERFLOW1}" | jq -r .error.message)
if [[ "${ERROR1}" == *"n_ctx must be between"* ]]; then
  echo "✅ Negative n_ctx blocked"
else
  echo "❌ Negative value not validated: ${ERROR1}"
fi

# Test 2: Huge n_parallel
OVERFLOW2=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d "{\"model\": \"${TEST_MODEL}\", \"n_parallel\": 1000000}")

ERROR2=$(echo "${OVERFLOW2}" | jq -r .error.message)
if [[ "${ERROR2}" == *"n_parallel must be between"* ]]; then
  echo "✅ Huge n_parallel blocked"
else
  echo "❌ Overflow not validated: ${ERROR2}"
fi

# Test 3: Huge timeout
OVERFLOW3=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/fake_job/wait?timeout=999999")

ERROR3=$(echo "${OVERFLOW3}" | jq -r .error.message)
if [[ "${ERROR3}" == *"timeout must be between"* ]]; then
  echo "✅ Huge timeout blocked"
else
  echo "⚠️  Timeout validation: ${ERROR3}"
fi
```

**Expected:** All overflow attempts return 400 Bad Request with validation message.

---

### 3.3 Test: Malformed JSON

**Purpose:** Verify exception handling for malformed input.

```bash
echo "Testing malformed JSON handling..."

MALFORMED=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{invalid json}')

ERROR=$(echo "${MALFORMED}" | jq -r .error.message)

if [[ "${ERROR}" == *"Invalid JSON"* ]]; then
  echo "✅ Malformed JSON caught: ${ERROR}"
else
  echo "❌ JSON exception not handled"
  echo "${MALFORMED}"
fi
```

**Expected:**
```json
{
  "error": {
    "message": "Invalid JSON: parse error at position X",
    "type": "invalid_request_error",
    "code": 400
  }
}
```

---

### 3.4 Test: Missing Required Fields

**Purpose:** Verify required field validation.

```bash
echo "Testing required field validation..."

MISSING=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"n_ctx": 2048}')

ERROR=$(echo "${MISSING}" | jq -r .error.message)

if [[ "${ERROR}" == *"model path is required"* ]]; then
  echo "✅ Missing field detected: ${ERROR}"
else
  echo "❌ Missing field not validated: ${ERROR}"
fi
```

---

## ⚡ Phase 4: Performance Testing

### 4.1 Test: Model Load Time Baseline

**Purpose:** Establish baseline for model loading performance.

```bash
echo "Testing model load performance..."

# Ensure no model loaded
llama_curl -X POST http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload
sleep 3

# Measure load time
START_TIME=$(date +%s)

LOAD_RESPONSE=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -H "Content-Type: application/json" \
  -d "{\"model\": \"${TEST_MODEL}\", \"n_ctx\": 2048}")

JOB_ID=$(echo "${LOAD_RESPONSE}" | jq -r .job_id)

# Wait for completion
WAIT_RESULT=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB_ID}/wait?timeout=300")

END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

DURATION=$(echo "${WAIT_RESULT}" | jq -r .duration_seconds)

echo "Load Performance:"
echo "  Wall time: ${TOTAL_TIME}s"
echo "  Job duration: ${DURATION}s"
echo "  Overhead: $((TOTAL_TIME - DURATION))s"

if [ ${TOTAL_TIME} -lt 120 ]; then
  echo "✅ Load time acceptable (<2 min)"
else
  echo "⚠️  Load time high (${TOTAL_TIME}s)"
fi
```

**Expected:**
- Small models (7B): 5-15 seconds
- Medium models (13B): 15-30 seconds
- Large models (70B): 30-120 seconds

**Overhead:** <3 seconds (job queue + state transitions)

---

### 4.2 Test: Request Tracking Overhead

**Purpose:** Measure performance impact of request tracking.

```bash
echo "Testing request tracking overhead..."

# Ensure model loaded
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
STATE=$(echo "${STATUS}" | jq -r .state)

if [ "${STATE}" != "ready" ]; then
  echo "Loading model first..."
  # Load model (reuse previous commands)
  # ... (abbreviated for brevity)
fi

# Run 100 inference requests and measure average time
echo "Running 100 inference requests..."

TOTAL_TIME=0
for i in {1..100}; do
  START=$(date +%s%N)

  RESPONSE=$(llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{
      "messages": [{"role": "user", "content": "Hi"}],
      "max_tokens": 5
    }')

  END=$(date +%s%N)
  ELAPSED=$((END - START))
  TOTAL_TIME=$((TOTAL_TIME + ELAPSED))

  if [ $((i % 10)) -eq 0 ]; then
    echo "  Completed ${i}/100 requests"
  fi
done

AVG_TIME=$((TOTAL_TIME / 100))
AVG_MS=$((AVG_TIME / 1000000))

echo "Average request time: ${AVG_MS}ms"

if [ ${AVG_MS} -lt 5000 ]; then
  echo "✅ Request overhead acceptable"
else
  echo "⚠️  High overhead: ${AVG_MS}ms"
fi
```

**Expected:**
- Overhead from atomic counter: <1ms per request
- Total overhead: <5% of inference time

---

### 4.3 Test: Concurrent Load Test

**Purpose:** Verify stability under concurrent load.

```bash
echo "Running concurrent load test..."

# Function to send request
send_request() {
  llama_curl -s -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{
      "messages": [{"role": "user", "content": "Test"}],
      "max_tokens": 10
    }' > /dev/null
}

# Send 50 concurrent requests
echo "Sending 50 concurrent requests..."
for i in {1..50}; do
  send_request &
done

# Wait for all to complete
wait

echo "✅ Concurrent load test completed"

# Check server status
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
STATE=$(echo "${STATUS}" | jq -r .state)

if [ "${STATE}" = "ready" ]; then
  echo "✅ Server still in READY state"
else
  echo "❌ Server state changed: ${STATE}"
fi
```

**Expected:** All requests succeed, server remains in READY state.

---

### 4.4 Test: Request Draining Performance

**Purpose:** Measure graceful shutdown timing.

```bash
echo "Testing request draining performance..."

# Start long-running request in background
llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "messages": [{"role": "user", "content": "Write a long story"}],
    "max_tokens": 500
  }' > /tmp/long_request.json &

REQUEST_PID=$!

# Wait for request to start
sleep 2

# Check active requests
STATUS=$(llama_curl http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
ACTIVE=$(echo "${STATUS}" | jq -r .active_requests)
echo "Active requests before unload: ${ACTIVE}"

# Trigger unload (should wait for request to complete)
START_TIME=$(date +%s)

UNLOAD=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)

UNLOAD_JOB=$(echo "${UNLOAD}" | jq -r .job_id)

# Wait for unload to complete
UNLOAD_RESULT=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UNLOAD_JOB}/wait?timeout=60")

END_TIME=$(date +%s)
DRAIN_TIME=$((END_TIME - START_TIME))

echo "Request draining took: ${DRAIN_TIME}s"

if [ ${DRAIN_TIME} -lt 30 ]; then
  echo "✅ Draining completed within timeout"
else
  echo "⚠️  Draining took longer than expected: ${DRAIN_TIME}s"
fi

# Verify long request completed
wait ${REQUEST_PID}
if [ -f /tmp/long_request.json ]; then
  CONTENT=$(jq -r .choices[0].message.content /tmp/long_request.json)
  if [ -n "${CONTENT}" ]; then
    echo "✅ Long request completed successfully"
  fi
fi
```

**Expected:** Unload waits for request to complete (max 30s timeout).

---

## 💪 Phase 5: Stress Testing

### 5.1 Test: Rapid Load/Unload Cycles

**Purpose:** Detect memory leaks and resource exhaustion.

```bash
echo "Running 100 load/unload cycles..."

for i in {1..100}; do
  # Load
  LOAD=$(llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
    -d "{\"model\": \"${TEST_MODEL}\"}")

  LOAD_JOB=$(echo "${LOAD}" | jq -r .job_id)

  # Wait for load
  llama_curl -s \
    "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${LOAD_JOB}/wait?timeout=60" \
    > /dev/null

  # Unload
  UNLOAD=$(llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)

  UNLOAD_JOB=$(echo "${UNLOAD}" | jq -r .job_id)

  # Wait for unload
  llama_curl -s \
    "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UNLOAD_JOB}/wait?timeout=60" \
    > /dev/null

  if [ $((i % 10)) -eq 0 ]; then
    echo "  Completed ${i}/100 cycles"

    # Check memory usage
    MEM=$(ps -o rss= -p ${SERVER_PID})
    echo "  Memory usage: $((MEM / 1024)) MB"
  fi
done

echo "✅ 100 cycles completed"

# Final memory check
FINAL_MEM=$(ps -o rss= -p ${SERVER_PID})
echo "Final memory: $((FINAL_MEM / 1024)) MB"

# Memory should be stable (not growing unbounded)
```

**Expected:**
- Memory usage stable across cycles
- No crashes or hangs
- All cycles complete successfully

---

### 5.2 Test: Job Map Cleanup

**Purpose:** Verify old jobs are cleaned up (5-minute TTL).

```bash
echo "Testing job cleanup..."

# Create 20 jobs
JOB_IDS=()
for i in {1..20}; do
  LOAD=$(llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
    -d "{\"model\": \"${TEST_MODEL}\"}")

  JOB_ID=$(echo "${LOAD}" | jq -r .job_id)
  JOB_IDS+=("${JOB_ID}")

  # Cancel by unloading immediately (forces failure/completion)
  llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload \
    > /dev/null

  sleep 1
done

echo "Created ${#JOB_IDS[@]} jobs"

# Verify all jobs exist
FOUND=0
for JOB_ID in "${JOB_IDS[@]}"; do
  RESULT=$(llama_curl \
    "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB_ID}")

  STATUS=$(echo "${RESULT}" | jq -r .status)
  if [ "${STATUS}" != "null" ]; then
    FOUND=$((FOUND + 1))
  fi
done

echo "Found ${FOUND}/${#JOB_IDS[@]} jobs immediately after creation"

# Wait 6 minutes for cleanup
echo "Waiting 6 minutes for job cleanup..."
sleep 360

# Check if jobs cleaned up
STILL_FOUND=0
for JOB_ID in "${JOB_IDS[@]}"; do
  RESULT=$(llama_curl \
    "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB_ID}")

  ERROR=$(echo "${RESULT}" | jq -r .error.message)
  if [[ "${ERROR}" != *"Job not found"* ]]; then
    STILL_FOUND=$((STILL_FOUND + 1))
  fi
done

echo "Found ${STILL_FOUND}/${#JOB_IDS[@]} jobs after 6 minutes"

if [ ${STILL_FOUND} -lt ${FOUND} ]; then
  echo "✅ Job cleanup working (removed $((FOUND - STILL_FOUND)) jobs)"
else
  echo "⚠️  Job cleanup not working (all jobs still present)"
fi
```

**Expected:** Old jobs removed after 5-minute TTL.

---

### 5.3 Test: Error Recovery

**Purpose:** Verify server can recover from failed loads.

```bash
echo "Testing error recovery..."

# Attempt to load invalid model
INVALID=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -d '{"model": "/nonexistent/model.gguf"}')

INVALID_JOB=$(echo "${INVALID}" | jq -r .job_id)

# Wait for failure
RESULT=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${INVALID_JOB}/wait?timeout=30")

STATUS=$(echo "${RESULT}" | jq -r .status)

if [ "${STATUS}" = "failed" ]; then
  echo "✅ Invalid load correctly failed"
else
  echo "⚠️  Unexpected status: ${STATUS}"
fi

# Check server state
SERVER_STATUS=$(llama_curl \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)

STATE=$(echo "${SERVER_STATUS}" | jq -r .state)
echo "Server state after failed load: ${STATE}"

if [ "${STATE}" = "error" ]; then
  echo "Server in ERROR state, testing reset..."

  # Reset server
  RESET=$(llama_curl -X POST \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/reset)

  echo "${RESET}" | jq .

  # Verify transition to NO_MODEL
  SERVER_STATUS=$(llama_curl \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)

  STATE=$(echo "${SERVER_STATUS}" | jq -r .state)

  if [ "${STATE}" = "no_model" ]; then
    echo "✅ Error recovery successful"
  else
    echo "❌ Reset failed, state: ${STATE}"
  fi
else
  echo "⚠️  Server not in ERROR state: ${STATE}"
fi

# Try loading valid model after reset
VALID=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
  -d "{\"model\": \"${TEST_MODEL}\"}")

VALID_JOB=$(echo "${VALID}" | jq -r .job_id)

VALID_RESULT=$(llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${VALID_JOB}/wait?timeout=120")

VALID_STATUS=$(echo "${VALID_RESULT}" | jq -r .status)

if [ "${VALID_STATUS}" = "completed" ]; then
  echo "✅ Successfully loaded model after error recovery"
else
  echo "❌ Failed to load model after recovery"
fi
```

---

## 🔗 Phase 6: Integration Testing

### 6.1 Test: Middleware State Blocking

**Purpose:** Verify middleware blocks requests during transitions.

```bash
echo "Testing middleware state blocking..."

# Start unload
UNLOAD=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)

UNLOAD_JOB=$(echo "${UNLOAD}" | jq -r .job_id)

# Immediately try inference (should be blocked)
BLOCKED=$(llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/chat/completions \
  -d '{"messages": [{"role": "user", "content": "Test"}]}')

ERROR=$(echo "${BLOCKED}" | jq -r .error.message)

if [[ "${ERROR}" == *"transitioning"* ]] || [[ "${ERROR}" == *"unavailable"* ]]; then
  echo "✅ Request blocked during transition: ${ERROR}"
else
  echo "⚠️  Request not blocked: ${ERROR}"
fi

# Wait for unload to complete
llama_curl \
  "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UNLOAD_JOB}/wait?timeout=60" \
  > /dev/null

echo "Unload completed"
```

**Expected:** Inference requests return 503 during TRANSITIONING state.

---

### 6.2 Test: Model Management Endpoints Always Accessible

**Purpose:** Verify model management works in any state.

```bash
echo "Testing model management accessibility..."

# Ensure no model loaded
llama_curl -X POST \
  http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload \
  > /dev/null
sleep 3

STATES=("no_model" "loading" "ready")

for STATE_NAME in "${STATES[@]}"; do
  echo "Testing in ${STATE_NAME} state..."

  # Status should always work
  STATUS=$(llama_curl \
    http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)

  CURRENT_STATE=$(echo "${STATUS}" | jq -r .state)

  if [ -n "${CURRENT_STATE}" ] && [ "${CURRENT_STATE}" != "null" ]; then
    echo "  ✅ Status endpoint works in ${CURRENT_STATE}"
  else
    echo "  ❌ Status endpoint failed"
  fi

  # Load for next iteration
  if [ "${STATE_NAME}" != "ready" ]; then
    LOAD=$(llama_curl -X POST \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
      -d "{\"model\": \"${TEST_MODEL}\"}")

    LOAD_JOB=$(echo "${LOAD}" | jq -r .job_id)

    llama_curl \
      "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${LOAD_JOB}/wait?timeout=120" \
      > /dev/null
  fi
done

echo "✅ Model management accessible in all states"
```

---

### 6.3 Test: Multiple Model Switching

**Purpose:** Verify server can switch between different models.

```bash
echo "Testing multiple model switching..."

# Define model paths (update these!)
MODEL_1="${TEST_MODEL}"
MODEL_2="/path/to/another/model.gguf"  # If available

if [ ! -f "${MODEL_2}" ]; then
  echo "⏭️  Skipping multiple model test (MODEL_2 not found)"
else
  for i in {1..5}; do
    echo "Cycle ${i}: Loading MODEL_1..."

    LOAD1=$(llama_curl -X POST \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
      -d "{\"model\": \"${MODEL_1}\"}")

    JOB1=$(echo "${LOAD1}" | jq -r .job_id)
    llama_curl \
      "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB1}/wait?timeout=120" \
      > /dev/null

    # Verify correct model
    STATUS=$(llama_curl \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
    MODEL=$(echo "${STATUS}" | jq -r .model)

    if [ "${MODEL}" = "${MODEL_1}" ]; then
      echo "  ✅ MODEL_1 loaded"
    else
      echo "  ❌ Wrong model: ${MODEL}"
    fi

    # Unload and load MODEL_2
    UNLOAD=$(llama_curl -X POST \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)
    UJOB=$(echo "${UNLOAD}" | jq -r .job_id)
    llama_curl \
      "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UJOB}/wait?timeout=60" \
      > /dev/null

    echo "Cycle ${i}: Loading MODEL_2..."

    LOAD2=$(llama_curl -X POST \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/load \
      -d "{\"model\": \"${MODEL_2}\"}")

    JOB2=$(echo "${LOAD2}" | jq -r .job_id)
    llama_curl \
      "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${JOB2}/wait?timeout=120" \
      > /dev/null

    # Verify correct model
    STATUS=$(llama_curl \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/status)
    MODEL=$(echo "${STATUS}" | jq -r .model)

    if [ "${MODEL}" = "${MODEL_2}" ]; then
      echo "  ✅ MODEL_2 loaded"
    else
      echo "  ❌ Wrong model: ${MODEL}"
    fi

    # Unload for next cycle
    UNLOAD=$(llama_curl -X POST \
      http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/unload)
    UJOB=$(echo "${UNLOAD}" | jq -r .job_id)
    llama_curl \
      "http://${LLAMA_SERVER_HOST}:${LLAMA_SERVER_PORT}/v1/models/jobs/${UJOB}/wait?timeout=60" \
      > /dev/null
  done

  echo "✅ Multiple model switching successful"
fi
```

---

## 📊 Test Results Summary

### Expected Results Summary

| Test Category | Tests | Expected Pass Rate |
|--------------|-------|-------------------|
| Build Verification | 3 | 100% |
| Functional Testing | 9 | 100% |
| Security Testing | 4 | 100% |
| Performance Testing | 4 | ≥90% |
| Stress Testing | 3 | ≥90% |
| Integration Testing | 3 | 100% |

### Performance Baselines

| Metric | Target | Acceptable Range |
|--------|--------|------------------|
| Model Load Time (7B) | 10s | 5-20s |
| Model Load Time (13B) | 20s | 15-40s |
| Request Tracking Overhead | <1ms | <5ms |
| Graceful Drain Time | <30s | <60s |
| Memory Leak | 0 MB/cycle | <10 MB/100 cycles |
| Job Cleanup | 5 min | 4-6 min |

---

## 🐛 Troubleshooting

### Issue: Server Won't Start

**Symptoms:** Server exits immediately or logs show errors.

**Solutions:**
```bash
# Check if port is already in use
lsof -i :8080

# Check server logs
cat /tmp/llama-server.log

# Try different port
../bin/llama-server --port 8081 --api-key test
```

### Issue: Model Load Hangs

**Symptoms:** Job status stuck at "in_progress" for >5 minutes.

**Solutions:**
```bash
# Check server logs for errors
tail -f /tmp/llama-server.log

# Verify model file is accessible
ls -lh ${TEST_MODEL}
file ${TEST_MODEL}  # Should show "data"

# Check for GGUF magic number
head -c 4 ${TEST_MODEL} | xxd
# Should show: 0000000: 4747 5546  GGUF
```

### Issue: 503 Errors on All Requests

**Symptoms:** All endpoints return "Service Unavailable".

**Solutions:**
```bash
# Check server state
curl -s http://localhost:8080/v1/models/status | jq .

# If in ERROR state, reset
curl -X POST http://localhost:8080/v1/models/reset

# If in TRANSITIONING state, wait for job to complete
curl -s http://localhost:8080/v1/models/jobs/JOB_ID
```

### Issue: Memory Usage Growing

**Symptoms:** RSS increases significantly after each cycle.

**Solutions:**
```bash
# Monitor memory in real-time
watch -n 1 'ps aux | grep llama-server'

# Check for job map growth
curl -s http://localhost:8080/v1/models/jobs/NONEXISTENT
# Should return 404 after 5 minutes

# Restart server to clear memory
kill ${SERVER_PID}
# Start fresh
```

### Issue: Inference Requests Slow

**Symptoms:** Requests take >10s for simple prompts.

**Solutions:**
```bash
# Check if GPU layers are being used
curl -s http://localhost:8080/v1/models/status

# Reload with more GPU layers
curl -X POST http://localhost:8080/v1/models/load \
  -d '{"model": "/path/to/model.gguf", "n_gpu_layers": 99}'

# Check server load
top -p ${SERVER_PID}
```

---

## 🎯 Cleanup

After testing, clean up resources:

```bash
# Stop server
kill ${SERVER_PID}

# Wait for graceful shutdown
sleep 3

# Force kill if needed
kill -9 ${SERVER_PID} 2>/dev/null

# Clean up log files
rm -f /tmp/llama-server.log
rm -f /tmp/long_request.json

# Clean up test artifacts
rm -rf /tmp/test_output/

echo "✅ Cleanup complete"
```

---

## 📝 Test Report Template

After running tests, document results:

```markdown
# Model Management Testing Report

**Date:** YYYY-MM-DD
**Tester:** Your Name
**Model Tested:** /path/to/model.gguf
**Model Size:** X GB
**System:** OS, CPU, RAM, GPU

## Test Results

### Phase 1: Build Verification
- [ ] 1.1 Compile with Debug Symbols - PASS/FAIL
- [ ] 1.2 Static Analysis - PASS/FAIL (X warnings)
- [ ] 1.3 No Regressions - PASS/FAIL

### Phase 2: Functional Testing
- [ ] 2.1 Server Startup - PASS/FAIL
- [ ] 2.2 Get Initial Status - PASS/FAIL
- [ ] 2.3 Load Model - PASS/FAIL (Xs)
- [ ] 2.4 Poll Job Status - PASS/FAIL
- [ ] 2.5 Verify Model Ready - PASS/FAIL
- [ ] 2.6 Inference Request - PASS/FAIL
- [ ] 2.7 Unload Model - PASS/FAIL (Xs)
- [ ] 2.8 Inference During NO_MODEL - PASS/FAIL
- [ ] 2.9 Wait Endpoint - PASS/FAIL

### Phase 3: Security Testing
- [ ] 3.1 Path Traversal Attack - PASS/FAIL
- [ ] 3.2 Parameter Overflow - PASS/FAIL
- [ ] 3.3 Malformed JSON - PASS/FAIL
- [ ] 3.4 Missing Required Fields - PASS/FAIL

### Phase 4: Performance Testing
- [ ] 4.1 Model Load Time - Xs (Target: <20s)
- [ ] 4.2 Request Overhead - Xms (Target: <5ms)
- [ ] 4.3 Concurrent Load - PASS/FAIL
- [ ] 4.4 Request Draining - Xs (Target: <30s)

### Phase 5: Stress Testing
- [ ] 5.1 Rapid Load/Unload - PASS/FAIL (X cycles)
- [ ] 5.2 Job Map Cleanup - PASS/FAIL
- [ ] 5.3 Error Recovery - PASS/FAIL

### Phase 6: Integration Testing
- [ ] 6.1 Middleware Blocking - PASS/FAIL
- [ ] 6.2 Always Accessible - PASS/FAIL
- [ ] 6.3 Multiple Model Switching - PASS/FAIL

## Issues Found

1. **Issue Description**
   - Severity: High/Medium/Low
   - Steps to Reproduce:
   - Expected Behavior:
   - Actual Behavior:
   - Logs/Screenshots:

## Performance Metrics

| Metric | Result | Target | Status |
|--------|--------|--------|--------|
| Model Load Time | Xs | <20s | PASS/FAIL |
| Request Overhead | Xms | <5ms | PASS/FAIL |
| Drain Time | Xs | <30s | PASS/FAIL |
| Memory Leak | X MB | <10 MB | PASS/FAIL |

## Conclusion

- [ ] All critical tests passed
- [ ] Performance within acceptable range
- [ ] No security vulnerabilities found
- [ ] Ready for production / Needs fixes

**Overall Status:** PASS / FAIL

**Recommendations:**
- Recommendation 1
- Recommendation 2
```

---

## 🎓 Additional Resources

- **CHANGELOG_MODEL_MANAGEMENT.md** - Full API reference
- **IMPLEMENTATION_PLAN.md** - Architecture details
- **Server logs:** `/tmp/llama-server.log`
- **Model files:** Ensure GGUF format
- **Tools:** curl, jq, ab, wrk

---

**End of Testing Guide**
