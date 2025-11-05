# Building WireGuard Tools as Static Executables

## Static Linking

To build `wg` as a fully static executable with no runtime dependencies, use the `STATIC=yes` option:

```bash
cd src
make clean
make STATIC=yes
```

This will produce a statically linked binary that includes all libraries and has no external dependencies.

## Verification

You can verify the binary is static by running:

```bash
ldd wg
```

For a static binary, this should output: `not a dynamic executable`

You can also check with:

```bash
file wg
```

Which should show: `statically linked`

## Using musl libc (Recommended for smaller binaries)

For even smaller static binaries without glibc, you can use musl libc:

```bash
# Install musl-gcc (Ubuntu/Debian)
sudo apt-get install musl-tools musl-dev

# Build with musl
cd src
make clean
make CC=musl-gcc STATIC=yes
```

## Notes

- The static glibc build may show a warning about `getaddrinfo` requiring runtime shared libraries. This is expected and the binary will still work correctly.
- Static binaries are significantly larger than dynamic ones (typically ~1.2MB vs ~100KB)
- Static binaries are portable across different Linux distributions without dependency concerns
- The `STATIC` option works for Linux builds; other platforms may have different requirements

## Build Options Summary

| Option | Command | Result |
|--------|---------|--------|
| Default (dynamic) | `make` | Small binary, requires glibc at runtime |
| Static with glibc | `make STATIC=yes` | Larger binary, no runtime dependencies |
| Static with musl | `make CC=musl-gcc STATIC=yes` | Medium binary, no glibc dependency |
