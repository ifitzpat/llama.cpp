#!/usr/bin/env bash
# Monitor Guix build CI status from GitHub Actions
#
# Usage:
#   ./scripts/ci/monitor-guix-build.sh [--watch] [--branch BRANCH]
#
# Requirements:
#   - gh (GitHub CLI): https://cli.github.com/
#   - jq: JSON processor
#
# Environment:
#   GITHUB_TOKEN: Personal access token (optional, uses gh auth otherwise)

set -euo pipefail

# Configuration
REPO="${REPO:-ggml-org/llama.cpp}"
WORKFLOW_NAME="Guix Build"
BRANCH="${BRANCH:-claude/integration-scenarios-ffi-gstreamer}"
WATCH_MODE=false
WATCH_INTERVAL=30

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --watch)
            WATCH_MODE=true
            shift
            ;;
        --branch)
            BRANCH="$2"
            shift 2
            ;;
        --interval)
            WATCH_INTERVAL="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--watch] [--branch BRANCH] [--interval SECONDS]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check dependencies
command -v gh >/dev/null 2>&1 || { echo >&2 "Error: 'gh' (GitHub CLI) is required but not installed."; exit 1; }
command -v jq >/dev/null 2>&1 || { echo >&2 "Error: 'jq' is required but not installed."; exit 1; }

# Check authentication
if ! gh auth status >/dev/null 2>&1; then
    echo >&2 "Error: Not authenticated with GitHub CLI. Run 'gh auth login'"
    exit 1
fi

# Function to get latest workflow run
get_latest_run() {
    gh api \
        -H "Accept: application/vnd.github+json" \
        "/repos/${REPO}/actions/workflows" \
        | jq -r ".workflows[] | select(.name == \"${WORKFLOW_NAME}\") | .id" \
        | head -1
}

# Function to get run status
get_run_status() {
    local workflow_id=$1

    gh api \
        -H "Accept: application/vnd.github+json" \
        "/repos/${REPO}/actions/workflows/${workflow_id}/runs?branch=${BRANCH}&per_page=1" \
        | jq -r '.workflow_runs[0] // empty'
}

# Function to display status
display_status() {
    local run_json=$1

    if [ -z "$run_json" ] || [ "$run_json" = "null" ]; then
        echo -e "${YELLOW}No workflow runs found for branch: ${BRANCH}${NC}"
        return
    fi

    local status=$(echo "$run_json" | jq -r '.status')
    local conclusion=$(echo "$run_json" | jq -r '.conclusion // "in_progress"')
    local created_at=$(echo "$run_json" | jq -r '.created_at')
    local updated_at=$(echo "$run_json" | jq -r '.updated_at')
    local run_number=$(echo "$run_json" | jq -r '.run_number')
    local run_id=$(echo "$run_json" | jq -r '.id')
    local html_url=$(echo "$run_json" | jq -r '.html_url')
    local commit_sha=$(echo "$run_json" | jq -r '.head_sha' | cut -c1-7)
    local commit_msg=$(echo "$run_json" | jq -r '.head_commit.message' | head -1)

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${BLUE}Guix Build Status${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "Branch:     ${YELLOW}${BRANCH}${NC}"
    echo -e "Run:        #${run_number} (${run_id})"
    echo -e "Commit:     ${commit_sha} - ${commit_msg}"
    echo -e "Created:    ${created_at}"
    echo -e "Updated:    ${updated_at}"

    # Status with color
    case "$status" in
        completed)
            case "$conclusion" in
                success)
                    echo -e "Status:     ${GREEN}✓ SUCCESS${NC}"
                    ;;
                failure)
                    echo -e "Status:     ${RED}✗ FAILED${NC}"
                    ;;
                cancelled)
                    echo -e "Status:     ${YELLOW}○ CANCELLED${NC}"
                    ;;
                *)
                    echo -e "Status:     ${YELLOW}${conclusion}${NC}"
                    ;;
            esac
            ;;
        in_progress)
            echo -e "Status:     ${BLUE}⟳ IN PROGRESS${NC}"
            ;;
        queued)
            echo -e "Status:     ${YELLOW}⋯ QUEUED${NC}"
            ;;
        *)
            echo -e "Status:     ${status}"
            ;;
    esac

    echo -e "URL:        ${html_url}"
    echo ""

    # Get jobs
    local jobs=$(gh api \
        -H "Accept: application/vnd.github+json" \
        "/repos/${REPO}/actions/runs/${run_id}/jobs" \
        | jq -r '.jobs[]')

    if [ -n "$jobs" ]; then
        echo -e "${BLUE}Jobs:${NC}"
        echo "$jobs" | jq -r '. | "  \(.name): \(.status) \(.conclusion // "in_progress")"'
    fi

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Function to download artifacts
download_artifacts() {
    local run_id=$1
    local output_dir="${2:-./artifacts}"

    echo -e "${BLUE}Downloading artifacts...${NC}"

    mkdir -p "$output_dir"

    gh api \
        -H "Accept: application/vnd.github+json" \
        "/repos/${REPO}/actions/runs/${run_id}/artifacts" \
        | jq -r '.artifacts[] | "\(.name) \(.id)"' \
        | while read -r name id; do
            echo "  Downloading: $name"
            gh api \
                -H "Accept: application/vnd.github+json" \
                "/repos/${REPO}/actions/artifacts/${id}/zip" \
                > "${output_dir}/${name}.zip"
        done

    echo -e "${GREEN}Artifacts downloaded to: ${output_dir}${NC}"
}

# Main monitoring loop
monitor() {
    local workflow_id=$(get_latest_run)

    if [ -z "$workflow_id" ]; then
        echo -e "${RED}Error: Could not find workflow '${WORKFLOW_NAME}'${NC}"
        exit 1
    fi

    while true; do
        clear
        echo "Monitoring workflow: ${WORKFLOW_NAME}"
        echo "Workflow ID: ${workflow_id}"
        echo ""

        local run_status=$(get_run_status "$workflow_id")
        display_status "$run_status"

        if [ "$WATCH_MODE" = false ]; then
            break
        fi

        local status=$(echo "$run_status" | jq -r '.status')
        if [ "$status" = "completed" ]; then
            echo -e "${GREEN}Workflow completed!${NC}"

            read -p "Download artifacts? (y/n) " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                local run_id=$(echo "$run_status" | jq -r '.id')
                download_artifacts "$run_id"
            fi
            break
        fi

        echo ""
        echo "Refreshing in ${WATCH_INTERVAL} seconds... (Ctrl+C to stop)"
        sleep "$WATCH_INTERVAL"
    done
}

# Run monitor
monitor
