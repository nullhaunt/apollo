# Apollo source readability

- Format project-owned C++ files with the repository's `.clang-format`. Use a formatter version that accepts the file; the installed clang-format 22 does, while the Visual Studio clang-format 19 does not.
- Write control flow as full blocks. Keep validation, allocation, SDK calls, rendering, and cleanup in separate paragraphs with blank lines between them.
- Split a function when its stages become hard to scan. Do not rely on the formatter to make a dense function readable.
- Review the whole touched function after formatting, then build both x64 and NX64 configurations affected by the change.
