# Abstract
This deployment wrapper runs a script optionally preloaded into the binary via the compile process. We provide some
extra support for various functionalities needed in bash scripts, while maintaining some of the wrapped shell's
usefulness.

# Installation

## Prerequisites: cmake
Acquire and install cmake and other dev tools such as make and g++ needed for compiling linux binaries.

## Prerequisites: libsendao
Libsendao is a collection of useful datatypes such as lists and hashes that support application architecture.

git clone https://github.com/sendao/libsendao ./libsendao
cd libsendao
./mkbuild
cd build
make && make install

## Compiling a build version
The script 'mkbuild' will create a 'build' directory and populate it.

./mkbuild
cd build
make

## Compiling and debugging
The script 'mkdebug' populates the 'debug' directory. 
./mkdebug
cd debug
make
ulimit -c unlimited   ## Allow core dumps
./deploy
gdb deploy core


# Use cases

## Buffered Shell
To wrap a custom script, first you'll need to read the examples and create your own script. Once you do,
use the ./deploy <scriptname> syntax to run the script inside the wrapper.

## Wrapped Binary
To wrap the script in the binary, modify 'prescript.cpp'.

# Scripting Reference

## Sections

The wrapped script may have multiple sections separated by directive lines. All directive lines start with the
hash (#) sign. To include an english-language comment ignored by the script driver just use two pounds (##).
If the first section doesn't contain any commands, its directives will be used as script-wide defaults.
Sections may be repeated depending on their configuration and whether or not the commands in the section succeed.

## Directives

* \#response (scancode)=(responsetext)
 If 'scancode' is detected in the application output, 'responsetext' is sent as a response to its input.

* \#scan (scancode)=(saveas)
 When 'scancode' is seen, the text immediately after it to the end of the line is saved in 'saveas' variable.

* \#bscan (scancode)=(saveas)
 Scans to the end of the input command instead of the end of the line.

## Variables

Variables may be used in the commandline to represent data scanned from applications. Just use ${varname} to
include the variable. 





