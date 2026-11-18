#!/usr/bin/env bash
# Constitution II: nothing under src/dp may allocate, lock, block, do I/O or log unthrottled.
set -u
cd "$(dirname "$0")/.."
bad=0
forbidden_calls='(^|[^[:alnum:]_.>])(malloc|calloc|realloc|aligned_alloc|free|pthread_[a-z_]+|sem_[a-z_]+|usleep|sleep|nanosleep|printf|fprintf|puts|putchar|fputs|fwrite|write|read|open|close|socket|bind|send|sendto|sendmsg|sendmmsg|recv|recvfrom|recvmsg|recvmmsg|epoll_[a-z_]+|poll|select|ioctl|mmap|munmap|fopen|fclose|syscall|clock_gettime|gettimeofday|getenv|exit|abort|log_debug|log_info|log_warn|log_error|log_write)[[:space:]]*\('
forbidden_headers='#[[:space:]]*include[[:space:]]*<(unistd|pthread|semaphore|stdio|stdlib|errno|fcntl|time|signal|poll|sys/[a-z_/]+|linux/[a-z_/]+|netinet/[a-z_/]+|arpa/[a-z_/]+)\.h>'
for f in src/dp/*.c; do
    # strip comments, keep line numbers
    stripped=$(gcc -fpreprocessed -dD -E "$f" 2>/dev/null | sed 's/^#[[:space:]]*[0-9].*//')
    hits=$(printf '%s\n' "$stripped" | grep -nE "$forbidden_calls|$forbidden_headers" || true)
    if [ -n "$hits" ]; then
        echo "FASTPATH VIOLATION in $f:"
        printf '%s\n' "$hits" | sed 's/^/    /'
        bad=1
    fi
done
if [ $bad -eq 0 ]; then
    echo "fastpath OK: src/dp has no allocation, locks, blocking calls, I/O or unthrottled logging"
fi
exit $bad
