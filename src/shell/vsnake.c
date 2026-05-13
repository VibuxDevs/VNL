#include "vsnake.h"
#include "vga.h"
#include "printf.h"
#include "keyboard.h"
#include "timer.h"
#include "vfs.h"
#include "string.h"

#define WIDTH 40
#define HEIGHT 20

typedef struct { int x, y; } Point;

void cmd_vsnake(int argc, char **argv) {
    (void)argc; (void)argv;

    if (vfs_resolve("/usr/bin/vsnake") < 0) {
        kprintf("vsnake: Game not installed. Run 'vpkg install vsnake' to fetch binary assets.\n");
        return;
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    Point snake[WIDTH * HEIGHT];
    int length = 3;
    snake[0] = (Point){10, 10};
    snake[1] = (Point){9, 10};
    snake[2] = (Point){8, 10};

    int dx = 1, dy = 0;
    Point food = {15, 10};
    int score = 0;
    bool running = true;

    while (running) {
        /* Draw Board */
        vga_set_cursor(1, 1);
        vga_set_color(VGA_LGREEN, VGA_BLACK);
        kprintf("         V N L   S N A K E   -   Score: %05d         \n", score);
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("====================================================\n");

        for (int y = 0; y < HEIGHT; y++) {
            vga_set_cursor(3 + y, 5);
            vga_set_color(VGA_DGRAY, VGA_BLACK);
            vga_putchar('|');
            for (int x = 0; x < WIDTH; x++) {
                bool is_snake = false;
                for (int i = 0; i < length; i++) {
                    if (snake[i].x == x && snake[i].y == y) {
                        vga_set_color(i == 0 ? VGA_LRED : VGA_GREEN, VGA_BLACK);
                        vga_putchar(i == 0 ? '@' : 'o');
                        is_snake = true;
                        break;
                    }
                }
                if (!is_snake) {
                    if (food.x == x && food.y == y) {
                        vga_set_color(VGA_YELLOW, VGA_BLACK);
                        vga_putchar('*');
                    } else {
                        vga_putchar(' ');
                    }
                }
            }
            vga_set_color(VGA_DGRAY, VGA_BLACK);
            vga_putchar('|');
        }
        vga_set_cursor(3 + HEIGHT, 5);
        for(int i=0; i<WIDTH+2; i++) vga_putchar('-');

        /* Input */
        int k = keyboard_poll();
        if (k == 'q' || k == 27) break;
        if (k == KEY_UP && dy == 0) { dx = 0; dy = -1; }
        else if (k == KEY_DOWN && dy == 0) { dx = 0; dy = 1; }
        else if (k == KEY_LEFT && dx == 0) { dx = -1; dy = 0; }
        else if (k == KEY_RIGHT && dx == 0) { dx = 1; dy = 0; }

        /* Logic */
        Point next = {snake[0].x + dx, snake[0].y + dy};
        if (next.x < 0 || next.x >= WIDTH || next.y < 0 || next.y >= HEIGHT) running = false;
        for (int i = 0; i < length; i++) if (snake[i].x == next.x && snake[i].y == next.y) running = false;

        if (next.x == food.x && next.y == food.y) {
            length++;
            score += 100;
            food.x = (food.x * 7 + 13) % WIDTH;
            food.y = (food.y * 3 + 7) % HEIGHT;
        }

        for (int i = length - 1; i > 0; i--) snake[i] = snake[i - 1];
        snake[0] = next;

        timer_sleep(100);
    }

    vga_set_cursor(10, 15);
    vga_set_color(VGA_LRED, VGA_BLACK);
    kprintf(" G A M E   O V E R ");
    vga_set_cursor(12, 15);
    kprintf(" Final Score: %d ", score);
    timer_sleep(2000);
    vga_clear();
}
