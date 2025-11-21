# ============================================================
#  Makefile - Cliente FTP Concurrente (RFC 959)
#  Autor: Erick
#  Archivos: clientFTP.c + helpers (connectTCP, passiveTCP...)
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -g
OBJS    = RomeroE-clienteFTP.o connectTCP.o passiveTCP.o connectsock.o passivesock.o errexit.o

TARGET  = RomeroE-clienteFTP

# ------------------------------------------------------------
# Regla principal
# ------------------------------------------------------------
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# ------------------------------------------------------------
# Reglas de compilación generales
# ------------------------------------------------------------
%.o: %.c
	$(CC) $(CFLAGS) -c $<

# ------------------------------------------------------------
# Limpieza
# ------------------------------------------------------------
clean:
	rm -f *.o $(TARGET)

# ------------------------------------------------------------
# Forzar recompilación completa
# ------------------------------------------------------------
rebuild: clean all
