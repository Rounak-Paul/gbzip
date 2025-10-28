# gbzip

A ZIP utility with gitignore-style patterns and real-time progress reporting.

## Installation

```bash
git clone https://github.com/yourusername/gbzip.git
cd gbzip
mkdir build && cd build
cmake .. && make
```

Requires CMake and libzip. On macOS: `brew install libzip`. On Ubuntu/Debian: `apt install libzip-dev`.

## Basic Usage

Create archives:
```bash
gbzip archive.zip file1.txt file2.txt
gbzip archive.zip directory/
```

Extract archives:
```bash
gbzip -x archive.zip
gbzip -x -d destination/ archive.zip
```

List contents:
```bash
gbzip -l archive.zip
```

## Ignore Patterns

Create a `.zipignore` file to exclude files during archiving:

```
build/
*.tmp
.git/
node_modules/
venv/
!important.log
```

Uses gitignore syntax. Patterns are processed during directory traversal for efficiency.

## Progress Reporting

For large files, use verbose mode to see real-time progress:
```bash
gbzip -v dataset.zip large_directory/
```

Shows file processing progress (0-2%) followed by compression progress (2-100%) with transfer speeds.

## Advanced Features

Differential updates (only process changed files):
```bash
gbzip -D backup.zip ~/Documents
```

Custom ignore file:
```bash
gbzip -I custom.ignore archive.zip .
```

Compression levels:
```bash
gbzip -0 fast.zip files/     # no compression
gbzip -9 small.zip files/    # maximum compression
```

Machine-readable output for GUI applications:
```bash
gbzip -s archive.zip files/
```

Outputs JSON events for progress tracking and status updates.

## Options

- `-v` verbose output with progress
- `-q` quiet operation
- `-s` structured JSON output
- `-D` differential update
- `-I <file>` custom ignore patterns
- `-x` extract mode
- `-l` list contents
- `-f` force overwrite
- `-0` to `-9` compression level