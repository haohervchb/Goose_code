#!/bin/bash
set -e

echo "=== goosecode installer ==="

# Detect OS
OS="$(uname -s)"
DISTRO=""
if [ -f /etc/os-release ]; then
    DISTRO="$(. /etc/os-release && echo "$ID")"
fi

echo "Detected: $OS ($DISTRO)"

# Install dependencies
if [ "$OS" = "Linux" ]; then
    if [ "$DISTRO" = "debian" ] || [ "$DISTRO" = "ubuntu" ] || [ "$DISTRO" = "linuxmint" ]; then
        echo "Installing dependencies (Debian/Ubuntu)..."
        sudo apt update
        sudo apt install -y build-essential libcurl4-openssl-dev golang-go
    elif [ "$DISTRO" = "fedora" ]; then
        echo "Installing dependencies (Fedora)..."
        sudo dnf install -y gcc make libcurl-devel golang
    elif [ "$DISTRO" = "arch" ] || [ "$DISTRO" = "manjaro" ]; then
        echo "Installing dependencies (Arch/Manjaro)..."
        sudo pacman -Sy --noconfirm base-devel curl go
    elif [ "$DISTRO" = "opensuse" ] || [ "$DISTRO" = "suse" ]; then
        echo "Installing dependencies (openSUSE)..."
        sudo zypper install -y gcc make patterns-devel-base-devel_basis libcurl-devel go
    else
        echo "Please install manually: gcc, make, libcurl-dev, golang"
        echo "Then run: make"
        exit 1
    fi
elif [ "$OS" = "Darwin" ]; then
    if command -v brew >/dev/null 2>&1; then
        echo "Installing dependencies (macOS via Homebrew)..."
        brew install curl gcc go
    else
        echo "Please install Homebrew first, then run this script again"
        exit 1
    fi
else
    echo "Unsupported OS: $OS"
    exit 1
fi

# Build
echo "Building..."
cd "$(dirname "$0")"
make clean
make

# Install
if [ "$1" = "--install" ]; then
    echo "Installing to ~/.local/bin..."
    make install

    # Add to PATH automatically in ~/.bashrc
    PROFILE_SNIPPET='export PATH="$HOME/.local/bin:$PATH"'
    BASHRC="$HOME/.bashrc"
    ZSHRC="$HOME/.zshrc"

    add_to_path() {
        local rcfile="$1"
        if [ -f "$rcfile" ] && ! grep -q "export PATH=.*\.local/bin" "$rcfile" 2>/dev/null; then
            echo "" >> "$rcfile"
            echo "# goosecode" >> "$rcfile"
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$rcfile"
            echo "Added PATH update to $rcfile"
        elif [ ! -f "$rcfile" ]; then
            echo "# goosecode" >> "$rcfile"
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$rcfile"
            echo "Created $rcfile with PATH update"
        fi
    }

    if [ -f "$BASHRC" ] || [ ! -f "$ZSHRC" ]; then
        add_to_path "$BASHRC"
    fi
    if [ -f "$ZSHRC" ]; then
        add_to_path "$ZSHRC"
    fi

    echo ""
    echo "=== IMPORTANT: Restart your terminal or run this to refresh PATH ==="
    echo 'export PATH="$HOME/.local/bin:$PATH"'
    echo ""
    echo "Done! After restarting terminal, run 'goosecode' to start."
else
    echo ""
    echo "Build complete!"
    echo "Run './goosecode' to start, or './install.sh --install' to install globally"
fi