# sistema-archivos-distribuidos

## Concepto

Sistema de archivos distribuidos compuesto por dos tipos de actores:

- FNS - File Name System - Sistema de nombres de archivos
- Cliente - Servidor de archivos

El FNS contendra una tabla de archivos con:

- Nombre de archivo (con extension)
- Lock (1 sí archivo esta bloqueado)
- Cola de espera (primera posición estará el que lo tiene en uso o NULL)
- Tabla de registros (puede ser NULL, tabla donde llegar actividad de registros - bloqueos especificos)

Cada tabla de archivos tendrá una tabla de registros:

- Nro de registro
- Lock
- Cola de espera

El sistema está centralizado en el FNS, si un actor quiere editar un archivo, este deberá hacer una petición al FNS, el cual se lo otorgara en caso de estar desbloqueado, registrando el actor en la cola de espera, sino lo pondra en espera.
Siempre para modificar o leer un archivo, el cliente debe hacer una petición al FNS. Debido a esto y a que cada cliente es a su vez cliente y servidor de archivos, para modificar o leer un archivo propio mediante el sistema, debe hacer una petición al FNS.

El cliente tendrá una carpeta archivos en el mismo directorio donde se ejecuta el proceso cliente, el cual contendra todos los archivos que da a disposición mediante el FNS.

El sistema también debe contemplar la modificación y lectura de "registros" dentro de un archivo, los cuales serán las filas de un archivo.

## Consideraciones importantes

Al cancelar un cliente con CTRL+C se debe enviar una petición al FNS para dar de baja servidor, es decir todos los archivos que tengan la IP y puerto del servidor que envia la petición deben ser eliminados, por más que estén bloqueados o no y enviar un aviso a los servidores que esten editando, leyendo o esperando el archivo. Esta función también debe ejecutarse en la opción 0 del menu.

Cuando se bloquea el registro de un archivo, se bloquea el archivo completo, por lo que lock debe estar en 1, si se solicitar bloquear un registro se busca si esta bloqueado por más que lock esté en 1.

## Menu cliente

0. Salir
1. Subir todos los archivos a FNS (los archivos de ./archivos)
2. Subir archivo (nombre completo)
3. Bajar archivo (nombre completo)
4. Listar archivos disponibles (de FNS, los bloqueados y no bloqueados)
5. Leer archivo (solicitar lectura "remota", abrir archivo con vi)
6. Escribir archivo (abrir archivo con vi, al guardar enviar archivo modificado)
7. Descargar archivo (guarda archivo en descargas)
8. Leer registro (nombre archivo + nro registro de 0 a n)
9. Escribir registro (nombre archivo + nro registro de 0 a n)

opcionales:

- Crear archivo remoto (FNS decide donde se almacena en base a carga de cada cliente)

## Comandos cliente a FNS

Lista de comandos que se envian al inicio de un mensaje utilizando sockets desde el cliente hacia el FNS.
Los comandos que empiezan con 0 son cuando actua como servidor de archivos, los que empiezan con 1 son cuando actua como cliente.
Con esto en el FNS se puede hacer el siguiente manejo de solicitudes

``` C
if (primer_caracter == '0') {
    handleFileServer()
} else {
    handleClient()
}
```

### Comandos como File Server

- SUBIR texto.txt contenido - 00 texto.txt contenido (subir nuevo o sobreescribir archivo actuando como )
- DOWN texto.txt - 01 texto.txt

### Comandos como cliente

- LISTAR - 10 (retorna tabla de archivos)
- LEER texto.txt - 11 texto.txt
- LEER_BLOQUEAR texto.txt - 12 texto.txt (para escribir archivos, primero bloqueo, luego de obtener archivo, hago escritura)
- ESCRIBIR texto.txt contenido - 13 texto.txt contenido
- LEER_REGISTRO texto.txt nro_registro - 14 texto.txt nro
- LEER_BLOQUEAR_REGISTRO texto.txt nro_Registro - 15 texto.txt nro
- ESCRIBIR_REGISTRO texto.txt nro_registro contenido - 16 texto.txt nro contenido

## Comandos FNS a cliente

Lista de comandos que envia FNS a cliente, para eso el cliente debe tener un puerto escuchando peticiones, que será el mismo que se almacene en la tabla de archivos
(cuando se envia contenido solicitado como en LISTAR no se envia)

Si bien el FNS le envia a un mismo ip y socket, el contexto puede variar entre si el FNS le solicita o responde algo como si este fuera Cliente o File server

Si comando empieza con 0 -> le esta enviando al "File server"
Si comando empieza con 1 -> le esta enviando al "Cliente"

### Comandos a File Server

- OK - 00 (archivo subido o bajado correctamente) (opcional)
- LEER texto.txt - 01 texto.txt (cuando le solicitan a FNS leer archivo)
- ESCRIBIR texto.txt contenido - 02 texto.txt contenido (sobreescribir archivo por nuevo contendio)
- LEER_REGISTRO texto.txt nro - 03 texto.txt nro
- ESCRIBIR_REGISTRO texto.txt nro contenido - 04 texto.txt nro contenido

### Comandos a cliente (respuestas a solicitudes)

En los casos de espera, el cliente ya ha sido agregado a la cola de espera del archivo o registro.

- DAR_ARCHIVO texto.txt contenido - 10 texto.txt contenido (luego de solicitar leer, leer bloquear o escribir)
- ESPERAR_ARCHIVO texto.txt - 11 texto.txt (luego de solicitar leer bloquear señal de espera para recibir mensaje de desbloqueado que es el DAR_ARCHIVO)
- DAR_REGISTRO texto.txt nro_registro contenido - 12 texto.txt nro contenido
- ESPERAR_REGISTRO texto.txt nro - 13 texto.txt nro
