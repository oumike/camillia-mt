# Camillia-MT Copilot Instructions

This repository is ESP32-S3 firmware for Meshtastic-compatible handhelds.
Treat this file as the default operating guide for AI-assisted work in this
workspace.

## Goals

- Keep changes safe on real hardware.
- Keep changes small and target-specific.
- Prefer clear evidence over assumptions.

## Hardware And Build Truth Sources

Use these files as the source of truth before changing board-specific behavior:

- `platformio.ini` for build environments and board flags.
- `src/hal/hw_tdeck.h`
- `src/hal/hw_tlora_pager.h`
- `src/hal/hw_cardputer.h`
- `src/hal/hw_heltec_v4.h`
- `src/hal/hw_mesh_deck.h`
- `docs/HARDWARE.md`

Do not invent pin maps, bus addresses, or peripheral wiring.

## Default Workflow

Use this staged flow for feature and bug work:

1. Scope
   - Identify target environment(s) in `platformio.ini`.
   - Identify affected modules in `src/` and `src/hal/`.
2. Implement
   - Apply the smallest possible patch.
   - Keep board-specific logic behind existing `DEVICE_*` guards.
3. Validate
   - Run checks only when requested by the user.
   - If validation is skipped, say so explicitly.
4. Summarize
   - Report changed files, behavior impact, and any residual risk.

## Non-Negotiables

- Do not run `pio run` automatically unless the user explicitly asks.
- Do not claim hardware success without evidence (serial logs, measured behavior,
  or user confirmation).
- Do not silently change behavior across all boards when only one target is
  requested.
- Do not move settings persistence away from NVS/Preferences to SPIFFS.
- Do not rewrite unrelated code during targeted fixes.

## Build Targets

Current PlatformIO environments include:

- `tdeck`
- `tlora-pager-tft`
- `cardputer-cap`
- `heltec-v4`
- `heltec-v4-vertical`
- `mesh-deck`
- `m9`

When a request says "mesh-deck", scope changes to `DEVICE_MESH_DECK` paths
unless asked to propagate.

## Config And Persistence

- Runtime config shape lives in `src/config_io.h` (`RhinoConfig`).
- Defaults and compile-time gates live in `src/config.h` and
  `src/config_io.cpp`.
- Config import/export handling is in `src/config_io.cpp` and web form handling
  in `src/web_config.cpp`.

When adding new config fields:

- Append only to `RhinoConfig`.
- Keep serialization, import/export, and UI/web handlers in sync.
- Preserve backward compatibility for existing saved blobs.

## Safety-Critical Repo Conventions

- Full-device backup and restore path is `backup.sh`; keep that workflow intact.
- Prefer explicit restore safeguards over convenience shortcuts.
- Preserve identity-sensitive behavior (node identity and key material) unless
  user intent is explicit.

## Stage Map (Plugin-Style)

Use this compact sequence, adapted from skill-chain workflows:

1. Plan: define target board, affected modules, and acceptance check.
2. Patch: implement focused code changes.
3. Verify: run requested build/test/monitor steps.
4. Deploy support: provide exact flash/verification commands when asked.

If a request is ambiguous on target board or acceptance criteria, ask one short
clarifying question before editing.
