# Contributing

Bug reports with a log are the most useful contribution: use the bug report form under Issues and
attach the newest file from `out\CUSA28763\user\log`.

For code changes:

1. Build with `scripts/build-windows.sh` (see [BUILDING.md](BUILDING.md)).
2. Run the tests: `python3 -m unittest discover -s tests -p 'test_*.py'`.
3. If you add or rename a `PS4RT_*` environment variable, run `python3 scripts/gen_env_docs.py` and
   commit the updated `docs/ENVIRONMENT.md` (CI fails otherwise).
4. Keep binaries out of git (`scripts/check_repo_size.py` enforces this). Releases carry the zip.
5. One change per commit, with a message that says what and why.

Game data, keys, PKGs and decrypted executables must never be committed or posted.
