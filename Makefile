CC := /usr/bin/i686-w64-mingw32-gcc

CFLAGS := -I /home/ubuntu/x86_64/include -fopenmp -static

LDFLAGS := -L /home/ubuntu/x86_64/lib -lsox -lwinmm -fopenmp -static

tc.exe: tc.c
	$(CC) $(CFLAGS) tc.c $(LDFLAGS) -o tc.exe

clean:
	rm tc.exe
