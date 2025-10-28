# Security Policy

## Disclaimer

**This software is provided "AS IS" without warranty of any kind. Users assume all risks associated with its use.**

## Implemented Security Features

This tool includes basic security protections:

- **Path traversal protection**: Blocks extraction of files with ".." or absolute paths
- **File size limits**: Warns and stops extraction when total size would exceed 50GB
- **File count limits**: Warns and stops extraction of archives with >100,000 files
- **Compression ratio detection**: Warns about potential ZIP bombs (high compression ratios)
- **Dangerous file warnings**: Alerts when extracting potentially executable files
- **Force override**: Use `-f` flag to bypass size/count limits when needed

## Remaining Limitations

These protections are basic and do not include enterprise-level security:

- No complete ZIP bomb protection (only warnings)
- No sandboxing or isolation mechanisms
- No advanced malware detection
- No memory usage limits during processing

## Safe Usage Recommendations

1. **Only use with trusted sources**: Don't extract archives from unknown or untrusted sources
2. **Review before execution**: Always review extracted contents before running any executables
3. **Use in controlled environments**: Consider running in isolated environments for untrusted content
4. **Monitor disk space**: Be aware that compressed archives can expand to consume significant disk space
5. **Backup important data**: Ensure backups exist before processing important files

## Reporting Issues

This is a community project. While we appreciate reports of potential security issues, we cannot guarantee fixes or response times. Users are encouraged to:

1. Review the source code themselves
2. Implement additional security measures as needed
3. Use alternative tools for high-security environments

## No Security Guarantees

The maintainer(s) of this project:
- Make no claims about the security of this software
- Provide no guarantees against data loss or system compromise
- Are not responsible for any damages resulting from use of this software
- May not respond to or fix reported security issues

**Use this software only if you accept these limitations and risks.**