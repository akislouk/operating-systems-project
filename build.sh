#!/bin/sh
make
./validate_api -c 1,2,4 basic_tests
gnome-terminal -- bash -c "./terminal 0"
gnome-terminal -- bash -c "./terminal 1"
./validate_api -c 1,2,4 --term=0,2 thread_tests
./tinyos_shell 4 2
