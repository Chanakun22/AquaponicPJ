---
name: Project Documentation Standards
description: Rules for maintaining project documentation, specifically CHANGELOG.md.
---

# 📝 Project Documentation Standards

This skill enforces the consistent updating of project documentation, ensuring that `CHANGELOG.md` is always up-to-date with the latest features and fixes.

## 1. Automatic Changelog Update

**Rule:** AFTER completing any significant task (Feature Implementation, Bug Fix, Refactoring), you **MUST** update `CHANGELOG.md` in the project root.

### Format

Append the new changes under a new header with the current date, OR add to the existing date's section if it already exists.

```markdown
## [YYYY-MM-DD] - Task Name/Summary

### Added

- List of new features...

### Changed

- List of modifications...

### Fixed

- List of bug fixes...
```

## 2. Commit Message Style

When describing changes in the Changelog, use the "Commit Message" style:

- **feat:** New feature
- **fix:** Bug fix
- **docs:** Documentation only
- **style:** Formatting, missing semi-colons, etc
- **refactor:** Refactoring production code
- **test:** Adding tests

## 3. Persistent Memory

If you are starting a new session, **ALWAYS** read `CHANGELOG.md` first to understand the project's recent history and current state.
