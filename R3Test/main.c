#include <stdio.h>
#include <Windows.h>

int main() {
	for (int i = 0; i < 10; i++) {
		LONG l = 10+i;
		printf("[W] %d\n", l);
	}
	return 0;
}