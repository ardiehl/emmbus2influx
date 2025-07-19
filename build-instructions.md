# Build Instructions

## Common

First get git submodules:

```txt
git submodule update --init --recursive
```

## Alpine Linux

Install dependencies: to be added...

Build:

```txt
make
```

## Debian Linux

Install dependencies: to be added...

Build:

```txt
make
```

## FreeBSD

Install dependencies:

```txt
pkg install autotools gmake wget
pkg install curl libpaho-mqtt3 muparser readline zstd
```

Build:

```txt
gmake
```

## macOS (Darwin)

Install dependencies with Homebrew:

```txt
brew install automake make wget
brew install curl libpaho-mqtt muparser
```

Build:

```txt
make
```
