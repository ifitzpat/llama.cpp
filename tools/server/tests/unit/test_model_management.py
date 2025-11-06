"""
Test-Driven Development tests for Model Management API

These tests define the expected behavior of the model management endpoints:
- POST /v1/models/load
- POST /v1/models/unload
- GET  /v1/models/status
- GET  /v1/models/jobs/{job_id}
- GET  /v1/models/jobs/{job_id}/wait
- POST /v1/models/reset

Run with: ./tests.sh unit/test_model_management.py -v
"""

import pytest
import time
import requests
from utils import *


server = ServerPreset.tinyllama2()


@pytest.fixture(autouse=True)
def create_server():
    global server
    server = ServerPreset.tinyllama2()


class TestModelManagementBasics:
    """Test basic model management functionality"""

    def test_model_management_disabled_by_default(self):
        """Model management endpoints should be disabled unless explicitly enabled"""
        global server
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": "models/test.gguf"
        })

        # Should return 501 Not Supported when feature is disabled
        assert res.status_code == 501
        assert "error" in res.body
        assert "disabled" in res.body["error"]["message"].lower()

    def test_model_management_enabled(self):
        """When enabled, endpoints should be accessible"""
        global server
        server.endpoint_model_management = True  # Enable the feature
        server.start()

        # Status endpoint should work even without a job
        res = server.make_request("GET", "/v1/models/status")
        assert res.status_code == 200
        assert "server_state" in res.body
        assert "model_loaded" in res.body

    def test_model_status_shows_current_model(self):
        """Status endpoint should show currently loaded model info"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("GET", "/v1/models/status")
        assert res.status_code == 200
        assert res.body["model_loaded"] == True
        assert res.body["server_state"] == "ready"
        assert "model_info" in res.body
        assert ".gguf" in res.body["model_info"]["path"]
        assert res.body["model_info"]["n_ctx"] > 0
        assert res.body["model_info"]["n_parallel"] > 0


class TestModelLoading:
    """Test model loading functionality"""

    def test_load_model_requires_model_path(self):
        """Load request without model_path should fail with validation error"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "n_ctx": 2048
        })

        assert res.status_code == 400  # Bad request
        assert "error" in res.body
        assert "model_path" in res.body["error"]["message"].lower()

    def test_load_model_returns_job_id(self):
        """Load request should return immediately with job_id"""
        global server
        server.endpoint_model_management = True
        server.model_allowed_dirs = ["./models"]
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file,  # Use existing model
            "n_ctx": 2048
        })

        # Should return 202 Accepted (not 200)
        assert res.status_code == 202
        assert "job_id" in res.body
        assert "status" in res.body
        assert res.body["status"] == "accepted"
        assert "model_job_" in res.body["job_id"]

    def test_load_model_path_validation(self):
        """Load should reject paths outside allowed directories"""
        global server
        server.endpoint_model_management = True
        server.model_allowed_dirs = ["./models"]
        server.start()

        # Try to load from parent directory
        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": "../../etc/passwd"
        })

        assert res.status_code == 202  # Job accepted
        job_id = res.body["job_id"]

        # Wait briefly for job to process
        time.sleep(1)

        # Check job status - should have failed
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}")
        assert res.status_code == 200
        assert res.body["status"] == "failed"
        assert "path" in res.body.get("error", "").lower() or \
               "allowed" in res.body.get("error", "").lower()

    def test_load_model_invalid_file(self):
        """Load should reject non-GGUF files"""
        global server
        server.endpoint_model_management = True
        server.model_allowed_dirs = ["."]
        server.start()

        # Try to load a non-GGUF file
        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": "./README.md"
        })

        assert res.status_code == 202
        job_id = res.body["job_id"]
        time.sleep(1)

        res = server.make_request("GET", f"/v1/models/jobs/{job_id}")
        assert res.body["status"] == "failed"
        assert "gguf" in res.body.get("error", "").lower()


class TestJobTracking:
    """Test job status tracking and polling"""

    def test_job_status_endpoint(self):
        """Job status endpoint should return job details"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Create a job
        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file
        })
        job_id = res.body["job_id"]

        # Get job status
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}")
        assert res.status_code == 200
        assert res.body["job_id"] == job_id
        assert "status" in res.body
        assert res.body["status"] in ["pending", "in_progress", "completed", "failed"]
        assert "progress" in res.body

    def test_job_not_found(self):
        """Requesting non-existent job should return 404"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("GET", "/v1/models/jobs/nonexistent_job_12345")
        assert res.status_code == 404
        assert "error" in res.body

    def test_job_progress_tracking(self):
        """Job should report progress during execution"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file
        })
        job_id = res.body["job_id"]

        # Poll for progress
        max_polls = 10
        saw_in_progress = False

        for _ in range(max_polls):
            res = server.make_request("GET", f"/v1/models/jobs/{job_id}")
            status = res.body["status"]

            if status == "in_progress":
                saw_in_progress = True
                assert res.body["progress"] >= 0
                assert res.body["progress"] <= 100
                assert "progress_message" in res.body

            if status in ["completed", "failed"]:
                break

            time.sleep(0.5)

        # For small models, might complete too fast to catch in_progress
        # but should at least complete
        assert res.body["status"] in ["completed", "failed", "in_progress"]

    def test_job_wait_endpoint(self):
        """Wait endpoint should block until job completes"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file
        })
        job_id = res.body["job_id"]

        # Wait for completion (with timeout)
        start_time = time.time()
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=30")
        elapsed = time.time() - start_time

        assert res.status_code == 200
        assert res.body["waited"] == True
        assert res.body["status"] in ["completed", "failed"]

        # Should complete within timeout
        assert elapsed < 30


class TestModelUnloading:
    """Test model unloading functionality"""

    def test_unload_model_success(self):
        """Unload should successfully unload current model"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Verify model is loaded
        res = server.make_request("GET", "/v1/models/status")
        assert res.body["model_loaded"] == True

        # Unload
        res = server.make_request("POST", "/v1/models/unload")
        assert res.status_code == 202
        job_id = res.body["job_id"]

        # Wait for completion
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=10")
        assert res.body["status"] == "completed"

        # Check status - no model should be loaded
        res = server.make_request("GET", "/v1/models/status")
        assert res.body["model_loaded"] == False
        assert res.body["server_state"] == "no_model"

    def test_unload_when_no_model(self):
        """Unload when no model loaded should return error"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Unload first time
        res = server.make_request("POST", "/v1/models/unload")
        job_id = res.body["job_id"]
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=10")

        # Try to unload again
        res = server.make_request("POST", "/v1/models/unload")
        assert res.status_code == 400  # Bad request
        assert "no model" in res.body["error"]["message"].lower()

    def test_requests_rejected_after_unload(self):
        """Inference requests should be rejected when no model loaded"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Unload model
        res = server.make_request("POST", "/v1/models/unload")
        job_id = res.body["job_id"]
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=10")

        # Try to make inference request
        res = server.make_request("POST", "/completion", data={
            "prompt": "Hello",
            "n_predict": 10
        })

        assert res.status_code == 503  # Service unavailable
        assert "error" in res.body
        assert "no model" in res.body["error"]["message"].lower()


class TestGracefulRequestHandling:
    """Test in-flight request handling during model transitions"""

    def test_requests_rejected_during_transition(self):
        """New requests should be rejected during model load/unload"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Start a model unload
        res = server.make_request("POST", "/v1/models/unload")
        job_id = res.body["job_id"]

        # Immediately try to make inference request
        res = server.make_request("POST", "/completion", data={
            "prompt": "Hello",
            "n_predict": 10
        })

        # Should be rejected during transition
        assert res.status_code in [503, 429]  # Unavailable or Too Many Requests

    def test_active_requests_complete_before_unload(self):
        """Active streaming requests should complete before model unloads"""
        # This is a more complex test that would require streaming support
        # Placeholder for future implementation
        pass


class TestErrorRecovery:
    """Test error recovery and rollback functionality"""

    def test_failed_load_maintains_previous_model(self):
        """If model load fails, previous model should remain loaded"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Get current model
        res = server.make_request("GET", "/v1/models/status")
        original_model = res.body["model_info"]["path"]

        # Try to load invalid model
        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": "./nonexistent.gguf"
        })
        job_id = res.body["job_id"]
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=10")

        # Job should have failed
        assert res.body["status"] == "failed"

        # Original model should still be loaded
        res = server.make_request("GET", "/v1/models/status")
        assert res.body["model_loaded"] == True
        assert res.body["model_info"]["path"] == original_model

        # Server should be ready, not in error state
        assert res.body["server_state"] == "ready"

    def test_reset_from_error_state(self):
        """Reset endpoint should recover from error state"""
        # This test requires a way to force error state
        # Placeholder for future implementation
        pass


class TestConcurrency:
    """Test concurrent job handling"""

    def test_sequential_job_processing(self):
        """Jobs should be processed sequentially, not in parallel"""
        global server
        server.endpoint_model_management = True
        server.start()

        # Submit two jobs quickly
        res1 = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file
        })
        res2 = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file
        })

        job_id1 = res1.body["job_id"]
        job_id2 = res2.body["job_id"]

        # Both should be accepted
        assert res1.status_code == 202
        assert res2.status_code == 202

        # Jobs should have different IDs
        assert job_id1 != job_id2

        # Check that jobs are processed sequentially
        # (one completes before the other starts)
        # This is implementation-dependent and may need refinement


class TestConfigurationParameters:
    """Test various configuration options for model loading"""

    def test_load_with_custom_n_ctx(self):
        """Model should load with custom n_ctx parameter"""
        global server
        server.endpoint_model_management = True
        server.start()

        custom_n_ctx = 1024

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file,
            "n_ctx": custom_n_ctx
        })
        job_id = res.body["job_id"]
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=30")

        if res.body["status"] == "completed":
            # Check new model has correct n_ctx
            res = server.make_request("GET", "/v1/models/status")
            assert res.body["model_info"]["n_ctx"] == custom_n_ctx

    def test_load_with_gpu_layers(self):
        """Model should respect n_gpu_layers parameter"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("POST", "/v1/models/load", data={
            "model_path": server.model_file,
            "n_gpu_layers": 10
        })
        job_id = res.body["job_id"]
        res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=30")

        # Job should complete (even if GPU not available, it should handle gracefully)
        assert res.body["status"] in ["completed", "failed"]


class TestQueueManagement:
    """Test queue behavior during model transitions"""

    def test_queue_stats_in_status(self):
        """Status endpoint should include queue statistics"""
        global server
        server.endpoint_model_management = True
        server.start()

        res = server.make_request("GET", "/v1/models/status")
        assert "queue" in res.body
        assert "pending_tasks" in res.body["queue"]
        assert "running" in res.body["queue"]


# Mark slow tests
@pytest.mark.slow
class TestPerformance:
    """Performance and stress tests"""

    def test_multiple_load_unload_cycles(self):
        """Server should handle multiple load/unload cycles without leaks"""
        global server
        server.endpoint_model_management = True
        server.start()

        num_cycles = 5
        for i in range(num_cycles):
            # Load
            res = server.make_request("POST", "/v1/models/load", data={
                "model_path": server.model_file
            })
            job_id = res.body["job_id"]
            res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=30")
            assert res.body["status"] == "completed", f"Load cycle {i} failed"

            # Unload
            res = server.make_request("POST", "/v1/models/unload")
            job_id = res.body["job_id"]
            res = server.make_request("GET", f"/v1/models/jobs/{job_id}/wait?timeout=30")
            assert res.body["status"] == "completed", f"Unload cycle {i} failed"


if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
