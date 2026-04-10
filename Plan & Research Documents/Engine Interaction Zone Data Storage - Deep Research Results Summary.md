# Deep Research Results: FF8 Engine Interaction Zone Data Storage
# Received: 2026-04-07 (session 43)
# Source: ChatGPT deep research

# See uploaded file for full text.
# Key finding: Interaction zone coordinates are stored as PSHN_L literal values
# in each TARGET entity's own init script (script slot 0), NOT in the Director.
# The engine runs init scripts for ALL entities on field load and populates
# entity struct fields (SETLINE line coords, SET3 positions, TALKRADIUS).
# The engine's native proximity handler (0x4775C0) checks these struct values
# every frame, independent of Director script execution.
# The Director pattern is functionally redundant dead code on fields where it
# falls outside the active entity window.

# Full research text saved alongside this summary file.
