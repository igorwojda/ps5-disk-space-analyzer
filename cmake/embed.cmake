# Turn one file into a C array: cmake -DIN=file -DOUT=out.c -DSYM=name -P embed.cmake
file(READ ${IN} hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR len "${hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUT}
  "#include <stddef.h>\n"
  "const unsigned char ${SYM}[] = {${bytes}0};\n"
  "const size_t ${SYM}_len = ${len};\n")
