# Ideas, Features, Roadmap

## Known Bugs

- [fixed] the xauthority device changes when x or wayland is restarted. `igloo enter` should validate the existence of the xauthority device on each enter, replacing it if necessary~

## Ideas

- On a host with no VS Code install, we'll be running code inside each igloo container. It'd be nice to share settings between them. Can't really symlink the ~/.vscode dir from the host since it won't exist. Explore having a Shared State sort of thing where directories like that live in ~/.config/igloo/shared_state and are linked in (by default? configurable?). Implement: `[shared_state]` section in config file that stores listed directories in ~/.config/igloo/shared_state and symlinks listed directories into the container, allowing all igloo instances to share these directories. This feature could be used for one-off script storage too.

- copy the igloo binary into the container, and add one or more commands intended to run inside the container. Perhaps showing, editing `shared_state` contents? definitely `igloo status` should work inside the container and show that it knows it's inside.

- modify PS1 in the container to add `[igloo]` prefix, change color of prompt, something visual as an indicator? Or... simply add a tip in the readme showing how to do this.
