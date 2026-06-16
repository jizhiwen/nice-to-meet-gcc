// test.c — same example as the Worked Example section
int pick(int a, int b) {
    int x, y;
    if (a < b) {
        x = a + b;
        y = x * 2;
    } else {
        x = a - b;
        y = x + 1;
    }
    return y;
}

void main() {
	int a =0, b = 1;
	pick(a,b);
}
