# Contributing to Perlite

- C++23, formatted with the repository's `.clang-format`. No comments in code: name things so they read on their own.
- Keep each subsystem in its folder (see the layout in `README.md`). A new mob gets its own folder under `src/world/actor/mob/<category>/` and a `Register<Mob>` call in `VanillaMobs.cpp`. A new command goes in the matching file under `src/server/commands/builtin/`.
- Every change builds and passes `ctest --test-dir build`. Add a test next to the ones in `tests/` when you change behavior.
- Plugin-facing changes go through perlin-api, so the client and the server keep one ABI.
