#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>        
#include <netdb.h> 
#include <sys/socket.h>   
#include <netinet/in.h>   
#include <arpa/inet.h>    
#include <stdarg.h>
#include <ctype.h>     
#include <sys/wait.h>     


#define FTP_PORT     21          // Puerto estándar FTP
#define BUFSIZE      4096        // Tamaño de buffer de lectura/escritura
#define MAX_LINE     512         // Longitud máxima de una línea de comando
#define QLEN         5           // Longitud de cola para passiveTCP() en modo PORT

#define MODE_PASV    1           // Modo pasivo (default)
#define MODE_PORT    2           // Modo activo (PORT)


struct ftp_session {
    int ctrl_fd;            // descriptor de socket de control
    char host[256];         // dirección o nombre del servidor
    int port;               // puerto del servidor
    char user[64];          // usuario actual
    char pass[64];          // contraseña (guardada para hijos)
    int mode;               // modo de transferencia actual (PASV o PORT)
};


int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);
int passiveTCP(const char *service, int qlen);


int ftp_open(struct ftp_session *s);
int ftp_read_reply(int fd, int *code, char *buffer, size_t max);
int ftp_command(int fd, int *code, char *buffer, size_t max, const char *fmt, ...);
int ftp_login(struct ftp_session *s);
int ftp_set_binary(int fd);

int ftp_open(struct ftp_session *s){
    if (!s) {
        errno = EINVAL;
        return -1;
    }

    /*Converir puerto a cadena*/
    char service[16];
    int port = (s->port > 0) ? s->port : FTP_PORT;
    snprintf(service, sizeof(service), "%d", port);

    int fd = connectTCP(s->host, service);
    if (fd < 0) {
        return -1;
    }

    /* Guardar el descriptor del canal de control */
    s->ctrl_fd = fd;

    /* Leer la respuesta inicial del servidor */
    int code;
    char buffer[MAX_LINE];

    if (ftp_read_reply(fd, &code, buffer, sizeof(buffer)) < 0) {
        close(fd);
        s->ctrl_fd = -1;
        return -1;
    }

    if (code != 220) {
        /* El servidor no está listo */
        close(fd);
        s->ctrl_fd = -1;
        errno = ECONNREFUSED;
        return -1;
    }

    s->mode = MODE_PASV;

    return 0; // Éxito
}

int ftp_read_reply(int fd, int *code, char *buffer, size_t max) {
    if (fd < 0 || !code || !buffer || max == 0) {
        errno = EINVAL;
        return -1;
    }

    char line[MAX_LINE];
    size_t buf_len = 0;
    int first = 1;
    int multiline = 0;
    int expected = 0;

    char r_bloque[256];   // bloque de lectura
    size_t line_len = 0;

    for (;;) {
        ssize_t n = recv(fd, r_bloque, sizeof(r_bloque), 0);
        if (n <= 0) {
            errno = EPROTO;
            return -1;
        }

        for (ssize_t i = 0; i < n; i++) {
            char c = r_bloque[i];

            /* construir una línea */
            if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            }

            if (c == '\n') {
                line[line_len] = '\0';

                size_t add = line_len;
                if (buf_len + add >= max) {
                    errno = ENOMEM;
                    return -1;
                }
                memcpy(buffer + buf_len, line, add);
                buf_len += add;
                buffer[buf_len] = '\0';

                if (first) {
                    first = 0;

                    if (!isdigit(line[0]) || !isdigit(line[1]) || !isdigit(line[2])) {
                        errno = EPROTO;
                        return -1;
                    }

                    expected = atoi(line);

                    if (line[3] == '-') {
                        multiline = 1;
                    } else if (line[3] == ' ') {
                        *code = expected;
                        return 0;
                    } else {
                        errno = EPROTO;
                        return -1;
                    }
                } else if (multiline) {
                    if (isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2]) &&
                        atoi(line) == expected && line[3] == ' ') {
                        *code = expected;
                        return 0;
                    }
                }
                line_len = 0;
            }
        }
    }
    errno = EPROTO;
    return -1;
}

int ftp_command(int fd, int *code, char *buffer, size_t max, const char *fmt, ...) {

    if (fd < 0 || !code || !buffer || max == 0 || !fmt) {
        errno = EINVAL;
        return -1;
    }

    char cmd[MAX_LINE];
    va_list ap;
    va_start(ap, fmt);

    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    /* Añadir \r\n requerido por RFC 959 */
    strncat(cmd, "\r\n", sizeof(cmd) - strlen(cmd) - 1);

    ssize_t n = send(fd, cmd, strlen(cmd), 0);
    if (n < 0) {
        return -1;
    }

    /* Leer la respuesta del servidor FTP */
    if (ftp_read_reply(fd, code, buffer, max) < 0) {
        return -1;
    }

    return 0; 
}

int ftp_login(struct ftp_session *s) {
    if (!s || s->ctrl_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    char buffer[BUFSIZE];
    int code;

    if (ftp_command(s->ctrl_fd, &code, buffer, sizeof(buffer),
                    "USER %s", s->user) < 0) {
        return -1;
    }

    if (code == 230) {
        return 0;
    }

    if (code != 331) {
        fprintf(stderr, "Error: el servidor no aceptó USER (%d)\n", code);
        return -1;
    }

    if (ftp_command(s->ctrl_fd, &code, buffer, sizeof(buffer),
                    "PASS %s", s->pass) < 0) {
        return -1;
    }

    if (code != 230) {
        fprintf(stderr, "Error: contraseña incorrecta (%d)\n", code);
        return -1;
    }
    return 0; 
}

int ftp_set_binary(int fd) {
    int code;
    char buffer[BUFSIZE];

    if (ftp_command(fd, &code, buffer, sizeof(buffer), "TYPE I") < 0)
        return -1;

    if (code != 200) {
        fprintf(stderr, "Error: el servidor no aceptó TYPE I (code %d)\n", code);
        return -1;
    }

    return 0;
}

int ftp_open_data_pasv(struct ftp_session *s, int *data_fd) {
    if (!s || s->ctrl_fd < 0 || !data_fd) {
        errno = EINVAL;
        return -1;
    }

    int code;
    char buffer[BUFSIZE];

    /* Enviar PASV */
    if (ftp_command(s->ctrl_fd, &code, buffer, sizeof(buffer), "PASV") < 0)
        return -1;

    if (code != 227) {
        fprintf(stderr, "Error: el servidor no aceptó PASV (code %d)\n", code);
        return -1;
    }

    char *p = strchr(buffer, '(');
    if (!p) {
        fprintf(stderr, "Error: respuesta PASV sin paréntesis: %s\n", buffer);
        return -1;
    }

    int h1, h2, h3, h4, p1, p2;

    if (sscanf(p+1, "%d,%d,%d,%d,%d,%d",
               &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        fprintf(stderr, "Error: formato de respuesta PASV inválido\n");
        return -1;
    }

    char ip[64];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", h1, h2, h3, h4);

    int port = p1 * 256 + p2;

    char service[16];
    snprintf(service, sizeof(service), "%d", port);

    int fd = connectTCP(ip, service);
    if (fd < 0) {
        perror("connectTCP (data socket)");
        return -1;
    }

    *data_fd = fd;
    return 0;
}

int get_local_ip_str(int socket, char *ip_str) {
    struct sockaddr_in local_sin;
    socklen_t len = sizeof(local_sin);
    if (getsockname(socket, (struct sockaddr *)&local_sin, &len) < 0) return -1;
    inet_ntop(AF_INET, &local_sin.sin_addr, ip_str, 64);
    return 0;
}

int ftp_open_data_port(struct ftp_session *s, int *listen_fd) {
    if (!s || s->ctrl_fd < 0 || !listen_fd) {
        errno = EINVAL;
        return -1;
    }

    /* 1. Crear socket manual */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) return -1;

    /* 2. Bind a port 0 (puerto efímero) */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);   // Permite que el servidor conecte
    sin.sin_port = htons(0);                   // Puerto efímero

    if (bind(lsock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        close(lsock);
        return -1;
    }

    /* 3. Escuchar */
    if (listen(lsock, 1) < 0) {
        perror("listen");
        close(lsock);
        return -1;
    }

    /* 4. Obtener puerto asignado */
    socklen_t len = sizeof(sin);
    if (getsockname(lsock, (struct sockaddr *)&sin, &len) < 0) {
        perror("getsockname");
        close(lsock);
        return -1;
    }

    int port = ntohs(sin.sin_port);

    /* 5. Obtener IP local usada para el control */
    char my_ip[64];
    get_local_ip_str(s->ctrl_fd, my_ip);

    int h1, h2, h3, h4;
    sscanf(my_ip, "%d.%d.%d.%d", &h1, &h2, &h3, &h4);

    int p1 = port / 256;
    int p2 = port % 256;

    /* 6. Mandar comando PORT */
    int code;
    char buffer[BUFSIZE];

    if (ftp_command(s->ctrl_fd, &code, buffer, sizeof(buffer),
                    "PORT %d,%d,%d,%d,%d,%d",
                    h1, h2, h3, h4, p1, p2) < 0) {
        close(lsock);
        return -1;
    }

    if (code != 200) {
        fprintf(stderr, "Error: Servidor rechazó PORT (%d)\n", code);
        close(lsock);
        return -1;
    }

    *listen_fd = lsock;
    return 0;
}

void ftp_transfer_child(struct ftp_session *s, const char *direction,  const char *remote_file, const char *local_file){
    
    /* 1. Crear nueva sesión control independiente */
    int ctrl;
    {
        char service[16];
        int port = (s->port > 0) ? s->port : FTP_PORT;
        snprintf(service, sizeof(service), "%d", port);

        ctrl = connectTCP(s->host, service);


        if (ctrl < 0) {
            perror("[Hijo] connectTCP");
            exit(1);
        }

        int code;
        char buffer[BUFSIZE];

        ftp_read_reply(ctrl, &code, buffer, sizeof(buffer));
        ftp_command(ctrl, &code, buffer, sizeof(buffer), "USER %s", s->user);
        ftp_command(ctrl, &code, buffer, sizeof(buffer), "PASS %s", s->pass);
        ftp_command(ctrl, &code, buffer, sizeof(buffer), "TYPE I");
    }

    /* 2. Abrir canal de datos (PASV / PORT) */
    int data_sock = -1;
    int listen_sock = -1;

    struct ftp_session temp = *s;
    temp.ctrl_fd = ctrl;

    if (s->mode == MODE_PASV)
        ftp_open_data_pasv(&temp, &data_sock);
    else
        ftp_open_data_port(&temp, &listen_sock);

    /* 3. Enviar comando RETR / STOR */
    int code;
    char buffer[BUFSIZE];

    ftp_command(ctrl, &code, buffer, sizeof(buffer),
                "%s %s",
                (strcmp(direction, "GET") == 0) ? "RETR" : "STOR",
                remote_file);

    if (code >= 400) {
        printf("[Hijo] Error en comando: %s\n", buffer);
        exit(1);
    }

    /* 4. Aceptar conexión si es PORT */
    if (s->mode == MODE_PORT) {
        struct sockaddr_in fsin;
        socklen_t alen = sizeof(fsin);
        data_sock = accept(listen_sock, (struct sockaddr *)&fsin, &alen);
        close(listen_sock);
    }

    /* 5. Transferir datos */
    FILE *fp;
    char buf[BUFSIZE];
    ssize_t n;

    if (strcmp(direction, "GET") == 0) {
        fp = fopen(local_file, "wb");
        while ((n = read(data_sock, buf, BUFSIZE)) > 0)
            fwrite(buf, 1, n, fp);
    }
    else { /* PUT */
        fp = fopen(local_file, "rb");
        while ((n = fread(buf, 1, BUFSIZE, fp)) > 0)
            write(data_sock, buf, n);
    }

    fclose(fp);
    close(data_sock);

    ftp_read_reply(ctrl, &code, buffer, sizeof(buffer));
    printf("[Hijo] %s\n", buffer);

    close(ctrl);
    exit(0);
}

void ftp_list(struct ftp_session *s) {
    if (!s || s->ctrl_fd < 0) {
        printf("Error: sesión FTP no inicializada.\n");
        return;
    }

    int data_sock = -1;
    int listen_sock = -1;
    int code;
    char buffer[BUFSIZE];


    if (s->mode == MODE_PASV) {
        if (ftp_open_data_pasv(s, &data_sock) < 0) {
            printf("Error: no se pudo abrir canal PASV.\n");
            return;
        }
    } else {
        if (ftp_open_data_port(s, &listen_sock) < 0) {
            printf("Error: no se pudo abrir canal PORT.\n");
            return;
        }
    }

    if (ftp_command(s->ctrl_fd, &code, buffer, sizeof(buffer), "LIST") < 0) {
        printf("Error enviando LIST.\n");
        if (data_sock >= 0) close(data_sock);
        if (listen_sock >= 0) close(listen_sock);
        return;
    }

    if (code >= 400) {
        printf("Error LIST: %s", buffer);
        if (data_sock >= 0) close(data_sock);
        if (listen_sock >= 0) close(listen_sock);
        return;
    }
/
    if (s->mode == MODE_PORT) {
        struct sockaddr_in fsin;
        socklen_t alen = sizeof(fsin);

        data_sock = accept(listen_sock, (struct sockaddr *)&fsin, &alen);
        close(listen_sock);

        if (data_sock < 0) {
            perror("accept (PORT)");
            return;
        }
    }

    char data_buf[BUFSIZE];
    ssize_t n;

    while ((n = read(data_sock, data_buf, sizeof(data_buf))) > 0) {
        fwrite(data_buf, 1, n, stdout);
    }

    close(data_sock);

    if (ftp_read_reply(s->ctrl_fd, &code, buffer, sizeof(buffer)) == 0) {
        printf("%s", buffer);
    }
}

int ftp_reconnect(const char *host, int port,
                  const char *user, const char *pass) {

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int fd = connectTCP(host, portstr);
    if (fd < 0) return -1;

    int code;
    char buffer[BUFSIZE];

    ftp_read_reply(fd, &code, buffer, sizeof(buffer));

    /* USER */
    ftp_command(fd, &code, buffer, sizeof(buffer), "USER %s", user);
    if (code != 331 && code != 230) {
        close(fd);
        return -1;
    }

    /* PASS */
    ftp_command(fd, &code, buffer, sizeof(buffer), "PASS %s", pass);
    if (code != 230) {
        close(fd);
        return -1;
    }

    ftp_command(fd, &code, buffer, sizeof(buffer), "TYPE I");

    return fd;
}

void ayuda() {
    printf(
        "--------------------------------------------------------------------------\n"
        "                    Cliente FTP – Comandos disponibles             \n"
        "--------------------------------------------------------------------------\n"
        "  help                    - Muestra esta ayuda\n"
        "  ls  | dir               - Lista el directorio actual del servidor\n"
        "  get <remoto>            - Descarga un archivo del servidor (RETR)\n"
        "  put <local>             - Sube un archivo al servidor (STOR)\n"
        "  cd <directorio>         - Cambia el directorio remoto (CWD)\n"
        "  mode                    - Muestra o cambia el modo de transferencia\n"
        "  mode pasv               - Cambia a modo PASIVO (PASV)\n"
        "  mode act                - Cambia a modo ACTIVO (PORT)\n"
        "  quit | exit             - Finaliza la sesión FTP\n"
        "\n"
        "  * Comandos RAW soportados por el servidor:\n"
        "      PWD, MKD <dir>, RMD <dir>, DELE <archivo>, REST <offset>, etc.\n"
        "    (Se envían tal cual escribas el comando)\n"
        "---------------------------------------------------------------------------\n"
    );
}


/* MAIN*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <host> [port]\n", argv[0]);
        exit(1);
    }

    struct ftp_session session;
    memset(&session, 0, sizeof(session));
    
    strncpy(session.host, argv[1], sizeof(session.host)-1);
    if (argc > 2) session.port = atoi(argv[2]);
    
    printf("Por favor ingrese su usuario\n Usuario: ");
    if (!fgets(session.user, sizeof(session.user), stdin)) {
        fprintf(stderr, "Error leyendo usuario\n");
        exit(1);
    }
    session.user[strcspn(session.user, "\n")] = 0; 
    
    char *pw = getpass("Contraseña: ");
    strncpy(session.pass, pw, sizeof(session.pass)-1);
    session.pass[sizeof(session.pass)-1] = 0;

    /* 1. Conexión Inicial */
    if (ftp_open(&session) < 0) {
        errexit("No se pudo conectar al servidor FTP\n");
    }
    printf("Conectado a %s. Enviando credenciales...\n", session.host);

    /* 2. Login */
    if (ftp_login(&session) < 0) {
        close(session.ctrl_fd);
        errexit("Fallo en autenticación.\n");
    }
    printf("Login exitoso.\n");

    /* 3. Configurar Binario por defecto */
    ftp_set_binary(session.ctrl_fd);

    ayuda();

    /* 4. Bucle de Comandos */
    char input[MAX_LINE];
    char cmd[32], arg[MAX_LINE];

    while (1) {
        printf("ftp (%s) > ", session.host);
        if (!fgets(input, sizeof(input), stdin)) break;
        
        input[strcspn(input, "\n")] = 0; // Eliminar newline
        if (strlen(input) == 0) continue;

        memset(cmd, 0, sizeof(cmd));
        memset(arg, 0, sizeof(arg));
        sscanf(input, "%s %s", cmd, arg);

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            int c; char b[BUFSIZE];
            ftp_command(session.ctrl_fd, &c, b, sizeof(b), "QUIT");
            break;
        } 
        else if (strcmp(cmd, "mode") == 0) {
            if (strcmp(arg, "act") == 0 || strcmp(arg, "active") == 0) {
                session.mode = MODE_PORT;
                printf("Modo cambiado a ACTIVO (PORT)\n");
            } else if (strcmp(arg, "pasv") == 0 || strcmp(arg, "passive") == 0) {
                session.mode = MODE_PASV;
                printf("Modo cambiado a PASIVO (PASV)\n");
            } else {
                printf("Modo actual: %s. Use 'mode active' o 'mode passive'\n", 
                       (session.mode == MODE_PASV) ? "PASIVO" : "ACTIVO");
            }
        }
        else if (strcmp(cmd, "get") == 0) {
            if (strlen(arg) == 0) {
                printf("Uso: get <archivo>\n");
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    ftp_transfer_child(&session, "GET", arg, arg);
                    exit(0);
                } else {
                    printf("[PID %d] Descarga iniciada en background: %s\n", pid, arg);
                }
            }
        }
        else if (strcmp(cmd, "put") == 0) {
            if (strlen(arg) == 0) {
                printf("Uso: put <archivo_local>\n");
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    /* HIJO -realiza la subida */
                    ftp_transfer_child(&session, "PUT", arg, arg);
                    exit(0);
                } else {
                    /* PADRE - libera terminal */
                    printf("[PID %d] Subida iniciada en background: %s\n", pid, arg);
                }
            }
        }
        else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
            ftp_list(&session);
        }
        else if (strcmp(cmd, "cd") == 0) {
            if (strlen(arg) == 0) {
                printf("Uso: cd <directorio>\n");
            } else {
                int c; char b[BUFSIZE];
                ftp_command(session.ctrl_fd, &c, b, sizeof(b), "CWD %s", arg);
                printf("%s", b); 
            }
        }
        else if (strcmp(cmd, "help") == 0) {
            ayuda();
        }
        else {
            int c; char b[BUFSIZE];
            if (strlen(arg) > 0) ftp_command(session.ctrl_fd, &c, b, sizeof(b), "%s %s", cmd, arg);
            else ftp_command(session.ctrl_fd, &c, b, sizeof(b), "%s", cmd);
            printf("%s", b);
        }
    }

    close(session.ctrl_fd);
    return 0;
}