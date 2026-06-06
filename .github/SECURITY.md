# Security Policy

## Reporting a Vulnerability

Use GitHub's Security Advisories feature to report security concerns
privately.

Expect a response within 7 days. If the issue is confirmed, a fix
will be released as a patch version.

## Scope

pool-allocator is a small memory pool allocator library with no network
stack, no external dependencies, and no dynamic memory allocation. The
primary attack surface is integer overflow in slot/size calculations,
use-after-free in returned slots, and concurrency races in the
single-writer/many-readers contract.
