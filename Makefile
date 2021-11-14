# Specify what typing 'make' on its own will compile
default: antSim
%: %.c
	gcc -std=gnu99 -Wall -pedantic -g $@.c -o $@ \
	     `pkg-config --cflags --libs gtk+-3.0`
