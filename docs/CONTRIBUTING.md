# Contributing to ESP32OS

[Documentation](README.md) · [Architecture](ARCHITECTURE.md) · [Testing](TESTING.md)

Useful contributions include reproducible bug fixes, board validation, peripheral drivers, command improvements, examples and documentation. Keep each pull request focused and explain what a user can do differently after the change.

## Set up a change

1. Fork the repository and clone your fork.
2. Open an ESP-IDF 6.0 terminal and change into the checkout.
3. Create a feature branch from `master`.
4. Make the change and run the checks relevant to it.
5. Open a pull request targeting `master`, with results and any untested areas.

```bash
git clone https://github.com/YOUR_USERNAME/Esp32OS.git
cd Esp32OS
git switch -c feature/short-description
idf.py set-target esp32
idf.py build
python3 tools/test_build_health.py --target esp32
```

Replace the repository owner and target as needed. See [installation and flashing](USER_GUIDE.md#build-and-flash) for local setup. Do not commit credentials, local `sdkconfig`, build artifacts or generated dependency folders.

## Engineering conventions

- Follow existing C naming: `snake_case` functions, `s_` module state and uppercase constants.
- Use four-space indentation, braces for control flow and readable, focused functions.
- Check `esp_err_t`, allocation and task-creation results. Log useful context or propagate the error.
- Account for constrained memory. Keep buffers bounded, document significant allocations and avoid blocking work in interrupt/timer contexts.
- Protect shared state deliberately. A component's mutex does not make all its callers or related modules thread-safe.
- Declare direct CMake dependencies. Check the actual call graph before adding coupling.
- Use `shell_write()` / `shell_printf()` for command output and `OS_LOG*` for service logs.
- Keep firmware initialization order explicit and handle partial initialization/cleanup.

The [extension guide](EXTENDING.md) shows how to register a command, start a service and add a component. Public headers are the source of truth for API signatures.

## Match validation to the change

| Change | Expected evidence |
|---|---|
| Documentation or images | Check source claims, relative links, examples, rendering and screenshot provenance |
| Shared firmware | Build and run artifact checks for `esp32`, `esp32s3` and `esp32c3` where available |
| Shell/env/scheduler behavior | Relevant host-model scenarios plus compiled firmware/device validation |
| Peripheral or networking code | Board, wiring/network setup, procedure and observed result |
| Dashboard | Desktop/mobile browser checks, API behavior and device validation where available |

Use [Testing](TESTING.md) for exact commands and current limits. If hardware or a target toolchain is unavailable, say so in the PR. Do not label unrun checks as passing. Repository CI builds firmware and runs host checks; it does not certify connected hardware.

## Keep documentation honest

When changing behavior, update the matching guide and help text in the same PR. During this documentation refresh, several claims had drifted from the implementation; use these checks to prevent recurrence:

- Verify default behavior against `main/main.c`, `main/Kconfig.projbuild`, `sdkconfig.defaults` and `partitions.csv`.
- Link public headers instead of copying large declarations that can go stale.
- Distinguish implemented code, build coverage and measured hardware behavior.
- Label screenshots made with sample data; keep the real dashboard HTML as their visual source.
- Do not claim Linux process isolation, full POSIX filesystem behavior or OTA support from a command name alone.
- Keep proposed work separate from shipped features. MQTT, PWM, timers, environment storage, scheduled commands and the browser dashboard already exist.

For screenshot maintenance, see [Visual assets](assets/README.md).

## Pull request checklist

- [ ] Explain the concrete problem and resulting behavior.
- [ ] Scope changes to the intended feature or fix.
- [ ] Update relevant help, documentation and examples.
- [ ] Check public API errors, resource ownership and memory impact.
- [ ] Run applicable checks and include their results.
- [ ] State target, board and wiring for hardware evidence.
- [ ] List checks that could not be run and known limitations.
- [ ] Remove credentials and private data from fixtures, screenshots and logs.

The [PR workflow](../.github/workflows/pr-checks.yml) builds and publishes review artifacts. After changes reach `master`, the [release workflow](../.github/workflows/master-release.yml) verifies reusable PR artifacts or rebuilds, then publishes release bundles.

## Commit messages

Use a short component-oriented subject, with a body when the reason needs explanation:

```text
os_shell: report invalid arguments for scheduled commands
os_networking: handle reconnect failure explicitly
docs: clarify default partition and OTA requirements
```

## Good areas to improve

- Reproduce and fix filesystem directory semantics or document alternative storage choices.
- Add measured board/peripheral validation and target-specific examples.
- Improve browser command capture and remote-interface access control.
- Validate an OTA-capable flash layout and recovery procedure.
- Expand tests that exercise the actual C implementation.

Discuss broad architectural changes before implementation. Include a reproducible scenario and the constraints your approach addresses.
