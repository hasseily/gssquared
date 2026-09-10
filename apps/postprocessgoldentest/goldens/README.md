These lossless GPU reference images were generated from the unchanged SuperDuperDisplay CRT and bezel shaders identified in `manifest.json`. They are test-only resources.

The manifest contains all settings, timestamps, input phases and asset references needed to replay the cases. `apps/postprocessfixtures/Fixture.hpp` supplies identical source pixels and the shared comparison metrics. Goldens must be regenerated from `postprocessreferencetest`'s original OpenGL readbacks, never from the production renderer.

See `Docs/PostprocessingReference.md` for regeneration commands, tolerances, attribution and limitations.
