#define _POSIX_C_SOURCE 200809L
#include "slgw_transport_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "slgw_app.h"
#include "slgw_text.h"

static void slgw_sleep_ms(int delay_ms) {
    struct timespec ts;
    if (delay_ms <= 0) {
        return;
    }
    ts.tv_sec = delay_ms / 1000;
    ts.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static speed_t slgw_serial_baud_to_flag(int baudrate) {
    switch (baudrate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B115200;
    }
}

static void slgw_serial_close_fd(slgw_serial_transport *transport, const char *reason) {
    int fd = transport->fd;
    transport->fd = -1;
    transport->connected = false;
    if (fd >= 0) {
        close(fd);
    }
    slgw_app_notify_transport_state(transport->app, false, reason == NULL ? "serial_closed" : reason);
}

static int slgw_serial_open_fd(slgw_serial_transport *transport) {
    struct termios tty;
    speed_t baud_flag;
    int fd = open(transport->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }
    baud_flag = slgw_serial_baud_to_flag(transport->baudrate);
    cfsetispeed(&tty, baud_flag);
    cfsetospeed(&tty, baud_flag);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY);
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }
    transport->fd = fd;
    transport->connected = true;
    slgw_app_notify_transport_state(transport->app, true, "connected");
    return 0;
}

static void *slgw_serial_reader_main(void *arg) {
    slgw_serial_transport *transport = (slgw_serial_transport *)arg;
    char partial[SLGW_LINE_SIZE];
    size_t partial_len = 0;
    memset(partial, 0, sizeof(partial));

    while (!transport->stop_requested) {
        fd_set readfds;
        struct timeval tv;
        int rc;
        int fd;

        if (transport->fd < 0) {
            if (slgw_serial_open_fd(transport) != 0) {
                slgw_sleep_ms(transport->reconnect_delay_ms);
                continue;
            }
        }

        fd = transport->fd;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = transport->read_timeout_ms / 1000;
        tv.tv_usec = (transport->read_timeout_ms % 1000) * 1000;
        rc = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (rc < 0) {
            slgw_serial_close_fd(transport, "select_failed");
            slgw_sleep_ms(transport->reconnect_delay_ms);
            continue;
        }
        if (rc == 0) {
            continue;
        }
        if (FD_ISSET(fd, &readfds)) {
            char chunk[128];
            ssize_t nread = read(fd, chunk, sizeof(chunk));
            ssize_t idx;
            if (nread <= 0) {
                slgw_serial_close_fd(transport, nread == 0 ? "read_eof" : "read_failed");
                slgw_sleep_ms(transport->reconnect_delay_ms);
                continue;
            }
            for (idx = 0; idx < nread; ++idx) {
                char ch = chunk[idx];
                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n') {
                    if (partial_len > 0) {
                        partial[partial_len] = '\0';
                        slgw_app_handle_driver_line(transport->app, partial);
                        partial_len = 0;
                        partial[0] = '\0';
                    }
                    continue;
                }
                if (partial_len + 1 < sizeof(partial)) {
                    partial[partial_len++] = ch;
                    partial[partial_len] = '\0';
                }
            }
        }
    }
    return NULL;
}

int slgw_serial_transport_init(slgw_serial_transport *transport, struct slgw_app *app, const slgw_config *cfg) {
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->app = app;
    transport->baudrate = cfg->serial_baudrate;
    transport->reconnect_delay_ms = cfg->transport_reconnect_delay_ms;
    transport->read_timeout_ms = cfg->serial_timeout_ms;
    if (slgw_copy_text(transport->device, sizeof(transport->device), cfg->serial_port) != 0) {
        return -1;
    }
    pthread_mutex_init(&transport->io_mutex, NULL);
    return 0;
}

int slgw_serial_transport_start(slgw_serial_transport *transport) {
    transport->running = true;
    transport->stop_requested = false;
    return pthread_create(&transport->reader_thread, NULL, slgw_serial_reader_main, transport);
}

void slgw_serial_transport_stop(slgw_serial_transport *transport) {
    transport->stop_requested = true;
    transport->running = false;
    if (transport->reader_thread) {
        pthread_join(transport->reader_thread, NULL);
    }
    if (transport->fd >= 0) {
        slgw_serial_close_fd(transport, "stopped");
    }
    pthread_mutex_destroy(&transport->io_mutex);
}

int slgw_serial_transport_write_line(slgw_serial_transport *transport, const char *line) {
    char payload[SLGW_LINE_SIZE];
    ssize_t nwritten;
    if (transport->fd < 0) {
        return -1;
    }
    if (slgw_format(payload, sizeof(payload), "%s\n", line) != 0) {
        return -1;
    }
    pthread_mutex_lock(&transport->io_mutex);
    nwritten = write(transport->fd, payload, strlen(payload));
    pthread_mutex_unlock(&transport->io_mutex);
    if (nwritten < 0) {
        slgw_serial_close_fd(transport, "write_failed");
        return -1;
    }
    return 0;
}
