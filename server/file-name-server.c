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

typedef struct entrada_tabla_archivo {
    char *nombre_archivo;
    int ip;
    int port;
    int lock;
    pthread_mutex_t page_mutex;
    nodo_espera_t *cola_espera;
    entrada_tabla_registro *tabla_registros;
    struct entrada_tabla_archivo *next; // Linked list pointer
} entrada_tabla_archivo;

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
} datos_cliente_t;

//Variables globales
int server_socket, port;
entrada_tabla_archivo *tabla_archivos = NULL; // Head of the linked list
pthread_mutex_t tabla_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_conexion(void *arg);
void handle_file_server(char *buffer, datos_cliente_t *data);
void handle_client(char *buffer, datos_cliente_t *data);
int igual(char *str1, char *str2);
void add_file(char *archivo_contenido, datos_cliente_t *data);
int search_file(char *file_name);
void add_file_to_table(char *file_name, datos_cliente_t *data);
void print_file_table();
void print_file_table_unlocked();
void remove_file_from_table(char *filename, datos_cliente_t *data);

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
    
    close(client_socket);
    free(data);
    return NULL;
}

void handle_file_server(char *buffer, datos_cliente_t *data) {
    char *comando = buffer + 2; //estructura F comando

    char accion[3];
    strncpy(accion, comando, 2);
    accion[2] = '\0';

    if (igual(accion, "RS")) {
        // Register Server - registrar este cliente como file server
        printf("Registrando cliente como file server\n");
        
        char client_ip[16];
        inet_ntop(AF_INET, &data->client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(data->client_addr.sin_port);
        
        printf("File Server registrado - IP: %s, Puerto: %d\n", client_ip, client_port);
        
        char *respuesta = "REGISTERED_AS_FILE_SERVER";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }

    if (igual(accion, "AF")) {
        char *filename = comando + 3; // 2 letras y 1 espacio
        printf("Registrando archivo: %s\n", filename);
        add_file(filename, data); // solo nombre del archivo
    } else if (igual(accion, "RF")) {
        char *filename = comando + 3; // 2 letras y 1 espacio
        printf("Solicitando eliminar archivo: %s\n", filename);
        remove_file_from_table(filename, data);
        return; // remove_file_from_table envía su propia respuesta
    }

    char *respuesta = "OK";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
}

void handle_client(char *buffer, datos_cliente_t *data) {
    printf("Procesando mensaje de Cliente: %s", buffer);
    char *comando = buffer + 2; //estructura C comando

    if (strncmp(comando, "SALIR", 5) == 0) { //C SALIR
        char *respuesta = "DISCONNECT_ACK";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Cliente solicitó desconexión\n");
        close(data->client_socket);
        printf("Cliente desconectado\n");
        return;
    }
    
    char *respuesta = "CLIENT_OK";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    
    printf("Respuesta enviada al Cliente: %s\n", respuesta);
}

int igual(char *str1, char *str2) {
    if (str1 == NULL && str2 == NULL) {
        return 1;
    }
    if (str1 == NULL || str2 == NULL) {
        return 0;
    }
    return strcmp(str1, str2) == 0;
}

int search_file(char *file_name) {
    if (file_name == NULL || tabla_archivos == NULL) {
        return -1;
    }
    
    entrada_tabla_archivo *current = tabla_archivos;
    while (current != NULL) {
        if (current->nombre_archivo != NULL && 
            igual(current->nombre_archivo, file_name)) {
            return 1; // archivo encontrado
        }
        current = current->next;
    }
    
    return -1; // archivo no encontrado
}

void add_file(char *filename, datos_cliente_t *data) {
    // Remover salto de línea si existe
    char *newline = strchr(filename, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    printf("Archivo: %s\n", filename);
    
    pthread_mutex_lock(&tabla_mutex);
    
    int archivo_encontrado = search_file(filename);

    if (archivo_encontrado == -1) {
        printf("Agregando archivo a tabla\n");
        add_file_to_table(filename, data);
        pthread_mutex_unlock(&tabla_mutex);
    } else {
        printf("Archivo repetido\n");
        pthread_mutex_unlock(&tabla_mutex);
    }
}

void add_file_to_table(char *file_name, datos_cliente_t *data) {
    entrada_tabla_archivo *nuevo = malloc(sizeof(entrada_tabla_archivo));
    if (nuevo == NULL) {
        printf("Error: No se pudo asignar memoria para nuevo archivo\n");
        return;
    }

    nuevo->nombre_archivo = strdup(file_name);
    if (nuevo->nombre_archivo == NULL) {
        printf("Error: No se pudo asignar memoria para nombre_archivo\n");
        free(nuevo);
        return;
    }

    char client_ip[16];
    inet_ntop(AF_INET, &data->client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(data->client_addr.sin_port);
    
    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    int server_local_port = 0;
    if (getsockname(data->client_socket, (struct sockaddr*)&local_addr, &local_len) == 0) {
        server_local_port = ntohs(local_addr.sin_port);
    }
    
    printf("DEBUG - IP del cliente: %s\n", client_ip);
    printf("DEBUG - Puerto origen del cliente: %d\n", client_port);
    printf("DEBUG - Puerto local del servidor: %d\n", server_local_port);

    nuevo->ip = ntohl(data->client_addr.sin_addr.s_addr);
    nuevo->port = client_port; 
    nuevo->lock = 0;
    pthread_mutex_init(&nuevo->page_mutex, NULL);
    nuevo->cola_espera = NULL;
    nuevo->tabla_registros = NULL;
    nuevo->next = NULL;

    if (tabla_archivos == NULL) {
        tabla_archivos = nuevo;
    } else {
        entrada_tabla_archivo *actual = tabla_archivos;
        while (actual->next != NULL) {
            actual = actual->next;
        }
        actual->next = nuevo;
    }

    printf("Archivo '%s' agregado a la tabla con IP: %s, Puerto: %d\n", file_name, client_ip, nuevo->port);
    
    // Imprimir la tabla actualizada (mutex ya está bloqueado)
    print_file_table_unlocked();
}

void print_file_table() {
    pthread_mutex_lock(&tabla_mutex);
    print_file_table_unlocked();
    pthread_mutex_unlock(&tabla_mutex);
}

void print_file_table_unlocked() {
    printf("\n=== TABLA DE ARCHIVOS ===\n");
    printf("%-20s %-15s %-10s %-10s\n", "ARCHIVO", "IP", "PUERTO", "LOCK");
    printf("--------------------------------------------------------\n");
    
    if (tabla_archivos == NULL) {
        printf("No hay archivos registrados.\n");
    } else {
        entrada_tabla_archivo *current = tabla_archivos;
        int contador = 1;
        
        while (current != NULL) {
            // Convertir IP de formato numérico a string
            struct in_addr addr;
            addr.s_addr = htonl(current->ip);
            char ip_str[16];
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
            
            printf("%-20s %-15s %-10d %-10s\n", 
                   current->nombre_archivo, 
                   ip_str, 
                   current->port,
                   current->lock ? "SI" : "NO");
            
            current = current->next;
            contador++;
        }
        printf("--------------------------------------------------------\n");
        printf("Total de archivos: %d\n", contador - 1);
    }
    
    printf("=========================\n\n");
}

void remove_file_from_table(char *filename, datos_cliente_t *data) {
    // Remover salto de línea si existe
    char *newline = strchr(filename, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    pthread_mutex_lock(&tabla_mutex);
    
    if (tabla_archivos == NULL) {
        pthread_mutex_unlock(&tabla_mutex);
        char *respuesta = "ERROR: No hay archivos registrados";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    entrada_tabla_archivo *current = tabla_archivos;
    entrada_tabla_archivo *previous = NULL;
    
    // Buscar el archivo en la tabla
    while (current != NULL) {
        if (current->nombre_archivo != NULL && igual(current->nombre_archivo, filename)) {
            // Archivo encontrado, verificar condiciones para eliminarlo
            
            if (current->lock != 0) {
                pthread_mutex_unlock(&tabla_mutex);
                char *respuesta = "ERROR: El archivo está bloqueado, no se puede eliminar";
                send(data->client_socket, respuesta, strlen(respuesta), 0);
                printf("Intento de eliminar archivo bloqueado: %s\n", filename);
                return;
            }
            
            if (current->tabla_registros != NULL) {
                pthread_mutex_unlock(&tabla_mutex);
                char *respuesta = "ERROR: El archivo tiene registros activos, no se puede eliminar";
                send(data->client_socket, respuesta, strlen(respuesta), 0);
                printf("Intento de eliminar archivo con registros activos: %s\n", filename);
                return;
            }
            
            // Condiciones cumplidas, eliminar el archivo
            if (previous == NULL) {
                // Es el primer nodo de la lista
                tabla_archivos = current->next;
            } else {
                // No es el primer nodo
                previous->next = current->next;
            }
            
            printf("Archivo eliminado de la tabla: %s\n", filename);
            
            // Liberar memoria
            free(current->nombre_archivo);
            pthread_mutex_destroy(&current->page_mutex);
            free(current);
            
            // Imprimir tabla actualizada
            print_file_table_unlocked();
            
            pthread_mutex_unlock(&tabla_mutex);
            
            char *respuesta = "ARCHIVO_ELIMINADO";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
            return;
        }
        
        previous = current;
        current = current->next;
    }
    
    // Archivo no encontrado
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "ERROR: Archivo no encontrado";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    printf("Intento de eliminar archivo inexistente: %s\n", filename);
}

