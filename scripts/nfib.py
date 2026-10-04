def nfib(n):
    if n <= 1:
        return 1
    return nfib(n - 1) + nfib(n - 2) + 1

if __name__ == "__main__":
    print(nfib(40))
