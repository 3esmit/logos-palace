#!/usr/bin/env node

// User-outcome scenario entry point:
// operator startup -> creator authoring -> member join and interaction ->
// moderation -> durable door transition -> provider shutdown and recovery.
// The implementation remains shared with the proven local story while the
// supported launcher and report use this user-facing name.
await import("./basecamp_local_mvp_user_flow.mjs");
