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
#define CMD_CLOSE "chau"

typedef struct client_state {
    int port;
    int socket;
} client_state;

client_state client = {0};

void start_client(int, char*);
void chat();
int send_message(char*);
int receive_message();

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Uso: %s <port> <ip_server> <port_server>\n", argv[0]);
        exit(1);
    }
    client.port = atoi(argv[1]);
    char* ip_server = argv[2];
    int port_server = atoi(argv[3]);

    start_client(port_server, ip_server);

    chat();

    close(client.socket);
}

void start_client(int port_server, char* ip_server) {
    struct sockaddr_in server_addr = {0};

    client.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client.socket < 0) {
        printf("ERROR creando socket\n");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip_server);
    server_addr.sin_port = htons(port_server);

    printf("Conectando a %s:%d\n", ip_server, port_server);
    if (connect(client.socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        printf("ERROR conectando al servidor\n");
        exit(1);
    }

    printf("Conectado al servidor\n");
}

void chat() {
    char message[MAX_MSG];
    printf("Cliente: ");
    fgets(message, MAX_MSG, stdin);
    message[strcspn(message, "\n")] = '\0';

    while (strcmp(message, CMD_CLOSE) != 0) {
        int b = send_message(message);
        if (b > 0) receive_message();
        printf("Cliente: ");
        fgets(message, MAX_MSG, stdin);
        message[strcspn(message, "\n")] = '\0';
    }
}

int send_message(char* message) {
    int b = send(client.socket, message, strlen(message), 0);
    if (b <= 0) {
        printf("ERROR al enviar mensaje\n");
    }
    return b;
}

int receive_message() {
    char message[MAX_MSG];
    bzero(message, MAX_MSG);
    int b = recv(client.socket, message, MAX_MSG, 0);
    if (b == -1) {
        printf("NO HAY RESPUESTA DEL SERVIDOR\n");
        return -1;
    }
    printf("Servidor: %s\n", message);
    return b;
}