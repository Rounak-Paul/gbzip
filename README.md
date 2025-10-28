# gbzip

A ZIP utility with gitignore-style patterns and real-time progress reporting.

## Disclaimer

**This software is provided "as is" without warranty of any kind, express or implied. The author takes no responsibility for any damages, data loss, or other issues that may arise from using this codebase or application. Use at your own risk.**

### Security Notice
- Only extract archives from trusted sources
- Be cautious with archives from unknown origins
- This tool does not include advanced security protections against malicious archives
- Review extracted contents before executing any files

## Installation

```bash
git clone https://github.com/yourusername/gbzip.git
cd gbzip
mkdir build && cd build
cmake .. && make
```

Requires CMake and libzip. On macOS: `brew install libzip`. On Ubuntu/Debian: `apt install libzip-dev`.

## MacOS users (using the pre-built)

macOS may show: "Apple cannot check it for malicious software."  
This is Gatekeeper quarantining downloads. (If still in doubt, just read the codebase and build it from source.) To run the CLI after downloading:

1. Extract the archive (double-click or `tar -xzf ...`).
2. Make the binary executable and remove quarantine, then run:
```bash
chmod +x ./gbzip
xattr -d com.apple.quarantine ./gbzip
./gbzip -h
```
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

## Security Features

Basic protections against common archive vulnerabilities:

- **Path traversal protection**: Rejects paths containing ".." or absolute paths
- **File size limits**: Warns when total extraction would exceed 50GB (use `-f` to override)
- **File count limits**: Warns when archive contains >100,000 files (use `-f` to override) 
- **Compression ratio warnings**: Alerts on suspiciously high compression ratios
- **Dangerous file detection**: Warns when extracting potentially executable files
- **Safe extraction**: Only extracts to specified target directory

**Important**: These are basic protections. Only extract archives from trusted sources.

## Options

- `-v` verbose output with progress
- `-q` quiet operation
- `-s` structured JSON output
- `-D` differential update
- `-I <file>` custom ignore patterns
- `-x` extract mode
- `-l` list contents
- `-f` force overwrite / bypass security limits
- `-0` to `-9` compression level