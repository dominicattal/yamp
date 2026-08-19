#include "mp.h"
#include "db.h"
#include <iostream>
#include <cassert>
#include <miniaudio.h>
#include <id3v2lib.h>

struct MPContextInternal {
    ma_engine engine;
    ID3v2_Tag* current_song_tag;
    ma_sound current_song_sound;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void reset_current_song()
{
    mp_ctx.current_song.title = "";
    mp_ctx.current_song.album = "";
    mp_ctx.current_song.artist = "";
    mp_ctx.current_song.track = "";
}

void mp_init()
{
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);
    
    reset_current_song();
}

void mp_cleanup()
{
    if (ctx.current_song_tag != nullptr) {
        ma_sound_uninit(&ctx.current_song_sound);
        ID3v2_Tag_free(ctx.current_song_tag);
    }
    ma_engine_uninit(&ctx.engine);
    db_cleanup();
}

void mp_add_song(const std::string& song_path)
{
    (void)song_path;
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    (void)song_paths;
}

void mp_play_song(const std::string& song_path)
{
    std::cout << song_path << '\n';

    const char* song_path_c_str = song_path.c_str();

    //ma_engine_play_sound(&ctx.engine, song_path_c_str, NULL);

    if (ctx.current_song_tag != nullptr) {
        ma_sound_uninit(&ctx.current_song_sound);
        ID3v2_Tag_free(ctx.current_song_tag);
    }

    ma_sound_init_from_file(&ctx.engine, song_path_c_str, 0, NULL, NULL, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);

    ctx.current_song_tag = ID3v2_read_tag(song_path_c_str);

    ID3v2_TextFrame* frame;
    reset_current_song();
    frame = ID3v2_Tag_get_title_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.title = frame->data->text;
    frame = ID3v2_Tag_get_album_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.album = frame->data->text;
    frame = ID3v2_Tag_get_artist_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.artist = frame->data->text;
    frame = ID3v2_Tag_get_track_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.track = frame->data->text;
}

void mp_queue_song(const std::string& song_path)
{
    mp_ctx.queue.push_back(song_path);
}

void mp_queue_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_queue_song(song_path);
}

void mp_queue_skip()
{
    std::string song_path = mp_ctx.queue.front();
    mp_ctx.queue.pop_front();
    mp_play_song(song_path);
}
