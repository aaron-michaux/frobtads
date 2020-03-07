#!/bin/bash

# Create 'build.log'
# make clean
# make SHELL='sh -x' frob 2>&1 | tee build.log
# cat build.log | grep -E '\.c$' | grep gcc | awk '{ print $NF }' > file-list_C.text
# cat build.log | grep -E '\.cc$' | grep g++ | awk '{ print $NF }' > file-list_CC.text
# cat build.log | grep -E '\.cpp$' | grep g++ | awk '{ print $NF }' > file-list_CPP.text

gen_out()      
{
    FILE="$1"
    EXT="$2"
    echo -n "\$builddir/$(dirname $FILE)/$(basename $FILE $EXT).o"
}

print_rule()
{
    FILE="$1"
    RULE="$2"
    EXT="$3"
    OUT="$(gen_out "$FILE" "$EXT")"
    printf "build %-28s: %s %s\n" "$OUT" "$RULE" "$L"
}

print_build_rules()
{
    cat file-list_C.text   | while read L ; do print_rule "$L" c_rule .c ; done
    echo
    cat file-list_CC.text  | while read L ; do print_rule "$L" cpp .cc ; done
    echo
    cat file-list_CPP.text | while read L ; do print_rule "$L" cpp .cpp ; done
}

print_link_rule()
{
    printf "build \$target: link_exec "
    cat file-list_C.text   | while read L ; do echo -n " " ; gen_out "$L" .c ; done
    cat file-list_CC.text  | while read L ; do echo -n " " ; gen_out "$L" .cc ; done
    cat file-list_CPP.text | while read L ; do echo -n " " ; gen_out "$L" .cpp ; done

}

print_build_rules
echo
print_link_rule
