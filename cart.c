#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <locale.h>
#include <mpg123.h>
#include <ao/ao.h>
#include <pthread.h>
#include <ncurses.h>

#define MUSIC_DIR "/home/carter/media/music/"
#define MAX_SONGS 1000

char *songs[MAX_SONGS];
int num_songs = 0;
int current_song = 0;
int playing_song = -1;
int paused = 0;
int scroll_offset = 0;
pthread_t play_thread;

void* play_song(void* arg) {
    int index = *(int*)arg;
    mpg123_handle *mh;
    unsigned char *buffer;
    size_t buffer_size;
    size_t done;
    int err;
    long rate;
    int channels, encoding;

    ao_device *dev;
    ao_sample_format format;
    int driver;

    ao_initialize();
    driver = ao_default_driver_id();

    mpg123_init();
    mh = mpg123_new(NULL, &err);
    buffer_size = mpg123_outblock(mh);
    buffer = (unsigned char*) malloc(buffer_size * sizeof(unsigned char));

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", MUSIC_DIR, songs[index]);

    mpg123_open(mh, full_path);
    mpg123_getformat(mh, &rate, &channels, &encoding);

    format.bits = mpg123_encsize(encoding) * 8;
    format.rate = rate;
    format.channels = channels;
    format.byte_format = AO_FMT_NATIVE;
    format.matrix = 0;
    dev = ao_open_live(driver, &format, NULL);

    while (mpg123_read(mh, buffer, buffer_size, &done) == MPG123_OK) {
		if (!paused) {
			ao_play(dev, buffer, done);
		}
    }

    free(buffer);
    ao_close(dev);
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
    ao_shutdown();

    return NULL;
}

void load_songs() {
    DIR *dir = opendir(MUSIC_DIR);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            songs[num_songs] = strdup(entry->d_name);
            num_songs++;
        }
    }
    closedir(dir);
}

void draw_song(int i) {
    if (i == playing_song) {
        attron(COLOR_PAIR(1) | A_BOLD); // Use color pair 1 (yellow on black), and make it bold
    }
    if (i == current_song) {
        if (i == playing_song) {
            attron(A_REVERSE | COLOR_PAIR(1)); // If the song is playing, make the foreground yellow
        } else {
            attron(A_REVERSE);
        }
    }
    mvprintw(i - scroll_offset, 0, "%s", songs[i]);
    attroff(A_REVERSE | A_BOLD | COLOR_PAIR(1));
}

void draw_songs() {
    int max_y = 0, max_x = 0;
    getmaxyx(stdscr, max_y, max_x);
    clear();
    for (int i = scroll_offset; i < num_songs && i < scroll_offset + max_y; i++) {
        draw_song(i);
    }
    refresh();
}

void handle_signal(int sig) {
    int old_song = playing_song;
    if (sig == SIGRTMIN + 10) {
        playing_song = (playing_song + 1) % num_songs;
    } else if (sig == SIGRTMIN + 11) {
        playing_song = (playing_song - 1 + num_songs) % num_songs;
    } else if (sig == SIGRTMIN + 12) {
        paused = !paused;
    }
    if (playing_song != old_song) {
        if (old_song != -1) {
            pthread_cancel(play_thread);
        }
        pthread_create(&play_thread, NULL, play_song, &playing_song);
        draw_song(old_song);
        draw_song(playing_song);
    }
}


void handle_input(int* running) {
    int ch = getch();
    int old_song;
    switch (ch) {
        case 'j':
            old_song = current_song;
            current_song = (current_song + 1) % num_songs;
            if (current_song >= scroll_offset + LINES) {
                scroll_offset++;
                draw_songs();
            } else {
                draw_song(old_song);
                draw_song(current_song);
            }
            break;
        case 'k':
            old_song = current_song;
            current_song = (current_song - 1 + num_songs) % num_songs;
            if (current_song < scroll_offset) {
                scroll_offset--;
                draw_songs();
            } else {
                draw_song(old_song);
                draw_song(current_song);
            }
            break;
        case '\n':
            old_song = playing_song;
            if (playing_song != -1) {
                pthread_cancel(play_thread);
            }
            playing_song = current_song;
            pthread_create(&play_thread, NULL, play_song, &current_song);
            draw_song(old_song);
            draw_song(playing_song);
            break;
        case 'Q':
        case 'q':
            *running = 0;
            break;
        case ' ':
            paused = !paused;
            //if (!paused) {
            // pthread_create(&play_thread, NULL, play_song, &current_song);
            //}
            draw_songs();
            break;
		case 'g':
			current_song = 0;
			scroll_offset = 0;
			draw_songs();
			break;
		case 'G':
			current_song = num_songs - 1;
			scroll_offset = current_song - LINES + 1;
			draw_songs();
			break;
        case KEY_RESIZE:
            draw_songs();
            break;
        default:
            break;
    }
}

int main() {
    setlocale(LC_ALL, "");

    signal(SIGRTMIN + 10, handle_signal); // next
    signal(SIGRTMIN + 11, handle_signal); // prev
signal(SIGRTMIN + 12, handle_signal); // pause/unpause

    initscr();  // Initialize the ncurses library
	start_color();
	init_pair(1, COLOR_YELLOW, COLOR_BLACK); // Pair 1 will be yellow on black
    cbreak();   // Line buffering disabled, pass everything to play_song
    noecho();   // Don't echo() while we getch
    keypad(stdscr, TRUE); // We get special keys
    // timeout(100); // Wait 100ms for key press, then continue

    load_songs();
    draw_songs();

	int running = 1;
    while (running) {
        handle_input(&running);
    }

    endwin(); // End ncurses mode
    return 0;
}
