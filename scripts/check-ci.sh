#!/usr/bin/env bash
# Check GitHub Actions CI status for llama.cpp
# Adapted from el-rdf check-ci.sh script
#
# Usage: ./scripts/check-ci.sh [branch]

set -euo pipefail

# Configuration
REPO="ifitzpat/llama.cpp"
BRANCH="${1:-claude/gstreamer-implementation-011CUrgj9DxaFqUMTWowxyBi}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
GRAY='\033[0;90m'
NC='\033[0m' # No Color

echo "Checking CI status for ${BLUE}${REPO}${NC} branch ${BLUE}${BRANCH}${NC}"
echo ""

# Fetch latest workflow runs for the branch
API_URL="https://api.github.com/repos/${REPO}/actions/runs?branch=${BRANCH}&per_page=5"

echo "Fetching workflow runs..."
RUNS=$(curl -s "${API_URL}")

# Check if we got valid JSON
if ! echo "${RUNS}" | grep -q '"workflow_runs"'; then
    echo "${RED}❌ Failed to fetch workflow runs${NC}"
    echo "API Response: ${RUNS}"
    exit 1
fi

# Parse and display runs
echo "${RUNS}" | grep -o '"id":[0-9]*' | head -5 | while read -r line; do
    RUN_ID=$(echo "$line" | grep -o '[0-9]*')

    # Get run details
    RUN_DATA=$(echo "${RUNS}" | grep -A 20 "\"id\":${RUN_ID}")

    STATUS=$(echo "${RUN_DATA}" | grep '"status"' | head -1 | sed 's/.*"status":"\([^"]*\)".*/\1/')
    CONCLUSION=$(echo "${RUN_DATA}" | grep '"conclusion"' | head -1 | sed 's/.*"conclusion":"\([^"]*\)".*/\1/' || echo "null")
    NAME=$(echo "${RUN_DATA}" | grep '"name"' | head -1 | sed 's/.*"name":"\([^"]*\)".*/\1/')
    CREATED=$(echo "${RUN_DATA}" | grep '"created_at"' | head -1 | sed 's/.*"created_at":"\([^"]*\)".*/\1/')

    # Format output with status indicator
    if [ "${STATUS}" = "completed" ]; then
        if [ "${CONCLUSION}" = "success" ]; then
            ICON="${GREEN}✅${NC}"
            STATUS_TEXT="${GREEN}SUCCESS${NC}"
        elif [ "${CONCLUSION}" = "failure" ]; then
            ICON="${RED}❌${NC}"
            STATUS_TEXT="${RED}FAILURE${NC}"
        elif [ "${CONCLUSION}" = "cancelled" ]; then
            ICON="${YELLOW}⚠️${NC}"
            STATUS_TEXT="${YELLOW}CANCELLED${NC}"
        else
            ICON="${GRAY}❓${NC}"
            STATUS_TEXT="${GRAY}${CONCLUSION}${NC}"
        fi
    elif [ "${STATUS}" = "in_progress" ]; then
        ICON="${BLUE}⏳${NC}"
        STATUS_TEXT="${BLUE}IN PROGRESS${NC}"
    elif [ "${STATUS}" = "queued" ]; then
        ICON="${YELLOW}⏸️${NC}"
        STATUS_TEXT="${YELLOW}QUEUED${NC}"
    else
        ICON="${GRAY}❓${NC}"
        STATUS_TEXT="${GRAY}${STATUS}${NC}"
    fi

    echo -e "${ICON} ${STATUS_TEXT} - ${NAME}"
    echo -e "   ${GRAY}Run ID: ${RUN_ID}${NC}"
    echo -e "   ${GRAY}Created: ${CREATED}${NC}"
    echo -e "   ${GRAY}URL: https://github.com/${REPO}/actions/runs/${RUN_ID}${NC}"

    # If failed, try to get job details
    if [ "${CONCLUSION}" = "failure" ]; then
        JOBS_URL="https://api.github.com/repos/${REPO}/actions/runs/${RUN_ID}/jobs"
        JOBS=$(curl -s "${JOBS_URL}")

        # Find failed steps
        FAILED_JOBS=$(echo "${JOBS}" | grep -B 5 '"conclusion":"failure"' | grep '"name"' | sed 's/.*"name":"\([^"]*\)".*/\1/' | head -3)

        if [ -n "${FAILED_JOBS}" ]; then
            echo -e "   ${RED}Failed jobs:${NC}"
            echo "${FAILED_JOBS}" | while read -r job; do
                echo -e "   ${RED}  - ${job}${NC}"
            done
        fi
    fi

    echo ""
done

# Summary
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Count statuses
TOTAL=$(echo "${RUNS}" | grep -c '"id"' || echo "0")
SUCCESS=$(echo "${RUNS}" | grep -c '"conclusion":"success"' || echo "0")
FAILURE=$(echo "${RUNS}" | grep -c '"conclusion":"failure"' || echo "0")
IN_PROGRESS=$(echo "${RUNS}" | grep -c '"status":"in_progress"' || echo "0")

echo "Summary (last ${TOTAL} runs):"
echo -e "  ${GREEN}✅ Success: ${SUCCESS}${NC}"
echo -e "  ${RED}❌ Failure: ${FAILURE}${NC}"
echo -e "  ${BLUE}⏳ In Progress: ${IN_PROGRESS}${NC}"
echo ""

# Exit code based on latest run
LATEST_STATUS=$(echo "${RUNS}" | grep '"status"' | head -1 | sed 's/.*"status":"\([^"]*\)".*/\1/')
LATEST_CONCLUSION=$(echo "${RUNS}" | grep '"conclusion"' | head -1 | sed 's/.*"conclusion":"\([^"]*\)".*/\1/' || echo "null")

if [ "${LATEST_STATUS}" = "completed" ] && [ "${LATEST_CONCLUSION}" = "success" ]; then
    echo -e "${GREEN}✅ Latest run succeeded!${NC}"
    exit 0
elif [ "${LATEST_STATUS}" = "completed" ] && [ "${LATEST_CONCLUSION}" = "failure" ]; then
    echo -e "${RED}❌ Latest run failed!${NC}"
    echo -e "Check details at: https://github.com/${REPO}/actions?query=branch%3A${BRANCH}"
    exit 1
elif [ "${LATEST_STATUS}" = "in_progress" ] || [ "${LATEST_STATUS}" = "queued" ]; then
    echo -e "${BLUE}⏳ Latest run is still in progress...${NC}"
    echo -e "Watch at: https://github.com/${REPO}/actions?query=branch%3A${BRANCH}"
    exit 2
else
    echo -e "${GRAY}❓ Latest run status: ${LATEST_STATUS} / ${LATEST_CONCLUSION}${NC}"
    exit 3
fi
