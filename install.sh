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
    mkdir -p "$HOME/.local/bin"
    install -m 755 goosecode-tui "$HOME/.local/bin/"
    install -m 755 goosecode-backend "$HOME/.local/bin/"
    ln -sf "$HOME/.local/bin/goosecode-tui" "$HOME/.local/bin/goosecode"
    ln -sf "$HOME/.local/bin/goosecode-backend" "$HOME/.local/bin/goosecode-backend"

    # Add to PATH if not already there
    if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
        echo ""
        echo "=== Add this to your ~/.bashrc or ~/.zshrc ==="
        echo 'export PATH="$HOME/.local/bin:$PATH"'
    fi
    echo "Done! Run 'goosecode' to start."
else
    echo ""
    echo "Build complete!"
    echo "Run './goosecode' to start, or './goosecode --install' to install to ~/.local/bin"
fi