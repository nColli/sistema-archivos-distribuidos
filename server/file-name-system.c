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

    //recibir mensaje del cliente - primero determinar que es - client o file server
    // mensaje lo recibo todo junto - lo tengo que parsear

    //determinar si es un file_server o client - primer caracter que recibo, lo paso a nro
    //0 => file server
    //1 => client


    char buffer[MAX_MSG]; //al estar todo tamaño tiene que ser grande
    int flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags & ~O_NONBLOCK);

    int bytes_recibidos = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    printf("Se recibieron %d bytes del cliente\n", bytes_recibidos);

    if (bytes_recibidos < 0) {
        printf("Error al recibir datos del cliente\n");
        fcntl(client_socket, F_SETFL, flags);
        return NULL;
    }
    buffer[bytes_recibidos] = '\0';

    while (strncmp(buffer, "SALIR", 5) != 0) {
        //en buffer esta todo el mensaje del cliente
        printf("Mensaje del cliente: %s", buffer);
        char *respuesta = "OK";
        send(client_socket, respuesta, strlen(respuesta), 0);

        memset(buffer, 0, sizeof(buffer)); //limpio buffer para sig mensaje
        int bytes_recibidos = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        printf("Se recibieron %d bytes del cliente\n", bytes_recibidos);

        if (bytes_recibidos <= 0) {
            printf("Cliente desconectado o error al recibir datos\n");
            break;
        }
        buffer[bytes_recibidos] = '\0';
    }
    
    printf("Cliente terminando conexion\n");
    
    // Cleanup
    close(client_socket);
    free(data);
    return NULL;
}