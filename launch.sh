#! /bin/bash

make clean
make

if [ "$1" = "debug" ]; then
	gdb ./passwdmngr
else
	./passwdmngr
fi
