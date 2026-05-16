CC ?= gcc
CFLAGS ?= -O1 -Wall -Wextra -std=c11 -pthread
INCLUDES = -Iinclude -I../driver
PKG_CFLAGS := $(shell pkg-config --cflags libmosquitto 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs libmosquitto 2>/dev/null)

APP_SRC = \
	src/slgw_app.c \
	src/slgw_config.c \
	src/slgw_topics.c \
	src/slgw_protocol.c \
	src/slgw_json.c \
	src/slgw_text.c \
	src/slgw_process_lock.c \
	src/slgw_publish_buffer.c \
	src/slgw_transport.c \
	src/slgw_transport_driver.c \
	src/slgw_transport_serial.c

SRC = \
	src/main.c \
	$(APP_SRC)

TARGET = smartlight_gateway_c
MOCK_TARGET = smartlight_gateway_c_mock
MOCK_SRC = test_support/src/mock_mosquitto.c
MOCK_INCLUDES = -Itest_support/include
MOCK_TEST_TARGET = test_gateway_smoke_mock
MOCK_TEST_SRC = tests/test_gateway_smoke.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(PKG_CFLAGS) -o $@ $(SRC) $(PKG_LIBS)

mock: $(MOCK_TARGET)

$(MOCK_TARGET): $(SRC) $(MOCK_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(MOCK_INCLUDES) -o $@ $(SRC) $(MOCK_SRC)

mock-test: $(MOCK_TEST_TARGET)

$(MOCK_TEST_TARGET): $(APP_SRC) $(MOCK_SRC) $(MOCK_TEST_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(MOCK_INCLUDES) -o $@ $(MOCK_TEST_SRC) $(APP_SRC) $(MOCK_SRC)

check: mock-test
	./$(MOCK_TEST_TARGET)

clean:
	rm -f $(TARGET) $(MOCK_TARGET) $(MOCK_TEST_TARGET)
