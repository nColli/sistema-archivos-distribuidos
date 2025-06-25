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

// Estructura para mantener registro de file servers
typedef struct registered_file_server {
    uint32_t ip;
    int file_server_port;
    struct registered_file_server *next;
} registered_file_server_t;

//Variables globales
int server_socket, port;
entrada_tabla_archivo *tabla_archivos = NULL; // Head of the linked list
registered_file_server_t *file_servers = NULL; // Lista de file servers registrados
pthread_mutex_t tabla_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_servers_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_conexion(void *arg);
void handle_file_server(char *buffer, datos_cliente_t *data);
void handle_client(char *buffer, datos_cliente_t *data);
int igual(char *str1, char *str2);
void add_file(char *archivo_contenido, datos_cliente_t *data);
void add_file_with_port(char *filename, int file_server_port, datos_cliente_t *data);
int search_file(char *file_name);
void add_file_to_table(char *file_name, datos_cliente_t *data);
void add_file_to_table_with_port(char *file_name, int file_server_port, datos_cliente_t *data);
void print_file_table();
void print_file_table_unlocked();
void remove_file_from_table(char *filename, datos_cliente_t *data);
void send_file_list(datos_cliente_t *data);
void handle_read_file(char *filename, datos_cliente_t *data);
void handle_read_record(char *buffer, datos_cliente_t *data);
void handle_write_file(char *filename, datos_cliente_t *data);
void handle_write_record(char *buffer, datos_cliente_t *data);
void handle_write_back(char *buffer, datos_cliente_t *data);
void handle_write_record_back(char *buffer, datos_cliente_t *data);
void request_file_from_server(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client);
void request_file_from_server_read_only(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client);
void request_record_from_server(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *requesting_client);
void request_file_for_write(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client);
void send_modified_content_to_server(entrada_tabla_archivo *file_entry, char *content);
void add_to_waiting_queue(entrada_tabla_archivo *file_entry, datos_cliente_t *client);
void add_to_waiting_queue_front(entrada_tabla_archivo *file_entry, datos_cliente_t *client);
void process_next_in_queue(entrada_tabla_archivo *file_entry);
void register_file_server(uint32_t ip, int file_server_port);
int find_file_server_port(uint32_t ip);
void request_record_for_write(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *requesting_client);
void send_modified_record_to_server(entrada_tabla_archivo *file_entry, int record_number, char *content);
int is_record_locked(entrada_tabla_archivo *file_entry, int record_number);
void add_record_to_table(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *client);
void remove_record_from_table(entrada_tabla_archivo *file_entry, int record_number);
void add_to_record_waiting_queue(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *client);

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
        
        // Extraer el puerto del file server del mensaje
        int file_server_port = 0;
        if (sscanf(comando, "RS %d", &file_server_port) != 1) {
            printf("Error: Formato inválido en registro de file server\n");
            char *respuesta = "ERROR: Formato de registro inválido";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
            return;
        }
        
        char client_ip[16];
        inet_ntop(AF_INET, &data->client_addr.sin_addr, client_ip, sizeof(client_ip));
        
        printf("File Server registrado - IP: %s, Puerto File Server: %d, Puerto Cliente: %d\n", 
               client_ip, file_server_port, ntohs(data->client_addr.sin_port));
        
        // Registrar el file server
        register_file_server(data->client_addr.sin_addr.s_addr, file_server_port);
        
        char *respuesta = "REGISTERED_AS_FILE_SERVER";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }

    if (igual(accion, "AF")) {
        char filename[256];
        int file_server_port;
        if (sscanf(comando, "AF %255s %d", filename, &file_server_port) == 2) {
            printf("Registrando archivo: %s con puerto file server: %d\n", filename, file_server_port);
            add_file_with_port(filename, file_server_port, data);
        } else {
            printf("Error: Formato inválido en comando AF\n");
            char *respuesta = "ERROR: Formato de comando AF inválido";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
            return;
        }
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
    printf("Procesando mensaje de Cliente: %s\n", buffer);
    char *comando = buffer + 2; //estructura C comando

    if (strncmp(comando, "SALIR", 5) == 0) { //C SALIR
        char *respuesta = "DISCONNECT_ACK";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Cliente solicitó desconexión\n");
        close(data->client_socket);
        printf("Cliente desconectado\n");
        return;
    } else if (strncmp(comando, "LF", 2) == 0) { //C LF
        printf("Cliente solicitó lista de archivos\n");
        send_file_list(data);
        return;
    } else if (strncmp(comando, "RF", 2) == 0) { //C RF filename
        char *filename = comando + 3; // 2 letras "RF" + 1 espacio
        printf("Cliente solicitó leer archivo: %s\n", filename);
        handle_read_file(filename, data);
        return;
    } else if (strncmp(comando, "RR", 2) == 0) {
        printf("Cliente solicitó leer registro\n");
        handle_read_record(comando, data);
        return;
    } else if (strncmp(comando, "WF", 2) == 0) { //C WF filename
        char *filename = comando + 3; // 2 letras "WF" + 1 espacio
        printf("Cliente solicitó escribir archivo: %s\n", filename);
        handle_write_file(filename, data);
        return;
    } else if (strncmp(comando, "WRB", 3) == 0) { //C WRB filename record_number content
        printf("Cliente enviando registro modificado\n");
        handle_write_record_back(comando, data);
        return;
    } else if (strncmp(comando, "WR", 2) == 0) { //C WR filename record_number
        printf("Cliente solicitó escribir registro\n");
        handle_write_record(comando, data);
        return;
    } else if (strncmp(comando, "WB", 2) == 0) { //C WB filename content
        printf("Cliente enviando contenido modificado\n");
        handle_write_back(comando, data);
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
    
    // Buscar el file server port registrado para esta IP
    int file_server_port = find_file_server_port(data->client_addr.sin_addr.s_addr);
    if (file_server_port == -1) {
        printf("Error: No se encontró file server registrado para esta IP\n");
        free(nuevo->nombre_archivo);
        free(nuevo);
        return;
    }
    
    printf("DEBUG - IP del cliente: %s\n", client_ip);
    printf("DEBUG - Puerto file server: %d\n", file_server_port);
    printf("DEBUG - Puerto origen del cliente: %d\n", ntohs(data->client_addr.sin_port));

    nuevo->ip = ntohl(data->client_addr.sin_addr.s_addr);
    nuevo->port = file_server_port;  // Usar file server port, no client port
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

    printf("Archivo '%s' agregado a la tabla con IP: %s, Puerto File Server: %d\n", file_name, client_ip, nuevo->port);
    
    // Imprimir la tabla actualizada (mutex ya está bloqueado)
    print_file_table_unlocked();
}

void add_file_with_port(char *filename, int file_server_port, datos_cliente_t *data) {
    // Remover salto de línea si existe
    char *newline = strchr(filename, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    printf("Archivo: %s con puerto: %d\n", filename, file_server_port);
    
    pthread_mutex_lock(&tabla_mutex);
    
    int archivo_encontrado = search_file(filename);

    if (archivo_encontrado == -1) {
        printf("Agregando archivo a tabla con puerto específico\n");
        add_file_to_table_with_port(filename, file_server_port, data);
        pthread_mutex_unlock(&tabla_mutex);
    } else {
        printf("Archivo repetido\n");
        pthread_mutex_unlock(&tabla_mutex);
    }
}

void add_file_to_table_with_port(char *file_name, int file_server_port, datos_cliente_t *data) {
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
    
    printf("DEBUG - IP del cliente: %s\n", client_ip);
    printf("DEBUG - Puerto file server recibido: %d\n", file_server_port);
    printf("DEBUG - Puerto origen del cliente: %d\n", ntohs(data->client_addr.sin_port));

    nuevo->ip = ntohl(data->client_addr.sin_addr.s_addr);
    nuevo->port = file_server_port;  // Usar el puerto recibido directamente
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

    printf("Archivo '%s' agregado a la tabla con IP: %s, Puerto File Server: %d\n", file_name, client_ip, nuevo->port);
    
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

void send_file_list(datos_cliente_t *data) {
    pthread_mutex_lock(&tabla_mutex);
    
    char response[MAX_MSG];
    
    if (tabla_archivos == NULL) {
        strcpy(response, "LIST_EMPTY: No hay archivos registrados");
        pthread_mutex_unlock(&tabla_mutex);
        send(data->client_socket, response, strlen(response), 0);
        return;
    }
    
    // Construir la lista de archivos
    strcpy(response, "\nLISTA ARCHIVOS:\n");
    entrada_tabla_archivo *current = tabla_archivos;
    int count = 0;
    
    while (current != NULL && strlen(response) < MAX_MSG - 200) { // Dejar espacio para más info
        if (count > 0) {
            strcat(response, "\n");
        }

        char file_info[256];
        struct in_addr addr;
        addr.s_addr = htonl(current->ip);
        char ip_str[16];
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
        
        snprintf(file_info, sizeof(file_info), "%s | %s | %d | %s", current->nombre_archivo, ip_str, current->port, current->lock ? "LOCKED" : "UNLOCKED");
        
        strcat(response, file_info);
        current = current->next;
        count++;
    }
    
    pthread_mutex_unlock(&tabla_mutex);
    
    printf("Enviando lista de %d archivos al cliente\n", count);
    send(data->client_socket, response, strlen(response), 0);
}

void handle_read_file(char *filename, datos_cliente_t *data) {
    // Remover salto de línea si existe
    char *newline = strchr(filename, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *current = tabla_archivos;
    while (current != NULL) {
        if (current->nombre_archivo != NULL && igual(current->nombre_archivo, filename)) {
            // Archivo encontrado - leer inmediatamente sin bloqueo
            printf("Solicitud de lectura para archivo %s (sin bloqueo)\n", filename);
            
            pthread_mutex_unlock(&tabla_mutex);
            
            // Solicitar archivo al file server inmediatamente
            request_file_from_server_read_only(current, data);
            return;
        }
        current = current->next;
    }
    
    // Archivo no encontrado
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "NOTFOUND: El archivo no existe en el sistema";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    printf("Cliente solicitó archivo inexistente: %s\n", filename);
}

void add_to_waiting_queue(entrada_tabla_archivo *file_entry, datos_cliente_t *client) {
    nodo_espera_t *new_node = malloc(sizeof(nodo_espera_t));
    if (!new_node) {
        printf("Error: No se pudo asignar memoria para nodo de espera\n");
        return;
    }
    
    new_node->client_socket = client->client_socket;
    new_node->client_port = ntohs(client->client_addr.sin_port);
    new_node->next = NULL;
    
    // Agregar al final de la cola
    if (file_entry->cola_espera == NULL) {
        file_entry->cola_espera = new_node;
    } else {
        nodo_espera_t *current = file_entry->cola_espera;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }
    
    printf("Cliente agregado a cola de espera del archivo %s\n", file_entry->nombre_archivo);
}

void request_file_from_server(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client) {
    // Crear nuevo socket para conectar al file server
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server\n");
        
        // Desbloquear archivo y procesar siguiente en cola
        pthread_mutex_lock(&tabla_mutex);
        file_entry->lock = 0;
        process_next_in_queue(file_entry);
        pthread_mutex_unlock(&tabla_mutex);
        
        char *respuesta = "ERROR: No se pudo crear socket para file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Configurar dirección del file server
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server en IP: %s, Puerto: %d\n", 
           inet_ntoa((struct in_addr){htonl(file_entry->ip)}), file_entry->port);
    
    // Conectar al file server
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server\n");
        close(file_server_socket);
        
        // Desbloquear archivo y procesar siguiente en cola
        pthread_mutex_lock(&tabla_mutex);
        file_entry->lock = 0;
        process_next_in_queue(file_entry);
        pthread_mutex_unlock(&tabla_mutex);
        
        char *respuesta = "ERROR: No se pudo conectar al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Crear mensaje para solicitar el archivo al file server
    char request_msg[MAX_MSG];
    sprintf(request_msg, "SF %s", file_entry->nombre_archivo);
    
    printf("Solicitando archivo %s al file server\n", file_entry->nombre_archivo);
    
    // Enviar solicitud al file server
    int bytes_sent = send(file_server_socket, request_msg, strlen(request_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando solicitud al file server\n");
        close(file_server_socket);
        
        // Desbloquear archivo y procesar siguiente en cola
        pthread_mutex_lock(&tabla_mutex);
        file_entry->lock = 0;
        process_next_in_queue(file_entry);
        pthread_mutex_unlock(&tabla_mutex);
        
        char *respuesta = "ERROR: No se pudo enviar solicitud al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Solicitud enviada al file server, esperando respuesta...\n");
    
    // Recibir respuesta del file server
    char response[MAX_MSG];
    int bytes_received = recv(file_server_socket, response, MAX_MSG - 1, 0);
    
    close(file_server_socket);
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("Respuesta recibida del file server: %s\n", response);
        
        if (strncmp(response, "FILE_CONTENT", 12) == 0) {
            // Extraer filename y contenido
            char filename[256];
            char *content_start = NULL;
            
            if (sscanf(response, "FILE_CONTENT %255s", filename) == 1) {
                content_start = strstr(response, filename);
                if (content_start) {
                    content_start += strlen(filename) + 1; // +1 para el espacio
                    
                    // Enviar contenido directamente al cliente
                    char response_to_client[MAX_MSG];
                    snprintf(response_to_client, sizeof(response_to_client), 
                            "FILE_CONTENT:%s:%s", filename, content_start);
                    
                    send(requesting_client->client_socket, response_to_client, strlen(response_to_client), 0);
                    printf("Contenido del archivo %s enviado directamente al cliente\n", filename);
                } else {
                    printf("Error extrayendo contenido del archivo\n");
                    char *respuesta = "ERROR: Formato de respuesta inválido";
                    send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
                }
            } else {
                printf("Error extrayendo nombre del archivo\n");
                char *respuesta = "ERROR: Formato de respuesta inválido";
                send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
            }
        } else if (strncmp(response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File server reporta archivo no encontrado\n");
            char *respuesta = "ERROR: Archivo no encontrado en file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else {
            printf("Respuesta no reconocida del file server: %s\n", response);
            char *respuesta = "ERROR: Respuesta no reconocida del file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        }
    } else {
        printf("Error recibiendo respuesta del file server\n");
        char *respuesta = "ERROR: No se recibió respuesta del file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
    }
    
    // Desbloquear archivo y procesar siguiente en cola
    pthread_mutex_lock(&tabla_mutex);
    file_entry->lock = 0;
    process_next_in_queue(file_entry);
    pthread_mutex_unlock(&tabla_mutex);
}

void request_file_from_server_read_only(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client) {
    // Crear nuevo socket para conectar al file server
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server\n");
        char *respuesta = "ERROR: No se pudo crear socket para file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Configurar dirección del file server
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para lectura en IP: %s, Puerto: %d\n", 
           inet_ntoa((struct in_addr){htonl(file_entry->ip)}), file_entry->port);
    
    // Conectar al file server
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo conectar al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Crear mensaje para solicitar el archivo al file server
    char request_msg[MAX_MSG];
    sprintf(request_msg, "SF %s", file_entry->nombre_archivo);
    
    printf("Solicitando archivo %s para lectura al file server\n", file_entry->nombre_archivo);
    
    // Enviar solicitud al file server
    int bytes_sent = send(file_server_socket, request_msg, strlen(request_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando solicitud al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo enviar solicitud al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Solicitud de lectura enviada al file server, esperando respuesta...\n");
    
    // Recibir respuesta del file server
    char response[MAX_MSG];
    int bytes_received = recv(file_server_socket, response, MAX_MSG - 1, 0);
    
    close(file_server_socket);
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("Respuesta recibida del file server: %s\n", response);
        
        if (strncmp(response, "FILE_CONTENT", 12) == 0) {
            // Extraer filename y contenido
            char filename[256];
            char *content_start = NULL;
            
            if (sscanf(response, "FILE_CONTENT %255s", filename) == 1) {
                content_start = strstr(response, filename);
                if (content_start) {
                    content_start += strlen(filename) + 1; // +1 para el espacio
                    
                    // Enviar contenido directamente al cliente
                    char response_to_client[MAX_MSG];
                    snprintf(response_to_client, sizeof(response_to_client), 
                            "FILE_CONTENT:%s:%s", filename, content_start);
                    
                    send(requesting_client->client_socket, response_to_client, strlen(response_to_client), 0);
                    printf("Contenido del archivo %s enviado directamente al cliente\n", filename);
                } else {
                    printf("Error extrayendo contenido del archivo\n");
                    char *respuesta = "ERROR: Formato de respuesta inválido";
                    send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
                }
            } else {
                printf("Error extrayendo nombre del archivo\n");
                char *respuesta = "ERROR: Formato de respuesta inválido";
                send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
            }
        } else if (strncmp(response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File server reporta archivo no encontrado\n");
            char *respuesta = "ERROR: Archivo no encontrado en file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else {
            printf("Respuesta no reconocida del file server: %s\n", response);
            char *respuesta = "ERROR: Respuesta no reconocida del file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        }
    } else {
        printf("Error recibiendo respuesta del file server\n");
        char *respuesta = "ERROR: No se recibió respuesta del file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
    }
    
    // Lectura completada - no hay locks que manejar
    printf("Lectura del archivo %s completada\n", file_entry->nombre_archivo);
}

void handle_read_record(char *buffer, datos_cliente_t *data) {
    char filename[256];
    int record_number;
    
    if (sscanf(buffer, "RR %255s %d", filename, &record_number) != 2) {
        char *respuesta = "ERROR: Formato inválido en solicitud de registro";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Error: Formato inválido en comando RR\n");
        return;
    }
    
    if (record_number <= 0) {
        char *respuesta = "ERROR: El número de registro debe ser mayor a 0";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Error: Número de registro inválido: %d\n", record_number);
        return;
    }
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *current = tabla_archivos;
    while (current != NULL) {
        if (current->nombre_archivo != NULL && igual(current->nombre_archivo, filename)) {
            printf("Solicitud de lectura de registro %d para archivo %s\n", record_number, filename);
            
            pthread_mutex_unlock(&tabla_mutex);
            
            request_record_from_server(current, record_number, data);
            return;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "NOTFOUND: El archivo no existe en el sistema";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    printf("Cliente solicitó registro de archivo inexistente: %s\n", filename);
}

void handle_write_file(char *filename, datos_cliente_t *data) {
    // Remover salto de línea si existe
    char *newline = strchr(filename, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *current = tabla_archivos;
    while (current != NULL) {
        if (current->nombre_archivo != NULL && igual(current->nombre_archivo, filename)) {
            // Archivo encontrado
            
            if (current->lock == 0) {
                // Archivo no está bloqueado - bloquear para escritura
                current->lock = 1;
                printf("Archivo %s bloqueado para escritura\n", filename);
                
                // Agregar cliente al FRENTE de la cola (TOP priority)
                add_to_waiting_queue_front(current, data);
                
                pthread_mutex_unlock(&tabla_mutex);
                
                // Solicitar archivo al file server para edición
                request_file_for_write(current, data);
                return;
                
            } else {
                // Archivo está bloqueado - agregar a cola de espera
                printf("Archivo %s ya está bloqueado, agregando cliente a cola de espera\n", filename);
                add_to_waiting_queue(current, data);
                
                pthread_mutex_unlock(&tabla_mutex);
                
                char *respuesta = "WAIT: El archivo está siendo utilizado, esperando...";
                send(data->client_socket, respuesta, strlen(respuesta), 0);
                
                printf("Cliente agregado a cola, conexión mantenida abierta\n");
                return;
            }
        }
        current = current->next;
    }
    
    // Archivo no encontrado
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "NOTFOUND: El archivo no existe en el sistema";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    printf("Cliente solicitó escribir archivo inexistente: %s\n", filename);
}

void handle_write_record(char *buffer, datos_cliente_t *data) {
    char filename[256];
    int record_number;
    
    // Remove trailing newline if present
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    if (sscanf(buffer, "WR %255s %d", filename, &record_number) != 2) {
        char *respuesta = "ERROR: Formato inválido en solicitud de escritura de registro";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Error: Formato inválido en comando WR\n");
        return;
    }
    
    if (record_number <= 0) {
        char *respuesta = "ERROR: El número de registro debe ser mayor a 0";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        printf("Error: Número de registro inválido: %d\n", record_number);
        return;
    }
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *file_entry = tabla_archivos;
    while (file_entry != NULL) {
        if (file_entry->nombre_archivo != NULL && igual(file_entry->nombre_archivo, filename)) {
            // Archivo encontrado
            
            if (file_entry->lock != 0) {
                // Archivo está bloqueado para escritura completa - agregar a cola general
                printf("Archivo %s está bloqueado, agregando cliente a cola general\n", filename);
                add_to_waiting_queue(file_entry, data);
                
                pthread_mutex_unlock(&tabla_mutex);
                
                char *respuesta = "WAIT: El archivo está siendo utilizado completamente, esperando...";
                send(data->client_socket, respuesta, strlen(respuesta), 0);
                return;
            }
            
            // Archivo no está bloqueado - proceder con lógica de registros
            if (file_entry->tabla_registros == NULL) {
                // No hay registros bloqueados - mantener archivo desbloqueado pero agregar registro
                printf("No hay registros bloqueados, agregando registro %d a tabla\n", record_number);
                add_record_to_table(file_entry, record_number, data);
                
                pthread_mutex_unlock(&tabla_mutex);
                
                // Solicitar registro específico para escritura
                request_record_for_write(file_entry, record_number, data);
                return;
                
            } else {
                // Verificar si el registro específico está en conflicto
                if (is_record_locked(file_entry, record_number)) {
                    // Registro específico está bloqueado - agregar a cola del registro
                    printf("Registro %d está bloqueado, agregando a cola del registro\n", record_number);
                    add_to_record_waiting_queue(file_entry, record_number, data);
                    
                    pthread_mutex_unlock(&tabla_mutex);
                    
                    char respuesta[MAX_MSG];
                    sprintf(respuesta, "WAIT: El registro %d está siendo utilizado, esperando...", record_number);
                    send(data->client_socket, respuesta, strlen(respuesta), 0);
                    return;
                    
                } else {
                    // Registro no está en conflicto - agregarlo y proceder
                    printf("Registro %d no está en conflicto, agregando a tabla\n", record_number);
                    add_record_to_table(file_entry, record_number, data);
                    
                    pthread_mutex_unlock(&tabla_mutex);
                    
                    // Solicitar registro específico para escritura
                    request_record_for_write(file_entry, record_number, data);
                    return;
                }
            }
        }
        file_entry = file_entry->next;
    }
    
    // Archivo no encontrado
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "NOTFOUND: El archivo no existe en el sistema";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
    printf("Cliente solicitó escribir registro de archivo inexistente: %s\n", filename);
}

void add_to_waiting_queue_front(entrada_tabla_archivo *file_entry, datos_cliente_t *client) {
    nodo_espera_t *new_node = malloc(sizeof(nodo_espera_t));
    if (!new_node) {
        printf("Error: No se pudo asignar memoria para nodo de espera\n");
        return;
    }
    
    new_node->client_socket = client->client_socket;
    new_node->client_port = ntohs(client->client_addr.sin_port);
    
    // Agregar al FRENTE de la cola (priority)
    new_node->next = file_entry->cola_espera;
    file_entry->cola_espera = new_node;
    
    printf("Cliente agregado al FRENTE de la cola de espera del archivo %s\n", file_entry->nombre_archivo);
}

void request_record_from_server(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *requesting_client) {
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server\n");
        char *respuesta = "ERROR: No se pudo crear socket para file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para leer registro en IP: %s, Puerto: %d\n", 
           inet_ntoa((struct in_addr){htonl(file_entry->ip)}), file_entry->port);
    
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo conectar al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    char request_msg[MAX_MSG];
    sprintf(request_msg, "SR %s %d", file_entry->nombre_archivo, record_number);
    
    printf("Solicitando registro %d del archivo %s al file server\n", record_number, file_entry->nombre_archivo);
    
    int bytes_sent = send(file_server_socket, request_msg, strlen(request_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando solicitud al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo enviar solicitud al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Solicitud de registro enviada al file server, esperando respuesta...\n");
    
    char response[MAX_MSG];
    int bytes_received = recv(file_server_socket, response, MAX_MSG - 1, 0);
    
    close(file_server_socket);
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("Respuesta recibida del file server: %s\n", response);
        
        if (strncmp(response, "RECORD_CONTENT", 14) == 0) {
            send(requesting_client->client_socket, response, strlen(response), 0);
            printf("Registro %d del archivo %s enviado al cliente\n", record_number, file_entry->nombre_archivo);
        } else if (strncmp(response, "RECORD_NOT_FOUND", 16) == 0) {
            printf("File server reporta registro no encontrado\n");
            char *respuesta = "ERROR: Registro no encontrado en el archivo";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else if (strncmp(response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File server reporta archivo no encontrado\n");
            char *respuesta = "ERROR: Archivo no encontrado en file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else {
            printf("Respuesta no reconocida del file server: %s\n", response);
            char *respuesta = "ERROR: Respuesta no reconocida del file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        }
    } else {
        printf("Error recibiendo respuesta del file server\n");
        char *respuesta = "ERROR: No se recibió respuesta del file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
    }
    
    printf("Lectura del registro %d del archivo %s completada\n", record_number, file_entry->nombre_archivo);
}

void request_file_for_write(entrada_tabla_archivo *file_entry, datos_cliente_t *requesting_client) {
    // Similar a request_file_from_server_read_only pero envía WRITE_CONTENT en lugar de FILE_CONTENT
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server\n");
        char *respuesta = "ERROR: No se pudo crear socket para file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Configurar dirección del file server
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para escritura en IP: %s, Puerto: %d\n", 
           inet_ntoa((struct in_addr){htonl(file_entry->ip)}), file_entry->port);
    
    // Conectar al file server
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo conectar al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Crear mensaje para solicitar el archivo al file server
    char request_msg[MAX_MSG];
    sprintf(request_msg, "SF %s", file_entry->nombre_archivo);
    
    printf("Solicitando archivo %s para escritura al file server\n", file_entry->nombre_archivo);
    
    // Enviar solicitud al file server
    int bytes_sent = send(file_server_socket, request_msg, strlen(request_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando solicitud al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo enviar solicitud al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Solicitud de escritura enviada al file server, esperando respuesta...\n");
    
    // Recibir respuesta del file server
    char response[MAX_MSG];
    int bytes_received = recv(file_server_socket, response, MAX_MSG - 1, 0);
    
    close(file_server_socket);
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("Respuesta recibida del file server: %s\n", response);
        
        if (strncmp(response, "FILE_CONTENT", 12) == 0) {
            // Extraer filename y contenido
            char filename[256];
            char *content_start = NULL;
            
            if (sscanf(response, "FILE_CONTENT %255s", filename) == 1) {
                content_start = strstr(response, filename);
                if (content_start) {
                    content_start += strlen(filename) + 1; // +1 para el espacio
                    
                    // Enviar contenido al cliente para edición (con formato WRITE_CONTENT)
                    char response_to_client[MAX_MSG];
                    snprintf(response_to_client, sizeof(response_to_client), 
                            "WRITE_CONTENT:%s:%s", filename, content_start);
                    
                    send(requesting_client->client_socket, response_to_client, strlen(response_to_client), 0);
                    printf("Contenido del archivo %s enviado al cliente para edición\n", filename);
                } else {
                    printf("Error extrayendo contenido del archivo\n");
                    char *respuesta = "ERROR: Formato de respuesta inválido";
                    send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
                }
            } else {
                printf("Error extrayendo nombre del archivo\n");
                char *respuesta = "ERROR: Formato de respuesta inválido";
                send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
            }
        } else if (strncmp(response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File server reporta archivo no encontrado\n");
            char *respuesta = "ERROR: Archivo no encontrado en file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else {
            printf("Respuesta no reconocida del file server: %s\n", response);
            char *respuesta = "ERROR: Respuesta no reconocida del file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        }
    } else {
        printf("Error recibiendo respuesta del file server\n");
        char *respuesta = "ERROR: No se recibió respuesta del file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
    }
    
    // NO desbloqueamos aquí - el archivo permanece bloqueado hasta que se complete la escritura
}

void handle_write_back(char *buffer, datos_cliente_t *data) {
    // Formato esperado: "WB filename content"
    char filename[256];
    char *content_start = strstr(buffer, " ");
    if (!content_start) {
        char *respuesta = "ERROR: Formato inválido en write back";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    content_start++; // Skip the first space
    
    char *second_space = strstr(content_start, " ");
    if (!second_space) {
        char *respuesta = "ERROR: Formato inválido en write back";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Extract filename
    int filename_len = second_space - content_start;
    strncpy(filename, content_start, filename_len);
    filename[filename_len] = '\0';
    
    // Get content after filename
    char *modified_content = second_space + 1;
    
    printf("Recibido contenido modificado para archivo: %s\n", filename);
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *file_entry = tabla_archivos;
    while (file_entry != NULL) {
        if (file_entry->nombre_archivo != NULL && igual(file_entry->nombre_archivo, filename)) {
            // Archivo encontrado - enviar contenido modificado al file server
            pthread_mutex_unlock(&tabla_mutex);
            
            send_modified_content_to_server(file_entry, modified_content);
            
            // Ahora desbloquear archivo y procesar cola
            pthread_mutex_lock(&tabla_mutex);
            file_entry->lock = 0;
            
            // Remover el cliente actual de la cola (el que está al frente)
            if (file_entry->cola_espera != NULL) {
                nodo_espera_t *completed_client = file_entry->cola_espera;
                file_entry->cola_espera = completed_client->next;
                free(completed_client);
                printf("Cliente removido de la cola después de completar escritura\n");
            }
            
            // Procesar siguiente cliente en cola
            process_next_in_queue(file_entry);
            pthread_mutex_unlock(&tabla_mutex);
            
            char *respuesta = "WRITE_COMPLETE: Archivo guardado exitosamente";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
            return;
        }
        file_entry = file_entry->next;
    }
    
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "ERROR: Archivo no encontrado para write back";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
}

void handle_write_record_back(char *buffer, datos_cliente_t *data) {
    // Formato esperado: "WRB filename record_number content"
    char filename[256];
    int record_number;
    char *content_start = NULL;
    
    // Parse: WRB filename record_number content
    char *first_space = strstr(buffer, " ");
    if (!first_space) {
        char *respuesta = "ERROR: Formato inválido en write record back";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    first_space++; // Skip "WRB "
    
    char *second_space = strstr(first_space, " ");
    if (!second_space) {
        char *respuesta = "ERROR: Formato inválido en write record back";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    // Extract filename
    int filename_len = second_space - first_space;
    strncpy(filename, first_space, filename_len);
    filename[filename_len] = '\0';
    
    // Parse record number and content
    char *third_space = strstr(second_space + 1, " ");
    if (!third_space) {
        char *respuesta = "ERROR: Formato inválido en write record back";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    record_number = atoi(second_space + 1);
    content_start = third_space + 1;
    
    if (record_number <= 0) {
        char *respuesta = "ERROR: Número de registro inválido";
        send(data->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Recibido registro modificado para archivo: %s, registro: %d\n", filename, record_number);
    
    pthread_mutex_lock(&tabla_mutex);
    
    // Buscar el archivo en la tabla
    entrada_tabla_archivo *file_entry = tabla_archivos;
    while (file_entry != NULL) {
        if (file_entry->nombre_archivo != NULL && igual(file_entry->nombre_archivo, filename)) {
            // Archivo encontrado - enviar registro modificado al file server
            pthread_mutex_unlock(&tabla_mutex);
            
            send_modified_record_to_server(file_entry, record_number, content_start);
            
            // Procesar siguiente cliente en cola del registro 
            pthread_mutex_lock(&tabla_mutex);
            
            if (file_entry->tabla_registros != NULL && 
                file_entry->tabla_registros->registro == record_number) {
                
                if (file_entry->tabla_registros->cola_espera != NULL) {
                    // Hay clientes esperando - procesar el siguiente
                    nodo_espera_t *next_client = file_entry->tabla_registros->cola_espera;
                    file_entry->tabla_registros->cola_espera = next_client->next;
                    
                    // Crear datos_cliente_t temporales para el cliente que espera
                    datos_cliente_t temp_client;
                    temp_client.client_socket = next_client->client_socket;
                    temp_client.client_addr.sin_port = htons(next_client->client_port);
                    
                    printf("Procesando siguiente cliente en cola para registro %d\n", record_number);
                    
                    // Liberar el nodo de espera
                    free(next_client);
                    
                    // Mantener el registro bloqueado para el siguiente cliente
                    file_entry->tabla_registros->lock = 1;
                    
                    pthread_mutex_unlock(&tabla_mutex);
                    
                    // Solicitar registro para escritura para el siguiente cliente
                    request_record_for_write(file_entry, record_number, &temp_client);
                    
                    pthread_mutex_lock(&tabla_mutex);
                } else {
                    // No hay más clientes esperando - remover registro
                    remove_record_from_table(file_entry, record_number);
                }
            }
            
            pthread_mutex_unlock(&tabla_mutex);
            
            char *respuesta = "WRITE_RECORD_COMPLETE: Registro guardado exitosamente";
            send(data->client_socket, respuesta, strlen(respuesta), 0);
            return;
        }
        file_entry = file_entry->next;
    }
    
    pthread_mutex_unlock(&tabla_mutex);
    char *respuesta = "ERROR: Archivo no encontrado para write record back";
    send(data->client_socket, respuesta, strlen(respuesta), 0);
}

void send_modified_content_to_server(entrada_tabla_archivo *file_entry, char *content) {
    // Crear nuevo socket para conectar al file server
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server (write back)\n");
        return;
    }
    
    // Configurar dirección del file server
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para guardar archivo modificado\n");
    
    // Conectar al file server
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server para write back\n");
        close(file_server_socket);
        return;
    }
    
    // Crear mensaje para escribir el archivo al file server
    // Formato: "WF filename content"
    char write_msg[MAX_MSG];
    snprintf(write_msg, sizeof(write_msg), "WF %s %s", file_entry->nombre_archivo, content);
    
    printf("Enviando contenido modificado al file server: %s\n", file_entry->nombre_archivo);
    
    // Enviar contenido modificado al file server
    int bytes_sent = send(file_server_socket, write_msg, strlen(write_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando contenido modificado al file server\n");
    } else {
        printf("Contenido modificado enviado exitosamente al file server\n");
    }
    
    close(file_server_socket);
}



void process_next_in_queue(entrada_tabla_archivo *file_entry) {
    // Esta función debe ser llamada con el mutex ya bloqueado
    if (file_entry->cola_espera != NULL && file_entry->lock == 0) {
        // Hay clientes esperando y el archivo no está bloqueado
        file_entry->lock = 1;
        printf("Procesando siguiente cliente en cola para archivo %s\n", file_entry->nombre_archivo);
        
        // Obtener el próximo cliente en la cola y REMOVERLO de la cola
        nodo_espera_t *next_client = file_entry->cola_espera;
        file_entry->cola_espera = next_client->next; // Remover de la cola
        
        // Crear datos_cliente_t temporales para el cliente que espera
        datos_cliente_t temp_client;
        temp_client.client_socket = next_client->client_socket;
        temp_client.client_addr.sin_port = htons(next_client->client_port);
        
        // Liberar el nodo de espera ya que ya no está en la cola
        free(next_client);
        
        // Solicitar archivo para escritura (no para lectura)
        pthread_mutex_unlock(&tabla_mutex); // Desbloquear temporalmente
        request_file_for_write(file_entry, &temp_client);
        pthread_mutex_lock(&tabla_mutex); // Volver a bloquear
    }
}

void register_file_server(uint32_t ip, int file_server_port) {
    pthread_mutex_lock(&file_servers_mutex);
    
    // Verificar si ya existe (misma IP Y mismo puerto)
    registered_file_server_t *current = file_servers;
    while (current != NULL) {
        if (current->ip == ip && current->file_server_port == file_server_port) {
            // Ya existe exactamente el mismo servidor
            pthread_mutex_unlock(&file_servers_mutex);
            printf("File server ya registrado para IP y puerto existentes\n");
            return;
        }
        current = current->next;
    }
    
    // Agregar nuevo file server (permite múltiples puertos por IP)
    registered_file_server_t *new_server = malloc(sizeof(registered_file_server_t));
    if (new_server) {
        new_server->ip = ip;
        new_server->file_server_port = file_server_port;
        new_server->next = file_servers;
        file_servers = new_server;
        printf("Nuevo file server registrado\n");
    } else {
        printf("Error: No se pudo asignar memoria para file server\n");
    }
    
    pthread_mutex_unlock(&file_servers_mutex);
}

int find_file_server_port(uint32_t ip) {
    pthread_mutex_lock(&file_servers_mutex);
    
    registered_file_server_t *current = file_servers;
    while (current != NULL) {
        if (current->ip == ip) {
            int port = current->file_server_port;
            pthread_mutex_unlock(&file_servers_mutex);
            return port;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&file_servers_mutex);
    return -1; // No encontrado
}

int is_record_locked(entrada_tabla_archivo *file_entry, int record_number) {
    if (file_entry->tabla_registros == NULL) {
        return 0; // No hay registros bloqueados
    }
    
    // Verificar si el registro específico está bloqueado
    if (file_entry->tabla_registros->registro == record_number) {
        return 1; // Registro específico está bloqueado
    }
    
    return 0; // Registro no está bloqueado
}

void add_record_to_table(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *client) {
    // Crear nuevo registro en la tabla
    // Por simplicidad, usaremos el primer slot disponible en la tabla_registros
    if (file_entry->tabla_registros == NULL) {
        file_entry->tabla_registros = malloc(sizeof(entrada_tabla_registro));
        if (file_entry->tabla_registros == NULL) {
            printf("Error: No se pudo asignar memoria para tabla de registros\n");
            return;
        }
        file_entry->tabla_registros->registro = record_number;
        file_entry->tabla_registros->lock = 1;
        file_entry->tabla_registros->cola_espera = NULL;
        printf("Registro %d agregado a tabla de registros\n", record_number);
    }
}

void remove_record_from_table(entrada_tabla_archivo *file_entry, int record_number) {
    if (file_entry->tabla_registros != NULL && file_entry->tabla_registros->registro == record_number) {
        // Liberar la cola de espera del registro si existe
        nodo_espera_t *current = file_entry->tabla_registros->cola_espera;
        while (current != NULL) {
            nodo_espera_t *next = current->next;
            free(current);
            current = next;
        }
        
        free(file_entry->tabla_registros);
        file_entry->tabla_registros = NULL;
        printf("Registro %d removido de tabla de registros\n", record_number);
    }
}

void add_to_record_waiting_queue(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *client) {
    if (file_entry->tabla_registros != NULL && file_entry->tabla_registros->registro == record_number) {
        nodo_espera_t *new_node = malloc(sizeof(nodo_espera_t));
        if (!new_node) {
            printf("Error: No se pudo asignar memoria para nodo de espera de registro\n");
            return;
        }
        
        new_node->client_socket = client->client_socket;
        new_node->client_port = ntohs(client->client_addr.sin_port);
        new_node->next = NULL;
        
        // Agregar al final de la cola del registro
        if (file_entry->tabla_registros->cola_espera == NULL) {
            file_entry->tabla_registros->cola_espera = new_node;
        } else {
            nodo_espera_t *current = file_entry->tabla_registros->cola_espera;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = new_node;
        }
        
        printf("Cliente agregado a cola de espera del registro %d\n", record_number);
    }
}

// Función removida - la lógica ahora está integrada en handle_write_record_back

void request_record_for_write(entrada_tabla_archivo *file_entry, int record_number, datos_cliente_t *requesting_client) {
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server (record write)\n");
        char *respuesta = "ERROR: No se pudo crear socket para file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para escritura de registro en IP: %s, Puerto: %d\n", 
           inet_ntoa((struct in_addr){htonl(file_entry->ip)}), file_entry->port);
    
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server para record write\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo conectar al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    char request_msg[MAX_MSG];
    sprintf(request_msg, "SR %s %d", file_entry->nombre_archivo, record_number);
    
    printf("Solicitando registro %d del archivo %s para escritura al file server\n", record_number, file_entry->nombre_archivo);
    
    int bytes_sent = send(file_server_socket, request_msg, strlen(request_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando solicitud al file server\n");
        close(file_server_socket);
        char *respuesta = "ERROR: No se pudo enviar solicitud al file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        return;
    }
    
    printf("Solicitud de escritura de registro enviada al file server, esperando respuesta...\n");
    
    char response[MAX_MSG];
    int bytes_received = recv(file_server_socket, response, MAX_MSG - 1, 0);
    
    close(file_server_socket);
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("Respuesta recibida del file server: %s\n", response);
        
        if (strncmp(response, "RECORD_CONTENT", 14) == 0) {
            // Convertir RECORD_CONTENT a WRITE_RECORD_CONTENT
            char response_to_client[MAX_MSG];
            
            // Parse RECORD_CONTENT:filename:record_number:content
            char *first_colon = strchr(response + 14, ':');
            if (first_colon) {
                char *second_colon = strchr(first_colon + 1, ':');
                if (second_colon) {
                    char *content_start = second_colon + 1;
                    
                    snprintf(response_to_client, sizeof(response_to_client), 
                            "WRITE_RECORD_CONTENT:%s:%d:%s", file_entry->nombre_archivo, record_number, content_start);
                    
                    send(requesting_client->client_socket, response_to_client, strlen(response_to_client), 0);
                    printf("Contenido del registro %d enviado al cliente para edición\n", record_number);
                } else {
                    printf("Error: Formato de respuesta RECORD_CONTENT inválido\n");
                    char *respuesta = "ERROR: Formato de respuesta inválido";
                    send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
                }
            } else {
                printf("Error: Formato de respuesta RECORD_CONTENT inválido\n");
                char *respuesta = "ERROR: Formato de respuesta inválido";
                send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
            }
        } else if (strncmp(response, "RECORD_NOT_FOUND", 16) == 0) {
            printf("File server reporta registro no encontrado\n");
            char *respuesta = "ERROR: Registro no encontrado en el archivo";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else if (strncmp(response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File server reporta archivo no encontrado\n");
            char *respuesta = "ERROR: Archivo no encontrado en file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        } else {
            printf("Respuesta no reconocida del file server: %s\n", response);
            char *respuesta = "ERROR: Respuesta no reconocida del file server";
            send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
        }
    } else {
        printf("Error recibiendo respuesta del file server\n");
        char *respuesta = "ERROR: No se recibió respuesta del file server";
        send(requesting_client->client_socket, respuesta, strlen(respuesta), 0);
    }
}

void send_modified_record_to_server(entrada_tabla_archivo *file_entry, int record_number, char *content) {
    int file_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (file_server_socket < 0) {
        printf("Error creando socket para file server (record write back)\n");
        return;
    }
    
    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = htonl(file_entry->ip);
    file_server_addr.sin_port = htons(file_entry->port);
    
    printf("Conectando a file server para guardar registro modificado\n");
    
    if (connect(file_server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error conectando al file server para record write back\n");
        close(file_server_socket);
        return;
    }
    
    // Crear mensaje para escribir el registro al file server
    // Formato: "WR filename record_number content"
    char write_msg[MAX_MSG];
    snprintf(write_msg, sizeof(write_msg), "WR %s %d %s", file_entry->nombre_archivo, record_number, content);
    
    printf("Enviando registro modificado al file server: %s, registro: %d\n", file_entry->nombre_archivo, record_number);
    printf("DEBUG - Comando completo: %s\n", write_msg);
    
    int bytes_sent = send(file_server_socket, write_msg, strlen(write_msg), 0);
    if (bytes_sent < 0) {
        printf("Error enviando registro modificado al file server\n");
    } else {
        printf("Registro modificado enviado exitosamente al file server\n");
    }
    
    close(file_server_socket);
}

