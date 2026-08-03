int f(int arr[][10]) {
    return arr[0][1];
}

int main() {
    int a[10][10];
    a[0][1] = 42;
    return f(a);
}
