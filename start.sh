gcc -O3 -march=native -DNDEBUG test.c -o bucket_search
./bucket_search 5000000 1000000 24 90 123
rm bucket_search
