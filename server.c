#include <stdio.h>       
#include <stdlib.h>      
#include <string.h>      
#include <unistd.h>    
#include <pthread.h>    
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netdb.h>

#define RUTA_CARPETA "user_files/"

pthread_mutex_t mutex_funciones = PTHREAD_MUTEX_INITIALIZER;

/* Estructura que almacena la información relevante de un usuario */
typedef struct {
    char nombre_usuario[256];
    char ip_usuario[64];
    int puerto_usuario;
    int usuario_conectado; /* 0 = desconectado, 1 = conectado */
    unsigned int id_ultimo_mensaje;
} InfoUsuario;

/* Estructura en memoria para usuarios conectados. Lista enlazada */
typedef struct Conectado {
    char nombre_usuario[256];
    char ip_usuario[64];
    int puerto_usuario;
    struct Conectado *siguiente;
} Conectado;

/* Estructura para pasarle al hilo que atiende la peticion el socket y la IP*/
typedef struct {
    int socket;
    char ip[64];
} ArgumentosHilo;

Conectado *lista_conectados = NULL;
pthread_mutex_t mutex_conectados = PTHREAD_MUTEX_INITIALIZER;

/* Función que llema al servidor dfe logs*/
int call_rpc_log(char *user, char *op, char *filename);

/* Funciones auxiliares */

/* Elimina un mensaje pendiente por su id del fichero de pendientes */
static void eliminarMensajePendiente(const char *destinatario, unsigned int id_mensaje) {
    char ruta_pendientes[512];
    snprintf(ruta_pendientes, sizeof(ruta_pendientes), "%s%s_mensajes.txt", RUTA_CARPETA, destinatario);
    
    char ruta_temp[512];
    snprintf(ruta_temp, sizeof(ruta_temp), "%s%s_mensajes_tmp.txt", RUTA_CARPETA, destinatario);

    FILE *f_in = fopen(ruta_pendientes, "r");
    if (!f_in){ 
        return;
    }

    FILE *f_out = fopen(ruta_temp, "w");
    if (!f_out) { 
        fclose(f_in); 
        return; 
    }

    char linea[600];
    while (fgets(linea, sizeof(linea), f_in)) {
        unsigned int id;
        if (sscanf(linea, "%u|", &id) == 1 && id == id_mensaje) {
            continue; /* Saltar este mensaje (ya entregado) */
        }
        fputs(linea, f_out);
    }
    fclose(f_in);
    fclose(f_out);

    remove(ruta_pendientes);
    rename(ruta_temp, ruta_pendientes);
}


/* Lee un string que recibe del socket */
static int leerString(int socket, char *buffer, size_t longitud_buffer) {
    size_t i = 0;
    char caracter_leido;
    while (i < longitud_buffer - 1) {
        ssize_t n_bytes_leidos = read(socket, &caracter_leido, 1);
        if (n_bytes_leidos <= 0 || caracter_leido == '\0') {
            break;
        }
        buffer[i++] = caracter_leido;
    }
    buffer[i] = '\0';
    return i;
}

/* Envía un byte al socket como respuesta para el cliente */
static int enviarByte(int socket, char byte) {
    return write(socket, &byte, 1);
}

/* Envía un string terminado en '\0' por el socket */
static int enviarString(int socket, const char *string) {
    size_t longitud = strlen(string);
    if (write(socket, string, longitud) != (ssize_t)longitud){
        return -1;
    }
    char fin = '\0';
    if (write(socket, &fin, 1) != 1){
        return -1;
    }
    return 0;
}

/* Carga información de un usuario desde su archivo */
static int cargarUsuario(const char *nombre_usuario, InfoUsuario *info) {
    /* Creamos la ruta al archivo */
    char ruta_archivo[512];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s.txt", RUTA_CARPETA, nombre_usuario);
    
    /* Abrimos el archivo */
    FILE *archivo = fopen(ruta_archivo, "r");
    if (!archivo){
        return -1;
    }

    /* Inicializamos la estructura InfoUsuario */
    memset(info, 0, sizeof(InfoUsuario));

    /* Rellenamos el campo del nombre del usuario */
    strncpy(info->nombre_usuario, nombre_usuario, sizeof(info->nombre_usuario) - 1);
    
    /*
    Formato del archivo:
    - Línea 1: id_ultimo_mensaje
    - Línea 2: usuario_conectado
    - Línea 3: ip_usuario
    - Línea 4: puerto_usuario
    */

    /* Leemos los diferentes campos del archivo y rellenamos la estructura InfoUsuario */
    char buffer[64];
    if (fgets(buffer, sizeof(buffer), archivo)){
        info->id_ultimo_mensaje = atoi(buffer);
    }
    if (fgets(buffer, sizeof(buffer), archivo)){
        info->usuario_conectado = atoi(buffer);
    }
    if (fgets(buffer, sizeof(buffer), archivo)) {
        buffer[strcspn(buffer, "\n")] = '\0';
        strncpy(info->ip_usuario, buffer, sizeof(info->ip_usuario) - 1);
    }
    if (fgets(buffer, sizeof(buffer), archivo)){
        info->puerto_usuario = atoi(buffer);
    }
    fclose(archivo);
    return 0;
}

/* Guarda información de usuario en archivo */
static int guardarUsuario(const InfoUsuario *info) {
    /* Creamos la ruta al archivo */
    char ruta_archivo[512];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s.txt", RUTA_CARPETA, info->nombre_usuario);
    
    /* Abrimos el archivo */
    FILE *archivo = fopen(ruta_archivo, "w");
    if (!archivo) return -1;
    
    /* Escribimos los diferentes campos la estructura InfoUsuario en el archivo */
    fprintf(archivo, "%d\n", info->id_ultimo_mensaje);
    fprintf(archivo, "%d\n", info->usuario_conectado);
    fprintf(archivo, "%s\n", info->ip_usuario);
    fprintf(archivo, "%d\n", info->puerto_usuario);
    
    fclose(archivo);
    return 0;
}


/* Añadir a un usuario a la lista de conectados */
static void añadirConectado(const char *nombre, const char *ip, int puerto) {
    pthread_mutex_lock(&mutex_conectados);
    Conectado *nuevo = malloc(sizeof(Conectado));
    strncpy(nuevo->nombre_usuario, nombre, sizeof(nuevo->nombre_usuario)-1);
    strncpy(nuevo->ip_usuario, ip, sizeof(nuevo->ip_usuario)-1);
    nuevo->puerto_usuario = puerto;
    nuevo->siguiente = lista_conectados;
    lista_conectados = nuevo;
    pthread_mutex_unlock(&mutex_conectados);
}

/* Eliminar a un usuario de la lista de conectados */
static void eliminarConectado(const char *nombre) {
    pthread_mutex_lock(&mutex_conectados);
    Conectado *actual = lista_conectados;
    Conectado *anterior = NULL;
    
    while (actual != NULL) {
        if (strcmp(actual->nombre_usuario, nombre) == 0) {
            if (anterior == NULL) {
                lista_conectados = actual->siguiente;
            } else {
                anterior->siguiente = actual->siguiente;
            }
            free(actual);
            pthread_mutex_unlock(&mutex_conectados);
            return;
        }
        anterior = actual;
        actual = actual->siguiente;
    }
    pthread_mutex_unlock(&mutex_conectados);
}

/* Obtener la lista completa de nombres de usuarios conectados */
static void listarConectados(char listado[][512], int *count) {
    pthread_mutex_lock(&mutex_conectados);
    *count = 0;
    Conectado *actual = lista_conectados;
    while (actual != NULL && *count < 256) {
        snprintf(listado[*count], 512, "%s :: %s :: %d",
                 actual->nombre_usuario,
                 actual->ip_usuario,
                 actual->puerto_usuario);
        (*count)++;
        actual = actual->siguiente;
    }
    pthread_mutex_unlock(&mutex_conectados);
}

/* Función para restaurar la lista de usuarios conectados al arrancar el servidor */
static void restaurar_estado_servidor() {
    DIR *d = opendir(RUTA_CARPETA);
    if (!d){
        return;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        /* Buscamos archivos .txt */
        char *punto = strrchr(dir->d_name, '.');
        if (punto && strcmp(punto, ".txt") == 0) {
            /* Ignoramos los archivos de mensajes pendientes */
            if (strstr(dir->d_name, "_mensajes.txt") != NULL) {
                continue;
            }

            /* Extraemos el nombre de usuario */
            char nombre_usuario[256];
            memset(nombre_usuario, 0, sizeof(nombre_usuario));
            strncpy(nombre_usuario, dir->d_name, punto - dir->d_name);

            /* Cargamos los datos del usuario desde su fichero */
            InfoUsuario info;
            if (cargarUsuario(nombre_usuario, &info) == 0) {
                /* Si en el fichero pone que estaba conectado, lo metemos en memoria */
                if (info.usuario_conectado == 1) {
                    añadirConectado(info.nombre_usuario, info.ip_usuario, info.puerto_usuario);
                }
            }
        }
    }
    closedir(d);
}


/* Funciones del servidor */

/* Función para registrar a un usuario */
static void do_register(int socket_cliente) {    
    /* Definimos el nombre del usuario */
    char nombre_usuario[256];
    memset(nombre_usuario, 0, sizeof(nombre_usuario));
    
    /* Leemos nombre_usuario (terminado en \0) que recibimos del socket */
    leerString(socket_cliente, nombre_usuario, sizeof(nombre_usuario));

    /* Enviamos log */
    call_rpc_log(nombre_usuario, "REGISTER", "");

    /* No permitimos nombres que contengan "_mensajes" */
    if (strstr(nombre_usuario, "_mensajes") != NULL) {
        printf("s> REGISTER %s FAIL (no permitimos nombres que contengan \"_mensajes\")\n", nombre_usuario);
        char respuesta = 2;
        enviarByte(socket_cliente, respuesta);
        return;
    }
    
    /* Creamos directorio si no existe */
    mkdir(RUTA_CARPETA, 0777);
    
    /* Creamos la ruta al fichero */
    char ruta_archivo[512];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s.txt", RUTA_CARPETA, nombre_usuario);

    /* Sección crítica en la que ocurre el acceso al fichero */
    pthread_mutex_lock(&mutex_funciones);
    /* Abrimos el fichero */
    int fichero = open(ruta_archivo, O_WRONLY | O_CREAT | O_EXCL, 0644);

    /* Enviamos respuesta */
    if (fichero == -1) {
        if (errno == EEXIST) {
            /* El usuario ya existe*/
            printf("s> REGISTER %s FAIL\n", nombre_usuario);
            char respuesta = 1;
            enviarByte(socket_cliente, respuesta);
        } else {
            /* Otro error */
            printf("s> REGISTER %s FAIL\n", nombre_usuario);
            char respuesta = 2;
            enviarByte(socket_cliente, respuesta);
        }
    } else {
        /* El usuario no existía, así que rellenamos la estructura InfoUsuario y la escribimos en el fichero */
        InfoUsuario info;
        memset(&info, 0, sizeof(InfoUsuario));
        strncpy(info.nombre_usuario, nombre_usuario, sizeof(info.nombre_usuario) - 1);
        info.id_ultimo_mensaje = 0;
        info.usuario_conectado = 0;
        strcpy(info.ip_usuario, "");
        info.puerto_usuario = 0;
        guardarUsuario(&info);
        close(fichero);
        
        printf("s> REGISTER %s OK\n", nombre_usuario);
        char respuesta = 0;
        enviarByte(socket_cliente, respuesta);
    }
    pthread_mutex_unlock(&mutex_funciones);
}



/* Función para dar de baja a un usuario */
static void do_unregister(int socket_cliente) {
    /* Definimos el nombre del usuario */
    char nombre_usuario[256];
    memset(nombre_usuario, 0, sizeof(nombre_usuario));
    leerString(socket_cliente, nombre_usuario, sizeof(nombre_usuario));

    /* Enviamos log */
    call_rpc_log(nombre_usuario, "UNREGISTER", "");

    /* Creamos la ruta al fichero */
    char ruta_archivo[512];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s.txt", RUTA_CARPETA, nombre_usuario);

    /* Sección crítica en la que ocurre el acceso al fichero */
    pthread_mutex_lock(&mutex_funciones);

    /* Eliminamos el fichero */
    int resultado = remove(ruta_archivo);

    /* Eliminamos de la lista de conectados en memoria */
    eliminarConectado(nombre_usuario);

    /* Eliminamos el fichero de mensajes pendientes */
    char ruta_pendientes[512];
    snprintf(ruta_pendientes, sizeof(ruta_pendientes), "%s%s_mensajes.txt", RUTA_CARPETA, nombre_usuario);
    remove(ruta_pendientes);

    pthread_mutex_unlock(&mutex_funciones);

    /* Enviamos respuesta al cliente */
    if (resultado == -1) {
        if (errno == ENOENT) {
            /* El usuario no existe */
            printf("s> UNREGISTER %s FAIL\n", nombre_usuario);
            char respuesta = 1;
            enviarByte(socket_cliente, respuesta);
        } else {
            /* Otro error */
            printf("s> UNREGISTER %s FAIL\n", nombre_usuario);
            char respuesta = 2;
            enviarByte(socket_cliente, respuesta);
        }
    } else {
        /* Éxito */
        printf("s> UNREGISTER %s OK\n", nombre_usuario);
        char respuesta = 0;
        enviarByte(socket_cliente, respuesta);
    }
}


static void do_connect(int socket_cliente, char *ip_cliente) {
    char nombre_usuario[256];
    char puerto_cliente_char[64];
    memset(nombre_usuario, 0, sizeof(nombre_usuario));
    memset(puerto_cliente_char, 0, sizeof(puerto_cliente_char));
    
    leerString(socket_cliente, nombre_usuario, sizeof(nombre_usuario));
    leerString(socket_cliente, puerto_cliente_char, sizeof(puerto_cliente_char));
    int puerto_cliente = atoi(puerto_cliente_char);

    /* Enviamos log */
    call_rpc_log(nombre_usuario, "CONNECT", "");
    
    pthread_mutex_lock(&mutex_funciones);

    InfoUsuario info;
    int resultado_load = cargarUsuario(nombre_usuario, &info);
    
    if (resultado_load != 0) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> CONNECT %s FAIL\n", nombre_usuario);
        enviarByte(socket_cliente, 1);
        return;
    }
    
    if (info.usuario_conectado) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> CONNECT %s FAIL\n", nombre_usuario);
        enviarByte(socket_cliente, 2);
        return;
    }
    
    info.usuario_conectado = 1;
    strncpy(info.ip_usuario, ip_cliente, sizeof(info.ip_usuario) - 1);
    info.puerto_usuario = puerto_cliente;
    guardarUsuario(&info);
    añadirConectado(nombre_usuario, ip_cliente, puerto_cliente);
    
    pthread_mutex_unlock(&mutex_funciones);
    
    printf("s> CONNECT %s OK\n", nombre_usuario);
    enviarByte(socket_cliente, 0);
    
    /* Enviar mensajes pendientes */
    char ruta_pendientes[512];
    snprintf(ruta_pendientes, sizeof(ruta_pendientes), "%s%s_mensajes.txt", RUTA_CARPETA, nombre_usuario);
    
    FILE *f = fopen(ruta_pendientes, "r");
    if (f != NULL) {
        /* Leer todos los mensajes pendientes */
        typedef struct MsgPendiente {
            unsigned int id;
            char remitente[256];
            char mensaje[256];
            char fichero[256];
        } MsgPendiente;
        
        MsgPendiente pendientes[256];
        int num_pendientes = 0;
        
        char linea[600];
        while (fgets(linea, sizeof(linea), f) && num_pendientes < 256) {
            /* Formato: id|remitente|mensaje */
            linea[strcspn(linea, "\n")] = '\0';
            
            char *ptr_id = linea;
            char *ptr_rem = strchr(linea, '|');
            if (!ptr_rem) continue;
            *ptr_rem = '\0';
            ptr_rem++;
            
            char *ptr_msg = strchr(ptr_rem, '|');
            if (!ptr_msg) continue;
            *ptr_msg = '\0';
            ptr_msg++;

            char *ptr_file = strchr(ptr_msg, '|');
            if (ptr_file) {
                /* SENDATTACH con fichero */
                *ptr_file = '\0';
                ptr_file++;
                strncpy(pendientes[num_pendientes].fichero, ptr_file, 255);
            } else {
                /* SEND sin fichero */
                pendientes[num_pendientes].fichero[0] = '\0';
            }

            pendientes[num_pendientes].id = (unsigned int)atoi(ptr_id);
            strncpy(pendientes[num_pendientes].remitente, ptr_rem, 255);
            strncpy(pendientes[num_pendientes].mensaje, ptr_msg, 255);

            num_pendientes++;
        }
        fclose(f);
        
        /* Enviar cada mensaje pendiente al cliente recién conectado */
        for (int i = 0; i < num_pendientes; i++) {
            int sock_destino = socket(AF_INET, SOCK_STREAM, 0);
            if (sock_destino == -1) continue;
            
            struct sockaddr_in dir_destino;
            memset(&dir_destino, 0, sizeof(dir_destino));
            dir_destino.sin_family = AF_INET;
            dir_destino.sin_port = htons(puerto_cliente);
            inet_pton(AF_INET, ip_cliente, &dir_destino.sin_addr);
            
            if (connect(sock_destino, (struct sockaddr *)&dir_destino, sizeof(dir_destino)) == 0) {
                char id_str[32];
                snprintf(id_str, sizeof(id_str), "%u", pendientes[i].id);
                
                if (strlen(pendientes[i].fichero) > 0) {
                    /* SENDATTACH */
                    enviarString(sock_destino, "SEND_MESSAGE_ATTACH");
                    enviarString(sock_destino, pendientes[i].remitente);
                    enviarString(sock_destino, id_str);
                    enviarString(sock_destino, pendientes[i].mensaje);
                    enviarString(sock_destino, pendientes[i].fichero);
                } else {
                    /* SEND normal */
                    enviarString(sock_destino, "SEND_MESSAGE");
                    enviarString(sock_destino, pendientes[i].remitente);
                    enviarString(sock_destino, id_str);
                    enviarString(sock_destino, pendientes[i].mensaje);
                }
                close(sock_destino);

                eliminarMensajePendiente(nombre_usuario, pendientes[i].id);
                
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", 
                       pendientes[i].id, pendientes[i].remitente, nombre_usuario);
                
                /* Enviar ACK al remitente si está conectado */
                pthread_mutex_lock(&mutex_funciones);
                InfoUsuario info_rem;
                if (cargarUsuario(pendientes[i].remitente, &info_rem) == 0 && info_rem.usuario_conectado) {
                    int sock_ack = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock_ack != -1) {
                        struct sockaddr_in dir_rem;
                        memset(&dir_rem, 0, sizeof(dir_rem));
                        dir_rem.sin_family = AF_INET;
                        dir_rem.sin_port = htons(info_rem.puerto_usuario);
                        inet_pton(AF_INET, info_rem.ip_usuario, &dir_rem.sin_addr);
                        
                        if (connect(sock_ack, (struct sockaddr *)&dir_rem, sizeof(dir_rem)) == 0) {
                            if (strlen(pendientes[i].fichero) > 0) {
                                enviarString(sock_ack, "SEND_MESS_ATTACH_ACK");
                                enviarString(sock_ack, id_str);
                                enviarString(sock_ack, pendientes[i].fichero);
                            } else {
                                enviarString(sock_ack, "SEND_MESS_ACK");
                                enviarString(sock_ack, id_str);
                            }
                        
                        }
                        close(sock_ack);
                    }
                }
                pthread_mutex_unlock(&mutex_funciones);
            } else {
                close(sock_destino);
            }
        }
    }
}


/* Función para permitir que se desconecte un usuario */
static void do_disconnect(int socket_cliente) {
    /* Definimos el nombre del usuario */
    char nombre_usuario[256];
    memset(nombre_usuario, 0, sizeof(nombre_usuario));

    /* Leemos nombre_usuario (terminado en \0) que recibimos del socket */
    leerString(socket_cliente, nombre_usuario, sizeof(nombre_usuario));

    /* Enviamos log */
    call_rpc_log(nombre_usuario, "DISCONNECT", "");

    /* Sección crítica */
    pthread_mutex_lock(&mutex_funciones);

    /* Cargamos información del usuario */
    InfoUsuario info;
    int resultado_load = cargarUsuario(nombre_usuario, &info);
    
    if (resultado_load != 0) {
        /* Usuario no existe */
        printf("s> DISCONNECT %s FAIL\n", nombre_usuario);
        char respuesta = 1;
        enviarByte(socket_cliente, respuesta);
        return;
    }
    
    if (!info.usuario_conectado) {
        /* No está conectado */
        printf("s> DISCONNECT %s FAIL\n", nombre_usuario);
        char respuesta = 2;
        enviarByte(socket_cliente, respuesta);
        return;
    }
    
    /* Actualizamos el estado del cliente a desconectado (0) */
    info.usuario_conectado = 0;
    strcpy(info.ip_usuario, "");
    info.puerto_usuario = 0;
    
    /* Guardamos el archivo modificado y salimos de la sección crítica*/
    guardarUsuario(&info);
    /* Eliminamos de la lista de conectados en memoria */
    eliminarConectado(nombre_usuario);
    pthread_mutex_unlock(&mutex_funciones);
    
    /* Respondemos */
    printf("s> DISCONNECT %s OK\n", nombre_usuario);
    char respuesta = 0;
    enviarByte(socket_cliente, respuesta);
}

static void do_users(int socket_cliente) {
    /* Obtenemos la IP del cliente que hace la petición */
    char nombre_usuario[256];
    memset(nombre_usuario, 0, sizeof(nombre_usuario));
    leerString(socket_cliente, nombre_usuario, sizeof(nombre_usuario));
    
    /* Enviamos log */
    call_rpc_log(nombre_usuario, "USERS", "");

    /* Buscamos si ese usuario existe y está conectado */
    InfoUsuario info_usuario;
    pthread_mutex_lock(&mutex_funciones);
    int existe = cargarUsuario(nombre_usuario, &info_usuario);
    pthread_mutex_unlock(&mutex_funciones);
    
    if (existe == -1 || info_usuario.usuario_conectado == 0) {
        /* Usuario no existe o no está conectado */
        printf("s> CONNECTEDUSERS FAIL\n");
        enviarByte(socket_cliente, 1);
        return;
    }

    /* Obtenemos la lista de todos los conectados y cuántos usuarios conectados hay */
    char usuarios_conectados[256][512];
    int num_conectados = 0;
    listarConectados(usuarios_conectados, &num_conectados);
    
    /* Enviamos señal de éxito */
    enviarByte(socket_cliente, 0);
    
    /* Enviamos el número de usuarios como cadena */
    char num_str[32];
    snprintf(num_str, sizeof(num_str), "%d", num_conectados);
    enviarString(socket_cliente, num_str);
    
    /* Enviamos cada nombre de usuario */
    for (int i = 0; i < num_conectados; i++) {
        enviarString(socket_cliente, usuarios_conectados[i]);
    }
    
    printf("s> CONNECTEDUSERS OK\n");
}


static void do_send(int socket_cliente) {
    /* remitente, destinatario, mensaje */
    char remitente[256];
    memset(remitente, 0, sizeof(remitente));
    leerString(socket_cliente, remitente, sizeof(remitente));

    char destinatario[256];
    memset(destinatario, 0, sizeof(destinatario));
    leerString(socket_cliente, destinatario, sizeof(destinatario));

    char mensaje[256];
    memset(mensaje, 0, sizeof(mensaje));
    leerString(socket_cliente, mensaje, sizeof(mensaje));

    /* Enviamos log */
    call_rpc_log(remitente, "SEND", "");

    pthread_mutex_lock(&mutex_funciones);

    /* Verificamos que ambos usuarios existen */
    InfoUsuario info_remitente;
    InfoUsuario info_destinatario;

    if (cargarUsuario(remitente, &info_remitente) == -1) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SEND FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }

    if (cargarUsuario(destinatario, &info_destinatario) == -1) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SEND FAIL, USER DOES NOT EXIST\n");
        enviarByte(socket_cliente, 1);
        return;
    }

    /* Verificamos que el remitente esté conectado */
    if (info_remitente.usuario_conectado == 0) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SEND FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }

    /* Incrementamos id de mensaje */
    info_destinatario.id_ultimo_mensaje++;
    if (info_destinatario.id_ultimo_mensaje == 0) {
        info_destinatario.id_ultimo_mensaje = 1;
    }
    unsigned int id_mensaje = info_destinatario.id_ultimo_mensaje;
    guardarUsuario(&info_destinatario);

    /* Almacenamos mensaje pendiente */
    char ruta_pendientes[512];
    snprintf(ruta_pendientes, sizeof(ruta_pendientes), "%s%s_mensajes.txt", RUTA_CARPETA, destinatario);
    FILE *f = fopen(ruta_pendientes, "a");
    if (f == NULL) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SEND FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }
    /* Formato en el que se almacenará en el fichero */
    fprintf(f, "%u|%s|%s\n", id_mensaje, remitente, mensaje);
    fclose(f);

    /* Respondemos al remitente: éxito + id */
    enviarByte(socket_cliente, 0);
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id_mensaje);
    enviarString(socket_cliente, id_str);

    /* Si destinatario está conectado, entregamos ahora */
    if (info_destinatario.usuario_conectado) {
        int sock_destino = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_destino != -1) {
            struct sockaddr_in dir_destino;
            memset(&dir_destino, 0, sizeof(dir_destino));
            dir_destino.sin_family = AF_INET;
            dir_destino.sin_port = htons(info_destinatario.puerto_usuario);
            inet_pton(AF_INET, info_destinatario.ip_usuario, &dir_destino.sin_addr);

            if (connect(sock_destino, (struct sockaddr *)&dir_destino, sizeof(dir_destino)) == 0) {
                enviarString(sock_destino, "SEND_MESSAGE");
                enviarString(sock_destino, remitente);
                enviarString(sock_destino, id_str);
                enviarString(sock_destino, mensaje);
                close(sock_destino);

                /* Borramos mensaje pendiente ya entregado */
                eliminarMensajePendiente(destinatario, id_mensaje);

                printf("s> SEND MESSAGE %u FROM %s TO %s\n", id_mensaje, remitente, destinatario);

                /* Enviamos ACK al remitente si está conectado */
                if (info_remitente.usuario_conectado) {
                    int sock_ack = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock_ack != -1) {
                        struct sockaddr_in dir_rem;
                        memset(&dir_rem, 0, sizeof(dir_rem));
                        dir_rem.sin_family = AF_INET;
                        dir_rem.sin_port = htons(info_remitente.puerto_usuario);
                        inet_pton(AF_INET, info_remitente.ip_usuario, &dir_rem.sin_addr);

                        if (connect(sock_ack, (struct sockaddr *)&dir_rem, sizeof(dir_rem)) == 0) {
                            enviarString(sock_ack, "SEND_MESS_ACK");
                            enviarString(sock_ack, id_str);
                        }
                        close(sock_ack);
                    }
                }
            } else {
                close(sock_destino);
                printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
            }
        } else {
            printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
        }
    } else {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
    }

    pthread_mutex_unlock(&mutex_funciones);
}




static void do_sendattach(int socket_cliente) {
    /* remitente, destinatario, mensaje */
    char remitente[256];
    memset(remitente, 0, sizeof(remitente));
    leerString(socket_cliente, remitente, sizeof(remitente));

    char destinatario[256];
    memset(destinatario, 0, sizeof(destinatario));
    leerString(socket_cliente, destinatario, sizeof(destinatario));

    char mensaje[256];
    memset(mensaje, 0, sizeof(mensaje));
    leerString(socket_cliente, mensaje, sizeof(mensaje));

    char fichero[256];
    memset(fichero, 0, sizeof(fichero));
    leerString(socket_cliente, fichero, sizeof(fichero));


    /* Enviamos log */
    call_rpc_log(remitente, "SENDATTACH", fichero);

    pthread_mutex_lock(&mutex_funciones);

    /* Verificamos que ambos usuarios existen */
    InfoUsuario info_remitente;
    InfoUsuario info_destinatario;

    if (cargarUsuario(remitente, &info_remitente) == -1) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SENDATTACH FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }

    if (cargarUsuario(destinatario, &info_destinatario) == -1) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SENDATTACH FAIL, USER DOES NOT EXIST\n");
        enviarByte(socket_cliente, 1);
        return;
    }

    /* Verificamos que el remitente esté conectado */
    if (info_remitente.usuario_conectado == 0) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SENDATTACH FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }

    /* Incrementamos id de mensaje */
    info_destinatario.id_ultimo_mensaje++;
    if (info_destinatario.id_ultimo_mensaje == 0) {
        info_destinatario.id_ultimo_mensaje = 1;
    }
    unsigned int id_mensaje = info_destinatario.id_ultimo_mensaje;
    guardarUsuario(&info_destinatario);

    /* Almacenamos mensaje pendiente */
    char ruta_pendientes[512];
    snprintf(ruta_pendientes, sizeof(ruta_pendientes), "%s%s_mensajes.txt", RUTA_CARPETA, destinatario);
    FILE *f = fopen(ruta_pendientes, "a");
    if (f == NULL) {
        pthread_mutex_unlock(&mutex_funciones);
        printf("s> SENDATTACH FAIL\n");
        enviarByte(socket_cliente, 2);
        return;
    }
    /* Formato en el que se almacenará en el fichero */
    fprintf(f, "%u|%s|%s|%s\n", id_mensaje, remitente, mensaje,fichero);
    fclose(f);

    /* Respondemos al remitente: éxito + id */
    enviarByte(socket_cliente, 0);
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id_mensaje);
    enviarString(socket_cliente, id_str);

    /* Si destinatario está conectado, entregamos ahora */
    if (info_destinatario.usuario_conectado) {
        int sock_destino = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_destino != -1) {
            struct sockaddr_in dir_destino;
            memset(&dir_destino, 0, sizeof(dir_destino));
            dir_destino.sin_family = AF_INET;
            dir_destino.sin_port = htons(info_destinatario.puerto_usuario);
            inet_pton(AF_INET, info_destinatario.ip_usuario, &dir_destino.sin_addr);

            if (connect(sock_destino, (struct sockaddr *)&dir_destino, sizeof(dir_destino)) == 0) {
                enviarString(sock_destino, "SEND_MESSAGE_ATTACH");
                enviarString(sock_destino, remitente);
                enviarString(sock_destino, id_str);
                enviarString(sock_destino, mensaje);
                enviarString(sock_destino, fichero); 
                close(sock_destino);

                /* Borramos mensaje pendiente ya entregado */
                eliminarMensajePendiente(destinatario, id_mensaje);

                printf("s> SEND MESSAGE %u FROM %s TO %s\n", id_mensaje, remitente, destinatario);

                /* Enviamos ACK al remitente si está conectado */
                if (info_remitente.usuario_conectado) {
                    int sock_ack = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock_ack != -1) {
                        struct sockaddr_in dir_rem;
                        memset(&dir_rem, 0, sizeof(dir_rem));
                        dir_rem.sin_family = AF_INET;
                        dir_rem.sin_port = htons(info_remitente.puerto_usuario);
                        inet_pton(AF_INET, info_remitente.ip_usuario, &dir_rem.sin_addr);

                        if (connect(sock_ack, (struct sockaddr *)&dir_rem, sizeof(dir_rem)) == 0) {
                            enviarString(sock_ack, "SEND_MESS_ATTACH_ACK");
                            enviarString(sock_ack, id_str);
                            enviarString(sock_ack, fichero); 
                        }
                        close(sock_ack);
                    }
                }
            } else {
                close(sock_destino);
                printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
            }
        } else {
            printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
        }
    } else {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", id_mensaje, remitente, destinatario);
    }

    pthread_mutex_unlock(&mutex_funciones);
}



/* Función que ejecutará cada hilo */
void *manejar_cliente(void *arg) {
    ArgumentosHilo *args = (ArgumentosHilo *)arg;
    int socket_cliente = args->socket;
    char ip_cliente[64];
    strcpy(ip_cliente, args->ip);
    free(args); /* Liberar memoria asignada en main */

    /* Leemos el tipo de operación */
    char tipo_operacion[256];
    memset(tipo_operacion, 0, sizeof(tipo_operacion));
    leerString(socket_cliente, tipo_operacion, sizeof(tipo_operacion));

    /*/ Ejecutamos la operación correspondiente */
    if (strcmp(tipo_operacion, "REGISTER") == 0) {
        do_register(socket_cliente);
    } else if (strcmp(tipo_operacion, "UNREGISTER") == 0) {
        do_unregister(socket_cliente);
    } else if (strcmp(tipo_operacion, "CONNECT") == 0) {
        do_connect(socket_cliente, ip_cliente);
    } else if (strcmp(tipo_operacion, "DISCONNECT") == 0) {
        do_disconnect(socket_cliente);
    } else if (strcmp(tipo_operacion, "USERS") == 0) {
        do_users(socket_cliente);
    } else if (strcmp(tipo_operacion, "SENDATTACH") == 0) {
        do_sendattach(socket_cliente);
    } else if (strcmp(tipo_operacion, "SEND") == 0) {
        do_send(socket_cliente);
    } else {
        printf("s> Operación no reconocida: %s\n", tipo_operacion);
    }

    close(socket_cliente);
    return NULL;
}



int main(int argc, char *argv[]) {
    /* Comprobamos el número de argumentos */
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        printf("Uso: ./server -p <puerto>\n");
        exit(1);
    }

    /* Creamos la carpeta */
    if (mkdir(RUTA_CARPETA, 0777) == -1) {
        if (errno != EEXIST) {
            perror("Error al crear el directorio de usuarios");
            exit(1);
        }
    }

    /* Limpiamos posibles estados inconsistentes en memoria */
    restaurar_estado_servidor();

    /* Creamos el socket */
    int desc_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (desc_socket == -1) {
        perror("Error al crear el socket");
        exit(1);
    }

    /* Esto permite que si cierras el servidor puedes volver a levantarlo inmediatemente en el mismo puerto 
    Sin esto sale un error */
    int opt = 1;
    setsockopt(desc_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Asignamos un puerto */
    struct sockaddr_in dir_socket;
    memset(&dir_socket, 0, sizeof(dir_socket));
    dir_socket.sin_family = AF_INET;
    dir_socket.sin_addr.s_addr = INADDR_ANY;
    dir_socket.sin_port = htons(atoi(argv[2]));
    if (bind(desc_socket, (struct sockaddr *)&dir_socket, sizeof(dir_socket)) == -1) {
        perror("Error en bind");
        exit(1);
    }

    /* Configuramos el socket para que pueda recibir conexiones */
    if (listen(desc_socket, SOMAXCONN) == -1) {
        perror("Error en listen");
        exit(1);
    }

    /* Imprimimos la inicialización del servidor */
    char hostname[256];
    struct hostent *hp;
    struct in_addr in_ip;
    char *ip_final = "0.0.0.0";

    if (gethostname(hostname, sizeof(hostname)) != -1) {
        hp = gethostbyname(hostname);
        if (hp != NULL) {
            memcpy(&in_ip.s_addr, *(hp->h_addr_list), sizeof(in_ip.s_addr));
            ip_final = inet_ntoa(in_ip);
        }
    }

    printf("s> init server %s:%s\n", ip_final, argv[2]);
    fflush(stdout);

    /* Bucle de espera en el que se aceptan las conexiones */
    while (1) {
        /* Información del cliente */
        struct sockaddr_in dir_socket_cliente;
        socklen_t long_dir_socket_cliente = sizeof(dir_socket_cliente);

        /* Reservamos memoria */
        ArgumentosHilo *args = malloc(sizeof(ArgumentosHilo));
        if (!args) {
            perror("Error malloc");
            continue;
        }
        
        /* Aceptamos la conexión y la guardamos en la estructura */
        args->socket = accept(desc_socket, (struct sockaddr *)&dir_socket_cliente, &long_dir_socket_cliente);
        if (args->socket == -1) {
            perror("Error en accept");
            free(args);
            continue;
        }

        /* Sacamos la IP del accept */
        strcpy(args->ip, inet_ntoa(dir_socket_cliente.sin_addr));

        /* Creamos un hilo para atender a este cliente */        
        pthread_t thread;
        if (pthread_create(&thread, NULL, manejar_cliente, args) != 0) {
            perror("Error creando el hilo");
            free(args);
            continue;
        }
        pthread_detach(thread);
    }
}