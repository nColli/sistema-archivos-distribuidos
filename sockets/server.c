#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_MSG 1024

typedef struct server_state {
    int port;
    int socket;
} server_state;

server_state server = {0};

void start_server();
void run_server();
int handle_connection(int);
void* handle_client(void *);
void print_client_info(int);
void print_client_port(int);
void del_start();
void del_end();
int recv_message(int, char*);
int send_message(int, char*);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <server_port>\n", argv[0]);
        exit(1);
    }
    server.port = atoi(argv[1]);
    start_server();
    run_server();

    close(server.socket);
    exit(0);
}

void start_server() {
    server.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server.socket < 0) {
        printf("ERROR crear socket\n");
        close(server.socket);
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("ERROR, setsockopt fallo\n");
        close(server.socket);
        exit(1);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server.port);

    if (bind(server.socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("ERROR bind socket\n");
        close(server.socket);
        exit(1);
    }

    if (listen(server.socket, 5)) {
        printf("ERROR listen socket\n");
        close(server.socket);
        exit(1);
    }

    printf("Server iniciado en puerto %d\n", server.port);
}

void run_server() {
    while (1) {
        socklen_t client_len = sizeof(struct sockaddr_in);
        struct sockaddr_in client_addr;
        printf("Servidor escuchando en %d\n", server.port);
        int client_socket = accept(server.socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            printf("ERROR client_socket\n");
            close(client_socket);
            exit(1);
        }

        del_start();
        printf("Nro SOCKET Nuevo cliente: %d\n", client_socket);
        print_client_port(client_socket);
        del_end();

        handle_connection(client_socket);
    }
}

int handle_connection(int client_socket) {
    pthread_t threadId;
    int *socket_ptr = malloc(sizeof(int));
    if (!socket_ptr) {
        printf("\nERROR alocando socket\n");
        close(client_socket);
        close(server.socket);
        exit(1);
    }

    *socket_ptr = client_socket;
    if (pthread_create(&threadId, NULL, handle_client, socket_ptr) != 0) {
        printf("\nERROR crear thread\n");
        close(client_socket);
        free(socket_ptr);
        return 1;
    }

    pthread_detach(threadId);
    return 0;
}

void* handle_client(void* arg) {
    int *socket_ptr = arg;
    int client_socket = *socket_ptr;
    
    char buffer[MAX_MSG];
    int n = recv_message(client_socket, buffer);

    while (n > 0) {
        send_message(client_socket, buffer);

        n = recv_message(client_socket, buffer);
    }

    free(socket_ptr);
    pthread_exit(&client_socket);
}

void print_client_info(int client_socket) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char ip_str[INET_ADDRSTRLEN];

    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len) == -1) {
        printf("ERROR getpeername\n");
        return;
    }

    if (inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN) == NULL) {
        printf("ERROR inet_ntop");
        return;
    }

    int port = ntohs(client_addr.sin_port);

    printf("INFO CLIENTE:\nIP:%s\nPORT:%d\n", ip_str, port);
}

void print_client_port(int client_socket) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len) == -1) {
        printf("ERROR getpeername\n");
        return;
    }
    
    int port = ntohs(client_addr.sin_port);
    printf("Cliente alojado en puerto %d\n", port);
}

void del_start() {
    printf("\n-----------------------------------------------\n");
}

void del_end() {
    printf("-----------------------------------------------\n\n");
}

int recv_message(int client_socket, char* buffer) {
    bzero(buffer, MAX_MSG);
    int n = recv(client_socket, buffer, MAX_MSG, 0);
    buffer[n] = '\0';
    printf("Msj cliente %d: %s\n", client_socket, buffer);
    return n;
}

int send_message(int client_socket, char* message) {
    printf("Msj servidor: %s\n", message);
    int n = send(client_socket, message, strlen(message), 0);

    if (n > 0) {
        printf("Mensaje transmitido al cliente %d\n", client_socket);
    }

    return n;
}