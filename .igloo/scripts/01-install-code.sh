#!/bin/bash
# Install Visual Studio Code on Debian Trixie
# This script adds the Microsoft repository and installs VS Code

set -e

echo "Installing Visual Studio Code..."

# Install dependencies
apt-get update
apt-get install -y wget gpg apt-transport-https

# Add Microsoft GPG key
wget -qO- https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > /usr/share/keyrings/packages.microsoft.gpg

# Add VS Code repository
echo "deb [arch=amd64,arm64,armhf signed-by=/usr/share/keyrings/packages.microsoft.gpg] https://packages.microsoft.com/repos/code stable main" > /etc/apt/sources.list.d/vscode.list

# Update and install VS Code
apt-get update
apt-get install -y code

echo "Visual Studio Code installed successfully!"
