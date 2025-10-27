# gbzip

A fast, cross-platform ZIP utility with advanced ignore patterns and differential updates.

## Features

- **Smart Ignore Patterns**: `.zipignore` files work like `.gitignore` to exclude unwanted files
- **Differential Updates**: Only process changed files for faster incremental backups
- **Cross-Platform**: Native support for Windows, macOS, and Linux
- **Fast Performance**: Optimized for large archives and directory trees
- **Standard Compatible**: Drop-in replacement for standard zip tools

## Quick Start

```bash
# Build from source
git clone https://github.com/yourusername/gbzip.git
cd gbzip
mkdir build && cd build
cmake .. && make

# Create archive
./gbzip archive.zip project/

# Extract archive
./gbzip -x archive.zip

# Use ignore patterns
echo "*.tmp" > .zipignore
echo "build/" >> .zipignore
./gbzip archive.zip .
```

## Installation

### Dependencies
- CMake 3.10+
- C compiler (GCC/Clang/MSVC)
- libzip

### Build Instructions

**Linux/macOS:**
```bash
# Install dependencies
sudo apt install cmake gcc libzip-dev  # Ubuntu/Debian
brew install cmake libzip              # macOS

# Build
mkdir build && cd build
cmake ..
make
sudo make install
```

**Windows:**
```cmd
# Using vcpkg
vcpkg install libzip
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Usage

### Basic Operations

```bash
# Create archive
gbzip archive.zip file1.txt file2.txt
gbzip -r archive.zip directory/

# Extract archive
gbzip -x archive.zip
gbzip -x -d destination/ archive.zip

# List contents
gbzip -l archive.zip

# Test integrity
gbzip -t archive.zip
```

### Ignore Patterns

Create a `.zipignore` file to exclude files:

```gitignore
# Ignore build artifacts
build/
dist/
*.o
*.tmp

# Ignore version control
.git/
.svn/

# Ignore dependencies
node_modules/
venv/
target/

# Allow specific files
!important.log
```

### Advanced Features

```bash
# Use custom ignore file
gbzip -I custom.zipignore archive.zip .

# Differential update (only changed files)
gbzip -D archive.zip project/

# Compression levels
gbzip -0 fast.zip files/     # No compression (fast)
gbzip -9 small.zip files/    # Maximum compression

# Verbose output
gbzip -v archive.zip project/
```

## Command Reference

| Option | Description |
|--------|-------------|
| `-r` | Recurse into directories |
| `-x` | Extract files |
| `-l` | List archive contents |
| `-t` | Test archive integrity |
| `-v` | Verbose output |
| `-q` | Quiet operation |
| `-f` | Force overwrite |
| `-u` | Update changed files only |
| `-D` | Differential update |
| `-I <file>` | Use custom zipignore file |
| `-Z` | Create default .zipignore |
| `-0` to `-9` | Compression level |
| `-d <dir>` | Extract to directory |

## Ignore Patterns

Patterns follow `.gitignore` syntax:

- `*.ext` - Match files by extension
- `dir/` - Match directories
- `**/logs/**` - Match in any subdirectory
- `!exception` - Negate pattern (include file)
- `#comment` - Comments

## Performance

gbzip is optimized for real-world usage:

- **Fast startup**: Minimal initialization overhead
- **Efficient traversal**: Skips ignored directories entirely
- **Smart updates**: Only processes changed files with `-D`
- **Memory efficient**: Streams large files without loading into memory

## Examples

### Project Backup
```bash
# Setup ignore patterns
cat > .zipignore << EOF
build/
.git/
*.tmp
node_modules/
EOF

# Create backup
gbzip project-backup.zip .
```

### Incremental Backups
```bash
# Initial backup
gbzip backup-$(date +%Y%m%d).zip ~/Documents

# Daily incremental (only changed files)
gbzip -D backup-$(date +%Y%m%d).zip ~/Documents
```

### Extract with Progress
```bash
# Extract large archive with progress
gbzip -xv archive.zip
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if needed
5. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) for details.

---

**Note**: When using wildcards like `*`, use `gbzip archive.zip .` instead of `gbzip archive.zip *` to ensure ignore patterns work correctly.