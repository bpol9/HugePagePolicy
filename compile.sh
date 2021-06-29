gcc -O2 -Wall -fPIC -o mymalloc.so -shared mymalloc.c -lnuma
gcc -o break_hugepages break_hugepages.c
gcc -o test_malloc test_malloc.c
