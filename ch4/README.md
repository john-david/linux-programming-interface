
append.c usage:

> gcc -std=c11 -Wall -Wextra -o append append.c

> echo -e "hello\nworld" | ./append out.txt       # overwrite out.txt
> echo -e "append\nthis" | ./append -a out.txt     # append to out.txt
> cat out.txt

sparse-aware-cp.c usage:

> gcc -std=c11 -Wall -Wextra -o sparse_cp sparse_aware_cp.c

> ./sparse_cp some-sparse-file.dst some-sparse-file.copy

