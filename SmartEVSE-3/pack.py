# Shamelessly translated mongsoose 7.13 pack.c to python to provide portability for platformio core software

# perhaps Copyright (c) Cesanta Software Limited, I'm not sure because I'm not a lawyer
# might also be Copyright me or Copyright ChatGPT
# All rights reserved.
# but not sure by whom

# This program is used to pack arbitrary data into a C binary. It takes
# a list of files as an input, and produces a .c data file that contains
# contents of all these files as a collection of byte arrays.
#
# Usage:
#   2. Convert list of files into single .c:
#      ./pack file1.data file2.data > fs.c
#
#   3. In your application code, you can access files using this function:
#      const char *mg_unpack(const char *file_name, size_t *size);
#
#   4. Build your app with fs.c:
#      cc -o my_app my_app.c fs.c

import errno
import sys
import os
import stat
import time

code = """static int scmp(const char *a, const char *b) {
  while (*a && (*a == *b)) a++, b++;
  return *(const unsigned char *) a - *(const unsigned char *) b;
}
const char *mg_unlist(size_t no) {
  return packed_files[no].name;
}
const char *mg_unpack(const char *name, size_t *size, time_t *mtime) {
  const struct packed_file *p;
  for (p = packed_files; p->name != NULL; p++) {
    if (scmp(p->name, name) != 0) continue;
    if (size != NULL) *size = p->size - 1;
    if (mtime != NULL) *mtime = p->mtime;
    return (const char *) p->data;
  }
  return NULL;
}
"""

def main(argv):
    i = 0
    strip_prefix = ""

    print("#include <stddef.h>")
    print("#include <string.h>")
    print("#include <time.h>")
    print("")
    print("#if defined(__cplusplus)\nextern \"C\" {\n#endif")
    print("const char *mg_unlist(size_t no);")
    print("const char *mg_unpack(const char *, size_t *, time_t *);")
    print("#if defined(__cplusplus)\n}\n#endif\n\n", end='')

    while i < len(argv):
        if argv[i] == "-s":
            strip_prefix = argv[i + 1]
            i += 2
        elif argv[i] == "-h" or argv[i] == "--help":
            sys.stderr.write("Usage: %s[-s STRIP_PREFIX] files...\n" % argv[0])
            sys.exit(os.EX_USAGE)
        else:
            ascii = [''] * 12
            with open(argv[i], "rb") as fp:
                print("static const unsigned char v%d[] = {" % (i + 1))
                j = 0
                while True:
                    ch = fp.read(1)
                    if not ch:
                        break
                    ch = ord(ch)
                    if j == len(ascii):
                        print(" // %s" % ''.join(ascii))
                        j = 0
                    ascii[j] = chr(ch) if ch >= 32 and ch <= 126 and ch != 92 else '.'
                    print(" %3u," % ch, end='')
                    j += 1
                print(" 0 // %s\n};" % ''.join(ascii))

        i += 1

    print("")
    print("static const struct packed_file {")
    print("  const char *name;")
    print("  const unsigned char *data;")
    print("  size_t size;")
    print("  time_t mtime;")
    print("} packed_files[] = {")

    i = 0
    while i < len(argv):
        if argv[i] == "-s":
            i += 1
            continue
        st = os.stat(argv[i])
        name = argv[i]
        n = len(strip_prefix)
        if argv[i] == "-s":
            i += 1
            continue
        if name.startswith(strip_prefix):
            name = name[n:]
        print("  {\"/%s\", v%d, sizeof(v%d), %lu}," % (name, i + 1, i + 1, st.st_mtime))
        i += 1

    print("  {NULL, NULL, 0, 0}")
    print("};\n")
    print(code, end='')

if __name__ == "__main__":
    main(sys.argv[1:])

