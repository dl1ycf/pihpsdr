#!/bin/sh
#
# Take the capture instrument back out.
#
#   sh test/diversity/devtools/remove.sh          say what would go
#   sh test/diversity/devtools/remove.sh --do     do it
#
# Deletes the new files and every "#ifdef DIVERSITY_CAPTURE" block in the
# permanent ones, plus the "ifdef DIVCAP" block in the Makefile and the
# two .gitignore lines. Nothing else in those files is touched.
#
# Afterwards:
#
#   grep -rn 'DIVERSITY_CAPTURE\|DIVCAP\|diversity_capture' .   # empty
#   make && md5sum -c /tmp/before.md5                           # unchanged
#
# The second is the one that matters. With DIVCAP unset every object file
# is byte-identical to what it was before any of this existed, so a
# correct removal changes nothing at all.
#
set -e
cd "$(dirname "$0")/../../.."
DO=0
[ "$1" = "--do" ] && DO=1

python3 - "$DO" <<'PY'
import os, re, sys
do = sys.argv[1] == "1"

def strip_ifdef(path, token, opener, closer):
    src = open(path).read().splitlines(keepends=True)
    out, depth, removed = [], 0, 0
    for line in src:
        s = line.strip()
        if depth == 0 and s.startswith(opener) and token in s:
            depth = 1
            removed += 1
            continue
        if depth:
            if s.startswith(opener):
                depth += 1
            elif s.startswith(closer):
                depth -= 1
            continue
        out.append(line)
    if depth:
        raise SystemExit("%s: unbalanced %s" % (path, opener))
    # collapse a blank line left doubled where a block was lifted out
    text = re.sub(r"\n\n\n+", "\n\n", "".join(out))
    return text, removed

jobs = [
    ("src/diversity_auto.c", "DIVERSITY_CAPTURE", "#ifdef", "#endif"),
    ("src/diversity_menu.c", "DIVERSITY_CAPTURE", "#ifdef", "#endif"),
    ("Makefile",             "DIVCAP",            "ifdef",  "endif"),
]

for path, token, opener, closer in jobs:
    text, n = strip_ifdef(path, token, opener, closer)
    print("%-24s %d block(s)" % (path, n))
    if do:
        open(path, "w").write(text)

gi = open(".gitignore").read()
cut = ("\n# development-only diversity I/Q captures (see test/diversity/devtools)\n"
       "*.divc\nsrc/.divcap-*\ntest/diversity/devtools/build/\n")
print("%-24s %s" % (".gitignore", "4 line(s)" if cut in gi else "NOT FOUND - check by hand"))
if do and cut in gi:
    open(".gitignore", "w").write(gi.replace(cut, ""))

for f in ("src/diversity_capture.c", "src/diversity_capture.h"):
    print("%-24s %s" % (f, "delete" if os.path.exists(f) else "gone"))
    if do and os.path.exists(f):
        os.remove(f)

print("%-24s %s" % ("test/diversity/devtools", "delete"))
PY

if [ "$DO" = "1" ]; then
  rm -rf test/diversity/devtools
  echo
  echo "done. now:"
  echo "  grep -rn 'DIVERSITY_CAPTURE\\|DIVCAP\\|diversity_capture' ."
  echo "  make"
else
  echo
  echo "dry run - pass --do to actually remove"
fi
