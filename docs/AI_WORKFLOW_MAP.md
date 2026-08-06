# AI Workflow Map (Codex Plugin Adaptation)

This document maps the Attaky-style staged plugin flow to Camillia-MT work.
It is intentionally lightweight and operational.

## Stage Mapping

1. Brainstorm -> Task framing
   - Capture user intent, target board, and success condition.

2. Spec -> Change contract
   - Define files, guards (`DEVICE_*`), and behavior to preserve.

3. UI -> Interaction updates
   - For UI/config changes, update both on-device UI (`main_lvgl.cpp`) and web
     UI (`web_config.cpp`) when applicable.

4. Codegen -> Implementation
   - Apply the smallest patch set that satisfies the requested behavior.

5. Build -> Compile validation
   - Run `pio run -e <env>` only when requested.
   - Report errors with actionable next edits.

6. Deploy -> Flash and runtime verification support
   - Provide exact `flash.sh` or PlatformIO upload commands when asked.
   - Treat serial output and observed behavior as the acceptance signal.

7. Port -> New board bring-up discipline
   - Start from `src/hal/hw_<board>.h` and `platformio.ini` env flags.
   - Do not assert pin facts without repo evidence.

## Quick Checklists

### Feature/Fix Change

- Confirm target env(s).
- Patch code behind existing board guards when board-specific.
- Update config serialization paths if config fields change.
- Validate only when requested.
- Summarize behavior and risk.

### Board-Specific Change

- Verify pins/macros from the board header.
- Keep other boards unchanged.
- Confirm no hidden cross-target side effects in shared modules.

## Repository-Specific Rules

- Prefer NVS/Preferences persistence patterns.
- Keep `backup.sh` safety behavior conservative.
- Avoid broad refactors during targeted hardware fixes.
