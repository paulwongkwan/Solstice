# Create a stable release

Pre-releases are created automatically on every push to the `master` branch. A stable release is made by promoting one
of those pre-releases.

1. Wait for the pre-release to be created and for its build workflows to finish.
2. Confirm the pre-release contains the Windows installer (`Solstice.exe`).
3. Edit the release notes.

   - Add any top-level release notes.
   - Ensure any security fixes are mentioned first with links to the security advisories.
   - Following security advisories, breaking changes should be mentioned. Be sure to mention anything the user may need
     to do to ensure a smooth upgrade experience.
   - Then highlight any notable new features and/or fixes.
   - Lastly, reduce the automated changelog list. Things like dependency updates and ci updates can mostly be removed.

4. When saving, uncheck "pre-release" and save the release.

## Building a release locally

```bash
cmake -B cmake-build-release -G Ninja -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=OFF
cmake --build cmake-build-release
```

`cpack` writes the installer as `cmake-build-release/cpack_artifacts/Solstice.exe`, and `cpack -G ZIP` writes the portable archive
next to it. Attach them to the GitHub release manually when you build outside of CI.
