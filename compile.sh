gcc -O2 -Wall -fPIC -o rhp.so -shared rhp.c -lnuma
gcc -o break_hugepages break_hugepages.c
gcc -o test_rhp test_rhp.c
