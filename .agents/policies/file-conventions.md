# File Conventions

> Referenced from CLAUDE.md. Update this file when adding a new agent or changing agent scope.

Each path area has conventions and a primary owner. When working across areas, follow each area's conventions. Ownership is about **review responsibility and default routing**, not hard write-locks.

| Path | Primary Owner | Follow These Rules |
|------|-------------------|-------------------|
| `main/include/chopper/adapters/`, `main/chopper/adapters/` | hardware | Serial protocol specs, datasheet references, checksum validation |
| `main/include/chopper/hal/`, `main/chopper/hal/` | hardware | HAL interface contracts, driver registration |
| `main/include/chopper/`, `main/chopper/` (everything else) | implementer | No heap on hot path, fixed arrays, safety gating |
| `test/test_*.cpp` | tester | TEST/PASS/ASSERT macros, results summary, Makefile target |
| `test/mocks/` | tester | Mirror ESP-IDF paths, minimal stubs |
| `docs/` | docs / architect (`design/` only) | Verify API signatures against source |
| `tools/telemetry_ui/` | telemetry-ui | Zero-build JS, `--simulate` must work |
| `description/` | telemetry-ui | URDF/STL meshes |
| `.scripts/` | implementer | Shell scripts, build utilities |
| `Makefile` | implementer | Tester may add test targets to the test section |
| `platformio.ini`, `sdkconfig.defaults` | implementer | Build configuration |
