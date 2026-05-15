#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "smartlight_ioctl.h"

static void print_frame(const char *name, const smartlight_frame_t *f)
{
    printf("%s length=%u data=\"%.*s\"\n",
           name,
           f->length,
           (int)f->length,
           f->data);
}

int main(int argc, char **argv)
{
    const char *devpath = argc > 1 ? argv[1] : "/dev/smartlight0";
    int fd = open(devpath, O_RDWR);
    smartlight_status_v3_t st_v3;
    smartlight_status_v5_t st_v5;
    smartlight_stats_t stats;
    smartlight_frame_t rx;
    smartlight_frame_t tx;

    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, SL_IOC_GET_LAST_STATUS_V5, &st_v5) == 0) {
        printf("status_v5: mode=%s scene=%s brightness=%d flow=%u preset=%s manual_override=%u\n",
               st_v5.active_mode,
               st_v5.active_scene,
               st_v5.brightness_pct,
               st_v5.flow_enabled,
               st_v5.flow_preset,
               st_v5.manual_override);
    } else {
        perror("ioctl(GET_LAST_STATUS_V5)");
    }

    if (ioctl(fd, SL_IOC_GET_LAST_STATUS, &st_v3) == 0) {
        printf("status_v3_compat: mode=%s scene=%s report=%u flow=%u seq=%u lux=%.2f\n",
               st_v3.mode,
               st_v3.scene,
               st_v3.report_enabled,
               st_v3.flow_enabled,
               st_v3.seq,
               st_v3.lux_x100 / 100.0);
    } else {
        perror("ioctl(GET_LAST_STATUS)");
    }

    if (ioctl(fd, SL_IOC_GET_STATS, &stats) == 0) {
        printf("stats: open=%u tx=%u rx=%u dropped=%u parse_err=%u wr_err=%u rd_err=%u\n",
               stats.open_count,
               stats.tx_frames,
               stats.rx_frames,
               stats.dropped_frames,
               stats.parse_errors,
               stats.uart_write_errors,
               stats.uart_read_errors);
    } else {
        perror("ioctl(GET_STATS)");
    }

    if (ioctl(fd, SL_IOC_GET_LAST_RX_FRAME, &rx) == 0) {
        print_frame("last_rx", &rx);
    } else {
        perror("ioctl(GET_LAST_RX_FRAME)");
    }

    if (ioctl(fd, SL_IOC_GET_LAST_TX_FRAME, &tx) == 0) {
        print_frame("last_tx", &tx);
    } else {
        perror("ioctl(GET_LAST_TX_FRAME)");
    }

    close(fd);
    return 0;
}
