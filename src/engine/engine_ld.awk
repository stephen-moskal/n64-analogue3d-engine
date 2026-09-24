# Builds build/<variant>/engine.ld from libdragon's n64.ld (see the Makefile):
# inserts the engine's placement fragments at fixed anchors and fails when an
# anchor is not found exactly once (a libdragon upgrade could move them).
#
#   awk -v hot_text=src/engine/hot_text.ld -v hot_data=src/engine/hot_data.ld \
#       -f src/engine/engine_ld.awk n64.ld > engine.ld
#
#   hot_text.ld             after the boot code in .text         (code, D25)
#   hot_data.ld rodata_pad  before ".rodata : {"                  (data, D34)
#               rodata      after  ".rodata : {"
#               sbss_pad    before ".sbss : {"
#               sbss        after  "__bss_start = .;"
#               bss_pre     before ".bss : {"
#               bss         after  ".bss : {"

# Print one part of a fragment file: the lines after "/* ==== <tag> ==== */"
# up to the next such marker (tag "" prints a file without markers whole)
function insert(file, tag,   line, on) {
    on = (tag == "")
    while ((getline line < file) > 0) {
        if (line ~ /^\/\* ==== [a-z_]+ ==== \*\/$/) {
            on = (line == "/* ==== " tag " ==== */")
            continue
        }
        if (on) print line
    }
    close(file)
}

/^[ \t]*\.rodata[ \t]*:[ \t]*\{[ \t]*$/ { insert(hot_data, "rodata_pad"); print; insert(hot_data, "rodata"); ro++; next }
/^[ \t]*\.sbss[ \t]*:[ \t]*\{[ \t]*$/   { insert(hot_data, "sbss_pad"); print; sp++; next }
/^[ \t]*__bss_start[ \t]*=[ \t]*\.[ \t]*;[ \t]*$/ { print; insert(hot_data, "sbss"); sb++; next }
/^[ \t]*\.bss[ \t]*:[ \t]*\{[ \t]*$/    { insert(hot_data, "bss_pre"); print; insert(hot_data, "bss"); bs++; next }
{ print }
/\*\(\.boot\)/ { boot = 1; next }
boot == 1 && /ALIGN\(16\)/ { insert(hot_text, ""); boot = 2 }

END {
    err = ""
    if (boot != 2) err = err " *(.boot)+ALIGN(16)"
    if (ro != 1) err = err " .rodata"
    if (sp != 1) err = err " .sbss"
    if (sb != 1) err = err " __bss_start"
    if (bs != 1) err = err " .bss"
    if (err != "") {
        print "engine.ld: anchor(s) not found exactly once in n64.ld:" err > "/dev/stderr"
        exit 1
    }
}
