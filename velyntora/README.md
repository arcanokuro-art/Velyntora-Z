# Velyntora application layer

This directory is the clean application layer built on top of the Krita engine.

## Modules
- `drawing/`: Velyntora Drawing UI ownership and integration boundary.
- `animation/`: reserved for the Animation environment.
- `shared/`: shared application-level UI/services (introduced only when code is actually shared).

Krita engine files are not moved here blindly. Existing engine paths remain intact until their CMake/runtime dependencies are verified.
