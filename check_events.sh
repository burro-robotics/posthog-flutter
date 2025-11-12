#!/bin/bash

DB_PATH="$HOME/.local/share/posthog_flutter/posthog.db"

if [ ! -f "$DB_PATH" ]; then
    echo "Database not found at $DB_PATH"
    exit 1
fi

echo "=== PostHog Event Status ==="
echo ""

echo "Events in queue:"
sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM events;" 2>/dev/null || echo "0"

echo ""
echo "Recent events:"
sqlite3 "$DB_PATH" "SELECT id, substr(event_json, 1, 150) as preview FROM events ORDER BY created_at DESC LIMIT 5;" 2>/dev/null

echo ""
echo "Settings:"
sqlite3 "$DB_PATH" "SELECT key, value FROM settings;" 2>/dev/null

echo ""
echo "To see full event JSON:"
echo "sqlite3 $DB_PATH \"SELECT event_json FROM events ORDER BY created_at DESC LIMIT 1;\""

