# Scinterm - Scintilla for NotCurses

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![NotCurses](https://img.shields.io/badge/NotCurses-3.0+-brightgreen.svg)](https://github.com/dankamongmen/notcurses)
[![Build](https://github.com/yourusername/scinterm/actions/workflows/build.yml/badge.svg)](https://github.com/yourusername/scinterm/actions/workflows/build.yml)

Scinterm is a port of the Scintilla editing component to the NotCurses terminal environment. It provides a full-featured text editor widget with syntax highlighting, multiple cursors, auto-completion, and true color support.

## Features

- ✨ **Full Scintilla editing capabilities** - All the power of Scintilla in the terminal
- 🎨 **True color support** - 24-bit RGB colors for syntax highlighting and UI
- 🌐 **Unicode and UTF-8 support** - Full international character support
- 📝 **Syntax highlighting** - Optional lexers for many programming languages
- 🔍 **Auto-completion** - Intelligent code completion with images
- 📑 **Multiple cursors and selections** - Modern editing features
- 🖱️ **Mouse support** - Click, drag, and scroll with mouse
- 📦 **Shared and static libraries** - Flexible integration options

## Building

### Dependencies

- NotCurses 3.0 or higher
- CMake 3.10 or higher
- C++17 compatible compiler
- C11 compatible compiler

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/scinterm.git
cd scinterm

# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Install (optional)
sudo make install
