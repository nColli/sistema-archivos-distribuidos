#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

typedef struct {
    int socket;
    char* server_ip;
    int server_port;
} client_state_t;

client_state_t client_state = {0};

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Uso: %s <puerto_cliente> <ip_servidor> <puerto_servidor>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    client_state.server_ip = argv[2];
    client_state.server_port = atoi(argv[3]);

    if (client_state.server_port <= 0 || client_state.server_ip <= 0) {
        printf("Puerto o IP invalida\n");
        exit(1);
    }

    //crear socket para conectarse con server
    client_state.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_state.socket < 0) {
        printf("Error al crear socket\n");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client_state.server_port);
    server_addr.sin_addr.s_addr = inet_addr(client_state.server_ip);
    
    if (inet_pton(AF_INET, client_state.server_ip, &server_addr.sin_addr) <= 0) {
        printf("Dir IP invalida: %s\n", client_state.server_ip);
        close(client_state.socket);
        exit(1);
    }
    
    if (connect(client_state.socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error al conectarse con el servidor");
        close(client_state.socket);
        exit(1);
    }

    //una vez creado el socket, muestro menu y puedo enviar mensajes al server

    //entorno de prueba - enviar comandos de forma manual
    char *mensaje = NULL;
    char respuesta[1024];
    size_t len = 0;
    ssize_t n;
    printf("Comando: ");
    n = getline(&mensaje, &len, stdin);

    printf("Ingresaste: %s\n", mensaje);

    while (strcmp(mensaje, "SALIR\n") != 0) {
        printf("\nEnviando comando...\n");
        send(client_state.socket, mensaje, strlen(mensaje), 0);
        int bytes = recv(client_state.socket, respuesta, 1024, 0);
        respuesta[bytes] = '\0';  // Null-terminate the response
        printf("Bytes recibidos del servidor %d \n", bytes);
        printf("Mensaje recibido: %s\n", respuesta);

        printf("Comando: ");
        n = getline(&mensaje, &len, stdin);
    }

    free(mensaje);
    close(client_state.socket);

    exit(0);
}