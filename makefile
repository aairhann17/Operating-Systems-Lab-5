# Variables
CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = group29_bankers
SRC = group29_banker.c

# Default rule
all: $(TARGET)

# Link the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean up build files
clean:
	rm -f $(TARGET)

# Run the program
run: $(TARGET)
	./$(TARGET)