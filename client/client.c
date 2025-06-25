#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#define MAX_MSG 1024

typedef struct {
    int client_socket;  // Socket for client requests
    int server_socket;  // Socket for file server functionality
    char* server_ip;
    int server_port;
    int client_port;        // Port for client connections
    int file_server_port;   // Port where this client acts as file server
} client_state_t;

client_state_t client_state = {0};
char last_requested_file[256] = "";

void print_menu();
void *file_server_listener(void *arg);
void *handle_fns_request(void *arg);
void *client_interface_thread(void *arg);
void send_client_request(char *message);
void send_write_request(char *message);
void upload_all_files();
void upload_file();
void remove_file();
void list_files();
void read_file();
void read_record();
void write_file();
void upload(char *filename);

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Uso: %s <puerto_file_server> <puerto_cliente> <ip_servidor> <puerto_servidor>\n", argv[0]);
        exit(1);
    }

    client_state.file_server_port = atoi(argv[1]);
    client_state.client_port = atoi(argv[2]);
    client_state.server_ip = argv[3];
    client_state.server_port = atoi(argv[4]);

    if (client_state.server_port <= 0 || client_state.file_server_port <= 0 || client_state.client_port <= 0) {
        printf("Puerto invalido\n");
        exit(1);
    }

    printf("=== SISTEMA DE ARCHIVOS DISTRIBUIDOS ===\n");
    printf("File Server Port: %d\n", client_state.file_server_port);
    printf("Client Port: %d\n", client_state.client_port);
    printf("FNS Address: %s:%d\n", client_state.server_ip, client_state.server_port);
    printf("========================================\n\n");

    // 1. Setup file server socket (for listening to FNS requests)
    client_state.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_state.server_socket < 0) {
        printf("Error al crear socket file server\n");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(client_state.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt fallo");
        close(client_state.server_socket);
        exit(1);
    }

    struct sockaddr_in file_server_addr;
    memset(&file_server_addr, 0, sizeof(file_server_addr));
    file_server_addr.sin_family = AF_INET;
    file_server_addr.sin_addr.s_addr = INADDR_ANY;
    file_server_addr.sin_port = htons(client_state.file_server_port);
    
    if (bind(client_state.server_socket, (struct sockaddr*)&file_server_addr, sizeof(file_server_addr)) < 0) {
        printf("Error al hacer bind en puerto file server %d\n", client_state.file_server_port);
        close(client_state.server_socket);
        exit(1);
    }

    if (listen(client_state.server_socket, 5) < 0) {
        printf("Error listen socket file server\n");
        close(client_state.server_socket);
        exit(1);
    }

    printf("File server escuchando en puerto %d\n", client_state.file_server_port);

    // 2. Start file server listener thread
    pthread_t file_server_thread;
    if (pthread_create(&file_server_thread, NULL, file_server_listener, NULL) != 0) {
        printf("Error al crear hilo file server\n");
        close(client_state.server_socket);
        exit(1);
    }

    // 3. Start client interface thread
    pthread_t client_thread;
    if (pthread_create(&client_thread, NULL, client_interface_thread, NULL) != 0) {
        printf("Error al crear hilo cliente\n");
        close(client_state.server_socket);
        pthread_cancel(file_server_thread);
        exit(1);
    }

    printf("Sistema iniciado. File server y cliente funcionando...\n\n");

    // Wait for client thread to finish (when user exits)
    pthread_join(client_thread, NULL);
    
    // Cleanup
    printf("Cerrando sistema...\n");
    pthread_cancel(file_server_thread);
    close(client_state.client_socket);
    close(client_state.server_socket);
    
    return 0;
}

void *client_interface_thread(void *arg) {
    char opcion[10];
    char mensaje[MAX_MSG];
    
    // Setup client socket (for sending requests to FNS)
    client_state.client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_state.client_socket < 0) {
        printf("Error al crear socket cliente\n");
        pthread_exit(NULL);
    }

    // Bind to specified client port
    int opt = 1;
    if (setsockopt(client_state.client_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt cliente fallo");
        close(client_state.client_socket);
        pthread_exit(NULL);
    }

    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(client_state.client_port);
    
    if (bind(client_state.client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        printf("Error al hacer bind en puerto cliente %d\n", client_state.client_port);
        close(client_state.client_socket);
        pthread_exit(NULL);
    }

    // Connect to FNS
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client_state.server_port);
    
    if (inet_pton(AF_INET, client_state.server_ip, &server_addr.sin_addr) <= 0) {
        printf("Dir IP invalida: %s\n", client_state.server_ip);
        close(client_state.client_socket);
        pthread_exit(NULL);
    }
    
    if (connect(client_state.client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error al conectarse con el servidor\n");
        close(client_state.client_socket);
        pthread_exit(NULL);
    }

    printf("Cliente conectado al FNS en %s:%d desde puerto %d\n", 
           client_state.server_ip, client_state.server_port, client_state.client_port);

    // Register as file server with FNS
    sprintf(mensaje, "F RS %d", client_state.file_server_port);
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
                upload_all_files();
                break;
            case 2:
                upload_file();
                break;
            case 3:
                remove_file();
                break;
            case 4:
                list_files();
                break;
            case 5:
                read_file();
                break;
            case 6:
                write_file();
                break;
            case 7:
                read_record();
                break;
            default:
                printf("Opcion no implementada\n");
                continue;
        }
        print_menu();
    }

    close(client_state.client_socket);
    pthread_exit(NULL);
}

void send_client_request(char *message) {
    printf("Enviando mensaje: %s\n", message);
    send(client_state.client_socket, message, strlen(message), 0);
    
    // Receive response
    char respuesta[MAX_MSG];
    int bytes = recv(client_state.client_socket, respuesta, MAX_MSG - 1, 0);
    if (bytes > 0) {
        respuesta[bytes] = '\0';
        
        // Process the response
        if (strncmp(respuesta, "FILE_CONTENT:", 13) == 0) {
            // Parse FILE_CONTENT:filename:content
            char received_filename[256];
            char *content_start = NULL;
            
            // Find the second colon to separate filename from content
            char *first_colon = strchr(respuesta + 13, ':');
            if (first_colon) {
                *first_colon = '\0';  // Temporarily null-terminate filename
                strncpy(received_filename, respuesta + 13, sizeof(received_filename) - 1);
                received_filename[sizeof(received_filename) - 1] = '\0';
                *first_colon = ':';  // Restore the colon
                content_start = first_colon + 1;
                
                // Save file to client directory
                FILE *file = fopen(received_filename, "w");
                if (file) {
                    fprintf(file, "%s", content_start);
                    fclose(file);
                    printf("Archivo %s guardado exitosamente\n", received_filename);
                    
                    // Display the content
                    printf("\n=== CONTENIDO DEL ARCHIVO: %s ===\n", received_filename);
                    printf("%s", content_start);
                    if (content_start[strlen(content_start) - 1] != '\n') {
                        printf("\n");  // Add newline if content doesn't end with one
                    }
                    printf("================================\n");
                } else {
                    printf("Error: No se pudo guardar el archivo %s\n", received_filename);
                }
            } else {
                printf("Error: Formato de respuesta FILE_CONTENT inválido\n");
            }
        } else if (strncmp(respuesta, "RECORD_CONTENT:", 15) == 0) {
            char received_filename[256];
            int record_number;
            char *content_start = NULL;
            
            char *first_colon = strchr(respuesta + 15, ':');
            if (first_colon) {
                *first_colon = '\0';
                strncpy(received_filename, respuesta + 15, sizeof(received_filename) - 1);
                received_filename[sizeof(received_filename) - 1] = '\0';
                *first_colon = ':';
                
                char *second_colon = strchr(first_colon + 1, ':');
                if (second_colon) {
                    record_number = atoi(first_colon + 1);
                    content_start = second_colon + 1;
                    
                    printf("\n=== REGISTRO %d DEL ARCHIVO: %s ===\n", record_number, received_filename);
                    printf("%s", content_start);
                    if (content_start[strlen(content_start) - 1] != '\n') {
                        printf("\n");
                    }
                    printf("=====================================\n");
                } else {
                    printf("Error: Formato de respuesta RECORD_CONTENT inválido (falta segundo :)\n");
                }
            } else {
                printf("Error: Formato de respuesta RECORD_CONTENT inválido (falta primer :)\n");
            }
        } else if (strncmp(respuesta, "FILE_READY:", 11) == 0) {
            printf("Archivo descargado exitosamente\n");
            
            // Print the content of the downloaded file
            if (strlen(last_requested_file) > 0) {
                FILE *file = fopen(last_requested_file, "r");
                if (file) {
                    printf("\n=== CONTENIDO DEL ARCHIVO: %s ===\n", last_requested_file);
                    char line[1024];
                    while (fgets(line, sizeof(line), file)) {
                        printf("%s", line);
                    }
                    printf("================================\n");
                    fclose(file);
                } else {
                    printf("Error: No se pudo abrir el archivo %s para mostrar contenido\n", last_requested_file);
                }
            }
        } else if (strncmp(respuesta, "WAIT:", 5) == 0) {
            printf("%s\n", respuesta + 6); // Skip "WAIT: "
        } else if (strncmp(respuesta, "NOTFOUND:", 9) == 0) {
            printf("%s\n", respuesta + 10); // Skip "NOTFOUND: "
        } else if (strncmp(respuesta, "ERROR:", 6) == 0) {
            printf("%s\n", respuesta + 7); // Skip "ERROR: "
        } else if (strncmp(respuesta, "ARCHIVO_ELIMINADO", 17) == 0) {
            printf("Archivo eliminado exitosamente del sistema\n");
        } else if (strncmp(respuesta, "\nLISTA ARCHIVOS:", 16) == 0) {
            printf("%s\n", respuesta);
        } else {
            printf("Respuesta del servidor: %s\n", respuesta);
        }
    }
}

void *file_server_listener(void *arg) {
    while (1) {
        struct sockaddr_in fns_addr;
        socklen_t fns_len = sizeof(fns_addr);
        
        int fns_socket = accept(client_state.server_socket, (struct sockaddr*)&fns_addr, &fns_len);
        if (fns_socket < 0) {
            printf("Error al aceptar conexion FNS\n");
            continue;
        }
        
        char fns_ip[16];
        inet_ntop(AF_INET, &fns_addr.sin_addr, fns_ip, sizeof(fns_ip));
        printf("\n======= FILE SERVER =======\n");
        printf("FNS conectado desde %s:%d para solicitar archivo\n", 
               fns_ip, ntohs(fns_addr.sin_port));
        
        // Create thread to handle FNS request
        pthread_t request_thread;
        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = fns_socket;
        
        if (pthread_create(&request_thread, NULL, handle_fns_request, socket_ptr) != 0) {
            printf("Error al crear hilo para manejar solicitud FNS\n");
            close(fns_socket);
            free(socket_ptr);
            continue;
        }
        
        pthread_detach(request_thread);
    }
    return NULL;
}

void *handle_fns_request(void *arg) {
    int fns_socket = *(int*)arg;
    free(arg);
    
    char buffer[MAX_MSG];
    int bytes = recv(fns_socket, buffer, MAX_MSG - 1, 0);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Solicitud del FNS: %s\n", buffer);
        
        if (strncmp(buffer, "SF", 2) == 0) {
            // Send File - leer archivo y enviarlo
            char filename[256];
            sscanf(buffer, "SF %s", filename);
            
            // Construir la ruta completa del archivo en el directorio archivos
            char filepath[512];
            sprintf(filepath, "archivos/%s", filename);
            
            printf("Buscando archivo: %s\n", filepath);
            
            FILE *file = fopen(filepath, "r");
            if (file) {
                char file_content[MAX_MSG];
                size_t read_size = fread(file_content, 1, MAX_MSG - 1, file);
                file_content[read_size] = '\0';
                fclose(file);
                
                char response[MAX_MSG];
                sprintf(response, "FILE_CONTENT %s %s", filename, file_content);
                send(fns_socket, response, strlen(response), 0);
                printf("Enviado contenido del archivo %s al FNS\n", filename);
            } else {
                send(fns_socket, "FILE_NOT_FOUND", 14, 0);
                printf("Archivo %s no encontrado\n", filename);
            }
            printf("====================================\n");
        } else if (strncmp(buffer, "SR", 2) == 0) {
            char filename[256];
            int record_number;
            
            if (sscanf(buffer, "SR %255s %d", filename, &record_number) != 2) {
                send(fns_socket, "RECORD_ERROR", 12, 0);
                printf("Error: Formato inválido en comando SR\n");
                printf("====================================\n");
                return NULL;
            }
            
            char filepath[512];
            sprintf(filepath, "archivos/%s", filename);
            
            printf("Buscando registro %d del archivo: %s\n", record_number, filepath);
            
            FILE *file = fopen(filepath, "r");
            if (file) {
                char line[MAX_MSG];
                int current_line = 1;
                int found = 0;
                
                while (fgets(line, sizeof(line), file) && current_line <= record_number) {
                    if (current_line == record_number) {
                        found = 1;
                        
                        size_t len = strlen(line);
                        if (len > 0 && line[len-1] == '\n') {
                            line[len-1] = '\0';
                        }
                        
                        char response[MAX_MSG];
                        sprintf(response, "RECORD_CONTENT:%s:%d:%s", filename, record_number, line);
                        send(fns_socket, response, strlen(response), 0);
                        printf("Enviado registro %d del archivo %s al FNS\n", record_number, filename);
                        break;
                    }
                    current_line++;
                }
                fclose(file);
                
                if (!found) {
                    send(fns_socket, "RECORD_NOT_FOUND", 16, 0);
                    printf("Registro %d no encontrado en archivo %s\n", record_number, filename);
                }
            } else {
                send(fns_socket, "FILE_NOT_FOUND", 14, 0);
                printf("Archivo %s no encontrado\n", filename);
            }
            printf("====================================\n");
        } else if (strncmp(buffer, "WF", 2) == 0) {
            // Write File - recibir contenido y escribir archivo
            char filename[256];
            char *content_start = strstr(buffer, " ");
            if (content_start) {
                content_start++; // Skip first space
                char *second_space = strstr(content_start, " ");
                if (second_space) {
                    // Extract filename
                    int filename_len = second_space - content_start;
                    strncpy(filename, content_start, filename_len);
                    filename[filename_len] = '\0';
                    
                    // Get content
                    char *file_content = second_space + 1;
                    
                    // Construir la ruta completa del archivo en el directorio archivos
                    char filepath[512];
                    sprintf(filepath, "archivos/%s", filename);
                    
                    printf("Escribiendo archivo: %s\n", filepath);
                    
                    FILE *file = fopen(filepath, "w");
                    if (file) {
                        fprintf(file, "%s", file_content);
                        fclose(file);
                        
                        send(fns_socket, "WRITE_SUCCESS", 13, 0);
                        printf("Archivo %s escrito exitosamente\n", filename);
                    } else {
                        send(fns_socket, "WRITE_ERROR", 11, 0);
                        printf("Error escribiendo archivo %s\n", filename);
                    }
                    printf("====================================\n");
                } else {
                    send(fns_socket, "WRITE_ERROR", 11, 0);
                    printf("Error: Formato inválido en comando WF\n");
                    printf("====================================\n");
                }
            } else {
                send(fns_socket, "WRITE_ERROR", 11, 0);
                printf("Error: Formato inválido en comando WF\n");
                printf("====================================\n");
            }
        }
    }
    
    close(fns_socket);
    return NULL;
}

void print_menu() {
    printf("\n=== SISTEMA DE ARCHIVOS DISTRIBUIDOS ===\n");
    printf("0. Salir\n");
    printf("1. Subir todos los archivos a FNS\n");
    printf("2. Subir archivo\n");
    printf("3. Eliminar archivo\n");
    printf("4. Listar archivos disponibles\n");
    printf("5. Leer y descargar archivo\n");
    printf("6. Escribir archivo\n");
    printf("7. Leer registro\n");
    printf("8. Escribir registro\n");
}

void upload(char *filename) {
    char filepath[512];
    sprintf(filepath, "archivos/%s", filename);
    
    // Verificar que el archivo existe antes de registrarlo
    FILE *file = fopen(filepath, "r");
    if (file) {
        fclose(file);
        
        // Enviar solo el nombre del archivo para registrarlo
        char command[MAX_MSG];
        sprintf(command, "F AF %s", filename);
        send_client_request(command);
        
        printf("Archivo %s registrado exitosamente en FNS\n", filename);
    } else {
        printf("No se pudo abrir el archivo %s\n", filename);
    }
}

void upload_all_files() {
    printf("Subiendo todos los archivos a FNS\n");
    DIR *dir = opendir("archivos");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
                char filename[256];
                snprintf(filename, sizeof(filename), "%s", entry->d_name);
                upload(filename);
            }
        }
        closedir(dir);
    }
}

void upload_file() {
    char filename[256];
    printf("Ingrese el nombre del archivo a subir (con extension): ");
    scanf("%s", filename);
    
    // Limpiar el buffer de entrada
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    upload(filename);
}

void remove_file() {
    char filename[256];
    printf("Ingrese el nombre del archivo a eliminar: ");
    scanf("%s", filename);
    
    // Limpiar el buffer de entrada
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    char command[MAX_MSG];
    sprintf(command, "F RF %s", filename);
    send_client_request(command);
}

void list_files() {
    printf("Solicitando lista de archivos al FNS...\n");
    char command[MAX_MSG];
    sprintf(command, "C LF");
    send_client_request(command);
}

void read_file() {
    char filename[256];
    printf("Ingrese el nombre del archivo a leer: ");
    scanf("%s", filename);
    
    // Limpiar el buffer de entrada
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    // Store the filename for later use when displaying content
    strncpy(last_requested_file, filename, sizeof(last_requested_file) - 1);
    last_requested_file[sizeof(last_requested_file) - 1] = '\0';
    
    char command[MAX_MSG];
    sprintf(command, "C RF %s", filename);
    send_client_request(command);
}

void read_record() {
    char filename[256];
    int record_number;
    
    printf("Ingrese el nombre del archivo: ");
    scanf("%s", filename);
    
    printf("Ingrese el número de registro a leer: ");
    scanf("%d", &record_number);
    
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    if (record_number <= 0) {
        printf("Error: El número de registro debe ser mayor a 0\n");
        print_menu();
        return;
    }
    
    strncpy(last_requested_file, filename, sizeof(last_requested_file) - 1);
    last_requested_file[sizeof(last_requested_file) - 1] = '\0';
    
    char command[MAX_MSG];
    sprintf(command, "C RR %s %d", filename, record_number);
    send_client_request(command);
}

void write_file() {
    char filename[256];
    printf("Ingrese el nombre del archivo a escribir: ");
    scanf("%s", filename);

    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    strncpy(last_requested_file, filename, sizeof(last_requested_file) - 1);
    last_requested_file[sizeof(last_requested_file) - 1] = '\0';
    
    char command[MAX_MSG];
    sprintf(command, "C WF %s", filename);
    send_write_request(command);
}

void send_write_request(char *message) {
    printf("Enviando solicitud de escritura: %s\n", message);
    send(client_state.client_socket, message, strlen(message), 0);
    
    char respuesta[MAX_MSG];
    int bytes = recv(client_state.client_socket, respuesta, MAX_MSG - 1, 0);
    if (bytes > 0) {
        respuesta[bytes] = '\0';
        
        if (strncmp(respuesta, "WRITE_CONTENT:", 14) == 0) {
            char received_filename[256];
            char *content_start = NULL;
            
            char *first_colon = strchr(respuesta + 14, ':');
            if (first_colon) {
                *first_colon = '\0';
                strncpy(received_filename, respuesta + 14, sizeof(received_filename) - 1);
                received_filename[sizeof(received_filename) - 1] = '\0';
                *first_colon = ':';
                content_start = first_colon + 1;
                
                char temp_filename[300];
                sprintf(temp_filename, "%s.tmp", received_filename);
                
                FILE *temp_file = fopen(temp_filename, "w");
                if (temp_file) {
                    fprintf(temp_file, "%s", content_start);
                    fclose(temp_file);
                    
                    printf("Abriendo editor para %s...\n", received_filename);
                    printf("Contenido actual:\n");
                    printf("====================\n");
                    printf("%s", content_start);
                    printf("\n====================\n");
                    
                    char editor_command[400];
                    sprintf(editor_command, "vi %s", temp_filename);
                    int result = system(editor_command);
                    
                    if (result == 0) {
                        FILE *modified_file = fopen(temp_filename, "r");
                        if (modified_file) {
                            char modified_content[MAX_MSG];
                            size_t read_size = fread(modified_content, 1, MAX_MSG - 1, modified_file);
                            modified_content[read_size] = '\0';
                            fclose(modified_file);
                            
                            char write_back_command[MAX_MSG];
                            sprintf(write_back_command, "C WB %s %s", received_filename, modified_content);
                            
                            printf("Enviando contenido modificado...\n");
                            send(client_state.client_socket, write_back_command, strlen(write_back_command), 0);
                            
                            char confirm_response[MAX_MSG];
                            int confirm_bytes = recv(client_state.client_socket, confirm_response, MAX_MSG - 1, 0);
                            if (confirm_bytes > 0) {
                                confirm_response[confirm_bytes] = '\0';
                                printf("Respuesta del servidor: %s\n", confirm_response);
                            }
                            
                            unlink(temp_filename);
                        } else {
                            printf("Error: No se pudo leer el archivo modificado\n");
                        }
                    } else {
                        printf("Error: Editor cerrado sin guardar o con error\n");
                        unlink(temp_filename);
                    }
                } else {
                    printf("Error: No se pudo crear archivo temporal\n");
                }
            } else {
                printf("Error: Formato de respuesta WRITE_CONTENT inválido\n");
            }
        } else if (strncmp(respuesta, "WAIT:", 5) == 0) {
            printf("%s\n", respuesta + 6); // Skip "WAIT: "
            
            printf("Esperando en cola... manteniendo conexión abierta\n");
            int wait_bytes = recv(client_state.client_socket, respuesta, MAX_MSG - 1, 0);
            if (wait_bytes > 0) {
                respuesta[wait_bytes] = '\0';
                if (strncmp(respuesta, "WRITE_CONTENT:", 14) == 0) {
                    char received_filename[256];
                    char *content_start = NULL;
                    
                    char *first_colon = strchr(respuesta + 14, ':');
                    if (first_colon) {
                        *first_colon = '\0';
                        strncpy(received_filename, respuesta + 14, sizeof(received_filename) - 1);
                        received_filename[sizeof(received_filename) - 1] = '\0';
                        *first_colon = ':';
                        content_start = first_colon + 1;
                        
                        char temp_filename[300];
                        sprintf(temp_filename, "%s.tmp", received_filename);
                        
                        FILE *temp_file = fopen(temp_filename, "w");
                        if (temp_file) {
                            fprintf(temp_file, "%s", content_start);
                            fclose(temp_file);
                            
                            printf("Archivo disponible! Abriendo editor para %s...\n", received_filename);
                            printf("Contenido actual:\n");
                            printf("====================\n");
                            printf("%s", content_start);
                            printf("\n====================\n");
                            
                            char editor_command[400];
                            sprintf(editor_command, "vi %s", temp_filename);
                            int result = system(editor_command);
                            
                            if (result == 0) {
                                FILE *modified_file = fopen(temp_filename, "r");
                                if (modified_file) {
                                    char modified_content[MAX_MSG];
                                    size_t read_size = fread(modified_content, 1, MAX_MSG - 1, modified_file);
                                    modified_content[read_size] = '\0';
                                    fclose(modified_file);
                                    
                                    char write_back_command[MAX_MSG];
                                    sprintf(write_back_command, "C WB %s %s", received_filename, modified_content);
                                    
                                    printf("Enviando contenido modificado...\n");
                                    send(client_state.client_socket, write_back_command, strlen(write_back_command), 0);
                                    
                                    char confirm_response[MAX_MSG];
                                    int confirm_bytes = recv(client_state.client_socket, confirm_response, MAX_MSG - 1, 0);
                                    if (confirm_bytes > 0) {
                                        confirm_response[confirm_bytes] = '\0';
                                        printf("Respuesta del servidor: %s\n", confirm_response);
                                    }
                                    
                                    unlink(temp_filename);
                                } else {
                                    printf("Error: No se pudo leer el archivo modificado\n");
                                }
                            } else {
                                printf("Error: Editor cerrado sin guardar o con error\n");
                                unlink(temp_filename);
                            }
                        } else {
                            printf("Error: No se pudo crear archivo temporal\n");
                        }
                    } else {
                        printf("Error: Formato de respuesta WRITE_CONTENT inválido\n");
                    }
                } else {
                    printf("Respuesta inesperada mientras esperaba: %s\n", respuesta);
                }
            } else {
                printf("Error: Conexión perdida mientras esperaba en cola\n");
            }
        } else if (strncmp(respuesta, "NOTFOUND:", 9) == 0) {
            printf("%s\n", respuesta + 10); // Skip "NOTFOUND: "
        } else if (strncmp(respuesta, "ERROR:", 6) == 0) {
            printf("%s\n", respuesta + 7); // Skip "ERROR: "
        } else {
            printf("Respuesta del servidor: %s\n", respuesta);
        }
    }
}