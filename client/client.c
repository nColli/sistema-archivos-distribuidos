#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#define MAX_MSG 1024

typedef struct {
    int socket;
    char* server_ip;
    int server_port;
    int client_port;
} client_state_t;

client_state_t client_state = {0};

void print_menu();
void *listen_for_fns_requests(void *arg);
void send_client_request(char *message);
void handle_fns_request(char *buffer);

int main(int argc, char *argv[]) {
    char opcion[10];
    char mensaje[MAX_MSG];
    char respuesta[MAX_MSG];

    if (argc != 4) {
        printf("Uso: %s <puerto_cliente> <ip_servidor> <puerto_servidor>\n", argv[0]);
        exit(1);
    }

    client_state.client_port = atoi(argv[1]);
    client_state.server_ip = argv[2];
    client_state.server_port = atoi(argv[3]);

    if (client_state.server_port <= 0 || client_state.client_port <= 0) {
        printf("Puerto invalido\n");
        exit(1);
    }

    client_state.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_state.socket < 0) {
        printf("Error al crear socket\n");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client_state.server_port);
    
    if (inet_pton(AF_INET, client_state.server_ip, &server_addr.sin_addr) <= 0) {
        printf("Dir IP invalida: %s\n", client_state.server_ip);
        close(client_state.socket);
        exit(1);
    }
    
    if (connect(client_state.socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error al conectarse con el servidor\n");
        close(client_state.socket);
        exit(1);
    }

    printf("Conectado al FNS en %s:%d\n", client_state.server_ip, client_state.server_port);

    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, listen_for_fns_requests, NULL) != 0) {
        printf("Error al crear hilo de escucha\n");
        close(client_state.socket);
        exit(1);
    }

    sprintf(mensaje, "F RS");
    send_client_request(mensaje);

    print_menu();

    while (1) {
        printf("Opcion: ");
        fgets(opcion, sizeof(opcion), stdin);
        opcion[strcspn(opcion, "\n")] = 0;

        if (strcmp(opcion, "0") == 0) {
            break;
        }

        switch(atoi(opcion)) {
            case 1:
                strcpy(mensaje, "F AF upload_all_files");
                break;
            case 2:
                printf("Nombre del archivo: ");
                fgets(mensaje, sizeof(mensaje), stdin);
                mensaje[strcspn(mensaje, "\n")] = 0;
                break;
            case 3:
                printf("Nombre del archivo a bajar: ");
                char filename[256];
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                sprintf(mensaje, "C DOWNLOAD %s", filename);
                break;
            case 4:
                strcpy(mensaje, "C LIST");
                break;
            default:
                printf("Opcion no implementada\n");
                continue;
        }

        send_client_request(mensaje);
        print_menu();
    }

    pthread_cancel(listener_thread);
    close(client_state.socket);
    return 0;
}

void send_client_request(char *message) {
    send(client_state.socket, message, strlen(message), 0);
    
    char respuesta[MAX_MSG];
    int bytes = recv(client_state.socket, respuesta, MAX_MSG - 1, 0);
    if (bytes > 0) {
        respuesta[bytes] = '\0';
        printf("Respuesta del servidor: %s\n", respuesta);
    }
}

void *listen_for_fns_requests(void *arg) {
    char buffer[MAX_MSG];
    int bytes;
    
    while (1) {
        int flags = fcntl(client_state.socket, F_GETFL, 0);
        fcntl(client_state.socket, F_SETFL, flags | O_NONBLOCK);
        
        bytes = recv(client_state.socket, buffer, MAX_MSG - 1, 0);
        
        fcntl(client_state.socket, F_SETFL, flags & ~O_NONBLOCK);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            handle_fns_request(buffer);
        } else if (bytes == 0) {
            printf("Conexion cerrada por el servidor\n");
            break;
        }
        
        usleep(100000);
    }
    return NULL;
}

void handle_fns_request(char *buffer) {
    printf("Solicitud del FNS: %s\n", buffer);
    
    if (strncmp(buffer, "SEND_FILE", 9) == 0) {
        char filename[256];
        sscanf(buffer, "SEND_FILE %s", filename);
        
        FILE *file = fopen(filename, "r");
        if (file) {
            char file_content[MAX_MSG];
            size_t read_size = fread(file_content, 1, MAX_MSG - 1, file);
            file_content[read_size] = '\0';
            fclose(file);
            
            char response[MAX_MSG];
            sprintf(response, "FILE_CONTENT %s %s", filename, file_content);
            send(client_state.socket, response, strlen(response), 0);
        } else {
            send(client_state.socket, "FILE_NOT_FOUND", 14, 0);
        }
    }
}

void print_menu() {
    printf("\n=== SISTEMA DE ARCHIVOS DISTRIBUIDOS ===\n");
    printf("0. Salir\n");
    printf("1. Subir todos los archivos a FNS\n");
    printf("2. Subir archivo\n");
    printf("3. Bajar archivo\n");
    printf("4. Listar archivos disponibles\n");
    printf("5. Leer archivo\n");
    printf("6. Escribir archivo\n");
    printf("7. Descargar archivo\n");
    printf("8. Leer registro\n");
    printf("9. Escribir registro\n");
    printf("Seleccione una opción: ");
}

