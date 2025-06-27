# Sistema de Archivos Distribuidos - Protocolo de Comunicación por Sockets

## Arquitectura del Sistema

El sistema de archivos distribuidos consiste en tres componentes principales que se comunican vía sockets TCP:

1. **Servidor de Nombres de Archivos (FNS)** - Servidor central de coordinación
2. **Cliente** - Interfaz de usuario para operaciones de archivos  
3. **Servidor de Archivos** - Componente de almacenamiento (integrado dentro de cada cliente)

## Modelo de Comunicación

### Conexiones Socket

1. **Cliente ↔ FNS**: Conexión TCP persistente iniciada por el cliente
2. **FNS ↔ Servidor de Archivos**: Conexiones TCP bajo demanda iniciadas por el FNS

### Estructura de Mensajes

Todos los mensajes siguen un protocolo basado en prefijos:
- **Mensajes del cliente**: Comienzan con `C ` (operaciones de cliente) o `F ` (operaciones de servidor de archivos)
- **Mensajes del servidor de archivos**: Comandos sin prefijos (SF, SR, WF, WR)

## Protocolo Cliente a FNS

### Configuración de Conexión
```c
// Cliente se conecta al FNS
connect(client_socket, server_addr, sizeof(server_addr))

// Registrarse como servidor de archivos
send(client_socket, "F RS <puerto_servidor_archivos>", ...)
```

### Operaciones del Servidor de Archivos (prefijo F)

#### Registrar Servidor de Archivos
```
Comando: F RS <puerto_servidor_archivos>
Propósito: Registrar cliente como servidor de archivos con puerto específico
Respuesta: REGISTERED_AS_FILE_SERVER
```

#### Agregar Archivo
```
Comando: F AF <nombre_archivo> <puerto_servidor_archivos>
Propósito: Registrar archivo en la tabla de archivos del FNS
Respuesta: OK
```

#### Eliminar Archivo
```
Comando: F RF <nombre_archivo>
Propósito: Eliminar archivo de la tabla de archivos del FNS
Respuesta: ARCHIVO_ELIMINADO | ERROR: <mensaje>
```

### Operaciones del Cliente (prefijo C)

#### Listar Archivos
```
Comando: C LF
Propósito: Obtener lista de todos los archivos registrados
Respuesta: LIST_EMPTY | \nLISTA ARCHIVOS:\n<lista_archivos>
```

#### Leer Archivo
```
Comando: C RF <nombre_archivo>
Propósito: Descargar y leer contenido del archivo
Respuesta: FILE_CONTENT:<nombre_archivo>:<contenido> | NOTFOUND: <mensaje>
```

#### Leer Registro
```
Comando: C RR <nombre_archivo> <numero_registro>
Propósito: Leer registro específico del archivo
Respuesta: RECORD_CONTENT:<nombre_archivo>:<numero_registro>:<contenido> | ERROR: <mensaje>
```

#### Solicitar Escritura de Archivo
```
Comando: C WF <nombre_archivo>
Propósito: Solicitar archivo para edición
Respuesta: WRITE_CONTENT:<nombre_archivo>:<contenido> | WAIT: <mensaje>
```

#### Devolver Archivo Modificado
```
Comando: C WB <nombre_archivo> <contenido>
Propósito: Enviar contenido del archivo modificado de vuelta
Respuesta: WRITE_COMPLETE: <mensaje>
```

#### Solicitar Escritura de Registro
```
Comando: C WR <nombre_archivo> <numero_registro>
Propósito: Solicitar registro específico para edición
Respuesta: WRITE_RECORD_CONTENT:<nombre_archivo>:<numero_registro>:<contenido> | WAIT: <mensaje>
```

#### Devolver Registro Modificado
```
Comando: C WRB <nombre_archivo> <numero_registro> <contenido>
Propósito: Enviar registro modificado de vuelta
Respuesta: WRITE_RECORD_COMPLETE: <mensaje>
```

#### Agregar Registro
```
Comando: C AR <nombre_archivo> <contenido_registro>
Propósito: Agregar un nuevo registro al final del archivo
Respuesta: ADD_RECORD_SUCCESS | ADD_RECORD_ERROR | NOTFOUND: <mensaje>
```

#### Desconectar
```
Comando: C SALIR
Propósito: Desconexión elegante del cliente
Respuesta: DISCONNECT_ACK
```

## Protocolo FNS a Servidor de Archivos

El FNS inicia conexiones a los servidores de archivos cuando se necesitan operaciones de archivos.

### Patrón de Conexión
```c
// FNS se conecta al servidor de archivos
file_server_socket = socket(AF_INET, SOCK_STREAM, 0)
connect(file_server_socket, file_server_addr, sizeof(file_server_addr))
```

### Operaciones de Archivos

#### Enviar Archivo (Solicitud de Lectura)
```
Comando: SF <nombre_archivo>
Propósito: Solicitar contenido del archivo del servidor de archivos
Respuesta: FILE_CONTENT <nombre_archivo> <contenido> | FILE_NOT_FOUND
```

#### Enviar Registro (Solicitud de Lectura de Registro)
```
Comando: SR <nombre_archivo> <numero_registro>
Propósito: Solicitar registro específico del servidor de archivos
Respuesta: RECORD_CONTENT:<nombre_archivo>:<numero_registro>:<contenido> | RECORD_NOT_FOUND | FILE_NOT_FOUND
```

#### Escribir Archivo
```
Comando: WF <nombre_archivo> <contenido>
Propósito: Guardar contenido del archivo modificado en el servidor de archivos
Respuesta: WRITE_SUCCESS | WRITE_ERROR
```

#### Escribir Registro
```
Comando: WR <nombre_archivo> <numero_registro> <contenido>
Propósito: Guardar registro modificado en el servidor de archivos
Respuesta: WRITE_RECORD_SUCCESS | WRITE_RECORD_ERROR
```

#### Agregar Registro
```
Comando: ADD_RECORD:<nombre_archivo>:<contenido>
Propósito: Agregar un nuevo registro al final del archivo en el servidor de archivos
Respuesta: ADD_RECORD_SUCCESS | ADD_RECORD_ERROR
```

## Especificaciones de Formato de Respuesta

### Respuestas de Contenido de Archivo
```
FILE_CONTENT:<nombre_archivo>:<contenido>
WRITE_CONTENT:<nombre_archivo>:<contenido>
```

### Respuestas de Contenido de Registro
```
RECORD_CONTENT:<nombre_archivo>:<numero_registro>:<contenido>
WRITE_RECORD_CONTENT:<nombre_archivo>:<numero_registro>:<contenido>
```

### Respuestas de Estado
```
OK                          - Operación exitosa
WAIT: <mensaje>            - Cliente en cola, conexión permanece abierta
ERROR: <mensaje>           - Operación falló
NOTFOUND: <mensaje>        - Recurso no encontrado
REGISTERED_AS_FILE_SERVER  - Registro de servidor de archivos completo
WRITE_COMPLETE: <mensaje>  - Operación de escritura completada
ARCHIVO_ELIMINADO          - Archivo eliminado exitosamente
ADD_RECORD_SUCCESS         - Registro agregado exitosamente
ADD_RECORD_ERROR           - Error al agregar registro
```

## Protocolo de Concurrencia y Bloqueos

### Bloqueo a Nivel de Archivo
- Los archivos se bloquean para acceso exclusivo de escritura
- Se permiten múltiples lecturas concurrentes
- Las operaciones de escritura se bloquean hasta que el archivo esté disponible

### Bloqueo a Nivel de Registro
- Los registros individuales pueden ser bloqueados independientemente
- El bloqueo a nivel de archivo tiene precedencia sobre los bloqueos de registro
- Los bloqueos de registro previenen conflictos en el mismo número de registro

### Sistema de Colas
- Los clientes esperando recursos bloqueados son encolados
- La conexión permanece abierta durante la espera
- Procesamiento FIFO cuando los recursos se vuelven disponibles

## Detalles de Implementación de Sockets

### Tamaño de Buffer
```c
#define MAX_MSG 1024
```

### Opciones de Socket
```c
int opt = 1;
setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
```

### Modelo de Hilos
- FNS: Un hilo por conexión de cliente
- Cliente: Hilos separados para interfaz de cliente y servidor de archivos
- Servidor de Archivos: hilos para manejar solicitudes del FNS

## Ejemplo de Flujo de Comunicación

### Operación de Lectura de Archivo
```
1. Cliente -> FNS: "C RF ejemplo.txt"
2. FNS -> Servidor de Archivos: "SF ejemplo.txt"
3. Servidor de Archivos -> FNS: "FILE_CONTENT ejemplo.txt Hola Mundo"
4. FNS -> Cliente: "FILE_CONTENT:ejemplo.txt:Hola Mundo"
```

### Operación de Escritura de Archivo
```
1. Cliente -> FNS: "C WF ejemplo.txt"
2. FNS bloquea archivo y encola solicitud
3. FNS -> Servidor de Archivos: "SF ejemplo.txt"
4. Servidor de Archivos -> FNS: "FILE_CONTENT ejemplo.txt Hola Mundo"
5. FNS -> Cliente: "WRITE_CONTENT:ejemplo.txt:Hola Mundo"
6. [Cliente edita contenido]
7. Cliente -> FNS: "C WB ejemplo.txt Hola Mundo Modificado"
8. FNS -> Servidor de Archivos: "WF ejemplo.txt Hola Mundo Modificado"
9. Servidor de Archivos -> FNS: "WRITE_SUCCESS"
10. FNS desbloquea archivo y procesa cola
11. FNS -> Cliente: "WRITE_COMPLETE: Archivo guardado exitosamente"
```

### Operación de Agregar Registro
```
1. Cliente -> FNS: "C AR ejemplo.txt Nuevo contenido del registro"
2. FNS -> Servidor de Archivos: "ADD_RECORD:ejemplo.txt:Nuevo contenido del registro"
3. Servidor de Archivos -> FNS: "ADD_RECORD_SUCCESS"
4. FNS -> Cliente: "ADD_RECORD_SUCCESS"
```
