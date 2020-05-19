int f(int a) {
    if (a) {
        goto L1;
    }
    return 2;
L1:
    return 1;
}
