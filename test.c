// test.c — same example as the Worked Example section
void func(int a, int b) {
	int c = 0;
	if (a > b) {
		c = a - b;
	} else {
		c = b - a;
	}
}
void main() {
	func(1,2);
}
