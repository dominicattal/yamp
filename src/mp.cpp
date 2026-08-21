#include "mp.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <cassert>
#include <miniaudio.h>
#include <id3v2lib.h>
#include <sqlite3.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

struct MPContextInternal {
    ma_engine engine;
    ID3v2_Tag* current_song_tag;
    ma_sound current_song_sound;
    sqlite3* db;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg;
    sqlite3_exec(ctx.db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("SQLite3 error: %s", error_msg);
        sqlite3_free(error_msg);
    }
}

static void reset_current_song()
{
    mp_ctx.current_song.title = "";
    mp_ctx.current_song.artist = "";
    mp_ctx.current_song.track = "";
    mp_ctx.current_song.path = "";
    mp_ctx.current_song.id = 0;
}

static int song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int id = std::atoi(values[0]);
    std::string title = values[1];
    std::string artist = values[2];
    //int date = std::atoi(values[3]);
    std::string track = values[4];
    std::string path = values[5];
    mp_ctx.songs.emplace_back(title, artist, track, path, id);
    SPDLOG_INFO("Loaded song: {}|{}|{}", values[1], values[2], values[4]);
    return 0;
}

static int album_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    Album* album = static_cast<Album*>(data);
    int song_id = std::atoi(values[0]);
    album->songs.push_back(mp_get_song_by_id(song_id));
    return 0;
}

static int album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int album_id = std::atoi(values[0]);
    mp_ctx.albums.emplace_back(values[1], std::vector<Song*>{}, album_id);
    std::string query = std::format("SELECT * FROM AlbumSong WHERE album_id={}", album_id);
    sqlite3_exec(ctx.db, query.c_str(), album_song_callback, &mp_ctx.albums.back(), NULL);
    return 0;
}

static int playlist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    Playlist* playlist = static_cast<Playlist*>(data);
    int song_id = std::atoi(values[0]);
    playlist->songs.push_back(mp_get_song_by_id(song_id));
    return 0;
}

static int playlist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int playlist_id = std::atoi(values[0]);
    mp_ctx.playlists.emplace_back(values[1], std::vector<Song*>{}, playlist_id);
    std::string query = std::format("SELECT * FROM PlaylistSong WHERE playlist_id={}", playlist_id);
    sqlite3_exec(ctx.db, query.c_str(), playlist_song_callback, &mp_ctx.playlists.back(), NULL);
    return 0;
}

static std::string& db_fmt_str(std::string& string)
{
    int num_apostrophes = std::count(string.begin(), string.end(), '\'');
    if (num_apostrophes == 0)
        return string;

    size_t old_len = string.size();
    string.resize(old_len + num_apostrophes);
    for (ssize_t idx = old_len-1; idx >= 0; --idx) {
        string[idx+num_apostrophes] = string[idx];
        if (string[idx] == '\'') {
            string[idx+num_apostrophes-1] = string[idx];
            --num_apostrophes;
        }
    }
    return string;
}

static void db_init()
{
    sqlite3_open("build/mp.db", &ctx.db);

    execute_file("sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(*) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        puts("failed");
        sqlite3_finalize(stmt);
        return;
    }
    int num_rows = sqlite3_column_int(stmt, 0);
    SPDLOG_INFO("num rows: {}", num_rows);
    // load the example data
    if (num_rows == 0) {
        //execute_file("sql/insert.sql");
    }
    sqlite3_finalize(stmt);
}

void mp_init()
{
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    sqlite3_exec(ctx.db, "SELECT * FROM Songs", song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Albums", album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Playlists", playlist_callback, NULL, NULL);

    reset_current_song();
}

void mp_cleanup()
{
    if (ctx.current_song_tag != nullptr) {
        ma_sound_uninit(&ctx.current_song_sound);
        ID3v2_Tag_free(ctx.current_song_tag);
    }
    ma_engine_uninit(&ctx.engine);
    sqlite3_close(ctx.db);
}

void mp_add_song(const std::string& song_path)
{
    ID3v2_TextFrame* frame;
    std::string title{""};
    std::string artist{""};
    std::string track{"1"};
    std::string album{""};
    std::string path{song_path};
    ID3v2_Tag* tag = ID3v2_read_tag(song_path.c_str());
    if (!tag) {
        puts("Could not read tag");
        return;
    }
    frame = ID3v2_Tag_get_title_frame(tag);
    if (frame) {
        title = frame->data->text;
    } else {
        std::filesystem::path path{song_path};
        title = path.stem().string();
    }
    frame = ID3v2_Tag_get_artist_frame(tag);
    if (frame)
        artist = frame->data->text;
    frame = ID3v2_Tag_get_track_frame(tag);
    if (frame)
        track = frame->data->text;
    frame = ID3v2_Tag_get_album_frame(tag);
    if (frame)
        album = frame->data->text;

    db_fmt_str(title);
    db_fmt_str(artist);
    db_fmt_str(track);
    db_fmt_str(album);
    db_fmt_str(path);
    SPDLOG_INFO(track);
    std::string query = std::format("INSERT INTO Songs (title, artist, date, track, path) VALUES ('{}', '{}', 0, {}, '{}');", title, artist, track, path);
    //spdlog::info(query.c_str());
    SPDLOG_INFO(query);

    sqlite3_exec(ctx.db, query.c_str(), NULL, NULL, NULL);
    query = std::format("SELECT id FROM Songs WHERE title='{}' ORDER BY id DESC LIMIT 1", title);
    SPDLOG_INFO(query);
    sqlite3_stmt* stmt;
    sqlite3_prepare(ctx.db, query.c_str(), -1, &stmt, NULL);
    sqlite3_step(stmt);
    int song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    SPDLOG_INFO("song id: {}", song_id);

    if (album.size() == 0)
        return;

    int album_id;
    do {
        std::string query = std::format("SELECT id FROM Albums WHERE name='{}'", album);
        sqlite3_stmt* stmt;
        sqlite3_prepare(ctx.db, query.c_str(), -1, &stmt, NULL);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            SPDLOG_INFO("Creating album {}", album);
            query = std::format("INSERT INTO Albums (name) VALUES ('{}')", db_fmt_str(album));
            sqlite3_exec(ctx.db, query.c_str(), NULL, NULL, NULL);
        } else {
            album_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            break;
        }
    } while (true);

    query = std::format("INSERT INTO AlbumSong (song_id, album_id) VALUES ({}, {})", song_id, album_id);
    sqlite3_exec(ctx.db, query.c_str(), NULL, NULL, NULL);
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_add_song(song_path);
}

void mp_play_song(const std::string& song_path)
{
    const char* song_path_c_str = song_path.c_str();

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

Song* mp_get_song_by_id(int id)
{
    for (Song& song : mp_ctx.songs)
        if (song.id == id)
            return &song;
    return nullptr;
}

void mp_queue_skip()
{
    if (mp_ctx.queue.size() == 0)
        return;

    std::string song_path = mp_ctx.queue.front();
    mp_ctx.queue.pop_front();
    mp_play_song(song_path);
}
