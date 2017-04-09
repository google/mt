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

- Cleanup the code with clang-format.
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

## Why doesn't the Del key work in some programs?

This answer comes from the `st` documentation.

Taken from the terminfo manpage:

	If the terminal has a keypad that transmits codes when the keys
	are pressed, this information can be given. Note that it is not
	possible to handle terminals where the keypad only works in
	local (this applies, for example, to the unshifted HP 2621 keys).
	If the keypad can be set to transmit or not transmit, give these
	codes as smkx and rmkx. Otherwise the keypad is assumed to
	always transmit.

In the st case smkx=E[?1hE= and rmkx=E[?1lE>, so it is mandatory that
applications which want to test against keypad keys send these
sequences.

But buggy applications (like bash and irssi, for example) don't do this. A fast
solution for them is to use the following command:

	$ printf '\033[?1h\033=' >/dev/tty

or
	$ tput smkx

In the case of bash, readline is used. Readline has a different note in its
manpage about this issue:

	enable-keypad (Off)
		When set to On, readline will try to enable the
		application keypad when it is called. Some systems
		need this to enable arrow keys.

Adding this option to your .inputrc will fix the keypad problem for all
applications using readline.

If you are using zsh, then read the zsh FAQ
<http://zsh.sourceforge.net/FAQ/zshfaq03.html#l25>:

	It should be noted that the O / [ confusion can occur with other keys
	such as Home and End. Some systems let you query the key sequences
	sent by these keys from the system's terminal database, terminfo.
	Unfortunately, the key sequences given there typically apply to the
	mode that is not the one zsh uses by default (it's the "application"
	mode rather than the "raw" mode). Explaining the use of terminfo is
	outside of the scope of this FAQ, but if you wish to use the key
	sequences given there you can tell the line editor to turn on
	"application" mode when it starts and turn it off when it stops:

		function zle-line-init () { echoti smkx }
		function zle-line-finish () { echoti rmkx }
		zle -N zle-line-init
		zle -N zle-line-finish

Putting these lines into your .zshrc will fix the problems.

# Credits

This is based on the `st` terminal, but doesn't share the same goals or
development philosophy and so is very likely to diverge. From that project's
documentation:
  
	Based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.
