#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/tiocl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <libinput.h>

static int open_restricted(const char *path, int flags, void *user_data) {
    (void)user_data;
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    (void)user_data;
    close(fd);
}
const static struct libinput_interface interface = {
        .open_restricted = open_restricted,
        .close_restricted = close_restricted,
};

struct wrapper {
    int dummy;
    struct tiocl_selection data;
};

int con_get_mouse_reporting(int fd) {
    char cmd;
    cmd = TIOCL_GETMOUSEREPORTING;

    if (ioctl(fd, TIOCLINUX, &cmd) < 0) {
        fprintf(stderr, "con_get_mouse_reporting: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
    return cmd;
}

#define LINUX_SHIFT 1
#define LINUX_CTRL 4
#define LINUX_ALT 8

int con_get_shift_state(int fd) {
    char cmd;
    cmd = TIOCL_GETSHIFTSTATE;

    if (ioctl(fd, TIOCLINUX, &cmd) < 0) {
        fprintf(stderr, "con_get_shift_state: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
    // bit flag: 1 = shift, 2 = ?, 4 = ctrl, 8 = alt
    return cmd;
}

void con_report_click(int fd, int x, int y, int btn) {
    struct wrapper w;

    char *cmd = ((char*)&w.data)-1;
    *cmd = TIOCL_SETSEL;
    w.data.sel_mode = TIOCL_SELMOUSEREPORT | (btn & TIOCL_SELBUTTONMASK);
    w.data.xs = (unsigned short)(x + 1);
    w.data.ys = (unsigned short)(y + 1);

    if (ioctl(fd, TIOCLINUX, cmd) < 0) {
        fprintf(stderr, "ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
}

void con_move_pointer(int fd, int x, int y) {
    struct wrapper w;

    char *cmd = ((char*)&w.data)-1;
    *cmd = TIOCL_SETSEL;
    w.data.sel_mode = TIOCL_SELPOINTER;
    w.data.xs = (unsigned short)(x + 1);
    w.data.ys = (unsigned short)(y + 1);
    w.data.xe = (unsigned short)(x + 1);
    w.data.ye = (unsigned short)(y + 1);

    if (ioctl(fd, TIOCLINUX, cmd) < 0) {
        fprintf(stderr, "con_move_pointer: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
}

void con_scroll(int fd, int amount) {
    int32_t req[] = {0, 0};

    char *cmd = ((char*)req);
    *cmd = TIOCL_SCROLLCONSOLE;
    req[1] = amount;
    if (ioctl(fd, TIOCLINUX, cmd) < 0) {
        fprintf(stderr, "con_scroll: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
}

void con_update_selection(int fd, int start_x, int start_y, int end_x, int end_y, int kind) {
    struct wrapper w;

    char *cmd = ((char*)&w.data)-1;
    *cmd = TIOCL_SETSEL;
    switch (kind) {
        default:
        case 0:
            w.data.sel_mode = TIOCL_SELCHAR;
            break;
        case 1:
            w.data.sel_mode = TIOCL_SELWORD;
            break;
        case 2:
            w.data.sel_mode = TIOCL_SELLINE;
            break;
    }
    w.data.xs = (unsigned short)(start_x + 1);
    w.data.ys = (unsigned short)(start_y + 1);
    w.data.xe = (unsigned short)(end_x + 1);
    w.data.ye = (unsigned short)(end_y + 1);

    if (ioctl(fd, TIOCLINUX, cmd) < 0) {
        fprintf(stderr, "con_update_selection: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
}

void con_paste(int fd) {
    char cmd;
    cmd = TIOCL_PASTESEL;

    if (ioctl(fd, TIOCLINUX, &cmd) < 0) {
        fprintf(stderr, "con_paste: ioctl(fd, TIOCLINUX): %s\n", strerror(errno));
        exit(1);
    }
}

struct winsize console_size;

bool poll_state(int *fd) {

    // This is currently completely polled. This is acceptable for now but only barely.
    close(*fd);
    *fd = open("/dev/tty0", O_RDWR);
    if (*fd < 0) {
        perror("/dev/tty0");
        exit(1);
    }

    int out;
    if (ioctl(*fd, KDGETMODE, &out) < 0) {
        perror("KDGETMODE");
        exit(1);
    }

    if (out == KD_GRAPHICS) {
        // console is in graphics mode (X, wayland, etc), just ignore for now.
        return false;
    }

    if (ioctl(*fd, TIOCGWINSZ, &console_size) < 0) {
        perror("TIOCGWINSZ");
        exit(1);
    }

    return true;
}

int main( int argc, char **argv ) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) {
        perror("/dev/tty0");
        exit(1);
    }

    con_move_pointer(fd, 0, 0);


    struct libinput *li;
    struct libinput_event *event;

    struct udev *udev = udev_new();

    if (!udev) {
        fprintf(stderr, "Failed to initialize udev\n");
        return 1;
    }

    li = libinput_udev_create_context(&interface, NULL, udev);

    udev_unref(udev);

    int pointer_x = 0;
    int pointer_y = 0;

    double pointer_sub_x = 0;
    double pointer_sub_y = 0;

    int start_x = -1;
    int start_y = -1;
    int selection_kind = 0;
    int64_t last_click_time = 0;

    bool buttons[3] = {false, false, false};

    libinput_udev_assign_seat(li, "seat0");


    struct pollfd fds;
    fds.fd = libinput_get_fd(li);
    fds.events = POLLIN;
    fds.revents = 0;

    while (1) {
        libinput_dispatch(li);
        while ((event = libinput_get_event(li)) != NULL) {
            if (libinput_event_get_type(event) == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
                if (poll_state(&fd)) {
                    struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
                    double x = libinput_event_pointer_get_absolute_x_transformed(p, console_size.ws_col);
                    double y = libinput_event_pointer_get_absolute_y_transformed(p, console_size.ws_row);

                    pointer_x = (int)x;
                    pointer_y = (int)y;

                    pointer_sub_x = 0;
                    pointer_sub_y = 0;

                    // see twin in LIBINPUT_EVENT_POINTER_MOTION
                    if (start_x == -1) {
                        con_move_pointer(fd, pointer_x, pointer_y);
                    } else {
                        con_update_selection(fd, start_x, start_y, pointer_x, pointer_y, selection_kind);
                    }
                }
            }
            if (libinput_event_get_type(event) == LIBINPUT_EVENT_POINTER_MOTION) {
                if (poll_state(&fd)) {
                    struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
                    pointer_sub_x += 0.03 * libinput_event_pointer_get_dx(p);
                    pointer_sub_y += 0.03 * libinput_event_pointer_get_dy(p);

                    pointer_x = pointer_x + (int)pointer_sub_x;
                    pointer_y = pointer_y + (int)pointer_sub_y;

                    pointer_sub_x -= (int)pointer_sub_x;
                    pointer_sub_y -= (int)pointer_sub_y;

                    if (pointer_x < 0) pointer_x = 0;
                    if (pointer_x >= console_size.ws_col) pointer_x = console_size.ws_col - 1;

                    if (pointer_y < 0) pointer_y = 0;
                    if (pointer_y >= console_size.ws_row) pointer_y = console_size.ws_row - 1;

                    // see twin in LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE
                    if (start_x == -1) {
                        con_move_pointer(fd, pointer_x, pointer_y);
                    } else {
                        con_update_selection(fd, start_x, start_y, pointer_x, pointer_y, selection_kind);
                    }
                }
            }
            if (libinput_event_get_type(event) == LIBINPUT_EVENT_POINTER_BUTTON) {
                if (poll_state(&fd)) {
                    struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
                    uint32_t button = libinput_event_pointer_get_button(p);
                    enum libinput_button_state state = libinput_event_pointer_get_button_state(p);
                    int mouse_reporting_mode = con_get_mouse_reporting(fd) & 0xf;
                    if (mouse_reporting_mode) {
                        int button_nr = -1;
                        switch (button) {
                            case BTN_LEFT:
                                button_nr = 0;
                                break;
                            case BTN_MIDDLE:
                                button_nr = 1;
                                break;
                            case BTN_RIGHT:
                                button_nr = 2;
                                break;
                        }
                        if (button_nr != -1) {
                            buttons[button_nr] = state == LIBINPUT_BUTTON_STATE_PRESSED;
                        }
                        // overflow `'!' + mrx` in the kernel to '\0' to signal out of range.
                        int report_x = pointer_x < 223 ? pointer_x : 223;
                        int report_y = pointer_y < 223 ? pointer_y : 223;
                        if (mouse_reporting_mode == 1) {
                            if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
                                con_report_click(fd, report_x, report_y, button_nr);
                            }
                        } else {
                            int btn = 3;
                            if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
                                btn = button_nr;
                            }
                            int shift_state = con_get_shift_state(fd);
                            if (shift_state & LINUX_SHIFT) {
                                btn |= 4;
                            }
                            if (shift_state & LINUX_ALT) {
                                btn |= 8;
                            }
                            con_report_click(fd, report_x, report_y, btn);
                        }
                    } else {
                        if (button == BTN_LEFT) {
                            if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
                                struct timespec tp;
                                int ret = clock_gettime(CLOCK_MONOTONIC, &tp);
                                int64_t current_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000 / 1000;
                                if (current_time - last_click_time < 500 && selection_kind < 3) {
                                    selection_kind++;
                                } else {
                                    selection_kind = 0;
                                }
                                last_click_time = current_time;
                                start_x = pointer_x;
                                start_y = pointer_y;
                                con_update_selection(fd, start_x, start_y, pointer_x, pointer_y, selection_kind);
                            } else {
                                con_update_selection(fd, start_x, start_y, pointer_x, pointer_y, selection_kind);
                                start_x = -1;
                                start_y = -1;
                            }
                        }
                        if (button == BTN_MIDDLE) {
                            if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
                                con_paste(fd);
                            }
                        }
                    }
                }
            }
            if (libinput_event_get_type(event) == LIBINPUT_EVENT_POINTER_AXIS) {
                if (poll_state(&fd)) {
                    struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                        double v = libinput_event_pointer_get_axis_value(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
                        con_scroll(fd, (int)(v / 15 * 3));
                    }
                }
            }

            libinput_event_destroy(event);
            libinput_dispatch(li);
        }
        poll(&fds, 1, -1);
    };
    libinput_unref(li);

    return 0;
}
