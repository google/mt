# `mt` - Modern, minimalist, minty-fresh terminal.

This is intended to be a modern, minimalist terminal emulator for X with the
following design priorities:

- Work well with other platform's modern terminal emulators (iTerm2, hterm).
  This means largely being xterm-compatible rather than using a custom terminfo
  database entry.
- Zero non-terminal UI components (no menus, no scroll bars, etc.) and leverage
  in-terminal tools like tmux for these kinds of features.
- High quality font rendering and color support. The terminal itself should
  look as good as it is possible to look in X.

While satisfying these criteria, the terminal should be as small and
low-overhead as possible. Also, it should use any modern software development
tools that make it easier to work with.

# Requirements to build

You will need the Xlib header files, CMake, and a modern C++ toolchain. Using
the Ninja generator of CMake is encouraged.

# TODO list

## Cleanup

- Switch to C++
- Use C++ to simplify any constructs.
- Identify any existing libraries that can be used effectively.
- Factor code into libraries and write unittests.

## Terminal emulation

- Compare all terminal codes with xterm, iTerm2, and hterm; harmonize where
  possible.
- From the original `st` documentation
  - double-height support

## Display and rendering

- From the original `st` documentation
  - add diacritics support to xdraws()
  - make the font cache simpler
  - add better support for brightening of the upper colors

## Application

- Add configuration file reading rather than compiling all settings into the
  binary.

## Bugs and/or known isuses

- From the original `st` documentation
  - fix shift up/down (shift selection in emacs)
  - remove DEC test sequence when appropriate

# FAQ

## How do I get scrolling functionality?

Using a terminal multiplexer like `tmux` or `screen`.

# Disclaimer

This is not an official Google product.

# Credits

This is based on the `st` terminal, but doesn't share the same goals or
development philosophy and so is very likely to diverge. From that project's
documentation:

  Based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.
