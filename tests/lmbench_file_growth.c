#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { block = 4096 };
static unsigned char data[block * 3];

static void verify_growth(const char *path, size_t keep, size_t grown) {
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd >= 0);
    memset(data, 0xa5, sizeof(data));
    assert(write(fd, data, sizeof(data)) == sizeof(data));
    assert(fsync(fd) == 0 && close(fd) == 0);
    fd = open(path, O_RDWR);
    assert(fd >= 0 && ftruncate(fd, keep) == 0);
    assert(ftruncate(fd, grown) == 0);
    assert(fsync(fd) == 0 && close(fd) == 0);
    fd = open(path, O_RDONLY);
    assert(fd >= 0 && read(fd, data, sizeof(data)) == (ssize_t)grown);
    for (size_t i = 0; i < grown; ++i) {
        assert(data[i] == (i < keep ? 0xa5 : 0));
    }
    assert(close(fd) == 0);
}

static void verify_writeback(const char *path) {
    // A run longer than the page cache, an untouched gap, and a partial tail.
    enum { pages = 270, tail = 37 };
    unsigned char page[block];
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd >= 0 && ftruncate(fd, pages * block + tail) == 0);
    for (int i = 0; i <= pages; ++i) {
        if (i == 18) continue;
        memset(page, i % 251 + 1, sizeof(page));
        ssize_t n = i == pages ? tail : block;
        assert(pwrite(fd, page, n, i * block) == n);
    }
    assert(fdatasync(fd) == 0);
    unsigned char *map = mmap(NULL, block, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(map != MAP_FAILED);
    map[10] = 42;
    assert(fsync(fd) == 0);
    map[10] = 43;
    assert(fsync(fd) == 0 && munmap(map, block) == 0 && close(fd) == 0);
    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    for (int i = 0; i <= pages; ++i) {
        size_t n = i == pages ? tail : block;
        assert(read(fd, page, sizeof(page)) == (ssize_t)n);
        for (size_t j = 0; j < n; ++j) {
            int expected = i == 18 ? 0 : i % 251 + 1;
            if (i == 0 && j == 10) expected = 43;
            assert(page[j] == expected);
        }
    }
    assert(read(fd, page, 1) == 0 && close(fd) == 0);
}

int main(void) {
    const char *path = "./file-growth-check";
    verify_growth(path, 0, block);
    verify_growth(path, block, block * 2 + 19);
    verify_growth(path, block + 13, block + 27);
    verify_growth(path, block - 7, block * 2 + 15);
    verify_writeback(path);
    assert(unlink(path) == 0);
    puts("PASS file growth preserves prefix and clears new bytes");
    return 0;
}
