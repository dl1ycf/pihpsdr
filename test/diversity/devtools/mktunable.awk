#
# Generate a sweepable copy of src/rade_correlator.c.
#
#   awk -v manifest=tunable.manifest -f mktunable.awk \
#       ../../../src/rade_correlator.c > build/rade_correlator_tunable.c
#
# The constants named in the manifest have their definitions removed and
# are supplied instead by rade_tuning.h, which redefines each one as a
# field of a struct. Every *use* site in rade_correlator.c is left exactly
# as it is - which is the whole reason for doing it this way rather than
# editing the file: the correlator is the one source file the diversity
# work must not leave marks on.
#
# Fails, loudly and with a non-zero status, if any name in the manifest is
# not found. A constant that gets renamed upstream then stops the build
# instead of quietly dropping out of the sweep.
#
BEGIN {
  if (manifest == "") { print "mktunable: -v manifest=... required" > "/dev/stderr"; exit 1 }

  while ((getline line < manifest) > 0) {
    sub(/#.*/, "", line)
    n = split(line, f, /[ \t]+/)
    kind = ""; name = ""
    for (i = 1; i <= n; i++) {
      if (f[i] == "") { continue }
      if (kind == "") { kind = f[i] } else { name = f[i]; break }
    }
    if (kind == "" || name == "") { continue }
    if (kind != "define" && kind != "array") {
      printf "mktunable: bad manifest entry \"%s %s\"\n", kind, name > "/dev/stderr"
      exit 1
    }
    want[name] = kind
    order[++nwant] = name
  }
  close(manifest)

  print "/*"
  print " * GENERATED - do not edit, and do not commit."
  print " *"
  print " * src/rade_correlator.c with the constants named in"
  print " * tunable.manifest lifted into the rade_tuning struct. See"
  print " * mktunable.awk and README.md."
  print " */"
  print "#include \"rade_tuning.h\""
}

{
  for (i = 1; i <= nwant; i++) {
    name = order[i]
    if (name in seen) { continue }

    if (want[name] == "define" && $0 ~ ("^#define[ \t]+" name "[ \t]")) {
      seen[name] = 1
      printf "/* lifted: %s */\n", name
      next
    }

    if (want[name] == "array" && $0 ~ ("^static[ \t]+const[ \t].*[ \t]" name "\\[")) {
      seen[name] = 1
      printf "/* lifted: %s */\n", name
      next
    }
  }
  print
}

END {
  rc = 0
  for (i = 1; i <= nwant; i++) {
    if (!(order[i] in seen)) {
      printf "mktunable: %s not found in the correlator - renamed or removed?\n", \
             order[i] > "/dev/stderr"
      rc = 1
    }
  }
  exit rc
}
