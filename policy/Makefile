all: rhp.so

rhp.so: rhp.c
	gcc -O2 -Wall -fPIC -o rhp.so -shared rhp.c -lnuma

clean:
	@rm -f rhp.so
