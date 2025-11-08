#!/usr/bin/env bash
# Trigger Guix build CI workflow manually
#
# Usage:
#   ./scripts/ci/trigger-guix-build.sh [--branch BRANCH] [--wait]
#
# Requirements:
#   - gh (GitHub CLI): https://cli.github.com/

set -euo pipefail

# Configuration
REPO="${REPO:-ggml-org/llama.cpp}"
WORKFLOW_FILE="guix-build.yml"
BRANCH="${BRANCH:-claude/integration-scenarios-ffi-gstreamer}"
WAIT_FOR_COMPLETION=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --branch)
            BRANCH="$2"
            shift 2
            ;;
        --wait)
            WAIT_FOR_COMPLETION=true
            shift
            ;;
        --help)
            echo "Usage: $0 [--branch BRANCH] [--wait]"
            echo ""
            echo "Options:"
            echo "  --branch BRANCH    Branch to build (default: claude/integration-scenarios-ffi-gstreamer)"
            echo "  --wait             Wait for workflow to complete"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check dependencies
if ! command -v gh >/dev/null 2>&1; then
    echo -e "${RED}Error: 'gh' (GitHub CLI) is required but not installed.${NC}"
    echo "Install from: https://cli.github.com/"
    exit 1
fi

# Check authentication
if ! gh auth status >/dev/null 2>&1; then
    echo -e "${RED}Error: Not authenticated with GitHub CLI.${NC}"
    echo "Run: gh auth login"
    exit 1
fi

echo -e "${BLUE}Triggering Guix build workflow...${NC}"
echo "Repository: ${REPO}"
echo "Branch:     ${BRANCH}"
echo "Workflow:   ${WORKFLOW_FILE}"
echo ""

# Trigger workflow
if gh workflow run "${WORKFLOW_FILE}" \
    --repo "${REPO}" \
    --ref "${BRANCH}"; then
    echo -e "${GREEN}✓ Workflow triggered successfully!${NC}"
else
    echo -e "${RED}✗ Failed to trigger workflow${NC}"
    exit 1
fi

echo ""
echo "View workflow runs:"
echo "  gh run list --repo ${REPO} --workflow=${WORKFLOW_FILE} --branch=${BRANCH}"
echo ""
echo "Monitor status:"
echo "  ./scripts/ci/monitor-guix-build.sh --watch --branch ${BRANCH}"

if [ "$WAIT_FOR_COMPLETION" = true ]; then
    echo ""
    echo -e "${BLUE}Waiting for workflow to start...${NC}"
    sleep 5

    # Get latest run ID
    RUN_ID=$(gh run list \
        --repo "${REPO}" \
        --workflow="${WORKFLOW_FILE}" \
        --branch="${BRANCH}" \
        --limit=1 \
        --json databaseId \
        --jq '.[0].databaseId')

    if [ -n "$RUN_ID" ]; then
        echo -e "${BLUE}Watching run: ${RUN_ID}${NC}"
        gh run watch "${RUN_ID}" --repo "${REPO}"
    else
        echo -e "${YELLOW}Could not find run ID. Check manually.${NC}"
    fi
fi
