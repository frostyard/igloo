#!/bin/sh
set -e
rm -rf completions
mkdir completions
go build -o igloo .
for sh in bash zsh fish; do
  ./igloo completion "$sh" >"completions/igloo.$sh"
done