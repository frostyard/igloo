# Security Policy

## Supported versions

Only the latest release gets fixes: the latest `v*` tag, which the
Frostyard Flatpak remote serves as `latest`. Update with `flatpak update`.
Older releases and builds from pull requests get no fixes.

Igloo is based on Ptyxis. A vulnerability that is also in Ptyxis upstream
goes to the [GNOME security team](https://security.gnome.org/) as well; Igloo
takes the upstream fix.

## Reporting a vulnerability

Use GitHub private vulnerability reporting:
https://github.com/frostyard/igloo/security

Open the Security tab and click **Report a vulnerability**.

Do not open a public issue with the details of a vulnerability. Do not post
credentials in an issue, a pull request or a discussion.

## What to include

- The affected release or commit.
- The impact: what an attacker can do.
- The steps to reproduce it.
