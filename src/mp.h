#ifndef MP_H
#define MP_H

#include <string>
#include <string_view>
#include <vector>

struct Song {
    std::string title;
    std::string album;
    std::string artist;
    std::string track;
};

struct MPContext {
    Song current_song;
};

extern MPContext mp_ctx;

void mp_init();
void mp_add_song(const std::string& song_path);
void mp_add_songs(const std::vector<std::string>& song_paths);
void mp_cleanup();

#endif
