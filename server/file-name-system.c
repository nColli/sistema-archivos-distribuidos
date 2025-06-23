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

//estructura para registros - cada archivo tiene una tabla de archivos
typedef struct nodo_espera {
    int client_port;
    int client_socket;
    struct nodo_espera *next;
} nodo_espera_t;

typedef struct {
    int registro; //nro de registro - solo se pone cuando se pide bloquear
    int lock;
    nodo_espera_t *cola_espera;
} entrada_tabla_registro;

typedef struct {
    char *nombre_archivo;
    int ip;
    int port;
    int lock;
    pthread_mutex_t page_mutex;
    nodo_espera_t *cola_espera;
    entrada_tabla_registro *tabla_registros;
} entrada_tabla_archivo;

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} datos_cliente_t;

//Variables globales
int server_socket, port;
entrada_tabla_archivo *tabla_archivos;

void *handle_conexion(void *arg);
void handle_file_server(char *buffer, datos_cliente_t *data);
void handle_client(char *buffer, datos_cliente_t *data);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <puerto>\n", argv[0]);
        exit(1);
    } else {
        printf("Puerto: %s\n", argv[1]);
    }
    
    port = atoi(argv[1]);

    //configurar socket y esperar a recibir mensaje de file server y client
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        printf("Error al crear socket\n");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt fallo");
        close(server_socket);
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error bind socket\n");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 5) < 0) {
        printf("Error listen socket\n");
        close(server_socket);
        exit(1);
    }

    printf("Servidor iniciado en puerto %d\n", port);

    while (1) {
        socklen_t client_len = sizeof(struct sockaddr_in);
        struct sockaddr_in client_addr;

        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        printf("\nNueva conexion\n");

        if (client_socket < 0) {
            printf("Error al aceptar la conexion\n");
            close(client_socket);
            continue;
        }

        //creo estructura con datos del cliente para darselos al thread para enviarles el archivo o para agregarlo a cola de espera
        datos_cliente_t *datos_cliente = malloc(sizeof(datos_cliente_t));
        if (!datos_cliente) {
            printf("Error asignar memoria para datos_cliente");
            continue;
        }

        datos_cliente->client_socket = client_socket;
        datos_cliente->client_addr = client_addr;

        //creo un hilo para manejar el pedido del cliente
        pthread_t threadId;
        if (pthread_create(&threadId, NULL, handle_conexion, datos_cliente) != 0) {
            printf("Error al crear hilo\n");
            free(datos_cliente);
            close(client_socket);
            continue;
        }

        pthread_detach(threadId);
    }
    
    close(server_socket);
    exit(0);
}

void *handle_conexion(void *arg) {
    datos_cliente_t *data = (datos_cliente_t *)arg; //probar de poner como arg dir datos_clente
    int client_socket = data->client_socket;
    struct sockaddr_in client_addr = data->client_addr;

    char buffer[MAX_MSG]; //al estar todo tamaño tiene que ser grande
    int flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags & ~O_NONBLOCK);

    int bytes_recibidos = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    //se espera que cliente envie multiples mensajes, por lo que se mantiene la conexion abierta hasta que el cliente la cierra, cerrrando el el socket
    while (bytes_recibidos > 0) {
        printf("Se recibieron %d bytes del cliente\n", bytes_recibidos);

        buffer[bytes_recibidos] = '\0';

        char tipo_cliente = buffer[0];
     
        if (tipo_cliente == 'F') {
            handle_file_server(buffer, data);
        } else if (tipo_cliente == 'C') {
            handle_client(buffer, data);
        } else {
            printf("Tipo de cliente desconocido: %c\n", tipo_cliente);
            char *respuesta = "ERROR: Tipo de cliente no reconocido";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
        }
        
        memset(buffer, 0, sizeof(buffer)); //limpio buffer para sig mensaje
        bytes_recibidos = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        printf("Bytes recibidos %d\n", bytes_recibidos);
    }
    
    printf("Cliente terminando conexion\n");
    
    // Cleanup
    close(client_socket);
    free(data);
    return NULL;
}

void handle_file_server(char *buffer, datos_cliente_t *data) {
    printf("Procesando mensaje de File Server: %s", buffer);
    char *comando = buffer + 1;
    
    char *respuesta = "FS_ACK";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    
    printf("Respuesta enviada al File Server: %s\n", respuesta);
}

void handle_client(char *buffer, datos_cliente_t *data) {
    printf("Procesando mensaje de Cliente: %s", buffer);
    char *comando = buffer + 1;

    if (strncmp(comando, "SALIR", 5) == 0) {
        char *respuesta = "DISCONNECT_ACK";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Cliente solicitó desconexión\n");
        return;
    }
    
    char *respuesta = "CLIENT_OK";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    
    printf("Respuesta enviada al Cliente: %s\n", respuesta);
}