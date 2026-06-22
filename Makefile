CC = gcc
CFLAGS = -fPIC -O3 -Wall -shared
LDFLAGS = -lm
TARGET = True-Peak_Limiter.so

all: $(TARGET)

$(TARGET): true-peak_limiter.c ladspa.h
	@$(CC) $(CFLAGS) -o $(TARGET) true-peak_limiter.c $(LDFLAGS)
	@echo "Compilation done. $(TARGET) created."

install: $(TARGET)
	@sudo cp $(TARGET) /usr/lib/ladspa/
	@sudo ldconfig
	@echo "Done. Plugin installed to /usr/lib/ladspa/$(TARGET)"

clean:
	rm -f $(TARGET)

uninstall:
	@sudo rm -f /usr/lib/ladspa/$(TARGET)
	@echo "Done. Plugin removed from /usr/lib/ladspa/$(TARGET)"
