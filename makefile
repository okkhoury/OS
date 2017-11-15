make: Write_API.c
	gcc -shared -o libFAT32.so -fPIC Write_API.c
	gcc -o readFAT32 Write_API.c