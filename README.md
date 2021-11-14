AntSim
=======
This is my ant simulation project written in C. It doesn't really work but you're welcome to try the code yourself.

It has been built and tested on Linux Ubuntu 20.04.3 with GTK 3.24.20.

Running the code
-----------------
The program can be compiled on linux using the asociated make file or by running

	gcc -std=gnu99 -Wall -pedantic -g antSim.c -o antSim `pkg-config --cflags --libs gtk+-3.0`
	
and then running with `./antSim`

The program expects a file called `test.bmp` to be in the same directory as the compiled program.
This must be a `.bmp` image with the following properties:

 * Header of type `BITMAPINFOHEADER`
 * Positive hight
 * Only one colour plane
 * exactly 24 bits per pixel
 * `BI_RGB` compression
