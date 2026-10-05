# Animation module

Reserved for Velyntora's Animation environment.

Animation will reuse the shared Krita canvas/brush/layer/undo infrastructure. It must not duplicate the Drawing engine. Animation-specific UI and services (timeline, frames, playback, onion skin and audio integration) will live under this module as they are implemented.
