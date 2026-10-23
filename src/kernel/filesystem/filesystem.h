#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

namespace harixos {

// Shared shell + script working directory. Lives here rather than in
// main.cpp's anonymous namespace so the script engine can resolve relative
// paths against the same value the shell does.
extern String currentWorkingDirectory;

String normalizePath(const String &path);
String resolvePath(const String &cwd, const String &input);
String parentPath(const String &path);
String fileName(const String &path);
bool makeDirectory(const String &path);
bool touch(const String &path);
bool exists(const String &path);
bool isDirectory(const String &path);
bool writeText(const String &path, const String &content, bool append = false);
String readText(const String &path);
bool copyFile(const String &sourcePath, const String &destinationPath);
bool movePath(const String &sourcePath, const String &destinationPath);
bool removePath(const String &path);
void listDirectory(const String &path, Print &out, bool recursive = false, uint8_t depth = 0);

// grep: literal substring search (never regex; matching core in
// grep_logic.h). Returns match count, or -1 when path is not a readable
// file. Prints "prefix<line>: <text>" per match unless countOnly.
// Lines over 256 chars are truncated for matching.
int grepFile(const String &path, const char *pattern, bool icase, bool invert,
             bool countOnly, const char *prefix, Print &out);

// Shared shell + script entry. Parses "[-i] [-c] [-v] [-r] <pattern> <path>"
// (quoted words may contain spaces), runs a single-file or recursive
// search, prints results to out. Returns nullptr on success, else a
// static error message.
const char *runGrep(const String &args, const String &cwd, Print &out);

}  // namespace harixos
