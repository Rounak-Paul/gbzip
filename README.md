# gbzip

Fast ZIP utility with gitignore-style patterns and real-time progress.

## Install

```bash
git clone https://github.com/yourusername/gbzip.git
cd gbzip && mkdir build && cd build
cmake .. && make
```

**Dependencies:** CMake, libzip (`brew install libzip` or `apt install libzip-dev`)

## Use Cases

### 📁 Project Archives
```bash
# Exclude build files automatically
echo -e "build/\n*.tmp\n.git/\nnode_modules/" > .zipignore
gbzip project.zip .
```

### 🚀 Large File Processing
```bash
# Real-time progress for big datasets
gbzip -v dataset.zip data/
# Shows: Creating (2%) → Compressing (2-100%) with speed metrics
```

### 🔄 Incremental Backups
```bash
gbzip -D backup.zip ~/Documents  # Only changed files
```

### 🎯 UI Integration
```bash
gbzip -s archive.zip files/  # JSON output for GUIs
# {"event":"PROGRESS","percent":45.2,"speed":30.7,"speed_units":"MB/s"...}
```

### ⚡ Quick Operations
```bash
gbzip archive.zip file1 file2    # Create
gbzip -x archive.zip             # Extract  
gbzip -l archive.zip             # List
```

## Key Options

- `-v` verbose with progress bar
- `-s` structured JSON output for UIs
- `-D` differential (changed files only)
- `-I file` custom ignore patterns
- `-0/-9` compression level
- `-x` extract, `-l` list

## Ignore Patterns (`.zipignore`)

```
build/           # directories
*.tmp            # extensions  
node_modules/    # common excludes
venv/
.git/
!important.log   # exceptions
```

Works like `.gitignore` - patterns are processed efficiently during traversal.