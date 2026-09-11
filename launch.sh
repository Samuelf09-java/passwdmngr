#! /bin/bash

cloc . --not-match-d build --not-match-d resources/generated

make clean

if [ "$1" = "debug" ]; then
    make DEBUG=1
	gdb ./passwdmngr
else
    make
	./passwdmngr
fi
