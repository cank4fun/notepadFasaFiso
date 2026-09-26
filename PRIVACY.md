# Privacy

## No telemetry

notepadFasaFiso does not send usage analytics, telemetry, document contents,
file names, file paths, settings, session data, or crash reports to the project
operator or to a project-controlled service.

There is no built-in analytics service, cloud synchronization service, update
checker, advertising SDK, or automatic diagnostic upload path in the release.

## Local diagnostics

Diagnostics are failure-only and local. A caught application exception may
write a small local text record. On Windows, a fatal crash may also produce the
existing local minidump and crash text. On Linux, a fatal signal may produce a
local crash text record. These files are not uploaded automatically.

## Local application data

Settings, session state, recovery data, recent-file metadata, workspace state,
and optional local file-search indexes are stored in the normal per-user
platform locations. File-search/indexing features operate on roots selected by
the user and remain local to the machine.

## Links and external applications

When the user explicitly opens a web or email link, notepadFasaFiso hands that
link to the operating system's configured external handler. Any network access
that follows is performed by that external application under its own privacy
policy, not by a notepadFasaFiso network client.

## Scope

This document describes the official Windows portable and Linux AppImage
release builds produced by the repository's release tooling. A modified or
repackaged build may behave differently.
