#! /bin/bash

make clean

if [ "$1" = "debug" ]; then
    make DEBUG=1
	gdb ./passwdmngr
else
    make
	./passwdmngr
fi
