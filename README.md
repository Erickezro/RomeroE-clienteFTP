# Cliente FTP Concurrente en C 

Este proyecto implementa un cliente FTP totalmente funcional en C, capaz de comunicarse con servidores FTP estándar mediante el protocolo definido en el RFC 959.
Incluye soporte para:

- Modo PASV (el cliente se conecta al servidor)
- Modo PORT (el servidor se conecta al cliente)
- Transferencias concurrentes usando fork()
- Interpretación correcta de códigos y respuestas multilinea
- Comandos básicos y comandos “raw” enviados directamente al servidor
- Manejo de dos canales: control y datos
- Compatibilidad con servidores como vsftpd, ProFTPD, Pure-FTPd, etc.

## ⚙️ **Instalación**

### 1. Clonar el repositorio
```
git clone https://github.com/tuusuario/cliente-ftp-c.git
cd cliente-ftp-c
```

### 2. Requisitos

Este proyecto utiliza únicamente funciones estándar de C y sockets POSIX/Linux, por lo que funciona en:
- Linux
- WSL
- Unix 

Asegúrate de tener: 
- gcc
- make
- Un servidor FTP funcionando (ej. vsftpd)

## 📁 Estructura del Código
```
Proyecto-Cliente-FTP/
├── RomeroE-clienteFTP.c          # Cliente FTP principal
├── connectTCP.c                 
├── passiveTCP.c                  
├── connectsock.c        
├── passivesock.c        
├── errexit.c            
├── Makefile
└── README.md
```
## 🛠️ Compilación

Compilar simplemente con:
```
make
```
Esto generará el ejecutable:
```
RomeroE-clienteFTP
```

Si deseas limpiar los objetos:
```
make clean
```

## Características Principales

###  **1. Conexión y Autenticación**

- Conexión al servidor FTP mediante **TCP**  
- Lectura del **banner inicial** (código `220`)  
- Autenticación mediante **USER / PASS**  
- **Ocultamiento de contraseña** usando `getpass()`  

---

###  **2. Modos de Transferencia**

#### **PASV (por defecto)**
- El servidor abre un puerto de datos.
- El cliente se conecta al puerto indicado por el servidor.

#### **PORT (modo activo)**
- El cliente abre un socket local en un **puerto efímero**.
- El servidor se conecta de vuelta al cliente.

---

###  **3. Transferencias Concurrentes**

- Cada transferencia (`RETR` / `STOR`) se ejecuta en un **proceso hijo independiente** usando `fork()`.  
- El proceso padre **permanece disponible** para seguir recibiendo comandos por el canal de control.  
- Soporte para múltiples descargas/subidas simultáneas.

---

###  **4. Comandos Soportados**

####  **Comandos implementados directamente**
| Comando        | Descripción                              |
|----------------|-------------------------------------------|
| `ls` / `dir`   | Lista el directorio remoto                |
| `cd <dir>`     | Cambia el directorio remoto               |
| `get <archivo>`| Descarga archivo (RETR)                   |
| `put <archivo>`| Sube archivo (STOR)                       |
| `mode pasv`    | Cambia a modo PASV                        |
| `mode act`     | Cambia a modo PORT                        |
| `quit`         | Finaliza la sesión FTP                    |

####  **Comandos RAW**


## 🚀 Ejemplos de Uso
### ✨ 1. Iniciar el cliente
```
./RomeroE-clienteFTP <host> [puerto]
```

Ejemplos:
```
./RomeroE-clienteFTP 192.168.1.10
./RomeroE-clienteFTP 192.168.1.10 21
```

El cliente pedirá:
```
Usuario:
Contraseña:
```

### 📂 2. Listar contenido del servidor
```ftp > ls```

### 📥 3. Descargar un archivo (GET)
```ftp > get archivo.txt```

Esto genera un proceso hijo que maneja solo la transferencia.
Puedes seguir usando el cliente mientras descarga.

### 📤 4. Subir un archivo (PUT)
```ftp > put archivo_local.txt```

También se ejecuta concurrentemente.

### 🔄 5. Cambiar el modo de transferencia
Activar PASV (por defecto)
```ftp > mode pasv```

Activar PORT
```ftp > mode act```

### 📁 6. Cambiar directorio
```ftp > cd Carpeta```

### 🆘 7. Ayuda
```
ftp > help
--------------------------------------------------------------------------
                    Cliente FTP – Comandos disponibles             
--------------------------------------------------------------------------
  help                    - Muestra esta ayuda
  ls  | dir               - Lista el directorio actual del servidor
  get <remoto>            - Descarga un archivo del servidor (RETR)
  put <local>             - Sube un archivo al servidor (STOR)
  cd <directorio>         - Cambia el directorio remoto (CWD)
  mode                    - Muestra o cambia el modo de transferencia
  mode pasv               - Cambia a modo PASIVO (PASV)
  mode act                - Cambia a modo ACTIVO (PORT)
  quit | exit             - Finaliza la sesión FTP
---------------------------------------------------------------------------
```

### ❌ 8. Salir
```ftp > quit```

---
## 🤝 Autor

Erick Romero
Proyecto para la asignatura COMPUTACIÓN DISTRIBUIDA (ICCD654)
