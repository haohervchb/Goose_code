#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Detect OS
OS="$(uname -s)"
DISTRO=""
if [ -f /etc/os-release ]; then
    DISTRO="$(. /etc/os-release && echo "$ID")"
fi

# Handle --uninstall flag
if [ "$1" = "--uninstall" ] || [ "$1" = "-u" ]; then
    echo "=== goosecode uninstaller ==="
    echo ""

    # Remove binaries
    if [ -d "$HOME/.local/bin" ]; then
        rm -f "$HOME/.local/bin/goosecode"
        rm -f "$HOME/.local/bin/goosecode-tui"
        rm -f "$HOME/.local/bin/goosecode-backend"
        echo "Removed binaries from ~/.local/bin/"
    fi

    # Remove PATH lines from bashrc/zshrc
    for rcfile in "$HOME/.bashrc" "$HOME/.zshrc"; do
        if [ -f "$rcfile" ]; then
            # Remove goosecode PATH additions
            temp=$(mktemp)
            keeping=false
            in_goose_section=false
            while IFS= read -r line; do
                if [ "$line" = "# goosecode" ]; then
                    in_goose_section=true
                    continue
                fi
                if $in_goose_section && [[ "$line" == *"export PATH="*".local/bin"* ]]; then
                    continue
                fi
                if $in_goose_section && [[ "$line" != "" ]]; then
                    in_goose_section=false
                fi
                if ! $in_goose_section; then
                    echo "$line" >> "$temp"
                fi
            done < "$rcfile"
            mv "$temp" "$rcfile"
            echo "Cleaned PATH from $rcfile"
        fi
    done

    echo ""
    echo "Uninstall complete! Restart your terminal to complete PATH cleanup."
    exit 0
fi

echo "=== goosecode installer ==="
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
cd "$SCRIPT_DIR"
make clean
make

# Install (default behavior)
echo "Installing to ~/.local/bin..."
make install

# Add to PATH automatically in ~/.bashrc
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
echo "Done! goosecode is now installed."
echo ""

# Apply PATH to current session
export PATH="$HOME/.local/bin:$PATH"
echo "PATH updated for this session. Run 'goosecode' to start."
echo ""
echo "To uninstall: ./install.sh --uninstall"