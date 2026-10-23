#include "filesystem.h"

#include <ctype.h>

#include "grep_logic.h"

namespace harixos {

String currentWorkingDirectory = "/";

namespace {

bool isAbsolute(const String &path) {
  return path.startsWith("/");
}

bool ensureDirectoryChain(const String &path) {
  if (path.length() == 0 || path == "/") {
    return true;
  }

  String current = "/";
  size_t start = 1;
  while (start <= path.length()) {
    int slash = path.indexOf('/', start);
    String segment = slash == -1 ? path.substring(start) : path.substring(start, slash);
    if (segment.length() > 0) {
      if (current != "/") {
        current += '/';
      }
      current += segment;
      if (!LittleFS.exists(current)) {
        if (!LittleFS.mkdir(current)) {
          return false;
        }
      }
    }

    if (slash == -1) {
      break;
    }
    start = static_cast<size_t>(slash + 1);
  }

  return true;
}

String joinPath(const String &left, const String &right) {
  if (right.length() == 0) {
    return normalizePath(left);
  }
  if (isAbsolute(right)) {
    return normalizePath(right);
  }

  String base = normalizePath(left);
  if (!base.endsWith("/")) {
    base += '/';
  }
  base += right;
  return normalizePath(base);
}

bool fileIsDirectory(const String &path) {
  if (!LittleFS.exists(path)) {
    return false;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  return file.isDirectory();
}

}  // namespace

String normalizePath(const String &path) {
  if (path.length() == 0) {
    return String("/");
  }

  const bool absolute = path[0] == '/' || path[0] == '\\';

  // Walk the segments, resolving "." and ".." as they are seen. `segments`
  // holds the resolved path without a leading slash; absolute-ness is
  // re-applied at the end so relative inputs stay relative.
  String segments;
  segments.reserve(path.length());

  const size_t length = path.length();
  size_t index = 0;
  while (index < length) {
    while (index < length && (path[index] == '/' || path[index] == '\\')) {
      ++index;
    }

    const size_t start = index;
    while (index < length && path[index] != '/' && path[index] != '\\') {
      ++index;
    }
    if (index == start) {
      break;
    }

    const String segment = path.substring(start, index);

    if (segment == ".") {
      continue;
    }

    if (segment == "..") {
      const bool lastIsDotDot = segments == ".." || segments.endsWith("/..");
      if (lastIsDotDot) {
        segments += "/..";
      } else if (segments.length() == 0) {
        // Relative paths keep a leading ".."; absolute paths clamp at root.
        if (!absolute) {
          segments = "..";
        }
      } else {
        const int slash = segments.lastIndexOf('/');
        segments = (slash < 0) ? String("") : segments.substring(0, slash);
      }
      continue;
    }

    if (segments.length() > 0) {
      segments += '/';
    }
    segments += segment;
  }

  if (absolute) {
    if (segments.length() == 0) {
      return String("/");
    }
    String result = "/";
    result += segments;
    return result;
  }
  return segments.length() == 0 ? String(".") : segments;
}

String resolvePath(const String &cwd, const String &input) {
  if (input.length() == 0 || input == ".") {
    return normalizePath(cwd);
  }
  if (isAbsolute(input)) {
    return normalizePath(input);
  }
  return joinPath(cwd, input);
}

String parentPath(const String &path) {
  String normalized = normalizePath(path);
  if (normalized == "/") {
    return String("/");
  }

  int slash = normalized.lastIndexOf('/');
  if (slash <= 0) {
    return String("/");
  }

  return normalized.substring(0, slash);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

String fileName(const String &path) {
  String normalized = normalizePath(path);
  const char *slash = strrchr(normalized.c_str(), '/');
  if (!slash) {
    return normalized;
  }

  return String(slash + 1);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool exists(const String &path) {
  return LittleFS.exists(normalizePath(path));
}

bool isDirectory(const String &path) {
  return fileIsDirectory(normalizePath(path));
}

bool makeDirectory(const String &path) {
  String normalized = normalizePath(path);
  return ensureDirectoryChain(normalized);
}

bool touch(const String &path) {
  String normalized = normalizePath(path);
  if (LittleFS.exists(normalized)) {
    return true;
  }

  String parent = parentPath(normalized);
  if (!ensureDirectoryChain(parent)) {
    return false;
  }

  File file = LittleFS.open(normalized, "w");
  if (!file) {
    return false;
  }
  file.close();
  return true;
}

bool writeText(const String &path, const String &content, bool append) {
  String normalized = normalizePath(path);
  String parent = parentPath(normalized);
  if (!ensureDirectoryChain(parent)) {
    return false;
  }

  File file = LittleFS.open(normalized, append ? "a" : "w");
  if (!file) {
    return false;
  }

  file.print(content);
  file.close();
  return true;
}

String readText(const String &path) {
  String normalized = normalizePath(path);
  File file = LittleFS.open(normalized, "r");
  if (!file) {
    return String();
  }

  String content = file.readString();
  file.close();
  return content;
}

bool copyFile(const String &sourcePath, const String &destinationPath) {
  String source = normalizePath(sourcePath);
  String destination = normalizePath(destinationPath);

  File input = LittleFS.open(source, "r");
  if (!input) {
    return false;
  }

  String parent = parentPath(destination);
  if (!ensureDirectoryChain(parent)) {
    input.close();
    return false;
  }

  File output = LittleFS.open(destination, "w");
  if (!output) {
    input.close();
    return false;
  }

  while (input.available()) {
    output.write(input.read());
  }

  input.close();
  output.close();
  return true;
}

bool movePath(const String &sourcePath, const String &destinationPath) {
  if (!copyFile(sourcePath, destinationPath)) {
    return false;
  }
  return removePath(sourcePath);
}

bool removePath(const String &path) {
  String normalized = normalizePath(path);
  if (normalized == "/") {
    return false;
  }

  if (!LittleFS.exists(normalized)) {
    return true;
  }

  if (!fileIsDirectory(normalized)) {
    return LittleFS.remove(normalized);
  }

  Dir dir = LittleFS.openDir(normalized);
  while (dir.next()) {
    String childName = dir.fileName();
    if (childName.length() == 0) {
      continue;
    }

    String childPath;
    // dir.fileName() may return either absolute or relative names depending on platform.
    if (childName.startsWith(normalized)) {
      childPath = childName;
    } else if (childName.startsWith("/")) {
      childPath = childName;
    } else {
      // build full path under the directory we are iterating
      childPath = normalized;
      if (!childPath.endsWith("/")) childPath += '/';
      childPath += childName;
    }

    if (!removePath(childPath)) {
      return false;
    }
  }

  return LittleFS.rmdir(normalized);
}

void listDirectory(const String &path, Print &out, bool recursive, uint8_t depth) {
  String normalized = normalizePath(path);
  if (!LittleFS.exists(normalized)) {
    out.printf("Path not found: %s\r\n", normalized.c_str());
    return;
  }

  if (!fileIsDirectory(normalized)) {
    out.printf("%s\r\n", normalized.c_str());
    return;
  }

  Dir dir = LittleFS.openDir(normalized);
  while (dir.next()) {
    String entry = dir.fileName();
    File file = LittleFS.open(entry, "r");
    bool entryIsDirectory = file && file.isDirectory();
    size_t size = file ? file.size() : 0;
    if (file) {
      file.close();
    }

    for (uint8_t index = 0; index < depth; ++index) {
      out.print("  ");
    }
    out.printf("%s%s  %u bytes\r\n", entry.c_str(), entryIsDirectory ? "/" : "", static_cast<unsigned>(size));

    if (recursive && entryIsDirectory) {
      listDirectory(entry, out, true, static_cast<uint8_t>(depth + 1));
    }
  }
}

namespace {

constexpr size_t kGrepLineCap = 256;

void grepEol(Print &out) {
  // Serial console has no auto-CR; buffers (MQTT capture etc.) keep LF.
  out.print(&out == &Serial ? "\r\n" : "\n");
}

// True when the line matches under the flags; prints it (unless
// countOnly) with "prefix<line>: " in front.
bool grepEmit(const String &line, unsigned lineno, const char *pattern,
              bool icase, bool invert, bool countOnly, const char *prefix,
              Print &out) {
  if (!grep::lineMatches(line.c_str(), pattern, icase, invert)) {
    return false;
  }
  if (!countOnly) {
    out.printf("%s%u: %s", prefix, lineno, line.c_str());
    grepEol(out);
  }
  return true;
}

// Quote-aware single word: "foo bar" / 'foo bar' group, otherwise split
// on whitespace. Returns empty at end of input.
String takeGrepWord(String &text) {
  size_t i = 0;
  while (i < text.length() && isspace(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  String word;
  if (i >= text.length()) {
    return word;
  }
  if (text[i] == '"' || text[i] == '\'') {
    char quote = text[i++];
    while (i < text.length() && text[i] != quote) {
      word += text[i++];
    }
    if (i < text.length()) {
      ++i;
    }
  } else {
    while (i < text.length() &&
           !isspace(static_cast<unsigned char>(text[i]))) {
      word += text[i++];
    }
  }
  text = text.substring(i);
  return word;
}

}  // namespace

int grepFile(const String &path, const char *pattern, bool icase, bool invert,
             bool countOnly, const char *prefix, Print &out) {
  String normalized = normalizePath(path);
  File file = LittleFS.open(normalized, "r");
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }
    return -1;
  }

  unsigned lineno = 0;
  int matches = 0;
  String line;
  line.reserve(kGrepLineCap);
  while (file.available()) {
    int c = file.read();
    if (c == '\n') {
      ++lineno;
      if (grepEmit(line, lineno, pattern, icase, invert, countOnly, prefix,
                   out)) {
        ++matches;
      }
      line = "";
    } else if (c >= 0 && line.length() < kGrepLineCap) {
      line += static_cast<char>(c);
    }
  }
  if (line.length() > 0) {  // last line without trailing newline
    ++lineno;
    if (grepEmit(line, lineno, pattern, icase, invert, countOnly, prefix,
                 out)) {
      ++matches;
    }
  }
  file.close();
  return matches;
}

namespace {

// Depth-first walk over every regular file below dirPath, mirroring
// removePath's absolute/relative name handling.
void grepWalk(const String &dirPath, const char *pattern, bool icase,
              bool invert, bool countOnly, Print &out, int &total) {
  String normalized = normalizePath(dirPath);
  Dir dir = LittleFS.openDir(normalized);
  while (dir.next()) {
    String name = dir.fileName();
    if (name.length() == 0) {
      continue;
    }
    String child;
    if (name.startsWith(normalized) || name.startsWith("/")) {
      child = name;
    } else {
      child = normalized;
      if (!child.endsWith("/")) {
        child += '/';
      }
      child += name;
    }

    File probe = LittleFS.open(child, "r");
    if (!probe) {
      continue;
    }
    bool childIsDirectory = probe.isDirectory();
    probe.close();
    if (childIsDirectory) {
      grepWalk(child, pattern, icase, invert, countOnly, out, total);
      continue;
    }

    String prefix = child + ':';
    int n = grepFile(child, pattern, icase, invert, countOnly, prefix.c_str(),
                     out);
    if (n < 0) {
      continue;
    }
    total += n;
    if (countOnly) {
      out.print(child);
      out.print(": ");
      out.printf("%d", n);
      grepEol(out);
    }
  }
}

}  // namespace

const char *runGrep(const String &args, const String &cwd, Print &out) {
  bool icase = false;
  bool invert = false;
  bool countOnly = false;
  bool recursive = false;
  bool havePattern = false;
  bool havePath = false;
  String pattern;
  String path;

  String text = args;
  while (text.length() > 0) {
    String word = takeGrepWord(text);
    if (word.length() == 0) {
      break;
    }
    if (word[0] == '-') {
      if (word == "-i") {
        icase = true;
      } else if (word == "-c") {
        countOnly = true;
      } else if (word == "-v") {
        invert = true;
      } else if (word == "-r") {
        recursive = true;
      } else {
        return "grep: unknown flag (use -i -c -v -r)";
      }
    } else if (!havePattern) {
      pattern = word;
      havePattern = true;
    } else if (!havePath) {
      path = word;
      havePath = true;
    } else {
      return "Usage: grep [-i] [-c] [-v] [-r] <pattern> <path>";
    }
  }
  if (!havePattern || !havePath) {
    return "Usage: grep [-i] [-c] [-v] [-r] <pattern> <path>";
  }

  String full = resolvePath(cwd, path);
  int total = 0;

  if (isDirectory(full)) {
    if (!recursive) {
      return "grep: path is a directory (use -r)";
    }
    grepWalk(full, pattern.c_str(), icase, invert, countOnly, out, total);
    if (total == 0 && !countOnly) {
      out.print("grep: no matches");
      grepEol(out);
    }
    return nullptr;
  }

  if (!exists(full)) {
    return "grep: path not found";
  }
  int n = grepFile(full, pattern.c_str(), icase, invert, countOnly, "", out);
  if (n < 0) {
    return "grep: file not found";
  }
  if (countOnly) {
    out.printf("%d", n);
    grepEol(out);
  } else if (n == 0) {
    out.print("grep: no matches");
    grepEol(out);
  }
  return nullptr;
}

}  // namespace harixos
