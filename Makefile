CC = gcc
CFLAGS = -Wall
TARGET = snap

$(TARGET): snap.c
	$(CC) $(CFLAGS) snap.c -o $(TARGET)

clean:
	rm -f $(TARGET)



